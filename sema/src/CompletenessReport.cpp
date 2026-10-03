#include "agsem/Completeness.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>
namespace agsem {
namespace {
using J = nlohmann::ordered_json;
const char *owner(ObligationOwner v) {
  return v == ObligationOwner::Sema ? "sema" : "coge";
}
const char *kind(ObligationKind v) {
  static constexpr const char *names[]{
      "analysis_action",    "analyzer_interface", "inherited_input",
      "semantic_function",  "semantic_binding",   "semantic_contract",
      "execution_function", "execute_handler",    "evaluate_handler",
      "execution_contract", "lowering_role",      "backend_binding"};
  return names[static_cast<unsigned>(v)];
}
const char *state(ObligationState v) {
  return v == ObligationState::Pending       ? "pending"
         : v == ObligationState::Implemented ? "implemented"
                                             : "no_action";
}
auto subject(const ObligationSubject &value) -> J {
  return std::visit(
      [](const auto &s) -> J {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, AlternativeId>)
          return {{"alternative_id", s.value}};
        else if constexpr (std::is_same_v<T, AnalyzerSubject>)
          return {{"analyzer", s.name}};
        else if constexpr (std::is_same_v<T, FunctionSubject>)
          return {{"function", s.name}};
        else if constexpr (std::is_same_v<T, ContractSubject>)
          return {
              {"manifest", s.manifest}, {"owner", s.owner}, {"name", s.name}};
        else
          return {{"family", s.family}, {"policy", s.policy}, {"role", s.role}};
      },
      value);
}
auto key(const ObligationKey &k) -> J {
  return {{"owner", owner(k.owner)},
          {"kind", kind(k.kind)},
          {"subject", subject(k.subject)},
          {"slot", k.slot}};
}
auto type(const TypeRef &t) -> J {
  J args = J::array();
  for (const auto &a : t.arguments)
    args.push_back(type(a));
  return {{"name", t.name}, {"arguments", args}};
}
auto function(const FunctionContract &f) -> J {
  J parameters = J::array();
  for (const auto &p : f.parameters)
    parameters.push_back(type(p));
  return {{"parameters", parameters},
          {"result", type(f.result)},
          {"mutates", f.mutates},
          {"context", f.context}};
}
auto requirement(const ObligationRequirement &r) -> J {
  return std::visit(
      [](const auto &v) -> J {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, UnresolvedRequirement>)
          return {{"unresolved", v.reason}};
        else if constexpr (std::is_same_v<T, TypeRef>)
          return {{"type", type(v)}};
        else if constexpr (std::is_same_v<T, FunctionContract>)
          return {{"signature", function(v)}};
        else {
          J types = J::array();
          for (const auto &t : v.types)
            types.push_back(type(t));
          return {{"payload", types}};
        }
      },
      r);
}
bool required(const Obligation &o, CompletenessTarget t) {
  return std::ranges::find(o.requiredFor, t) != o.requiredFor.end();
}
} // namespace
auto targetName(CompletenessTarget t) -> std::string_view {
  static constexpr std::string_view names[]{"analysis", "interpreter",
                                            "lowering", "c", "llvm"};
  return names[static_cast<unsigned>(t)];
}
auto obligationKey(const ObligationKey &k) -> std::string {
  return key(k).dump();
}
auto CompletenessReport::hasErrors() const -> bool {
  return std::ranges::any_of(
      diagnostics, [](const auto &d) { return d.severity == Severity::Error; });
}
auto CompletenessReport::complete() const -> bool {
  return !results.empty() &&
         std::ranges::all_of(results, [](const auto &r) { return r.complete; });
}
void finalizeCompleteness(CompletenessReport &report) {
  std::ranges::sort(report.targets);
  report.targets.erase(
      std::unique(report.targets.begin(), report.targets.end()),
      report.targets.end());
  std::ranges::sort(report.obligations, {},
                    [](const auto &o) { return obligationKey(o.key); });
  for (auto &o : report.obligations) {
    std::ranges::sort(o.requiredFor);
    o.requiredFor.erase(std::unique(o.requiredFor.begin(), o.requiredFor.end()),
                        o.requiredFor.end());
    std::ranges::sort(o.dependsOn, {},
                      [](const auto &key) { return obligationKey(key); });
    o.dependsOn.erase(std::unique(o.dependsOn.begin(), o.dependsOn.end()),
                      o.dependsOn.end());
  }
  // Propagate only unmet dependencies; fulfilled recursive interfaces remain
  // valid.
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto &o : report.obligations) {
      if (o.state == ObligationState::Pending ||
          o.validation != ObligationValidation::Valid)
        continue;
      for (const auto &dependency : o.dependsOn) {
        const auto it = std::ranges::find_if(
            report.obligations,
            [&](const auto &candidate) { return candidate.key == dependency; });
        if (it == report.obligations.end() ||
            it->state == ObligationState::Pending ||
            it->validation != ObligationValidation::Valid) {
          o.state = ObligationState::Pending;
          o.validation = ObligationValidation::Blocked;
          o.reason = "A required dependency is incomplete.";
          changed = true;
          break;
        }
      }
    }
  }
  // Re-finalization after adding execution obligations must not duplicate
  // warnings.
  report.diagnostics.erase(
      std::remove_if(
          report.diagnostics.begin(), report.diagnostics.end(),
          [](const auto &d) { return d.code == "completeness.pending"; }),
      report.diagnostics.end());
  for (auto &o : report.obligations) {
    o.diagnostics.clear();
    if (o.state == ObligationState::Pending &&
        std::ranges::any_of(report.targets,
                            [&](auto t) { return required(o, t); }))
      report.diagnostics.push_back(
          {Severity::Warning, "completeness.pending",
           o.reason.empty() ? "Required obligation is incomplete." : o.reason,
           o.location, obligationKey(o.key), o.related});
  }
  for (std::size_t i = 0; i < report.diagnostics.size(); ++i)
    for (auto &o : report.obligations) {
      const auto &d = report.diagnostics[i];
      if (d.subject == obligationKey(o.key) ||
          (d.location && o.location &&
           d.location->beginByte >= o.location->beginByte &&
           d.location->endByte <= o.location->endByte))
        o.diagnostics.push_back(i);
    }
  report.results.clear();
  for (auto target : report.targets) {
    TargetCompleteness result{target, true, true};
    for (const auto &o : report.obligations) {
      if (!required(o, target))
        continue;
      if (o.state == ObligationState::Pending)
        ++result.pending;
      else if (o.state == ObligationState::Implemented)
        ++result.implemented;
      else
        ++result.noAction;
      result.complete &= o.state != ObligationState::Pending &&
                         o.validation == ObligationValidation::Valid;
    }
    result.complete &=
        std::ranges::none_of(report.diagnostics, [&](const auto &d) {
          if (d.severity != Severity::Error)
            return false;
          if (d.code.starts_with("coge.") &&
              target == CompletenessTarget::Analysis)
            return false;
          return true;
        });
    result.canEmit =
        result.complete && std::ranges::find(report.emissionBlocked, target) ==
                               report.emissionBlocked.end();
    report.results.push_back(result);
  }
}
auto serializeCompleteness(const CompletenessReport &r, std::string_view source,
                           std::string_view filename) -> std::string {
  const auto location = [&](const std::optional<SourceLocation> &l) -> J {
    if (!l)
      return nullptr;
    std::size_t line = 1, column = 1;
    for (std::size_t i = 0;
         i < std::min<std::size_t>(l->beginByte, source.size()); ++i) {
      if (source[i] == '\n') {
        ++line;
        column = 1;
      } else if ((static_cast<unsigned char>(source[i]) & 0xc0) != 0x80)
        ++column;
    }
    return {{"file", filename},
            {"source", l->source},
            {"begin_byte", l->beginByte},
            {"end_byte", l->endByte},
            {"line", line},
            {"column", column}};
  };
  J output{{"format", "agsem-completeness-v1"},
           {"source_sha256", r.sourceIdentity},
           {"contracts", J::array()},
           {"targets", J::array()},
           {"obligations", J::array()},
           {"diagnostics", J::array()},
           {"results", J::array()}};
  for (const auto &c : r.contracts)
    output["contracts"].push_back(
        {{"id", c.id}, {"version", c.version}, {"sha256", c.sha256}});
  for (auto t : r.targets)
    output["targets"].push_back(targetName(t));
  for (const auto &o : r.obligations) {
    J deps = J::array(), targets = J::array(), related = J::array();
    for (const auto &d : o.dependsOn)
      deps.push_back(key(d));
    for (auto t : o.requiredFor)
      targets.push_back(targetName(t));
    for (const auto &l : o.related)
      related.push_back(location(l));
    J evidence = nullptr;
    if (o.evidence) {
      static constexpr const char *names[]{
          "checked_body",    "checked_forwarding", "checked_profile",
          "checked_binding", "external_contract",  "checked_no_action"};
      evidence = names[static_cast<unsigned>(*o.evidence)];
    }
    J origins = J::array();
    for (const auto &origin : o.contractOrigins)
      origins.push_back({{"id", origin.identity.id},
                         {"version", origin.identity.version},
                         {"sha256", origin.identity.sha256},
                         {"path", origin.path}});
    output["obligations"].push_back(
        {{"key", key(o.key)},
         {"requirement", requirement(o.requirement)},
         {"declared_state",
          o.declaredState ? J(state(*o.declaredState)) : J(nullptr)},
         {"state", state(o.state)},
         {"validation", o.validation == ObligationValidation::Valid ? "valid"
                        : o.validation == ObligationValidation::Invalid
                            ? "invalid"
                            : "blocked"},
         {"evidence", evidence},
         {"identity_persisted", o.identityPersisted},
         {"inferred", o.inferred},
         {"depends_on", deps},
         {"required_for", targets},
         {"location", location(o.location)},
         {"related", related},
         {"rule", o.rule},
         {"label", o.label ? J(*o.label) : J(nullptr)},
         {"ordinal", o.ordinal ? J(*o.ordinal) : J(nullptr)},
         {"contract_origins", origins},
         {"reason", o.reason},
         {"diagnostics", o.diagnostics}});
  }
  for (std::size_t i = 0; i < r.diagnostics.size(); ++i) {
    const auto &d = r.diagnostics[i];
    J related = J::array();
    for (const auto &l : d.related)
      related.push_back(location(l));
    output["diagnostics"].push_back(
        {{"id", i},
         {"severity", d.severity == Severity::Error ? "error" : "warning"},
         {"code", d.code},
         {"message", d.message},
         {"subject", d.subject},
         {"location", location(d.location)},
         {"related", related}});
  }
  for (const auto &s : r.results)
    output["results"].push_back({{"target", targetName(s.target)},
                                 {"complete", s.complete},
                                 {"can_emit", s.canEmit},
                                 {"pending", s.pending},
                                 {"implemented", s.implemented},
                                 {"no_action", s.noAction}});
  return output.dump(2) + '\n';
}
} // namespace agsem
