#pragma once

#include <string>
#include <vector>

namespace coge {

enum class ExecutionTypeKind {
  Option, List, Result, Map, Store, I32, F32, U8, U64,
  Index, Bool, Unit, Text, SourceRange, Named
};

struct ExecutionType {
  ExecutionTypeKind kind;
  std::string name;
  std::vector<ExecutionType> arguments;
};

[[nodiscard]] auto executionTypeKind(const std::string &name)
    -> ExecutionTypeKind;
[[nodiscard]] auto prepareExecutionType(std::string name,
                                        std::vector<ExecutionType> arguments)
    -> ExecutionType;
[[nodiscard]] auto emitExecutionTypeRust(const ExecutionType &type)
    -> std::string;

} // namespace coge
