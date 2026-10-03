#include "coge/PropertyExpression.h"

#include <stdexcept>

namespace coge {

auto emitPropertyExpressionRust(const PropertyExpression &expression)
    -> std::string {
  const auto &children = expression.children;
  switch (expression.expressionKind) {
  case PropertyExprKind::Binding:
    return "i128::from(" + expression.value + ")";
  case PropertyExprKind::BoolLiteral:
    return expression.value;
  case PropertyExprKind::IntegerLiteral:
    return expression.value + "_i128";
  case PropertyExprKind::I32Min:
    return "i128::from(i32::MIN)";
  case PropertyExprKind::I32Max:
    return "i128::from(i32::MAX)";
  case PropertyExprKind::Negation:
    return "(-" + emitPropertyExpressionRust(children.at(0)) + ")";
  case PropertyExprKind::Comparison:
  case PropertyExprKind::Oracle:
    if (children.size() == 1)
      return "(-" + emitPropertyExpressionRust(children.at(0)) + ")";
    return "(" + emitPropertyExpressionRust(children.at(0)) + " " +
           expression.value + " " +
           emitPropertyExpressionRust(children.at(1)) + ")";
  }
  throw std::runtime_error("unsupported property expression");
}

auto emitPropertyArgumentRust(const PropertyExpression &expression)
    -> std::string {
  const auto code = emitPropertyExpressionRust(expression);
  return code.starts_with('(') && code.ends_with(')')
             ? code.substr(1, code.size() - 2) : code;
}

} // namespace coge
