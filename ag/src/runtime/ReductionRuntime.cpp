#include "agas/runtime/ReductionRuntime.h"

#include <cstddef>
#include <algorithm>
#include <tuple>
#include <utility>

namespace agas::runtime {
namespace {

auto reductionSpan(const generator::ReductionInstruction &instruction,
                   const std::vector<ReductionStackValue> &rhs,
                   std::uint64_t lookaheadByte) -> InputSpan {
  if (instruction.spanPolicy ==
      generator::ReductionSpanPolicy::EmptyAtLookahead) {
    return {lookaheadByte, lookaheadByte};
  }
  if (rhs.empty()) {
    throw ReductionRuntimeError(ReductionRuntimeErrorCode::InvalidSpan,
                                "a nonempty AST reduction has no RHS span");
  }
  for (const ReductionStackValue &value : rhs) {
    if (value.recognizedSpan.beginByte > value.recognizedSpan.endByte) {
      throw ReductionRuntimeError(ReductionRuntimeErrorCode::InvalidSpan,
                                  "an RHS value has an inverted input span");
    }
  }
  for (std::size_t index = 1; index < rhs.size(); ++index) {
    if (rhs[index - 1].recognizedSpan.endByte >
        rhs[index].recognizedSpan.beginByte) {
      throw ReductionRuntimeError(
          ReductionRuntimeErrorCode::InvalidSpan,
          "RHS input spans overlap or are out of source order");
    }
  }
  const ReductionStackValue *firstNonempty = nullptr;
  const ReductionStackValue *lastNonempty = nullptr;
  for (const ReductionStackValue &value : rhs) {
    if (value.recognizedSpan.beginByte != value.recognizedSpan.endByte) {
      if (firstNonempty == nullptr)
        firstNonempty = &value;
      lastNonempty = &value;
    }
  }
  if (firstNonempty == nullptr) {
    return rhs.front().recognizedSpan;
  }
  return {firstNonempty->recognizedSpan.beginByte,
          lastNonempty->recognizedSpan.endByte};
}

auto aggregateValue(AstValueKind kind,
                    const generator::ReductionInstruction &instruction,
                    std::vector<ReductionStackValue> &rhs, InputSpan span)
    -> AstValue {
  AstValue result;
  result.kind = kind;
  result.sourceSpan = span;
  result.typeName = *instruction.typeName;
  result.variantName = instruction.variantName.value_or("");
  result.fieldNames.reserve(instruction.fields.size());
  result.elements.reserve(instruction.fields.size());
  for (const generator::ReductionField &field : instruction.fields) {
    result.fieldNames.push_back(field.name);
    result.elements.push_back(std::move(rhs[field.rhsIndex].payload));
  }
  return result;
}

auto wrapperValue(AstValueKind kind, InputSpan span) -> AstValue {
  AstValue result;
  result.kind = kind;
  result.sourceSpan = span;
  return result;
}

} // namespace

auto measureAst(const AstValue &root) -> AstStatistics {
  AstStatistics result;
  std::vector<std::tuple<const AstValue *, std::size_t, std::size_t>> pending{{&root, 0, 0}};
  while (!pending.empty()) {
    const auto [value, depth, parentNodes] = pending.back();
    pending.pop_back();
    ++result.values;
    result.nodes += value->kind == AstValueKind::Node;
    result.maximumDepth = std::max(result.maximumDepth, depth);
    const auto nodeDepth = parentNodes + (value->kind == AstValueKind::Node);
    result.maximumNodeDepth = std::max(result.maximumNodeDepth, nodeDepth);
    for (const auto &child : value->elements)
      pending.emplace_back(&child, depth + 1, nodeDepth);
  }
  return result;
}

ReductionRuntimeError::ReductionRuntimeError(ReductionRuntimeErrorCode code,
                                             std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

auto ReductionRuntimeError::code() const noexcept -> ReductionRuntimeErrorCode {
  return code_;
}

auto makeTokenValue(std::uint32_t tokenKind, std::string text, InputSpan span)
    -> ReductionStackValue {
  if (span.beginByte > span.endByte) {
    throw ReductionRuntimeError(ReductionRuntimeErrorCode::InvalidSpan,
                                "a token has an inverted input span");
  }
  AstValue token;
  token.kind = AstValueKind::Token;
  token.sourceSpan = span;
  token.recognizedSpan = span;
  token.tokenKind = tokenKind;
  token.tokenText = std::move(text);
  return {std::move(token), span};
}

auto executeReduction(const generator::ReductionInstruction &instruction,
                      std::vector<ReductionStackValue> rhs,
                      std::uint64_t lookaheadByte) -> ReductionStackValue {
  if (rhs.size() != instruction.rhsLength) {
    throw ReductionRuntimeError(
        ReductionRuntimeErrorCode::RhsSizeMismatch,
        "AST reduction RHS size does not match its instruction");
  }
  const InputSpan span = reductionSpan(instruction, rhs, lookaheadByte);
  AstValue result;
  switch (instruction.opcode) {
  case generator::ReductionOpcode::Unit:
    result = wrapperValue(AstValueKind::Unit, span);
    break;
  case generator::ReductionOpcode::Forward:
    result = std::move(rhs[instruction.operands[0]].payload);
    break;
  case generator::ReductionOpcode::ConstructNode:
    result = aggregateValue(AstValueKind::Node, instruction, rhs, span);
    break;
  case generator::ReductionOpcode::ConstructNodeOrForward: {
    bool empty = true;
    const auto operand = instruction.operands[0];
    for (const auto &field : instruction.fields) {
      if (field.rhsIndex == operand) continue;
      const auto &guard = rhs[field.rhsIndex].payload;
      if (guard.kind != AstValueKind::List && guard.kind != AstValueKind::Optional)
        throw ReductionRuntimeError(ReductionRuntimeErrorCode::ValueKindMismatch,
                                    "forwarding guard requires a list or optional");
      empty = empty && guard.elements.empty();
    }
    result = empty ? std::move(rhs[operand].payload)
                   : aggregateValue(AstValueKind::Node, instruction, rhs, span);
    break;
  }
  case generator::ReductionOpcode::ConstructRecord:
    result = aggregateValue(AstValueKind::Record, instruction, rhs, span);
    break;
  case generator::ReductionOpcode::OptionalSome:
    result = wrapperValue(AstValueKind::Optional, span);
    result.elements.push_back(std::move(rhs[instruction.operands[0]].payload));
    break;
  case generator::ReductionOpcode::OptionalNone:
    result = wrapperValue(AstValueKind::Optional, span);
    break;
  case generator::ReductionOpcode::ListEmpty:
    result = wrapperValue(AstValueKind::List, span);
    break;
  case generator::ReductionOpcode::ListSingleton:
    result = wrapperValue(AstValueKind::List, span);
    result.elements.push_back(std::move(rhs[instruction.operands[0]].payload));
    break;
  case generator::ReductionOpcode::ListAppend: {
    AstValue &prefix = rhs[instruction.operands[0]].payload;
    if (prefix.kind != AstValueKind::List) {
      throw ReductionRuntimeError(
          ReductionRuntimeErrorCode::ValueKindMismatch,
          "ListAppend requires a list as its first operand");
    }
    result = std::move(prefix);
    result.sourceSpan = span;
    result.elements.push_back(std::move(rhs[instruction.operands[1]].payload));
    break;
  }
  }
  result.recognizedSpan = span;
  return {std::move(result), span};
}

} // namespace agas::runtime
