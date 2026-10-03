#pragma once
#include <string>
#include <vector>
namespace coge {
enum class ExecutionPatternForm { Unit, Tuple, Fields, Wildcard };
struct ExecutionPatternBinding {
  std::string field;
  std::string name;
  bool used = true;
};
struct ExecutionPattern {
  std::string owner;
  std::string variant;
  ExecutionPatternForm form = ExecutionPatternForm::Unit;
  std::vector<ExecutionPatternBinding> bindings;
};
[[nodiscard]] auto emitExecutionPatternRust(const ExecutionPattern &)
    -> std::string;
} // namespace coge
