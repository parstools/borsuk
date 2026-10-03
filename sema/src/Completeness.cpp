#include "CompletenessInternal.h"
#include "agas/artifact/Sha256.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
namespace agsem {
namespace {
using J = nlohmann::ordered_json;
bool hex(std::string_view s) {
  return s.size() == 64 && std::ranges::all_of(s, [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
auto string(const ast::Value &value) -> std::string {
  try {
    return J::parse(ast::token(value)).get<std::string>();
  } catch (const std::exception &error) {
    throw std::runtime_error(
        std::string{"completeness.invalid_metadata: expected JSON string: "} +
        error.what());
  }
}
} // namespace
auto alternativeSyntaxHash(const agas::model::ParserRule &r,
                           const agas::model::ParserAlternative &a)
    -> std::string {
  using namespace agas::model;
  J commands = J::array(), elements = J::array();
  for (const auto &c : r.lexerContext)
    commands.push_back(
        J::array({c.name, c.argument ? J(*c.argument) : J(nullptr)}));
  for (const auto &e : a.elements) {
    const char *kind = "";
    switch (e.symbol.kind) {
    case ParserSymbolKind::RuleReference:
      kind = "rule";
      break;
    case ParserSymbolKind::TokenReference:
      kind = "token";
      break;
    case ParserSymbolKind::Literal:
      kind = "literal";
      break;
    case ParserSymbolKind::QualifiedReference:
      kind = "qualified";
      break;
    }
    const char *quantifier = "";
    switch (e.quantifier) {
    case Quantifier::One:
      quantifier = "one";
      break;
    case Quantifier::Optional:
      quantifier = "optional";
      break;
    case Quantifier::ZeroOrMore:
      quantifier = "zero_or_more";
      break;
    case Quantifier::OneOrMore:
      quantifier = "one_or_more";
      break;
    }
    elements.push_back(J::array(
        {e.fieldName ? J(*e.fieldName) : J(nullptr), kind, e.symbol.name,
         e.symbol.qualifier ? J(*e.symbol.qualifier) : J(nullptr),
         quantifier}));
  }
  const auto wire = J::array(
      {"agsem-alternative-v1", r.name,
       r.treeModifier == TreeModifier::Node ? "node" : "inline", commands,
       a.explicitEmpty, a.label ? J(*a.label) : J(nullptr), elements});
  return agas::artifact::sha256Hex(wire.dump());
}
auto alternativeIdPrefix(std::string_view grammar, std::string_view hash)
    -> std::string {
  return "alt-v1:" +
         agas::artifact::sha256Hex(
             J::array({"agsem-alt-id-v1", grammar, hash}).dump()) +
         ":";
}
auto readAnalysisStatus(const ast::Value &node) -> AnalysisStatus try {
  AnalysisStatus result;
  result.location = ast::location(node);
  std::set<std::string> keys;
  for (const auto &entry : ast::field(node, "entries").elements) {
    const auto name = ast::token(ast::field(entry, "name"));
    if (!keys.insert(name).second)
      throw std::runtime_error("duplicate analysis_status field: " + name);
    const auto &value = ast::field(entry, "value");
    if (name == "id")
      result.id.value = string(value);
    else if (name == "syntax_sha256")
      result.syntaxSha256 = string(value);
    else if (name == "reason")
      result.reason = string(value);
    else if (name == "state") {
      const auto state = ast::token(value);
      if (state == "pending")
        result.declaredState = ObligationState::Pending;
      else if (state == "implemented")
        result.declaredState = ObligationState::Implemented;
      else if (state == "no_action")
        result.declaredState = ObligationState::NoAction;
      else
        throw std::runtime_error("unknown analysis_status state: " + state);
    } else
      throw std::runtime_error("unknown analysis_status field: " + name);
  }
  if (!keys.contains("id") || !keys.contains("syntax_sha256") ||
      !keys.contains("state"))
    throw std::runtime_error(
        "analysis_status requires id, syntax_sha256 and state");
  const auto &id = result.id.value;
  if (id.size() < 73 || !id.starts_with("alt-v1:") ||
      !hex(std::string_view(id).substr(7, 64)) || id[71] != ':')
    throw std::runtime_error("invalid AlternativeId");
  const auto counter = std::string_view(id).substr(72);
  if (counter.empty() || (counter.size() > 1 && counter.front() == '0') ||
      !std::ranges::all_of(counter,
                           [](char c) { return c >= '0' && c <= '9'; }))
    throw std::runtime_error("invalid AlternativeId counter");
  if (!hex(result.syntaxSha256))
    throw std::runtime_error("invalid alternative syntax_sha256");
  if (result.declaredState == ObligationState::NoAction &&
      std::ranges::all_of(result.reason, [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
      }))
    throw std::runtime_error("no_action requires a nonempty reason");
  return result;
} catch (const std::exception &error) {
  const std::string message = error.what();
  throw std::runtime_error(message.starts_with("completeness.")
                               ? message
                               : "completeness.invalid_metadata: " + message);
}
auto incompleteAnalysisDiagnostics(const ast::Value &root,
                                   const agas::model::SyntaxDocument &grammar)
    -> std::vector<Diagnostic> {
  std::vector<Diagnostic> result;
  const auto &rules = ast::field(root, "rules").elements;
  for (std::size_t ri = 0; ri < rules.size(); ++ri) {
    std::vector<const ast::Value *> alternatives{
        &ast::field(rules[ri], "first")};
    for (const auto &a : ast::field(rules[ri], "rest").elements)
      alternatives.push_back(&a);
    for (std::size_t ai = 0; ai < alternatives.size(); ++ai) {
      const auto *actions = ast::find(*alternatives[ai], "actions");
      if (!actions)
        continue;
      for (const auto &action : actions->elements) {
        if (action.typeName != "analysisStatus")
          continue;
        const auto status = readAnalysisStatus(action);
        if (status.syntaxSha256 !=
            alternativeSyntaxHash(
                grammar.parserRules.at(ri),
                grammar.parserRules.at(ri).alternatives.at(ai)))
          result.push_back({Severity::Error, "completeness.stale_alternative",
                            "analysis_status syntax signature no longer "
                            "matches this alternative",
                            status.location, status.id.value});
        if (status.declaredState == ObligationState::Implemented &&
            !ast::optional(ast::field(root, "model")))
          result.push_back({Severity::Error,
                            "completeness.invalid_implementation_claim",
                            "implemented requires a semantic model and a "
                            "checked analyzer interface",
                            status.location, status.id.value});
        if (status.declaredState == ObligationState::NoAction &&
            !ast::optional(ast::field(root, "model")))
          result.push_back({Severity::Error, "completeness.invalid_no_action",
                            "no_action requires a semantic model with an "
                            "explicit Unit interface",
                            status.location, status.id.value});
        if (status.declaredState == ObligationState::Pending)
          result.push_back({Severity::Error, "completeness.pending",
                            "semantic action is pending", status.location,
                            status.id.value});
      }
    }
  }
  return result;
}
auto executionTemplateDiagnostics(const ast::Value &root, bool requireComplete)
    -> std::vector<Diagnostic> {
  std::vector<Diagnostic> result;
  const auto *section = ast::find(root, "obligations");
  if (!section || !ast::optional(*section))
    return result;
  const auto &node = *ast::optional(*section);
  std::set<std::string> targets, keys;
  bool foundTargets = false;
  for (const auto &entry : ast::field(node, "entries").elements) {
    const auto name = ast::token(ast::field(entry, "name"));
    const auto &values = ast::field(entry, "values").elements;
    const auto invalid = [&](std::string message) {
      result.push_back({Severity::Error,
                        "completeness.invalid_metadata",
                        std::move(message),
                        ast::location(entry),
                        {}});
    };
    if (name == "targets") {
      if (foundTargets || values.empty())
        invalid("execution_obligations needs one nonempty targets entry");
      foundTargets = true;
      for (const auto &value : values) {
        const auto target = string(value);
        if ((target != "interpreter" && target != "c" && target != "llvm") ||
            !targets.insert(target).second)
          invalid("unknown or repeated execution template target: " + target);
      }
    } else if (name == "pending") {
      if (values.size() != 4) {
        invalid("pending needs kind, subject, slot and reason strings");
        continue;
      }
      const auto kind = string(values[0]), subject = string(values[1]),
                 slot = string(values[2]), reason = string(values[3]);
      const std::set<std::string> kinds{
          "execution_function", "execute_handler", "evaluate_handler",
          "execution_contract", "lowering_role",   "backend_binding"};
      if (!kinds.contains(kind) || subject.empty() || slot.empty() ||
          reason.empty())
        invalid("invalid execution template requirement");
      if (!keys.insert(J::array({kind, subject, slot}).dump()).second)
        invalid("duplicate execution template requirement");
      if (requireComplete)
        result.push_back({Severity::Error, "completeness.pending", reason,
                          ast::location(entry),
                          kind + "/" + subject + "/" + slot});
    } else
      invalid("unknown execution_obligations entry: " + name);
  }
  if (!foundTargets)
    result.push_back({Severity::Error,
                      "completeness.invalid_metadata",
                      "execution_obligations requires targets",
                      ast::location(node),
                      {}});
  return result;
}

auto generationDiagnostics(const CompletenessReport &report,
                           bool requireEmitter) -> std::vector<Diagnostic> {
  std::vector<Diagnostic> errors;
  for (auto diagnostic : report.diagnostics) {
    if (diagnostic.code == "completeness.pending")
      diagnostic.severity = Severity::Error;
    if (diagnostic.severity == Severity::Error)
      errors.push_back(std::move(diagnostic));
  }
  for (auto target : report.targets) {
    const auto result =
        std::ranges::find(report.results, target, &TargetCompleteness::target);
    if (result == report.results.end() || !result->complete) {
      if (errors.empty())
        errors.push_back({Severity::Error,
                          "completeness.pending",
                          "generation requires a complete " +
                              std::string(targetName(target)) + " model",
                          {},
                          std::string(targetName(target))});
    } else if (requireEmitter && !result->canEmit)
      errors.push_back({Severity::Error,
                        "document.unsupported_construct",
                        "the complete " + std::string(targetName(target)) +
                            " model has unresolved emitter prerequisites",
                        {},
                        std::string(targetName(target))});
  }
  if (report.targets.empty())
    errors.push_back({Severity::Error,
                      "completeness.pending",
                      "generation has no validated targets",
                      {},
                      {}});
  return errors;
}
} // namespace agsem
