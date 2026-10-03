#include "agsem/SemanticCompleteness.h"
#include "CompletenessInternal.h"
#include "DocumentStorage.h"
#include "ModelBindingsInternal.h"
#include "SemanticInputStorage.h"
#include <algorithm>
#include <set>
namespace agsem {
namespace {
auto interfaceKey(std::string name) -> ObligationKey {
  return {ObligationOwner::Sema, ObligationKind::AnalyzerInterface,
          AnalyzerSubject{std::move(name)}, "result"};
}
void directCalls(const ast::Value &value, std::set<std::string> &calls) {
  if (value.typeName == "actionUnary" && value.elements.size() == 3) {
    const auto &atom = value.elements[1];
    const auto &suffixes = value.elements[2];
    if (atom.kind == ast::Kind::Token && !suffixes.elements.empty() &&
        suffixes.elements.front().typeName == "actionPostfix" &&
        suffixes.elements.front().variantName == "Call")
      calls.insert(atom.tokenText);
  }
  for (const auto &child : value.elements)
    directCalls(child, calls);
}
void fulfilled(Obligation &o, ObligationEvidence evidence) {
  o.state = ObligationState::Implemented;
  o.validation = ObligationValidation::Valid;
  o.evidence = evidence;
}
} // namespace
auto assessCompleteness(const ParsedDocument &parsed,
                        const ContractEnvironment &contracts)
    -> Outcome<CompletenessReport> {
  CompletenessReport report;
  report.sourceIdentity = parsed.identity();
  report.contracts = contracts.identities();
  report.diagnostics = validateDocumentSections(parsed);
  auto document = DocumentAccess::sema(parsed);
  auto bound = bindSemantics(document, contracts);
  report.diagnostics.insert(report.diagnostics.end(), bound.diagnostics.begin(),
                            bound.diagnostics.end());
  auto input = bound.value ? boundSemanticInput(*bound.value)
                           : SemanticInputAccess::withContracts(
                                 document.semanticInput(), contracts);
  const auto &root = SemanticInputAccess::root(input);
  const auto &grammar = parsed.grammar().grammar;
  auto review = reviewSemanticFragments(input);
  if (!review.canEmit)
    report.emissionBlocked.push_back(CompletenessTarget::Analysis);
  report.diagnostics.insert(report.diagnostics.end(),
                            review.diagnostics.begin(),
                            review.diagnostics.end());
  Obligation context{{ObligationOwner::Sema, ObligationKind::SemanticContract,
                      ContractSubject{"", "semantic", "context"},
                      "rust_context"}};
  context.requiredFor = {CompletenessTarget::Analysis};
  context.location = ast::location(root);
  context.reason = "Declare a semantic model and its context.";
  if (const auto *model = ast::optional(ast::field(root, "model"))) {
    context.location = ast::location(*model);
    for (const auto &d : ast::field(*model, "declarations").elements)
      if (d.typeName == "rustContextDeclaration") {
        context.requirement = TypeRef{ast::token(ast::field(d, "name")), {}};
        context.location = ast::location(d);
        if (bound.value)
          fulfilled(context, ObligationEvidence::ExternalContract);
      }
  }
  const auto contextKey = context.key;
  report.obligations.push_back(std::move(context));
  std::set<std::string> ids;
  for (const auto &status : parsed.analysisStatuses())
    ids.insert(status.id.value);
  const auto &rawRules = ast::field(root, "rules").elements;
  for (std::size_t ri = 0; ri < grammar.parserRules.size(); ++ri) {
    const auto &rule = grammar.parserRules[ri];
    const auto &rawRule = rawRules.at(ri);
    Obligation interface{interfaceKey(rule.name)};
    interface.location = ast::location(rawRule);
    interface.requiredFor = {CompletenessTarget::Analysis};
    interface.reason =
        "Resolve the analyzer result and inherited parameter types.";
    const auto found = review.interfaces.find(rule.name);
    if (found != review.interfaces.end()) {
      interface.requirement = found->second;
      interface.inferred = true;
      fulfilled(interface, ObligationEvidence::CheckedBody);
      for (std::size_t i = 0; i < found->second.parameters.size(); ++i) {
        const auto &name = review.inputNames.at(rule.name).at(i);
        Obligation inherited{{ObligationOwner::Sema,
                              ObligationKind::InheritedInput,
                              AnalyzerSubject{rule.name}, name}};
        inherited.requirement = found->second.parameters[i];
        inherited.location = interface.location;
        inherited.requiredFor = {CompletenessTarget::Analysis};
        fulfilled(inherited, ObligationEvidence::CheckedBody);
        report.obligations.push_back(std::move(inherited));
      }
    }
    report.obligations.push_back(std::move(interface));
    std::vector<const ast::Value *> alternatives{&ast::field(rawRule, "first")};
    for (const auto &a : ast::field(rawRule, "rest").elements)
      alternatives.push_back(&a);
    for (std::size_t ai = 0; ai < rule.alternatives.size(); ++ai) {
      const auto &a = rule.alternatives[ai];
      const auto status =
          std::ranges::find_if(parsed.analysisStatuses(), [&](const auto &s) {
            return s.rule == rule.name && s.ordinal == ai;
          });
      AlternativeId id;
      if (status != parsed.analysisStatuses().end())
        id = status->id;
      else {
        const auto prefix = alternativeIdPrefix(grammar.grammarName,
                                                alternativeSyntaxHash(rule, a));
        std::size_t counter = 0;
        do {
          id.value = prefix + std::to_string(counter++);
        } while (ids.contains(id.value));
        ids.insert(id.value);
      }
      Obligation o{{ObligationOwner::Sema, ObligationKind::AnalysisAction, id,
                    "result"}};
      o.rule = rule.name;
      o.label = a.label;
      o.ordinal = ai;
      o.location = ast::location(*alternatives.at(ai));
      o.requiredFor = {CompletenessTarget::Analysis};
      o.dependsOn = {interfaceKey(rule.name), contextKey};
      o.reason = "Provide a checked analysis result or checked forwarding.";
      if (found != review.interfaces.end())
        o.requirement = found->second.result;
      const auto fragment =
          std::ranges::find_if(review.alternatives, [&](const auto &r) {
            return r.rule == rule.name && r.ordinal == ai;
          });
      if (fragment != review.alternatives.end()) {
        for (const auto &dependency : fragment->dependencies)
          o.dependsOn.push_back(interfaceKey(dependency));
        for (auto issue : fragment->diagnostics) {
          if (!issue.location)
            issue.location = o.location;
          issue.subject = obligationKey(o.key);
          report.diagnostics.push_back(std::move(issue));
        }
        if (!fragment->diagnostics.empty())
          o.validation = ObligationValidation::Invalid;
        else if (fragment->bodyChecked && fragment->hasResult &&
                 found != review.interfaces.end()) {
          fulfilled(o, fragment->forwarding
                           ? ObligationEvidence::CheckedForwarding
                           : ObligationEvidence::CheckedBody);
          o.inferred = true;
        }
        if (!fragment->representation)
          report.emissionBlocked.push_back(CompletenessTarget::Analysis);
      }
      if (status != parsed.analysisStatuses().end()) {
        o.declaredState = status->declaredState;
        o.identityPersisted = true;
        o.reason = status->reason;
        o.related.push_back(status->location);
        const bool stale = status->syntaxSha256 != status->currentSyntaxSha256;
        if (stale) {
          o.state = ObligationState::Pending;
          o.validation = ObligationValidation::Invalid;
          report.diagnostics.push_back(
              {Severity::Error, "completeness.stale_alternative",
               "analysis_status signature does not match the current "
               "alternative",
               status->location, obligationKey(o.key)});
        } else if (status->declaredState == ObligationState::Pending)
          o.state = ObligationState::Pending;
        else if (status->declaredState == ObligationState::NoAction) {
          bool explicitUnit = false;
          if (const auto *model = ast::optional(ast::field(root, "model")))
            for (const auto &d : ast::field(*model, "declarations").elements)
              if (d.typeName == "analyzerSignature" &&
                  ast::token(ast::field(d, "name")) == rule.name &&
                  found != review.interfaces.end() &&
                  found->second.result == TypeRef{"Unit", {}})
                explicitUnit = true;
          if (explicitUnit && fragment != review.alternatives.end() &&
              fragment->noAction && fragment->bodyChecked) {
            o.state = ObligationState::NoAction;
            o.validation = ObligationValidation::Valid;
            o.evidence = ObligationEvidence::CheckedNoAction;
          } else {
            o.state = ObligationState::Pending;
            o.validation = ObligationValidation::Invalid;
            report.diagnostics.push_back(
                {Severity::Error, "completeness.invalid_no_action",
                 "no_action requires an explicit Unit analyzer interface and "
                 "no instructions",
                 status->location, obligationKey(o.key)});
          }
        } else if (o.state != ObligationState::Implemented) {
          o.validation = ObligationValidation::Invalid;
          report.diagnostics.push_back(
              {Severity::Error, "completeness.invalid_implementation_claim",
               "implemented requires a checked analysis result and interface",
               status->location, obligationKey(o.key)});
        }
      }
      report.obligations.push_back(std::move(o));
    }
  }
  if (const auto *model = ast::optional(ast::field(root, "model"))) {
    for (const auto &d : ast::field(*model, "declarations").elements) {
      if (d.typeName != "functionDeclaration" &&
          d.typeName != "queryDeclaration" &&
          d.typeName != "intrinsicDeclaration")
        continue;
      const auto name = ast::token(ast::field(d, "name"));
      Obligation o{{ObligationOwner::Sema, ObligationKind::SemanticFunction,
                    FunctionSubject{name}, "implementation"}};
      o.location = ast::location(d);
      o.requiredFor = {CompletenessTarget::Analysis};
      o.dependsOn = {contextKey};
      o.reason =
          "Provide a checked function body or an external intrinsic contract.";
      if (bound.value) {
        const auto symbol =
            std::ranges::find_if(bound.value->symbols(), [&](const auto &s) {
              return s.contract.name == name && s.contract.function;
            });
        if (symbol != bound.value->symbols().end())
          o.requirement = *symbol->contract.function;
      }
      if (review.functions.contains(name) && review.functions.at(name))
        fulfilled(o, d.typeName == "intrinsicDeclaration"
                         ? ObligationEvidence::ExternalContract
                         : ObligationEvidence::CheckedBody);
      if (std::ranges::any_of(report.diagnostics, [&](const auto &d) {
            return d.code == "action.invalid_function" && d.subject == name;
          }))
        o.validation = ObligationValidation::Invalid;
      report.obligations.push_back(std::move(o));
    }
  }
  if (const auto &bindings = SemanticInputAccess::modelBindings(input)) {
    for (const auto &port : bindings->ports()) {
      Obligation o{{ObligationOwner::Sema, ObligationKind::SemanticBinding,
                    RoleSubject{port.family, port.policy, port.port},
                    port.port}};
      o.location = port.source;
      o.related = {port.policySource};
      o.requiredFor = {CompletenessTarget::Analysis};
      for (const auto &target : port.targets) {
        if (target.requiredType)
          o.requirement = *target.requiredType;
        else if (target.requiredFunction)
          o.requirement = *target.requiredFunction;
        else
          o.requirement = PayloadRequirement{target.requiredPayload};
        if (target.declaration)
          o.related.push_back(*target.declaration);
        if (target.contract)
          o.contractOrigins.push_back({*target.contract, target.contractPath});
      }
      fulfilled(o, ObligationEvidence::CheckedBinding);
      report.obligations.push_back(std::move(o));
    }
  }
  if (bound.value) {
    std::set<SymbolId> used;
    for (const auto &use : bound.value->uses())
      used.insert(use.symbol);
    for (const auto index : used) {
      const auto &symbol = bound.value->symbols().at(index);
      for (std::size_t ci = 0; ci < contracts.symbols().size(); ++ci) {
        if (contracts.symbols()[ci] != symbol.contract)
          continue;
        const auto &origin = contracts.origins().at(ci);
        Obligation o{{ObligationOwner::Sema, ObligationKind::SemanticContract,
                      ContractSubject{origin.identity.id, "semantic",
                                      symbol.contract.name},
                      "interface"}};
        o.requiredFor = {CompletenessTarget::Analysis};
        o.contractOrigins = {origin};
        if (symbol.contract.function)
          o.requirement = *symbol.contract.function;
        else
          o.requirement = TypeRef{symbol.contract.name, {}};
        for (const auto &use : bound.value->uses())
          if (use.symbol == index) {
            if (!o.location)
              o.location = use.location;
            else
              o.related.push_back(use.location);
          }
        fulfilled(o, ObligationEvidence::ExternalContract);
        report.obligations.push_back(std::move(o));
      }
    }
  }
  // Required policy ports exist even when binding validation fails.
  if (!SemanticInputAccess::modelBindings(input) &&
      hasModelBindingSchema(root)) {
    static const std::map<std::string, std::string> families{
        {"returnPolicy", "return"},       {"assignmentPolicy", "assignment"},
        {"conditionPolicy", "condition"}, {"statementIrPolicy", "statement"},
        {"flowActions", "flow"},          {"selectionPolicy", "selection"}};
    std::string contextName;
    const auto *model = ast::optional(ast::field(root, "model"));
    if (model) {
      for (const auto &d : ast::field(*model, "declarations").elements)
        if (d.typeName == "rustContextDeclaration")
          contextName = ast::token(ast::field(d, "name"));
      for (const auto &d : ast::field(*model, "declarations").elements) {
        if (!families.contains(d.typeName))
          continue;
        const auto &family = families.at(d.typeName);
        const auto policy = ast::token(ast::field(d, "name"));
        bool cleanup = false;
        for (const auto &entry : ast::field(d, "entries").elements)
          cleanup |= ast::token(ast::field(entry, "name")) == "cleanup" &&
                     ast::token(ast::field(entry, "value")) ==
                         "exited_automatic_lifetimes";
        for (const auto &[port, requirement] :
             standardBindingRequirements(family, contextName, cleanup)) {
          Obligation o{{ObligationOwner::Sema, ObligationKind::SemanticBinding,
                        RoleSubject{family, policy, port}, port}};
          o.location = ast::location(d);
          o.requiredFor = {CompletenessTarget::Analysis};
          if (requirement.function)
            o.requirement = *requirement.function;
          else if (!requirement.fields.empty())
            o.requirement = requirement.fields.front().second;
          o.reason = "Resolve and check the required policy binding port.";
          report.obligations.push_back(std::move(o));
        }
      }
    }
  }
  // Calls depend on local implementation obligations, not merely their
  // signatures.
  for (auto &o : report.obligations) {
    if (o.key.kind != ObligationKind::AnalysisAction &&
        o.key.kind != ObligationKind::SemanticFunction)
      continue;
    std::set<std::string> calls;
    if (o.location) {
      const auto visit = [&](const auto &self, const ast::Value &node) -> void {
        const auto l = ast::location(node);
        if (l.beginByte == o.location->beginByte &&
            l.endByte == o.location->endByte) {
          directCalls(node, calls);
          return;
        }
        for (const auto &child : node.elements)
          self(self, child);
      };
      visit(visit, root);
    }
    for (const auto &name : calls)
      for (const auto &candidate : report.obligations)
        if (candidate.key.kind == ObligationKind::SemanticFunction &&
            std::get<FunctionSubject>(candidate.key.subject).name == name)
          o.dependsOn.push_back(candidate.key);
  }
  finalizeCompleteness(report);
  Outcome<CompletenessReport> result;
  result.diagnostics = report.diagnostics;
  result.value = std::move(report);
  return result;
}
} // namespace agsem
