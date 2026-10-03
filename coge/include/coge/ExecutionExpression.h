#pragma once

#include <string>
#include <vector>

namespace coge {

enum class ExecutionExprKind {
  State, None, Unit, Boolean, Text, Number, Binding, Binary, Negate,
  MemberField, MemberCall, RuntimeCall, Variant, Constructor, Capture,
  DirectCall
};

struct ExecutionExpression {
  ExecutionExprKind kind;
  std::string value;
  std::string detail;
  bool propagate = false;
  std::vector<ExecutionExpression> children;
  bool optionConstructor = false;
};

[[nodiscard]] auto emitExecutionExpressionRust(
    const ExecutionExpression &expression) -> std::string;

} // namespace coge
