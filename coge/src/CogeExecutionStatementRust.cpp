#include "coge/ExecutionStatement.h"

#include <stdexcept>

namespace coge {
namespace {

auto renderStatements(const std::vector<ExecutionStatement> &statements,
                      int indent) -> std::string;

auto renderStatement(const ExecutionStatement &statement, int indent)
    -> std::string {
  const std::string pad(static_cast<std::size_t>(indent), ' ');
  const auto value = [&] {
    return emitExecutionExpressionRust(*statement.value);
  };
  switch (statement.kind) {
  case ExecutionStatementKind::Let:
    return pad + "let " + (statement.mutableBinding ? "mut " : "") +
           statement.binding + " = " + value() + ";\n";
  case ExecutionStatementKind::Expression:
    return pad + value() + ";\n";
  case ExecutionStatementKind::Return:
    return pad + "return " +
           (statement.wrapReturn ? "Ok(" + value() + ")" : value()) + ";\n";
  case ExecutionStatementKind::Error:
    return pad + "return Err(RuntimeError { message: " + value() +
           ", source: " + emitExecutionExpressionRust(*statement.extra) +
           " });\n";
  case ExecutionStatementKind::Assignment:
    return pad + value() + " = " +
           emitExecutionExpressionRust(*statement.extra) + ";\n";
  case ExecutionStatementKind::If: {
    auto result = pad + "if " + value() + " {\n" +
                  renderStatements(statement.thenBranch, indent + 4) + pad +
                  "}";
    if (statement.hasElse)
      result += " else {\n" +
                renderStatements(statement.elseBranch, indent + 4) + pad + "}";
    return result + "\n";
  }
  case ExecutionStatementKind::Foreach:
    return pad + "for " + statement.binding + " in " + value() + " {\n" +
           renderStatements(statement.thenBranch, indent + 4) + pad + "}\n";
  case ExecutionStatementKind::While:
    return pad + "while " + value() + " {\n" +
           renderStatements(statement.thenBranch, indent + 4) + pad + "}\n";
  case ExecutionStatementKind::Match: {
    auto result = pad + "match " + value() + " {\n";
    for (const auto &arm : statement.arms)
      result += pad + "    " +
                (arm.structuredPattern
                     ? emitExecutionPatternRust(*arm.structuredPattern)
                     : arm.pattern) +
                " => {\n" + renderStatements(arm.body, indent + 8) + pad +
                "    }\n";
    return result + pad + "}\n";
  }
  }
  throw std::runtime_error("unsupported execution statement plan");
}

auto renderStatements(const std::vector<ExecutionStatement> &statements,
                      int indent) -> std::string {
  std::string result;
  for (const auto &statement : statements)
    result += renderStatement(statement, indent);
  return result;
}

} // namespace

auto emitExecutionBodyRust(const ExecutionBody &body) -> std::string {
  const int indent = body.kind == ExecutionBodyKind::Function ? 8 : 16;
  std::string result;
  for (std::size_t index = 0; index < body.statements.size(); ++index) {
    const auto &statement = body.statements[index];
    if (body.kind != ExecutionBodyKind::Function &&
        index + 1 == body.statements.size() &&
        statement.kind == ExecutionStatementKind::Return)
      result += "                " +
                emitExecutionExpressionRust(*statement.value) + "\n";
    else
      result += renderStatement(statement, indent);
  }
  if (body.kind == ExecutionBodyKind::HandlerExecute &&
      (body.statements.empty() ||
       body.statements.back().kind != ExecutionStatementKind::Return))
    result += "                Ok(Control::Continue)\n";
  return result;
}

} // namespace coge
