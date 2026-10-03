#include "agsem/SemanticBinding.h"
#include "agsem/DocumentFrontend.h"
#include "coge/DocumentModel.h"
#include <iostream>
#include <stdexcept>
void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  try {
    if (argc != 3)
      return 2;
    const agsem::DocumentFrontend frontend{argv[1], argv[2]};
    agsem::ContractEnvironment contracts;
    require(
        contracts
            .addManifest(
                R"({"format":1,"id":"test","version":"1","semantic":[{"name":"Context","kind":"opaque"},{"name":"Handle","kind":"opaque"}]})")
            .empty(),
        "contract load");
    const auto bind = [&](std::string model, std::string action,
                          std::string execution = {}) {
      const auto parsed = frontend.parse(
          "coge Test; grammar Test; semantic_model { rust_context Context; " +
          model + " } node start : ID analysis { " + action + " }; ID:'a'; " +
          execution);
      require(parsed.value.has_value(),
              parsed.diagnostics.empty() ? "parse"
                                         : parsed.diagnostics.front().message);
      const auto document = coge::makeCogeDocument(*parsed.value);
      require(document.value.has_value(), "coge factory");
      return agsem::bindSemantics(document.value->semantics(), contracts);
    };
    require(bind("function helper() -> Int { return 1; }", "return helper();")
                .value.has_value(),
            "valid function");
    const auto direct =
        bind("", "return runtime();",
             "execution_model { function runtime() -> Int { return 1; } }");
    require(!direct.value &&
                direct.diagnostics.front().code == "sema.execution_dependency",
            "direct dependency");
    const auto indirect =
        bind("function unused() -> Int { return runtime(); }", "return 1;",
             "execution_model { function runtime() -> Int { return 1; } }");
    require(!indirect.value && indirect.diagnostics.front().code ==
                                   "sema.execution_dependency",
            "unused/transitive dependency");
    require(bind("", "let runtime = 1; return runtime;",
                 "execution_model { function runtime() -> Int { return 1; } }")
                .value.has_value(),
            "local scope");
    require(!bind("type Alias = Runtime;", "return 1;",
                  "execution_model { record Runtime(value: Int); }")
                 .value,
            "type dependency");
    require(!bind("function unused(value: Missing) -> Int { return 1; }",
                  "return 1;")
                 .value,
            "unknown nominal type");
    require(bind("function accept(value: Handle) -> Handle { return value; }",
                 "return 1;")
                .value.has_value(),
            "opaque value passing");
    require(
        !bind("function inspect(value: Handle) -> Int { return value.field; }",
              "return 1;")
             .value,
        "opaque introspection");
    require(!bind("intrinsic helper(value: Int) -> Int; function helper(value: "
                  "Bool) -> Int { return 1; }",
                  "return 1;")
                 .value,
            "function collision");
    require(!bind("type Recursive = List<Recursive>;", "return 1;").value,
            "recursive alias through a type argument");
    require(!bind("entity E { value: Int = runtime(); }", "return 1;",
                  "execution_model { function runtime() -> Int { return 1; } }")
                 .value,
            "retained field initializer dependency");
    require(
        !bind("function inspect(value: Int) -> Int { return value.missing(); }",
              "return 1;")
             .value,
        "unknown builtin member");
    agsem::ContractEnvironment invalid;
    require(
        invalid
            .addManifest(
                R"({"format":1,"id":"bad","version":"1","semantic":[{"name":"Alias","kind":"alias","target":{"name":"Runtime"}}],"execution":[{"name":"Runtime","kind":"opaque"}]})")
            .empty(),
        "invalid contract syntax");
    require(!invalid.validate().empty(), "manifest layer dependency");
    require(!contracts.addManifest(R"({"format":1,"id":"test","version":"2"})")
                 .empty(),
            "manifest version conflict");
    std::cout << "Binding closure, local scopes and external contracts OK\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
