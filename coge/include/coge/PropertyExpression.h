#pragma once

#include <string>
#include <vector>

namespace coge {

enum class PropertyValueKind { Scalar, Mathematical, Boolean };
enum class PropertyExprKind {
  Binding, BoolLiteral, IntegerLiteral, I32Min, I32Max,
  Negation, Comparison, Oracle
};

struct PropertyExpression {
  PropertyExprKind expressionKind;
  PropertyValueKind valueKind;
  std::string value;
  std::vector<PropertyExpression> children;
};

[[nodiscard]] auto emitPropertyExpressionRust(const PropertyExpression &expression)
    -> std::string;
[[nodiscard]] auto emitPropertyArgumentRust(const PropertyExpression &expression)
    -> std::string;

} // namespace coge
