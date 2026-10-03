#include "coge/LoweringGeneration.h"

#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

int main() {
  if (!coge::emitLoweringRust(coge::prepareLowering({})).empty())
    return 1;
  std::map<std::string, std::string> bindings;
  for (const auto &field : coge::loweringFields())
    bindings.emplace(field, "Value");
  bindings["lower"] = "lower_function";
  const auto checked = coge::prepareLowering({bindings});
  const auto rust = coge::emitLoweringRust(checked);
  if (checked.loweringName() != "lower_function" ||
      rust.find("pub fn lower_function") == std::string::npos ||
      rust.find("@LOWER@") != std::string::npos)
    return 2;
  bindings["lower"] = "source_span";
  try {
    static_cast<void>(coge::prepareLowering({bindings}));
    return 3;
  } catch (const std::runtime_error &error) {
    if (std::string_view{error.what()} !=
        "lowering_model function name conflicts with helper")
      return 4;
  }
  bindings["lower"] = "lower_function";
  bindings.erase("functions");
  try {
    static_cast<void>(coge::prepareLowering({bindings}));
    return 5;
  } catch (const std::runtime_error &error) {
    if (std::string_view{error.what()} !=
        "lowering_model missing field functions")
      return 6;
  }
  try {
    static_cast<void>(coge::prepareLowering({std::map<std::string, std::string>{
        {"profile", "structured_core_v1"}, {"lower", "lower_function"}}}));
    return 7;
  } catch (const std::runtime_error &error) {
    if (std::string_view{error.what()} !=
        "lowering profile requires explicit model contracts")
      return 8;
  }
  return 0;
}
