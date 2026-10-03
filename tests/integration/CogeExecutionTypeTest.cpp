#include "coge/ExecutionType.h"

#include <stdexcept>
#include <string_view>

int main() {
  const auto integer = coge::prepareExecutionType("I32", {});
  const auto text = coge::prepareExecutionType("Text", {});
  const auto result =
      coge::prepareExecutionType("Result", {integer, text});
  const auto store =
      coge::prepareExecutionType("Store", {integer, result});
  if (result.kind != coge::ExecutionTypeKind::Result ||
      coge::emitExecutionTypeRust(store) !=
          "agsem_runtime::Store<i32, Result<i32, &'static str>>")
    return 1;
  try {
    static_cast<void>(coge::prepareExecutionType("Result", {integer}));
    return 2;
  } catch (const std::runtime_error &error) {
    if (std::string_view{error.what()} !=
        "execution Result needs two type arguments")
      return 3;
  }
  try {
    static_cast<void>(coge::prepareExecutionType("I32", {integer}));
    return 4;
  } catch (const std::runtime_error &error) {
    if (std::string_view{error.what()} !=
        "I32 cannot have type arguments")
      return 5;
  }
  try {
    static_cast<void>(coge::prepareExecutionType("Self", {}));
    return 6;
  } catch (const std::runtime_error &error) {
    if (std::string_view{error.what()} !=
        "unsupported Rust type identifier: Self")
      return 7;
  }
  return 0;
}
