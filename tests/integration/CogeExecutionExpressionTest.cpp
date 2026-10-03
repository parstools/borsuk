#include "coge/ExecutionExpression.h"

#include <string>
#include <utility>

int main() {
  using coge::ExecutionExprKind;
  using coge::ExecutionExpression;
  const ExecutionExpression binding{ExecutionExprKind::Binding, "value"};
  const ExecutionExpression field{ExecutionExprKind::MemberField, "type", {},
                                  false, {binding}};
  const ExecutionExpression call{ExecutionExprKind::DirectCall, "load",
                                 {}, true, {field}};
  const ExecutionExpression capture{ExecutionExprKind::Capture, {}, {},
                                    false, {call}};
  const ExecutionExpression number{ExecutionExprKind::Number, "1"};
  const ExecutionExpression sum{ExecutionExprKind::Binary, "+", {}, false,
                                {number, number}};
  if (coge::emitExecutionExpressionRust(call) !=
          "self.load(value.r#type)?" ||
      coge::emitExecutionExpressionRust(sum) != "1 + 1")
    return 1;
  auto captured = capture;
  captured.children.front().propagate = false;
  if (coge::emitExecutionExpressionRust(captured) !=
      "self.load(value.r#type)")
    return 2;
  return 0;
}
