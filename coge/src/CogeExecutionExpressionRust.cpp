#include "coge/ExecutionExpression.h"

#include <stdexcept>
#include <string_view>

namespace coge {
namespace {
auto rustLocalName(std::string_view name) -> std::string {
  if (name == "type")
    return "r#type";
  std::string result;
  for (const auto character : name) {
    if (character >= 'A' && character <= 'Z') {
      result.push_back('_');
      result.push_back(static_cast<char>(character - 'A' + 'a'));
    } else {
      result.push_back(character);
    }
  }
  return result;
}

auto arguments(const ExecutionExpression &expression,
               std::size_t start) -> std::string {
  std::string result;
  for (std::size_t index = start; index < expression.children.size(); ++index) {
    if (index != start)
      result += ", ";
    result += emitExecutionExpressionRust(expression.children[index]);
  }
  return result;
}
} // namespace

auto emitExecutionExpressionRust(const ExecutionExpression &expression)
    -> std::string {
  const auto &children = expression.children;
  switch (expression.kind) {
  case ExecutionExprKind::State:
    return "self." + expression.value;
  case ExecutionExprKind::None:
    return "None";
  case ExecutionExprKind::Unit:
    return "()";
  case ExecutionExprKind::Boolean:
  case ExecutionExprKind::Text:
  case ExecutionExprKind::Number:
  case ExecutionExprKind::Binding:
    return expression.value;
  case ExecutionExprKind::Binary:
    return emitExecutionExpressionRust(children.at(0)) + " " +
           expression.value + " " +
           emitExecutionExpressionRust(children.at(1));
  case ExecutionExprKind::Negate:
    return "-" + emitExecutionExpressionRust(children.at(0));
  case ExecutionExprKind::MemberField:
    return emitExecutionExpressionRust(children.at(0)) + "." +
           rustLocalName(expression.value);
  case ExecutionExprKind::MemberCall:
    return emitExecutionExpressionRust(children.at(0)) + "(" +
           arguments(expression, 1) + ")";
  case ExecutionExprKind::RuntimeCall:
    return "agsem_runtime::" + expression.value + "(" +
           arguments(expression, 0) + ")";
  case ExecutionExprKind::Variant:
    return expression.value + "::" + expression.detail;
  case ExecutionExprKind::Constructor:
    return expression.value + "::" + expression.detail + "(" +
           arguments(expression, 0) + ")";
  case ExecutionExprKind::Capture:
    return emitExecutionExpressionRust(children.at(0));
  case ExecutionExprKind::DirectCall:
    return (expression.optionConstructor ? "Some(" :
            "self." + expression.value + "(") +
           arguments(expression, 0) + ")" +
           (expression.propagate ? "?" : "");
  }
  throw std::runtime_error("unsupported execution expression plan");
}

} // namespace coge
