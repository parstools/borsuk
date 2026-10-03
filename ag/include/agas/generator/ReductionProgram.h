#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "agas/model/BnfLowering.h"
#include "agas/model/SyntaxModel.h"
#include "grammar/Identifiers.h"

namespace agas::generator {

// This instruction set describes semantic reductions without embedding code
// for any target language. All RHS indices refer to the generated BNF rule.
enum class ReductionOpcode {
  Unit,
  Forward,
  ConstructNode,
  ConstructNodeOrForward,
  ConstructRecord,
  OptionalSome,
  OptionalNone,
  ListEmpty,
  ListSingleton,
  ListAppend,
};

enum class ReductionSpanPolicy {
  MatchedRhs,
  EmptyAtLookahead,
};

struct ReductionField {
  std::string name;
  std::uint32_t rhsIndex{};

  auto operator==(const ReductionField &) const -> bool = default;
};

struct ReductionInstruction {
  zbik::RuleId rule;
  std::uint32_t rhsLength{};
  ReductionOpcode opcode{};
  ReductionSpanPolicy spanPolicy{};
  std::optional<std::string> typeName;
  std::optional<std::string> variantName;
  std::vector<std::uint32_t> operands;
  std::vector<ReductionField> fields;

  auto operator==(const ReductionInstruction &) const -> bool = default;
};

class AstReductionProgram {
public:
  explicit AstReductionProgram(std::vector<ReductionInstruction> instructions);

  [[nodiscard]] auto instructions() const noexcept
      -> const std::vector<ReductionInstruction> &;
  [[nodiscard]] auto instruction(zbik::RuleId rule) const
      -> const ReductionInstruction &;

private:
  std::vector<ReductionInstruction> instructions_;
};

[[nodiscard]] auto
buildAstReductionProgram(const model::SyntaxDocument &document,
                         const model::BnfModel &bnf) -> AstReductionProgram;

} // namespace agas::generator
