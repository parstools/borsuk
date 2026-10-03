#include "agas/runtime/ReductionRuntime.h"
#include "agas/generator/ReductionProgram.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string{message});
}

auto token(std::uint32_t kind, std::string text, std::uint64_t begin,
           std::uint64_t end) -> agas::runtime::ReductionStackValue {
  return agas::runtime::makeTokenValue(kind, std::move(text), {begin, end});
}

} // namespace

int main() {
  using agas::generator::ReductionField;
  using agas::generator::ReductionInstruction;
  using agas::generator::ReductionOpcode;
  using agas::generator::ReductionSpanPolicy;
  using agas::runtime::AstValueKind;
  using agas::runtime::InputSpan;
  using agas::runtime::ReductionRuntimeError;
  using agas::runtime::ReductionRuntimeErrorCode;
  using agas::runtime::ReductionStackValue;

  try {
    const ReductionInstruction forward{zbik::RuleId{0},
                                       3,
                                       ReductionOpcode::Forward,
                                       ReductionSpanPolicy::MatchedRhs,
                                       std::nullopt,
                                       std::nullopt,
                                       {1},
                                       {}};
    ReductionStackValue forwarded = agas::runtime::executeReduction(
        forward,
        {token(1, "(", 0, 1), token(2, "name", 1, 5), token(3, ")", 5, 6)}, 6);
    require(forwarded.payload.kind == AstValueKind::Token &&
                forwarded.payload.tokenText == "name" &&
                forwarded.payload.sourceSpan == InputSpan{1, 5} &&
                forwarded.payload.recognizedSpan == InputSpan{0, 6} &&
                forwarded.recognizedSpan == InputSpan{0, 6},
            "inline forwarding must preserve the child payload and cover the "
            "complete reduction span");

    const ReductionInstruction node{
        zbik::RuleId{0},
        2,
        ReductionOpcode::ConstructNode,
        ReductionSpanPolicy::MatchedRhs,
        std::string{"pair"},
        std::string{"NamedPair"},
        {},
        {ReductionField{"left", 0}, ReductionField{"right", 1}}};
    ReductionStackValue pair = agas::runtime::executeReduction(
        node, {token(4, "a", 10, 11), token(4, "b", 12, 13)}, 13);
    require(pair.payload.kind == AstValueKind::Node &&
                pair.payload.typeName == "pair" &&
                pair.payload.variantName == "NamedPair" &&
                pair.payload.fieldNames ==
                    std::vector<std::string>({"left", "right"}) &&
                pair.payload.elements.size() == 2 &&
                pair.payload.sourceSpan == InputSpan{10, 13},
            "node execution must retain type, variant, fields and source span");

    const ReductionInstruction optionalNone{
        zbik::RuleId{0},
        0,
        ReductionOpcode::OptionalNone,
        ReductionSpanPolicy::EmptyAtLookahead,
        std::nullopt,
        std::nullopt,
        {},
        {}};
    const ReductionStackValue absent =
        agas::runtime::executeReduction(optionalNone, {}, 21);
    require(absent.payload.kind == AstValueKind::Optional &&
                absent.payload.elements.empty() &&
                absent.recognizedSpan == InputSpan{21, 21},
            "empty reductions must use the lookahead boundary");

    const ReductionInstruction listEmpty{zbik::RuleId{0},
                                         0,
                                         ReductionOpcode::ListEmpty,
                                         ReductionSpanPolicy::EmptyAtLookahead,
                                         std::nullopt,
                                         std::nullopt,
                                         {},
                                         {}};
    const ReductionInstruction listAppend{zbik::RuleId{1},
                                          2,
                                          ReductionOpcode::ListAppend,
                                          ReductionSpanPolicy::MatchedRhs,
                                          std::nullopt,
                                          std::nullopt,
                                          {0, 1},
                                          {}};
    ReductionStackValue list =
        agas::runtime::executeReduction(listEmpty, {}, 0);
    constexpr std::uint64_t itemCount = 10000;
    for (std::uint64_t index = 0; index < itemCount; ++index) {
      std::vector<ReductionStackValue> rhs;
      rhs.reserve(2);
      rhs.push_back(std::move(list));
      rhs.push_back(token(5, std::to_string(index), index, index + 1));
      list = agas::runtime::executeReduction(listAppend, std::move(rhs),
                                             index + 1);
    }
    require(list.payload.kind == AstValueKind::List &&
                list.payload.elements.size() == itemCount &&
                list.payload.elements.front().tokenText == "0" &&
                list.payload.elements.back().tokenText == "9999" &&
                list.recognizedSpan == InputSpan{0, itemCount},
            "list append must retain source order and scale to long lists");

    bool rejectedRhsSize = false;
    try {
      static_cast<void>(agas::runtime::executeReduction(forward, {}, 0));
    } catch (const ReductionRuntimeError &error) {
      rejectedRhsSize =
          error.code() == ReductionRuntimeErrorCode::RhsSizeMismatch;
    }
    require(rejectedRhsSize,
            "runtime errors must distinguish an invalid RHS size");

    bool rejectedValueKind = false;
    try {
      static_cast<void>(agas::runtime::executeReduction(
          listAppend, {token(5, "not-a-list", 0, 1), token(5, "x", 1, 2)}, 2));
    } catch (const ReductionRuntimeError &error) {
      rejectedValueKind =
          error.code() == ReductionRuntimeErrorCode::ValueKindMismatch;
    }
    require(rejectedValueKind,
            "runtime errors must distinguish an invalid semantic value kind");

    bool rejectedSpan = false;
    try {
      static_cast<void>(agas::runtime::executeReduction(
          node, {token(4, "a", 0, 2), token(4, "b", 1, 3)}, 3));
    } catch (const ReductionRuntimeError &error) {
      rejectedSpan = error.code() == ReductionRuntimeErrorCode::InvalidSpan;
    }
    require(rejectedSpan,
            "runtime errors must distinguish overlapping RHS spans");

    const ReductionInstruction ignoreEmptyEdges{zbik::RuleId{0},
                                                3,
                                                ReductionOpcode::ConstructNode,
                                                ReductionSpanPolicy::MatchedRhs,
                                                std::string{"bounded"},
                                                std::nullopt,
                                                {},
                                                {ReductionField{"value", 1}}};
    const ReductionStackValue emptyBefore{agas::runtime::AstValue{},
                                          InputSpan{2, 2}};
    const ReductionStackValue emptyAfter{agas::runtime::AstValue{},
                                         InputSpan{8, 8}};
    const auto bounded = agas::runtime::executeReduction(
        ignoreEmptyEdges, {emptyBefore, token(4, "text", 4, 7), emptyAfter}, 8);
    require(bounded.recognizedSpan == InputSpan{4, 7} &&
                bounded.payload.sourceSpan == InputSpan{4, 7},
            "empty helper values must not extend a nonempty parent span");

    std::cout << "AST runtime long-list-items=" << itemCount << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
