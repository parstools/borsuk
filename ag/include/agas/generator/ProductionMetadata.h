#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "agas/model/BnfLowering.h"
#include "agas/model/SyntaxModel.h"
#include "ebnf/EbnfGrammar.h"
#include "ebnf/EbnfToBnf.h"
#include "grammar/Identifiers.h"

namespace agas::generator {

struct RuntimeProductionMetadata {
  zbik::RuleId rule;
  zbik::NonterminalId lhs;
  std::size_t rhsLength{};

  auto operator==(const RuntimeProductionMetadata &) const -> bool = default;
};

struct DiagnosticProductionMetadata {
  zbik::RuleId rule;
  // Stable for the same source structure and independent of generated RuleId
  // or a collision-adjusted EBNF helper name.
  std::string stableIdentity;
  std::string sourceRuleName;
  std::size_t sourceRuleIndex{};
  std::size_t alternativeIndex{};
  std::optional<std::string> alternativeLabel;
  std::optional<std::size_t> elementIndex;
  std::optional<std::string> fieldName;
  zbik::Repetition repetition{zbik::Repetition::One};
  zbik::EbnfGeneratedRuleRole role{};
  std::optional<std::string> helperName;
  model::SourceSpan sourceSpan;

  auto operator==(const DiagnosticProductionMetadata &) const
      -> bool = default;
};

class ProductionMetadata {
public:
  ProductionMetadata(std::vector<RuntimeProductionMetadata> runtime,
                     std::vector<DiagnosticProductionMetadata> diagnostic);

  [[nodiscard]] auto runtime() const noexcept
      -> const std::vector<RuntimeProductionMetadata> &;
  [[nodiscard]] auto diagnostic() const noexcept
      -> const std::vector<DiagnosticProductionMetadata> &;
  [[nodiscard]] auto runtime(zbik::RuleId rule) const
      -> const RuntimeProductionMetadata &;
  [[nodiscard]] auto diagnostic(zbik::RuleId rule) const
      -> const DiagnosticProductionMetadata &;

private:
  std::vector<RuntimeProductionMetadata> runtime_;
  std::vector<DiagnosticProductionMetadata> diagnostic_;
};

[[nodiscard]] auto buildProductionMetadata(
    const model::SyntaxDocument &document, const model::BnfModel &bnf)
    -> ProductionMetadata;

} // namespace agas::generator
