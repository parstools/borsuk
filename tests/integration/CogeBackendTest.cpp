#include "coge/BackendGeneration.h"

#include <stdexcept>
#include <string_view>

int main() {
  coge::BackendGenerationInput input;
  input.lowering = "lower_function";
  input.c = coge::BackendBinding{"emit_c", "lower_function"};
  input.llvm = coge::BackendBinding{"emit_llvm", "lower_function"};
  const auto checked = coge::prepareBackends(input);
  if (coge::emitCBackendRust(checked).find("pub fn emit_c") ==
          std::string::npos ||
      coge::emitLlvmBackendRust(checked).find("pub fn emit_llvm") ==
          std::string::npos)
    return 1;
  input.lowering.reset();
  try {
    static_cast<void>(coge::prepareBackends(input));
    return 2;
  } catch (const std::runtime_error &error) {
    if (std::string_view{error.what()} != "backend_c requires lowering_model")
      return 3;
  }
  input.lowering = "other_lower";
  try {
    static_cast<void>(coge::prepareBackends(input));
    return 4;
  } catch (const std::runtime_error &error) {
    if (std::string_view{error.what()} !=
        "backend_c lower differs from lowering_model")
      return 5;
  }
  input.c.reset();
  try {
    static_cast<void>(coge::prepareBackends(input));
    return 6;
  } catch (const std::runtime_error &error) {
    if (std::string_view{error.what()} !=
        "backend_llvm lower differs from lowering_model")
      return 7;
  }
  return 0;
}
