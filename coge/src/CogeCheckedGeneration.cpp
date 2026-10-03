#include "CompletenessInternal.h"
#include "GenerationAccess.h"
#include "coge/CheckedGeneration.h"

#include <stdexcept>
#include <utility>

namespace coge {
namespace {

void checkExpression(const ExecutionExpression &expression) {
  const auto count = expression.children.size();
  const auto expected = [&](std::size_t size) {
    if (count != size)
      throw std::runtime_error("invalid execution expression plan");
  };
  switch (expression.kind) {
  case ExecutionExprKind::Binary:
    expected(2);
    break;
  case ExecutionExprKind::Negate:
  case ExecutionExprKind::MemberField:
  case ExecutionExprKind::Capture:
    expected(1);
    break;
  case ExecutionExprKind::MemberCall:
    if (count < 1)
      throw std::runtime_error("invalid execution member call plan");
    break;
  case ExecutionExprKind::State:
  case ExecutionExprKind::None:
  case ExecutionExprKind::Unit:
  case ExecutionExprKind::Boolean:
  case ExecutionExprKind::Text:
  case ExecutionExprKind::Number:
  case ExecutionExprKind::Binding:
  case ExecutionExprKind::Variant:
    expected(0);
    break;
  case ExecutionExprKind::RuntimeCall:
  case ExecutionExprKind::Constructor:
  case ExecutionExprKind::DirectCall:
    break;
  }
  for (const auto &child : expression.children)
    checkExpression(child);
}

void checkStatements(const std::vector<ExecutionStatement> &statements) {
  for (const auto &statement : statements) {
    if (!statement.value)
      throw std::runtime_error("execution statement has no expression");
    checkExpression(*statement.value);
    const bool needsExtra =
        statement.kind == ExecutionStatementKind::Error ||
        statement.kind == ExecutionStatementKind::Assignment;
    if (needsExtra != statement.extra.has_value())
      throw std::runtime_error("invalid execution statement operands");
    if (statement.extra)
      checkExpression(*statement.extra);
    checkStatements(statement.thenBranch);
    checkStatements(statement.elseBranch);
    for (const auto &arm : statement.arms)
      checkStatements(arm.body);
  }
}

void checkBody(const ExecutionBody &body) {
  checkStatements(body.statements);
  if (body.kind == ExecutionBodyKind::HandlerEvaluate &&
      (body.statements.empty() ||
       body.statements.back().kind != ExecutionStatementKind::Return))
    throw std::runtime_error("evaluate handler must return a value");
}

} // namespace

static void validateGeneration(const GenerationInput &input) {
  if (input.properties && !input.interpreter)
    throw std::runtime_error("properties require generate_interpreter");
  if (input.interpreter) {
    const auto &interpreter = *input.interpreter;
    if (interpreter.operations.empty() || interpreter.expressions.empty())
      throw std::runtime_error(
          "execution contract needs execute and evaluate handlers");
    if (interpreter.declarations &&
        interpreter.declarations->functions.size() !=
            interpreter.functionBodies.size())
      throw std::runtime_error("execution function plan mismatch");
    for (const auto &arm : interpreter.operations) {
      if (arm.body.kind != ExecutionBodyKind::HandlerExecute)
        throw std::runtime_error("invalid execute handler plan");
      checkBody(arm.body);
    }
    for (const auto &arm : interpreter.expressions) {
      if (arm.body.kind != ExecutionBodyKind::HandlerEvaluate)
        throw std::runtime_error("invalid evaluate handler plan");
      checkBody(arm.body);
    }
    for (const auto &body : interpreter.functionBodies) {
      if (body.kind != ExecutionBodyKind::Function)
        throw std::runtime_error("invalid execution function plan");
      checkBody(body);
    }
  }
}

auto prepareCheckedGeneration(GenerationInput input) -> CheckedGeneration {
  if (input.execution)
    throw std::runtime_error(
        "document.typed_preparation_required: checked execution requires its "
        "document completeness proof");
  validateGeneration(input);
  return CheckedGeneration{std::move(input)};
}
auto GenerationAccess::prepare(GenerationInput input,
                               const CogeValidation &validation)
    -> CheckedGeneration {
  const auto errors = agsem::generationDiagnostics(validation.completeness());
  if (!errors.empty())
    throw agsem::SemanticPreparationError(errors);
  validateGeneration(input);
  CheckedGeneration plan{std::move(input)};
  plan.validation_ = std::make_shared<const CogeValidation>(validation);
  return plan;
}

auto emitCheckedGeneration(const CheckedGeneration &plan) -> RustArtifacts {
  const auto &input = plan.input_;
  if (plan.validation_) {
    const auto errors =
        agsem::generationDiagnostics(plan.validation_->completeness());
    if (!errors.empty())
      throw agsem::SemanticPreparationError(errors);
    if (!input.execution || input.execution != plan.validation_->execution() ||
        input.execution->model().sourceIdentity !=
            plan.validation_->completeness().sourceIdentity)
      throw std::runtime_error(
          "document.identity_mismatch: generation lost its completeness proof");
  } else if (input.execution)
    throw std::runtime_error("document.typed_preparation_required: generation "
                             "has no completeness proof");
  return {emitInterpreterRust(input.interpreter),
          emitPropertiesRust(input.properties),
          emitLoweringRust(input.lowering), emitCBackendRust(input.backends),
          emitLlvmBackendRust(input.backends)};
}

} // namespace coge
