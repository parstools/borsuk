#include "agas/generator/ReductionProgram.h"

#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

#include "ebnf/EbnfToBnf.h"
#include "agas/model/AstChain.h"

namespace agas::generator {
namespace {

auto toProgramIndex(std::size_t value) -> std::uint32_t {
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    throw std::overflow_error("AST reduction RHS index exceeds uint32 range");
  }
  return static_cast<std::uint32_t>(value);
}

auto isStructuralEof(std::size_t sourceRuleIndex, std::size_t elementIndex,
                     const model::ParserAlternative &alternative) -> bool {
  const model::ParserElement &element = alternative.elements[elementIndex];
  return sourceRuleIndex == 0 &&
         elementIndex + 1 == alternative.elements.size() &&
         element.symbol.kind == model::ParserSymbolKind::TokenReference &&
         element.symbol.name == "EOF" &&
         element.quantifier == model::Quantifier::One;
}

auto sourceInstruction(const model::SyntaxDocument &document,
                       const model::ParserRule &sourceRule,
                       const model::ParserAlternative &alternative,
                       std::size_t sourceRuleIndex, const zbik::Rule &rule)
    -> ReductionInstruction {
  ReductionInstruction result;
  result.rule = rule.id();
  result.rhsLength = toProgramIndex(rule.size());
  result.spanPolicy = rule.size() == 0 ? ReductionSpanPolicy::EmptyAtLookahead
                                       : ReductionSpanPolicy::MatchedRhs;

  std::size_t rhsIndex = 0;
  for (std::size_t elementIndex = 0; elementIndex < alternative.elements.size();
       ++elementIndex) {
    const model::ParserElement &element = alternative.elements[elementIndex];
    if (isStructuralEof(sourceRuleIndex, elementIndex, alternative)) {
      continue;
    }
    if (element.fieldName.has_value()) {
      result.fields.push_back({*element.fieldName, toProgramIndex(rhsIndex)});
    }
    ++rhsIndex;
  }
  if (rhsIndex != rule.size()) {
    throw std::logic_error(
        "source AST reduction does not match generated BNF RHS");
  }

  if (sourceRule.treeModifier == model::TreeModifier::Node) {
    result.opcode = ReductionOpcode::ConstructNode;
    result.typeName = sourceRule.name;
    result.variantName = alternative.label;
    if (const auto operand = model::astChainOperand(document, alternative)) {
      result.operands = {toProgramIndex(*operand)};
      if (result.fields.size() == 1) {
        result.opcode = ReductionOpcode::Forward;
        result.typeName.reset();
        result.variantName.reset();
        result.fields.clear();
      } else {
        result.opcode = ReductionOpcode::ConstructNodeOrForward;
      }
    }
  } else if (result.fields.empty()) {
    result.opcode = ReductionOpcode::Unit;
  } else if (result.fields.size() == 1) {
    result.opcode = ReductionOpcode::Forward;
    result.operands.push_back(result.fields.front().rhsIndex);
    result.fields.clear();
  } else {
    result.opcode = ReductionOpcode::ConstructRecord;
    result.typeName = sourceRule.name;
  }
  return result;
}

auto helperInstruction(const model::BnfProductionOrigin &origin,
                       const zbik::Rule &rule) -> ReductionInstruction {
  ReductionInstruction result;
  result.rule = rule.id();
  result.rhsLength = toProgramIndex(rule.size());
  result.spanPolicy = rule.size() == 0 ? ReductionSpanPolicy::EmptyAtLookahead
                                       : ReductionSpanPolicy::MatchedRhs;
  switch (origin.role) {
  case zbik::EbnfGeneratedRuleRole::OptionalPresent:
    result.opcode = ReductionOpcode::OptionalSome;
    result.operands = {0};
    break;
  case zbik::EbnfGeneratedRuleRole::OptionalEmpty:
    result.opcode = ReductionOpcode::OptionalNone;
    break;
  case zbik::EbnfGeneratedRuleRole::RepetitionRecursive:
    result.opcode = ReductionOpcode::ListAppend;
    result.operands = {0, 1};
    break;
  case zbik::EbnfGeneratedRuleRole::RepetitionBase:
    if (origin.repetition == zbik::Repetition::ZeroOrMore) {
      result.opcode = ReductionOpcode::ListEmpty;
    } else if (origin.repetition == zbik::Repetition::OneOrMore) {
      result.opcode = ReductionOpcode::ListSingleton;
      result.operands = {0};
    } else {
      throw std::logic_error("invalid repetition base production");
    }
    break;
  case zbik::EbnfGeneratedRuleRole::SourceAlternative:
    throw std::logic_error("source production passed as EBNF helper");
  }
  return result;
}

auto expectedOperandCount(ReductionOpcode opcode) -> std::size_t {
  switch (opcode) {
  case ReductionOpcode::Forward:
  case ReductionOpcode::ConstructNodeOrForward:
  case ReductionOpcode::OptionalSome:
  case ReductionOpcode::ListSingleton:
    return 1;
  case ReductionOpcode::ListAppend:
    return 2;
  case ReductionOpcode::Unit:
  case ReductionOpcode::ConstructNode:
  case ReductionOpcode::ConstructRecord:
  case ReductionOpcode::OptionalNone:
  case ReductionOpcode::ListEmpty:
    return 0;
  }
  throw std::logic_error("unknown AST reduction opcode");
}

} // namespace

AstReductionProgram::AstReductionProgram(
    std::vector<ReductionInstruction> instructions)
    : instructions_(std::move(instructions)) {
  for (std::size_t index = 0; index < instructions_.size(); ++index) {
    const ReductionInstruction &instruction = instructions_[index];
    if (instruction.rule != zbik::RuleId{toProgramIndex(index)}) {
      throw std::invalid_argument(
          "AST reduction instructions must follow RuleId order");
    }
    if (instruction.operands.size() !=
        expectedOperandCount(instruction.opcode)) {
      throw std::invalid_argument(
          "AST reduction instruction has an invalid operand count");
    }
    for (std::uint32_t operand : instruction.operands) {
      if (operand >= instruction.rhsLength) {
        throw std::invalid_argument(
            "AST reduction operand is outside the BNF RHS");
      }
    }
    for (const ReductionField &field : instruction.fields) {
      if (field.rhsIndex >= instruction.rhsLength) {
        throw std::invalid_argument(
            "AST reduction field is outside the BNF RHS");
      }
    }
    const bool constructsFields =
        instruction.opcode == ReductionOpcode::ConstructNode ||
        instruction.opcode == ReductionOpcode::ConstructNodeOrForward ||
        instruction.opcode == ReductionOpcode::ConstructRecord;
    if (!constructsFields && !instruction.fields.empty()) {
      throw std::invalid_argument(
          "only node and record reductions may bind fields");
    }
    if (constructsFields != instruction.typeName.has_value()) {
      throw std::invalid_argument(
          "node and record reductions require exactly one type name");
    }
    if (instruction.variantName.has_value() &&
        instruction.opcode != ReductionOpcode::ConstructNode &&
        instruction.opcode != ReductionOpcode::ConstructNodeOrForward) {
      throw std::invalid_argument(
          "only node reductions may select a named variant");
    }
    if (instruction.opcode == ReductionOpcode::ConstructNodeOrForward &&
        (instruction.fields.size() < 2 ||
         std::count_if(instruction.fields.begin(), instruction.fields.end(),
             [&](const auto &field) {
               return field.rhsIndex == instruction.operands.front();
             }) != 1)) {
      throw std::invalid_argument(
          "conditional forwarding requires one bound operand and guarded fields");
    }
    if ((instruction.rhsLength == 0) !=
        (instruction.spanPolicy == ReductionSpanPolicy::EmptyAtLookahead)) {
      throw std::invalid_argument(
          "AST reduction span policy does not match its BNF RHS");
    }
  }
}

auto AstReductionProgram::instructions() const noexcept
    -> const std::vector<ReductionInstruction> & {
  return instructions_;
}

auto AstReductionProgram::instruction(zbik::RuleId rule) const
    -> const ReductionInstruction & {
  return instructions_.at(zbik::toIndex(rule));
}

auto buildAstReductionProgram(const model::SyntaxDocument &document,
                              const model::BnfModel &bnf)
    -> AstReductionProgram {
  std::vector<ReductionInstruction> instructions;
  instructions.reserve(bnf.grammar().ruleCount());
  for (const zbik::Rule &rule : bnf.grammar().rules()) {
    const model::BnfProductionOrigin &origin = bnf.origin(rule.id());
    if (origin.role == zbik::EbnfGeneratedRuleRole::SourceAlternative) {
      const model::ParserRule &sourceRule =
          document.parserRules.at(origin.sourceRuleIndex);
      const model::ParserAlternative &alternative =
          sourceRule.alternatives.at(origin.alternativeIndex);
      instructions.push_back(sourceInstruction(document, sourceRule, alternative,
                                               origin.sourceRuleIndex, rule));
    } else {
      instructions.push_back(helperInstruction(origin, rule));
    }
  }
  return AstReductionProgram{std::move(instructions)};
}

} // namespace agas::generator
