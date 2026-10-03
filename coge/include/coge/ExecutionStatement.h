#pragma once

#include "coge/ExecutionExpression.h"
#include "coge/ExecutionPattern.h"

#include <optional>
#include <string>
#include <vector>

namespace coge {

enum class ExecutionStatementKind {
  Let,
  Expression,
  Return,
  Error,
  Assignment,
  If,
  Foreach,
  While,
  Match
};

struct ExecutionStatement;

struct ExecutionMatchArm {
  std::string pattern;
  std::vector<ExecutionStatement> body;
  std::optional<ExecutionPattern> structuredPattern;
};

struct ExecutionStatement {
  ExecutionStatementKind kind;
  std::string binding;
  bool mutableBinding = false;
  bool wrapReturn = false;
  bool hasElse = false;
  std::optional<ExecutionExpression> value;
  std::optional<ExecutionExpression> extra;
  std::vector<ExecutionStatement> thenBranch;
  std::vector<ExecutionStatement> elseBranch;
  std::vector<ExecutionMatchArm> arms;
};

enum class ExecutionBodyKind { HandlerExecute, HandlerEvaluate, Function };

struct ExecutionBody {
  ExecutionBodyKind kind;
  std::vector<ExecutionStatement> statements;
};

[[nodiscard]] auto emitExecutionBodyRust(const ExecutionBody &body)
    -> std::string;

} // namespace coge
