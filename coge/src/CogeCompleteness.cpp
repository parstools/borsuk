#include "CogeCompletenessInternal.h"
#include "CogeDocumentStorage.h"
#include "CompletenessInternal.h"
#include "ExecutionModelInternal.h"
#include "SemanticInputStorage.h"
#include "coge/Completeness.h"
#include <algorithm>
#include <nlohmann/json.hpp>
namespace coge {
namespace {
using namespace agsem;
using J = nlohmann::ordered_json;
auto ref(const ExecutionType &t) -> TypeRef {
  TypeRef result{t.name, {}};
  for (const auto &a : t.arguments)
    result.arguments.push_back(ref(a));
  return result;
}
auto signature(const ExecutionFunctionSignature &f) -> FunctionContract {
  FunctionContract r;
  r.result = ref(f.result);
  r.mutates = f.mutates;
  for (const auto &p : f.parameters)
    r.parameters.push_back(ref(p.type));
  return r;
}
auto ref(const J &t) -> TypeRef {
  TypeRef result{t.at("name").get<std::string>(), {}};
  for (const auto &argument : t.at("arguments"))
    result.arguments.push_back(ref(argument));
  return result;
}
void fulfilled(Obligation &o, ObligationEvidence e) {
  o.state = ObligationState::Implemented;
  o.validation = ObligationValidation::Valid;
  o.evidence = e;
}
auto parseTarget(std::string_view target) -> CompletenessTarget {
  return target == "interpreter" ? CompletenessTarget::Interpreter
         : target == "c"         ? CompletenessTarget::C
                                 : CompletenessTarget::Llvm;
}
} // namespace
auto assessCompleteness(const ParsedDocument &parsed,
                        const ContractEnvironment &contracts,
                        std::vector<CompletenessTarget> requested)
    -> Outcome<CompletenessReport> {
  auto assessed = agsem::assessCompleteness(parsed, contracts);
  if (!assessed.value)
    return assessed;
  auto &report = *assessed.value;
  auto document = makeCogeDocument(parsed);
  if (!document.value) {
    report.diagnostics.insert(report.diagnostics.end(),
                              document.diagnostics.begin(),
                              document.diagnostics.end());
    finalizeCompleteness(report);
    assessed.diagnostics = report.diagnostics;
    return assessed;
  }
  const auto &root = DocumentAccess::root(document.value->execution());
  const auto selection = generationSelection(*document.value);
  report.targets.insert(report.targets.end(), requested.begin(),
                        requested.end());
  if (selection.interpreter)
    report.targets.push_back(CompletenessTarget::Interpreter);
  if (selection.lowering)
    report.targets.push_back(CompletenessTarget::Lowering);
  if (selection.c)
    report.targets.push_back(CompletenessTarget::C);
  if (selection.llvm)
    report.targets.push_back(CompletenessTarget::Llvm);
  const auto metadata = executionTemplateDiagnostics(root, false);
  report.diagnostics.insert(report.diagnostics.end(), metadata.begin(),
                            metadata.end());
  const auto *section = ast::optional(ast::field(root, "obligations"));
  if (section)
    for (const auto &entry : ast::field(*section, "entries").elements)
      if (ast::token(ast::field(entry, "name")) == "targets")
        for (const auto &value : ast::field(entry, "values").elements)
          report.targets.push_back(
              parseTarget(J::parse(ast::token(value)).get<std::string>()));
  const auto selected = [&](CompletenessTarget t) {
    return std::ranges::find(report.targets, t) != report.targets.end();
  };
  if (selected(CompletenessTarget::C) || selected(CompletenessTarget::Llvm))
    report.targets.push_back(CompletenessTarget::Lowering);
  for (auto &o : report.obligations)
    o.requiredFor = report.targets;
  if (std::ranges::find(report.emissionBlocked, CompletenessTarget::Analysis) !=
      report.emissionBlocked.end())
    report.emissionBlocked = report.targets;
  auto bound = bindSemantics(document.value->semantics(), contracts);
  std::optional<ExpandedExecutionModel> expanded;
  std::vector<Diagnostic> executionIssues;
  std::vector<ExecutionDependency> dependencies;
  std::string context;
  const auto &semanticRoot =
      SemanticInputAccess::root(document.value->semantics().semanticInput());
  if (const auto *model = ast::optional(ast::field(semanticRoot, "model")))
    for (const auto &d : ast::field(*model, "declarations").elements)
      if (d.typeName == "rustContextDeclaration")
        context = ast::token(ast::field(d, "name"));
  try {
    expanded = expandExecutionModel(root, context, parsed.identity(),
                                    contracts.fingerprint());
    if (bound.value)
      executionIssues =
          checkExpandedExecution(*expanded, *bound.value, dependencies, true);
  } catch (const std::runtime_error &error) {
    executionIssues.push_back({Severity::Error,
                               "coge.invalid_contract",
                               error.what(),
                               ast::location(root),
                               {}});
  }
  report.diagnostics.insert(report.diagnostics.end(), executionIssues.begin(),
                            executionIssues.end());
  const auto add = [&](Obligation o) {
    if (!o.location)
      o.location = section ? ast::location(*section) : ast::location(root);
    report.obligations.push_back(std::move(o));
  };
  const auto executionValid = [&](const SourceLocation &location,
                                  std::string_view member) {
    return bound.value &&
           std::ranges::none_of(executionIssues, [&](const auto &d) {
             return d.severity == Severity::Error &&
                    (d.subject == member || d.subject == "profile/interface" ||
                     !d.location ||
                     (d.location->beginByte <= location.beginByte &&
                      d.location->endByte >= location.endByte) ||
                     (d.location->beginByte >= location.beginByte &&
                      d.location->endByte < location.endByte));
           });
  };
  if (selected(CompletenessTarget::Interpreter)) {
    Obligation runtime{
        {ObligationOwner::Coge, ObligationKind::ExecutionContract,
         ContractSubject{"", "runtime", "runtime"}, "interpreter"}};
    runtime.requiredFor = {CompletenessTarget::Interpreter};
    runtime.reason =
        "Declare the execution runtime state and its typed contract.";
    if (expanded && expanded->declarations &&
        expanded->declarations->runtimeState) {
      runtime.requirement = ref(expanded->declarations->runtimeState->type);
      runtime.location = expanded->source;
      if (bound.value && executionIssues.empty())
        fulfilled(runtime, ObligationEvidence::CheckedBinding);
    }
    const auto runtimeKey = runtime.key;
    add(std::move(runtime));
    Obligation schema{{ObligationOwner::Coge, ObligationKind::ExecutionContract,
                       ContractSubject{"", "ir", "ir"}, "schema"}};
    schema.requiredFor = {CompletenessTarget::Interpreter};
    schema.reason = "Resolve Operation and ExpressionKind enum contracts.";
    std::vector<TypeRef> schemaTypes;
    std::vector<ContractSymbol> schemaSymbols = contracts.symbols();
    if (bound.value) {
      schemaSymbols.clear();
      for (const auto &s : bound.value->symbols())
        schemaSymbols.push_back(s.contract);
    }
    {
      for (const auto &s : schemaSymbols) {
        if ((s.name != "Operation" && s.name != "ExpressionKind") || !s.type ||
            s.type->kind != TypeContract::Kind::Enum)
          continue;
        schemaTypes.push_back({s.name, {}});
        std::string manifest;
        for (std::size_t i = 0; i < contracts.symbols().size(); ++i)
          if (contracts.symbols()[i].name == s.name)
            manifest = contracts.origins().at(i).identity.id;
        for (const auto &[variant, payload] : s.type->variants) {
          Obligation handler{
              {ObligationOwner::Coge,
               s.name == "Operation" ? ObligationKind::ExecuteHandler
                                     : ObligationKind::EvaluateHandler,
               ContractSubject{manifest, s.name, variant}, variant}};
          handler.requirement = PayloadRequirement{payload};
          handler.requiredFor = {CompletenessTarget::Interpreter};
          handler.dependsOn = {runtimeKey};
          handler.reason =
              "Provide a checked handler for " + s.name + "." + variant + ".";
          if (expanded) {
            const auto &handlers = s.name == "Operation"
                                       ? expanded->operations
                                       : expanded->expressions;
            const auto found =
                std::ranges::find_if(handlers, [&](const auto &h) {
                  return h.arm.structuredPattern &&
                         h.arm.structuredPattern->owner == s.name &&
                         h.arm.structuredPattern->variant == variant;
                });
            if (found != handlers.end()) {
              handler.location = found->origin.source;
              if (executionValid(found->origin.source, found->origin.member))
                fulfilled(handler, found->origin.kind == "profile_default"
                                       ? ObligationEvidence::CheckedProfile
                                       : ObligationEvidence::CheckedBody);
              else
                handler.validation = ObligationValidation::Invalid;
            }
          }
          add(std::move(handler));
        }
      }
    }
    if (schemaTypes.size() == 2) {
      schema.requirement = PayloadRequirement{schemaTypes};
      fulfilled(schema, ObligationEvidence::ExternalContract);
    }
    const auto schemaKey = schema.key;
    add(std::move(schema));
    Obligation entry{{ObligationOwner::Coge, ObligationKind::ExecutionFunction,
                      FunctionSubject{"interpreter"}, "entry"}};
    entry.requiredFor = {CompletenessTarget::Interpreter};
    entry.dependsOn = {runtimeKey, schemaKey};
    entry.reason =
        "Declare a checked execution model and interpreter entry points.";
    if (expanded &&
        (!expanded->operations.empty() || !expanded->expressions.empty()) &&
        bound.value && executionIssues.empty()) {
      entry.requirement = FunctionContract{{}, {"Control", {}}, true, context};
      fulfilled(entry, ObligationEvidence::CheckedBody);
    }
    add(std::move(entry));
  }
  if (expanded) {
    const auto targets =
        std::vector<CompletenessTarget>{CompletenessTarget::Interpreter};
    for (const auto &f : expanded->functions) {
      Obligation o{{ObligationOwner::Coge, ObligationKind::ExecutionFunction,
                    FunctionSubject{f.signature.name}, "implementation"}};
      o.requirement = signature(f.signature);
      o.location = f.origin.source;
      o.requiredFor = targets;
      if (executionValid(f.origin.source, f.signature.name))
        fulfilled(o, f.origin.kind == "profile_default"
                         ? ObligationEvidence::CheckedProfile
                         : ObligationEvidence::CheckedBody);
      else
        o.validation = ObligationValidation::Invalid;
      add(std::move(o));
    }
    if (expanded->profile)
      for (const auto &r : sharedValueProfile().requirements) {
        const auto &d = r.contract;
        if (!r.localBody)
          continue;
        if (std::ranges::any_of(expanded->functions, [&](const auto &f) {
              return f.signature.name == d.name;
            }))
          continue;
        Obligation o{{ObligationOwner::Coge, ObligationKind::ExecutionFunction,
                      FunctionSubject{d.name}, "implementation"}};
        if (d.function)
          o.requirement = *d.function;
        o.location = expanded->profile->source;
        o.requiredFor = targets;
        o.reason = "The execution profile requires a local implementation of " +
                   d.name + ".";
        add(std::move(o));
      }
    for (const auto &d : dependencies) {
      const auto manifest = d.contract ? d.contract->identity.id : "";
      Obligation o{{ObligationOwner::Coge, ObligationKind::ExecutionContract,
                    ContractSubject{manifest, d.owner, d.name},
                    d.member + d.path}};
      o.requiredFor = targets;
      o.location = d.declaration;
      if (d.function)
        o.requirement = *d.function;
      else if (d.type)
        o.requirement = *d.type;
      else
        o.requirement = PayloadRequirement{d.payload};
      if (d.contract)
        o.contractOrigins.push_back(*d.contract);
      fulfilled(o, d.contract ? ObligationEvidence::ExternalContract
                              : ObligationEvidence::CheckedBinding);
      add(std::move(o));
    }
  }
  ConfigurationReview configuration;
  if (bound.value)
    configuration = reviewConfiguration(*document.value, *bound.value);
  report.diagnostics.insert(report.diagnostics.end(),
                            configuration.diagnostics.begin(),
                            configuration.diagnostics.end());
  const auto loweringTargets = std::vector<CompletenessTarget>{
      CompletenessTarget::Lowering, CompletenessTarget::C,
      CompletenessTarget::Llvm};
  const ObligationKey loweringKey{
      ObligationOwner::Coge, ObligationKind::LoweringRole,
      RoleSubject{"lowering", "lowering", "configuration"}, "configuration"};
  if (selected(CompletenessTarget::Lowering)) {
    Obligation o{loweringKey};
    o.requiredFor = loweringTargets;
    o.reason =
        "Declare the lowering profile, roles and checked adapter bindings.";
    if (const auto *node = ast::optional(ast::field(root, "lowering")))
      o.location = ast::location(*node);
    if (configuration.lowering && configuration.lowering->loweringName()) {
      fulfilled(o, ObligationEvidence::CheckedBinding);
      const auto inspected = J::parse(configuration.lowering->inspection());
      FunctionContract lowerInterface;
      for (const auto &type : inspected.at("interface").at("parameters"))
        lowerInterface.parameters.push_back(ref(type));
      lowerInterface.result = TypeRef{
          inspected.at("interface").at("result").get<std::string>(), {}};
      lowerInterface.context =
          inspected.at("interface").at("context").get<std::string>();
      o.requirement = lowerInterface;
      for (const auto &binding : inspected.at("bindings")) {
        const auto role = binding.at("role").get<std::string>();
        Obligation port{
            {ObligationOwner::Coge, ObligationKind::LoweringRole,
             RoleSubject{"lowering", *configuration.lowering->loweringName(),
                         role},
             role}};
        port.requiredFor = loweringTargets;
        port.dependsOn = {loweringKey};
        port.location = o.location;
        if (role == "lower")
          port.requirement = lowerInterface;
        for (const auto &check : inspected.at("type_checks")) {
          if (check.at("role") != role)
            continue;
          if (check.at("kind") == "field")
            port.requirement = ref(check.at("type"));
          else {
            PayloadRequirement payload;
            for (const auto &type : check.at("payload"))
              payload.types.push_back(ref(type));
            port.requirement = std::move(payload);
          }
        }
        fulfilled(port, ObligationEvidence::CheckedBinding);
        add(std::move(port));
      }
    }
    add(std::move(o));
  }
  for (const auto target : {CompletenessTarget::C, CompletenessTarget::Llvm}) {
    if (!selected(target))
      continue;
    const auto name = std::string(targetName(target));
    Obligation o{{ObligationOwner::Coge, ObligationKind::BackendBinding,
                  RoleSubject{"backend", name, "configuration"},
                  "configuration"}};
    o.requiredFor = {target};
    o.dependsOn = {loweringKey};
    o.reason = "Declare the " + name +
               " backend profile and checked emitter bindings.";
    if (const auto *node = ast::optional(
            ast::field(root, target == CompletenessTarget::C ? "backendC"
                                                             : "backendLlvm")))
      o.location = ast::location(*node);
    if (configuration.backends) {
      const auto &binding = target == CompletenessTarget::C
                                ? configuration.backends->c()
                                : configuration.backends->llvm();
      if (binding) {
        const auto inspected = J::parse(binding->inspection);
        const auto &schema = inspected.at("interface");
        FunctionContract backendInterface;
        backendInterface.parameters = {
            {schema.at("context_parameter").get<std::string>(), {}},
            {schema.at("function_parameter").get<std::string>(), {}}};
        if (!schema.at("target_parameter").is_null())
          backendInterface.parameters.push_back(
              {schema.at("target_parameter").get<std::string>(), {}});
        backendInterface.result = {schema.at("result").get<std::string>(), {}};
        o.requirement = std::move(backendInterface);
        fulfilled(o, ObligationEvidence::CheckedBinding);
      }
    }
    add(std::move(o));
  }
  // Explicit pending declarations remain pending, even when their
  // implementation checks.
  if (section)
    for (const auto &entry : ast::field(*section, "entries").elements) {
      if (ast::token(ast::field(entry, "name")) != "pending")
        continue;
      const auto &values = ast::field(entry, "values").elements;
      if (values.size() != 4)
        continue;
      const auto string = [&](std::size_t i) {
        return J::parse(ast::token(values[i])).get<std::string>();
      };
      const auto kind = string(0), name = string(1), slot = string(2);
      auto found = std::ranges::find_if(report.obligations, [&](const auto &o) {
        const auto key = J::parse(obligationKey(o.key));
        if (key.at("owner") != "coge" || key.at("kind") != kind ||
            key.at("slot") != slot)
          return false;
        return std::visit(
            [&](const auto &s) {
              using T = std::decay_t<decltype(s)>;
              if constexpr (std::is_same_v<T, FunctionSubject>)
                return s.name == name;
              else if constexpr (std::is_same_v<T, ContractSubject>)
                return s.owner == name || s.name == name ||
                       s.manifest + "::" + s.owner == name;
              else if constexpr (std::is_same_v<T, RoleSubject>)
                return s.family == name || s.policy == name;
              else
                return false;
            },
            o.key.subject);
      });
      if (found != report.obligations.end()) {
        found->declaredState = ObligationState::Pending;
        found->state = ObligationState::Pending;
        found->reason = string(3);
        found->related.push_back(ast::location(entry));
      } else {
        // Unknown declarations are still obligations, never silently discarded.
        static const std::map<std::string, ObligationKind> kinds{
            {"execution_function", ObligationKind::ExecutionFunction},
            {"execute_handler", ObligationKind::ExecuteHandler},
            {"evaluate_handler", ObligationKind::EvaluateHandler},
            {"execution_contract", ObligationKind::ExecutionContract},
            {"lowering_role", ObligationKind::LoweringRole},
            {"backend_binding", ObligationKind::BackendBinding}};
        if (!kinds.contains(kind))
          continue;
        Obligation o{{ObligationOwner::Coge, kinds.at(kind),
                      ContractSubject{"", name, name}, slot}};
        o.declaredState = ObligationState::Pending;
        o.location = ast::location(entry);
        o.requiredFor = report.targets;
        o.reason = string(3);
        add(std::move(o));
      }
    }
  for (const auto &element : parsed.elements())
    if (element.kind == DocumentElementKind::ExecutionResult)
      report.diagnostics.push_back(
          {Severity::Error,
           "coge.unsupported_execution_result",
           "execution result has no supported execution contract",
           element.location,
           {}});
  finalizeCompleteness(report);
  assessed.diagnostics = report.diagnostics;
  return assessed;
}
} // namespace coge
