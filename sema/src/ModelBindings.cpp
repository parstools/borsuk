#include "AstAccess.h"
#include "ModelBindingsInternal.h"
#include "SemanticInputStorage.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>

namespace agsem {
struct ModelBindingAccess {
  static auto schema(CheckedModelBindings &value) -> auto & {
    return value.schema_;
  }
  static auto ports(CheckedModelBindings &value) -> auto & {
    return value.ports_;
  }
  static auto policies(CheckedModelBindings &value) -> auto & {
    return value.policies_;
  }
};
namespace {
using namespace ast;
using Json = nlohmann::ordered_json;
auto logical(std::string value) -> TypeRef {
  const auto open = value.find('<');
  if (open == std::string::npos)
    return {std::move(value), {}};
  return {value.substr(0, open),
          {logical(value.substr(open + 1, value.size() - open - 2))}};
}
auto typeJson(const TypeRef &type) -> Json {
  auto args = Json::array();
  for (const auto &argument : type.arguments)
    args.push_back(typeJson(argument));
  return {{"name", type.name}, {"arguments", args}};
}
auto signatureJson(const FunctionContract &signature) -> Json {
  auto parameters = Json::array();
  for (const auto &parameter : signature.parameters)
    parameters.push_back(typeJson(parameter));
  return {{"context", signature.context},
          {"parameters", parameters},
          {"result", typeJson(signature.result)},
          {"effect", signature.mutates ? "mutates" : "pure"}};
}
auto locationJson(SourceLocation source) -> Json {
  return {{"source", source.source},
          {"begin_byte", source.beginByte},
          {"end_byte", source.endByte}};
}
auto kindName(BindingTargetKind kind) -> std::string {
  switch (kind) {
  case BindingTargetKind::Field:
    return "field";
  case BindingTargetKind::Method:
    return "method";
  case BindingTargetKind::Variant:
    return "variant";
  case BindingTargetKind::Mode:
    return "mode";
  }
  throw std::logic_error("unknown binding kind");
}
auto evidenceJson(const BindingEvidence &entry) -> Json {
  Json result{{"kind", kindName(entry.kind)},
              {"owner", entry.owner},
              {"target", entry.target}};
  if (entry.requiredType) {
    result["required_type"] = typeJson(*entry.requiredType);
    result["actual_type"] = typeJson(*entry.actualType);
  }
  if (entry.requiredFunction) {
    result["required_signature"] = signatureJson(*entry.requiredFunction);
    result["actual_signature"] = signatureJson(*entry.actualFunction);
  }
  if (entry.kind == BindingTargetKind::Variant) {
    result["required_payload"] = Json::array();
    result["actual_payload"] = Json::array();
    for (const auto &type : entry.requiredPayload)
      result["required_payload"].push_back(typeJson(type));
    for (const auto &type : entry.actualPayload)
      result["actual_payload"].push_back(typeJson(type));
  }
  if (entry.declaration)
    result["declaration"] = locationJson(*entry.declaration);
  if (entry.contract)
    result["contract"] = {{"id", entry.contract->id},
                          {"version", entry.contract->version},
                          {"sha256", entry.contract->sha256},
                          {"path", entry.contractPath}};
  return result;
}
const std::map<std::string, std::string> policyKinds{
    {"returnPolicy", "return"},       {"assignmentPolicy", "assignment"},
    {"conditionPolicy", "condition"}, {"statementIrPolicy", "statement"},
    {"flowActions", "flow"},          {"selectionPolicy", "selection"}};
const std::map<std::string, std::string> bindingKinds{
    {"modelBindings", "return"},        {"assignmentBindings", "assignment"},
    {"conditionBindings", "condition"}, {"statementIrBindings", "statement"},
    {"flowBindings", "flow"},           {"selectionBindings", "selection"}};
struct Policy {
  std::string name;
  SourceLocation source;
  std::map<std::string, std::string> values;
};
class Resolver {
public:
  Resolver(const std::vector<BoundSymbol> &symbols,
           const ContractEnvironment &contracts)
      : symbols_(symbols), contracts_(contracts) {}
  std::string family, policy, port;
  SourceLocation source;
  std::optional<SourceLocation> schemaSource;
  [[noreturn]] void fail(std::string code, std::string message) const {
    std::vector<SourceLocation> related;
    if (schemaSource)
      related.push_back(*schemaSource);
    throw SemanticPreparationError(
        {{Severity::Error, std::move(code),
          family + "." + policy + "." + port + ": " + std::move(message),
          source, port, std::move(related)}});
  }
  auto resolve(SymbolKind kind, const std::string &name) const
      -> const BoundSymbol & {
    const BoundSymbol *found = nullptr;
    for (const auto &symbol : symbols_)
      if (symbol.contract.name == name && symbol.contract.kind == kind) {
        if (found)
          fail("sema.binding_target_mismatch", "ambiguous target " + name);
        found = &symbol;
      }
    if (!found || found->contract.owner == SymbolOwner::Execution)
      fail("sema.binding_target_mismatch",
           "required semantic target " + name +
               " is missing or belongs to execution");
    return *found;
  }
  auto normalized(TypeRef type, std::set<std::string> aliases = {}) const
      -> TypeRef {
    for (auto &argument : type.arguments)
      argument = normalized(argument, aliases);
    for (const auto &symbol : symbols_)
      if (symbol.contract.kind == SymbolKind::Type &&
          symbol.contract.name == type.name && symbol.contract.type &&
          symbol.contract.type->alias && type.arguments.empty()) {
        if (!aliases.insert(type.name).second)
          fail("sema.binding_type_mismatch", "cyclic alias " + type.name);
        return normalized(*symbol.contract.type->alias, std::move(aliases));
      }
    return type;
  }
  auto evidence(const BoundSymbol &symbol, BindingTargetKind kind,
                std::string target, std::string suffix) const
      -> BindingEvidence {
    BindingEvidence result;
    result.kind = kind;
    result.owner = symbol.contract.name;
    result.target = std::move(target);
    for (std::size_t i = 0; i < contracts_.symbols().size(); ++i)
      if (contracts_.symbols()[i] == symbol.contract) {
        result.contract = contracts_.origins().at(i).identity;
        result.contractPath = contracts_.origins().at(i).path + suffix;
        return result;
      }
    result.declaration = symbol.declaration;
    return result;
  }
  auto description(const BindingEvidence &evidence) const -> std::string {
    return evidence.contract ? " [" + evidence.contract->id + "@" +
                                   evidence.contract->version + " " +
                                   evidence.contractPath + "]"
                             : " [DSL declaration]";
  }
  auto fieldEvidence(const std::string &owner, const std::string &name,
                     TypeRef expected, bool dependency = false) const
      -> BindingEvidence {
    const auto &symbol = resolve(SymbolKind::Type, owner);
    auto result =
        evidence(symbol, BindingTargetKind::Field, name, "/fields/" + name);
    const auto code = dependency ? "sema.binding_dependency_mismatch"
                                 : "sema.binding_type_mismatch";
    if (!symbol.contract.type ||
        symbol.contract.type->kind != TypeContract::Kind::Record)
      fail(code, "requires record " + owner + description(result));
    const auto actual = symbol.contract.type->fields.find(name);
    if (actual == symbol.contract.type->fields.end())
      fail(code, "requires " + owner + "." + name + ": " +
                     typeSpelling(expected) + ", actual missing" +
                     description(result));
    result.requiredType = expected;
    result.actualType = actual->second;
    if (normalized(expected) != normalized(actual->second))
      fail(code, "requires " + owner + "." + name + ": " +
                     typeSpelling(expected) + ", actual " +
                     typeSpelling(actual->second) + description(result));
    return result;
  }
  auto variantEvidence(const std::string &owner, const std::string &name,
                       std::vector<TypeRef> expected,
                       bool dependency = false) const -> BindingEvidence {
    const auto &symbol = resolve(SymbolKind::Type, owner);
    auto result =
        evidence(symbol, BindingTargetKind::Variant, name, "/variants/" + name);
    const auto code = dependency ? "sema.binding_dependency_mismatch"
                                 : "sema.binding_type_mismatch";
    if (!symbol.contract.type ||
        symbol.contract.type->kind != TypeContract::Kind::Enum)
      fail(code, "requires enum " + owner + description(result));
    const auto actual = symbol.contract.type->variants.find(name);
    if (actual == symbol.contract.type->variants.end())
      fail(code, "requires variant " + owner + "." + name + ", actual missing" +
                     description(result));
    result.requiredPayload = expected;
    result.actualPayload = actual->second;
    auto required = expected, provided = actual->second;
    for (auto &type : required)
      type = normalized(type);
    for (auto &type : provided)
      type = normalized(type);
    if (required != provided)
      fail(code,
           "variant payload mismatch " + owner + "." + name + ": expected " +
               evidenceJson(result)["required_payload"].dump() + ", actual " +
               evidenceJson(result)["actual_payload"].dump() +
               description(result));
    return result;
  }
  auto methodEvidence(const std::string &name, FunctionContract expected) const
      -> BindingEvidence {
    const auto &symbol = resolve(SymbolKind::Method, name);
    auto result = evidence(symbol, BindingTargetKind::Method, name, "");
    if (!symbol.contract.function ||
        symbol.contract.function->context != expected.context)
      fail("sema.binding_target_mismatch", "required method " +
                                               expected.context + "." + name +
                                               description(result));
    const auto &actual = *symbol.contract.function;
    if (!symbol.contract.rust.empty() &&
        symbol.contract.rust != std::vector<std::string>{name} &&
        symbol.contract.rust !=
            std::vector<std::string>{expected.context, name})
      fail("sema.binding_target_mismatch",
           "adapter cannot call redirected Rust method " + name +
               description(result));
    auto requiredParameters = expected.parameters,
         actualParameters = actual.parameters;
    for (auto &type : requiredParameters)
      type = normalized(type);
    for (auto &type : actualParameters)
      type = normalized(type);
    if (requiredParameters != actualParameters ||
        normalized(expected.result) != normalized(actual.result))
      fail("sema.binding_type_mismatch",
           "method " + name + " expected " + signatureJson(expected).dump() +
               ", actual " + signatureJson(actual).dump() +
               description(result));
    if (expected.mutates != actual.mutates)
      fail("sema.binding_effect_mismatch",
           "method " + name + " expected " +
               (expected.mutates ? "mutates" : "pure") + ", actual " +
               (actual.mutates ? "mutates" : "pure") + description(result));
    result.owner = expected.context;
    result.requiredFunction = expected;
    result.actualFunction = actual;
    return result;
  }

private:
  const std::vector<BoundSymbol> &symbols_;
  const ContractEnvironment &contracts_;
};
} // namespace
auto hasModelBindingSchema(const Value &root) -> bool {
  const auto *model = optional(field(root, "model"));
  if (!model)
    return false;
  return std::ranges::any_of(
      field(*model, "declarations").elements, [](const auto &value) {
        return value.typeName == "modelSchemaDeclaration";
      });
}
auto bindModelBindings(const Value &root,
                       const std::vector<BoundSymbol> &symbols,
                       const ContractEnvironment &contracts)
    -> Outcome<std::shared_ptr<const CheckedModelBindings>> {
  Outcome<std::shared_ptr<const CheckedModelBindings>> outcome;
  try {
    auto checked = std::make_shared<CheckedModelBindings>();
    const auto *model = optional(field(root, "model"));
    if (!model) {
      outcome.value = checked;
      return outcome;
    }
    Resolver resolver{symbols, contracts};
    std::map<std::string, Policy> policies;
    struct Entry {
      std::string value;
      SourceLocation source;
    };
    std::map<std::pair<std::string, std::string>, std::map<std::string, Entry>>
        explicitBindings;
    std::string context;
    for (const auto &declaration : field(*model, "declarations").elements) {
      resolver.source = location(declaration);
      if (declaration.typeName == "rustContextDeclaration")
        context = token(field(declaration, "name"));
      else if (declaration.typeName == "modelSchemaDeclaration") {
        const auto name = token(field(declaration, "name"));
        if (checked->schema())
          resolver.fail("sema.duplicate_model_schema",
                        "only one model_schema is allowed");
        if (name != "standard_semantic_v1")
          resolver.fail("sema.unknown_model_schema",
                        "unknown model_schema " + name);
        ModelBindingAccess::schema(*checked) = BindingSchemaIdentity{
            name, "1", standardBindingSchemaHash(), location(declaration)};
        resolver.schemaSource = location(declaration);
      } else if (const auto family = policyKinds.find(declaration.typeName);
                 family != policyKinds.end()) {
        Policy policy{
            token(field(declaration, "name")), location(declaration), {}};
        resolver.family = family->second;
        resolver.policy = policy.name;
        for (const auto &entry : field(declaration, "entries").elements)
          policy.values.emplace(token(field(entry, "name")),
                                token(field(entry, "value")));
        if (!policies.emplace(family->second, std::move(policy)).second)
          resolver.fail(
              "sema.contract_mismatch",
              "only one " +
                  std::string{family->second == "statement"
                                  ? "statement_ir"
                                  : (family->second == "flow"
                                         ? "flow_actions"
                                         : family->second + "_policy")} +
                  " is supported");
      } else if (const auto family = bindingKinds.find(declaration.typeName);
                 family != bindingKinds.end()) {
        const auto name = token(field(declaration, "name"));
        resolver.family = family->second;
        resolver.policy = name;
        auto [block, inserted] = explicitBindings.emplace(
            std::pair{family->second, name}, std::map<std::string, Entry>{});
        if (!inserted)
          resolver.fail("sema.duplicate_binding", "duplicate binding block");
        for (const auto &entry : field(declaration, "entries").elements) {
          resolver.port = token(field(entry, "name"));
          resolver.source = location(entry);
          if (!block->second
                   .emplace(resolver.port, Entry{token(field(entry, "value")),
                                                 location(entry)})
                   .second)
            resolver.fail("sema.duplicate_binding", "duplicate binding key");
        }
      }
    }
    for (const auto &[key, entries] : explicitBindings) {
      resolver.family = key.first;
      resolver.policy = key.second;
      resolver.port.clear();
      if (!entries.empty())
        resolver.source = entries.begin()->second.source;
      if (!policies.contains(key.first) ||
          policies.at(key.first).name != key.second)
        resolver.fail("sema.orphan_binding", "binding has no matching policy");
    }
    for (const auto &[family, policy] : policies) {
      resolver.family = family;
      resolver.policy = policy.name;
      resolver.port.clear();
      resolver.source = policy.source;
      const bool cleanup =
          family == "return" && policy.values.contains("cleanup") &&
          policy.values.at("cleanup") == "exited_automatic_lifetimes";
      const auto requirements =
          standardBindingRequirements(family, context, cleanup);
      const auto explicitBlock = explicitBindings.find({family, policy.name});
      const std::map<std::string, Entry> empty;
      const auto &entries = explicitBlock == explicitBindings.end()
                                ? empty
                                : explicitBlock->second;
      for (const auto &[port, entry] : entries)
        if (!requirements.contains(port)) {
          resolver.port = port;
          resolver.source = entry.source;
          resolver.fail("sema.unknown_binding_slot",
                        "unknown or unused binding port");
        }
      CheckedBindingPolicy result{family, policy.name, {}, {}};
      for (const auto &[port, requirement] : requirements) {
        resolver.port = port;
        resolver.source = policy.source;
        const auto explicitEntry = entries.find(port);
        const bool explicitValue = explicitEntry != entries.end();
        if (!explicitValue &&
            (!checked->schema() || requirement.target.empty()))
          resolver.fail("sema.missing_binding", "missing explicit binding");
        const auto value =
            explicitValue ? explicitEntry->second.value : requirement.target;
        if (explicitValue)
          resolver.source = explicitEntry->second.source;
        CheckedBindingPort resolved{
            family,
            policy.name,
            port,
            value,
            requirement.kind,
            {},
            {},
            explicitValue ? "explicit" : "schema_default",
            explicitValue ? resolver.source : checked->schema()->source,
            policy.source,
            requirement.invocation,
            requirement.argumentPassing,
            requirement.access,
            {}};
        if (requirement.kind == BindingTargetKind::Field) {
          for (const auto &[owner, type] : requirement.fields)
            resolved.targets.push_back(
                resolver.fieldEvidence(owner, value, type));
        } else if (requirement.kind == BindingTargetKind::Method)
          resolved.targets.push_back(
              resolver.methodEvidence(value, *requirement.function));
        else if (requirement.kind == BindingTargetKind::Variant) {
          std::vector<TypeRef> payload;
          if (port == "construction_ir")
            payload = {logical("SymbolId"), logical("Option<FunctionId>"),
                       logical("List<ExprId>")};
          resolved.targets.push_back(
              resolver.variantEvidence("Operation", value, payload));
        } else if (port == "return_ir") {
          if (value != "tuple" && value != "record")
            resolver.fail("sema.binding_target_mismatch",
                          "return_ir requires tuple or record");
          resolved.returnRepresentation = value == "tuple"
                                              ? ReturnRepresentation::Tuple
                                              : ReturnRepresentation::Record;
          if (cleanup && value != "record")
            resolver.fail("sema.contract_mismatch",
                          "return cleanup requires record return_ir");
        } else {
          resolved.cleanupRepresentation = value == "no_scopes"
                                               ? CleanupRepresentation::NoScopes
                                               : CleanupRepresentation::Field;
          if (cleanup == (value == "no_scopes"))
            resolver.fail("sema.contract_mismatch",
                          "return cleanup and cleanup_scopes disagree");
          if (value != "no_scopes") {
            resolved.kind = BindingTargetKind::Field;
            resolved.access = "read";
            resolved.targets.push_back(resolver.fieldEvidence(
                context, value, logical("List<ScopeId>")));
          }
        }
        result.names.emplace(port, value);
        ModelBindingAccess::ports(*checked).push_back(std::move(resolved));
      }
      ModelBindingAccess::policies(*checked).push_back(std::move(result));
    }
    // Validate the policy choices before checking adapter dependencies.
    validateSemanticPolicies(root, checked.get());
    for (auto &result : ModelBindingAccess::policies(*checked)) {
      const auto &family = result.family;
      const auto &policy = policies.at(family);
      const bool cleanup =
          family == "return" &&
          policy.values.at("cleanup") == "exited_automatic_lifetimes";
      resolver.family = family;
      resolver.policy = policy.name;
      resolver.port = "dependencies";
      resolver.source = policy.source;
      for (const auto &dependency : standardBindingDependencies(
               family, policy.values, result.names, cleanup)) {
        if (dependency.kind == BindingTargetKind::Field)
          result.dependencies.push_back(
              resolver.fieldEvidence(dependency.owner, dependency.target,
                                     dependency.types.at(0), true));
        else
          result.dependencies.push_back(resolver.variantEvidence(
              dependency.owner, dependency.target, dependency.types, true));
      }
      std::ranges::sort(result.dependencies, {}, [](const auto &entry) {
        return std::tuple{entry.kind, entry.owner, entry.target};
      });
    }
    outcome.value = std::move(checked);
  } catch (const SemanticPreparationError &error) {
    outcome.diagnostics = error.diagnostics();
  }
  return outcome;
}
auto CheckedModelBindings::names(std::string_view family,
                                 std::string_view policy) const
    -> const std::map<std::string, std::string> & {
  for (const auto &entry : policies_)
    if (entry.family == family && entry.policy == policy)
      return entry.names;
  throw std::logic_error("missing checked model binding policy");
}
auto inspectModelBindings(const CheckedModelBindings &model) -> std::string {
  Json result{{"binding_schema", nullptr}, {"model_bindings", Json::array()}};
  if (model.schema())
    result["binding_schema"] = {
        {"id", model.schema()->id},
        {"version", model.schema()->version},
        {"sha256", model.schema()->sha256},
        {"source", locationJson(model.schema()->source)}};
  for (const auto &port : model.ports()) {
    Json item{{"family", port.family},
              {"policy", port.policy},
              {"port", port.port},
              {"kind", kindName(port.kind)},
              {"value", port.value},
              {"origin", port.origin},
              {"source", locationJson(port.source)},
              {"policy_source", locationJson(port.policySource)},
              {"invocation", port.invocation},
              {"argument_passing", port.argumentPassing},
              {"access", port.access},
              {"targets", Json::array()}};
    if (port.returnRepresentation)
      item["return_representation"] =
          *port.returnRepresentation == ReturnRepresentation::Tuple ? "tuple"
                                                                    : "record";
    if (port.cleanupRepresentation)
      item["cleanup_representation"] =
          *port.cleanupRepresentation == CleanupRepresentation::NoScopes
              ? "no_scopes"
              : "field";
    for (const auto &target : port.targets)
      item["targets"].push_back(evidenceJson(target));
    result["model_bindings"].push_back(std::move(item));
  }
  result["binding_dependencies"] = Json::array();
  for (const auto &policy : model.policies()) {
    Json entry{{"family", policy.family},
               {"policy", policy.policy},
               {"targets", Json::array()}};
    for (const auto &dependency : policy.dependencies)
      entry["targets"].push_back(evidenceJson(dependency));
    result["binding_dependencies"].push_back(std::move(entry));
  }
  result["binding_representation_obligations"] = {
      "Rust variant tuple/record layout and field names",
      "ID .0 representation", "Clone/Copy",
      "receiver and associated function ABI", "parameter borrowing"};
  return result.dump();
}
} // namespace agsem
