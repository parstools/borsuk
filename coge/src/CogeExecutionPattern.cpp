#include "coge/ExecutionPattern.h"
namespace coge {
auto emitExecutionPatternRust(const ExecutionPattern &pattern) -> std::string {
  if (pattern.form == ExecutionPatternForm::Wildcard)
    return "_";
  auto result = pattern.owner.empty() ? pattern.variant
                                      : pattern.owner + "::" + pattern.variant;
  if (pattern.form == ExecutionPatternForm::Unit)
    return result;
  const bool fields = pattern.form == ExecutionPatternForm::Fields;
  result += fields ? " { " : "(";
  for (std::size_t i = 0; i < pattern.bindings.size(); ++i) {
    if (i)
      result += ", ";
    const auto &binding = pattern.bindings[i];
    const auto name = binding.used ? binding.name : "_" + binding.name;
    if (fields && binding.field != name)
      result += binding.field + ": ";
    result += name;
  }
  return result + (fields ? " }" : ")");
}
} // namespace coge
