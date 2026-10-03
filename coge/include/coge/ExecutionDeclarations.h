#pragma once

#include "coge/ExecutionType.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace coge {

struct ExecutionField {
  std::string name;
  ExecutionType type;
};

struct ExecutionVariant {
  std::string name;
  std::vector<ExecutionType> payload;
};

enum class ExecutionDeclarationKind { Record, Enumeration };

struct ExecutionDeclaration {
  ExecutionDeclarationKind kind;
  std::string name;
  std::vector<ExecutionField> fields;
  std::vector<ExecutionVariant> variants;
};

struct ExecutionState {
  std::string name;
  ExecutionType type;
};

struct ExecutionParameter {
  std::string name;
  ExecutionType type;
  bool used = false;
};

struct ExecutionFunctionSignature {
  std::string name;
  bool mutates = false;
  ExecutionType result;
  std::vector<ExecutionParameter> parameters;
};

struct ExecutionDeclarations {
  std::vector<ExecutionDeclaration> declarations;
  std::optional<ExecutionState> runtimeState;
  std::vector<ExecutionFunctionSignature> functions;
};

[[nodiscard]] auto emitExecutionFunctionSignatureRust(
    const ExecutionFunctionSignature &function) -> std::string;
[[nodiscard]] auto emitExecutionDeclarationsRust(
    const ExecutionDeclarations &declarations,
    std::string_view contextType) -> std::string;

} // namespace coge
