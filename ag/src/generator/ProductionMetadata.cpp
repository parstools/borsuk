#include "agas/generator/ProductionMetadata.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace agas::generator {
namespace {

auto roleName(zbik::EbnfGeneratedRuleRole role) -> std::string_view {
  switch (role) {
  case zbik::EbnfGeneratedRuleRole::SourceAlternative:
    return "source";
  case zbik::EbnfGeneratedRuleRole::OptionalPresent:
    return "optional-present";
  case zbik::EbnfGeneratedRuleRole::OptionalEmpty:
    return "optional-empty";
  case zbik::EbnfGeneratedRuleRole::RepetitionRecursive:
    return "repetition-recursive";
  case zbik::EbnfGeneratedRuleRole::RepetitionBase:
    return "repetition-base";
  }
  throw std::logic_error("unknown generated production role");
}

auto repetitionName(zbik::Repetition repetition) -> std::string_view {
  switch (repetition) {
  case zbik::Repetition::One:
    return "one";
  case zbik::Repetition::Optional:
    return "optional";
  case zbik::Repetition::ZeroOrMore:
    return "zero-or-more";
  case zbik::Repetition::OneOrMore:
    return "one-or-more";
  }
  throw std::logic_error("unknown EBNF repetition");
}

auto stableIdentity(const model::ParserRule &rule,
                    const model::ParserAlternative &alternative,
                    const model::BnfProductionOrigin &origin) -> std::string {
  std::string result = "rule:" + rule.name + "/alternative:" +
                       std::to_string(origin.alternativeIndex);
  if (alternative.label) result += ":" + *alternative.label;
  if (origin.elementIndex) {
    result += "/element:" + std::to_string(*origin.elementIndex);
    const model::ParserElement &element =
        alternative.elements.at(*origin.elementIndex);
    if (element.fieldName) result += ":" + *element.fieldName;
    result += "/" + std::string{repetitionName(origin.repetition)};
  }
  result += "/" + std::string{roleName(origin.role)};
  return result;
}

} // namespace

ProductionMetadata::ProductionMetadata(
    std::vector<RuntimeProductionMetadata> runtime,
    std::vector<DiagnosticProductionMetadata> diagnostic)
    : runtime_(std::move(runtime)), diagnostic_(std::move(diagnostic)) {
  if (runtime_.size() != diagnostic_.size()) {
    throw std::invalid_argument(
        "runtime and diagnostic production metadata counts differ");
  }
  std::unordered_set<std::string> identities;
  for (std::size_t index = 0; index < runtime_.size(); ++index) {
    const zbik::RuleId expected{static_cast<std::uint32_t>(index)};
    if (runtime_[index].rule != expected ||
        diagnostic_[index].rule != expected) {
      throw std::invalid_argument(
          "production metadata must follow RuleId order");
    }
    if (!identities.insert(diagnostic_[index].stableIdentity).second) {
      throw std::invalid_argument(
          "production stable identities must be unique");
    }
  }
}

auto ProductionMetadata::runtime() const noexcept
    -> const std::vector<RuntimeProductionMetadata> & {
  return runtime_;
}

auto ProductionMetadata::diagnostic() const noexcept
    -> const std::vector<DiagnosticProductionMetadata> & {
  return diagnostic_;
}

auto ProductionMetadata::runtime(zbik::RuleId rule) const
    -> const RuntimeProductionMetadata & {
  return runtime_.at(zbik::toIndex(rule));
}

auto ProductionMetadata::diagnostic(zbik::RuleId rule) const
    -> const DiagnosticProductionMetadata & {
  return diagnostic_.at(zbik::toIndex(rule));
}

auto buildProductionMetadata(const model::SyntaxDocument &document,
                             const model::BnfModel &bnf)
    -> ProductionMetadata {
  std::vector<RuntimeProductionMetadata> runtime;
  std::vector<DiagnosticProductionMetadata> diagnostic;
  runtime.reserve(bnf.grammar().ruleCount());
  diagnostic.reserve(bnf.grammar().ruleCount());
  for (const zbik::Rule &rule : bnf.grammar().rules()) {
    const model::BnfProductionOrigin &origin = bnf.origin(rule.id());
    const model::ParserRule &sourceRule =
        document.parserRules.at(origin.sourceRuleIndex);
    const model::ParserAlternative &alternative =
        sourceRule.alternatives.at(origin.alternativeIndex);
    std::optional<std::string> fieldName;
    if (origin.elementIndex) {
      fieldName = alternative.elements.at(*origin.elementIndex).fieldName;
    }
    runtime.push_back({rule.id(), rule.lhs(), rule.size()});
    diagnostic.push_back(
        {rule.id(), stableIdentity(sourceRule, alternative, origin),
         sourceRule.name, origin.sourceRuleIndex, origin.alternativeIndex,
         alternative.label, origin.elementIndex, std::move(fieldName),
         origin.repetition, origin.role, origin.helperName, origin.sourceSpan});
  }
  return {std::move(runtime), std::move(diagnostic)};
}

} // namespace agas::generator
