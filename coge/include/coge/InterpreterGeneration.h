#pragma once

#include "coge/ExecutionDeclarations.h"
#include "coge/ExecutionStatement.h"

#include <optional>
#include <string>
#include <vector>

namespace coge {

struct ExecutionArm {
  std::string pattern;
  ExecutionBody body;
  std::optional<ExecutionPattern> structuredPattern;
};

struct InterpreterGenerationPlan {
  std::string contextType;
  std::vector<ExecutionArm> operations;
  std::vector<ExecutionArm> expressions;
  std::optional<ExecutionDeclarations> declarations;
  std::vector<ExecutionBody> functionBodies;
};

[[nodiscard]] auto
emitInterpreterRust(const std::optional<InterpreterGenerationPlan> &plan)
    -> std::string;

} // namespace coge
