#include "coge/CheckedGeneration.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

int main() {
  if (!coge::emitInterpreterRust(std::nullopt).empty())
    return 1;
  coge::InterpreterGenerationPlan plan;
  plan.contextType = "Context";
  plan.operations.push_back({"Operation::Noop", {coge::ExecutionBodyKind::HandlerExecute, {}}});
  coge::ExecutionStatement result{coge::ExecutionStatementKind::Return};
  result.value = coge::ExecutionExpression{
      coge::ExecutionExprKind::DirectCall, "zero", {}, false, {}};
  plan.expressions.push_back({"ExpressionKind::Zero",
      {coge::ExecutionBodyKind::HandlerEvaluate, {result}}});
  const auto first = coge::emitInterpreterRust(std::optional{plan});
  if (first != coge::emitInterpreterRust(std::optional{plan}) ||
      first.find("Operation::Noop") == std::string::npos ||
      first.find("ExpressionKind::Zero") == std::string::npos ||
      first.find("Ok(Control::Continue)") == std::string::npos ||
      first.find("self.zero()") == std::string::npos)
    return 2;
  coge::GenerationInput input;
  input.interpreter = plan;
  const auto checked = coge::prepareCheckedGeneration(std::move(input));
  if (coge::emitCheckedGeneration(checked).interpreter != first)
    return 3;
  plan.expressions.front().body.statements.front().value.reset();
  coge::GenerationInput invalid;
  invalid.interpreter = plan;
  try {
    static_cast<void>(coge::prepareCheckedGeneration(std::move(invalid)));
    return 4;
  } catch (const std::runtime_error &) {
  }
  return 0;
}
