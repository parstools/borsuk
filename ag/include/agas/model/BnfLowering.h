#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "agas/model/SyntaxModel.h"
#include "ebnf/EbnfToBnf.h"

namespace agas::model {

enum class TerminalBindingKind { LexerRule, Literal, QualifiedReference };

struct TerminalBinding {
  std::string internalName;
  TerminalBindingKind kind{};
  std::string spelling;
  std::optional<std::string> qualifier;
  SourceSpan sourceSpan;

  auto operator==(const TerminalBinding &) const -> bool = default;
};

struct BnfProductionOrigin {
  zbik::RuleId generatedRule;
  std::size_t sourceRuleIndex{};
  std::size_t alternativeIndex{};
  std::optional<std::size_t> elementIndex;
  zbik::Repetition repetition{zbik::Repetition::One};
  zbik::EbnfGeneratedRuleRole role{};
  std::optional<std::string> helperName;
  SourceSpan sourceSpan;

  auto operator==(const BnfProductionOrigin &) const -> bool = default;
};

class BnfModel {
public:
  BnfModel(zbik::Grammar grammar, std::vector<BnfProductionOrigin> origins,
           std::vector<TerminalBinding> terminals);

  [[nodiscard]] auto grammar() const noexcept -> const zbik::Grammar &;
  [[nodiscard]] auto origins() const noexcept
      -> const std::vector<BnfProductionOrigin> &;
  [[nodiscard]] auto origin(zbik::RuleId rule) const
      -> const BnfProductionOrigin &;
  [[nodiscard]] auto terminals() const noexcept
      -> const std::vector<TerminalBinding> &;

private:
  zbik::Grammar grammar_;
  std::vector<BnfProductionOrigin> origins_;
  std::vector<TerminalBinding> terminals_;
};

// The document must pass validateSyntaxModel() before it is lowered.
[[nodiscard]] auto lowerToBnf(const SyntaxDocument &document) -> BnfModel;

} // namespace agas::model
