#include "coge/DocumentModel.h"
#include "AstAccess.h"
#include "SemanticInputStorage.h"
#include "agsem/DocumentFrontend.h"
#include <iostream>
#include <stdexcept>
int main(int argc, char **argv) {
  try {
    if (argc != 3)
      return 2;
    const agsem::DocumentFrontend frontend{argv[1], argv[2]};
    const std::string grammar =
        "grammar Test; node start : ID analysis {}; ID:'a';";
    const auto sema = frontend.parse("sema Test;" + grammar);
    if (!sema.value || !agsem::makeSemaDocument(*sema.value).value ||
        coge::makeCogeDocument(*sema.value).value)
      return 3;
    for (const auto *section :
         {"execution_model {}", "execution_contract {}", "lowering_model {}",
          "backend_c {}", "backend_llvm {}", "generation {}"}) {
      const auto parsed = frontend.parse("sema Test;" + grammar + section);
      if (!parsed.value || agsem::makeSemaDocument(*parsed.value).value)
        return 4;
      const auto mixed = frontend.parse("coge Test;" + grammar + section);
      if (!mixed.value || !coge::makeCogeDocument(*mixed.value).value)
        return 5;
    }
    for (const auto *body : {"ID", "empty"}) {
      auto parsed =
          frontend.parse(std::string{"sema Test; grammar Test; node start : "} +
                         body + " execution result {}; ID:'a';");
      if (!parsed.value || agsem::makeSemaDocument(*parsed.value).value)
        return 6;
    }
    const auto mixed = frontend.parse(
        "coge Test;" + grammar +
        "execution_model { function helper() -> Unit { return none; } }");
    const auto coge = coge::makeCogeDocument(*mixed.value);
    if (!coge.value)
      return 7;
    const auto &root = agsem::SemanticInputAccess::root(
        coge.value->semantics().semanticInput());
    if (!agsem::ast::field(root, "execution").elements.empty())
      return 8;
    for (const auto *settings : {"generation { unknown = true; }",
                                 "generation { generate_interpreter = false; }",
                                 "generation { generate_interpreter = true; "
                                 "generate_interpreter = true; }"}) {
      const auto invalid = frontend.parse("coge Test;" + grammar + settings);
      if (!invalid.value || coge::makeCogeDocument(*invalid.value).value)
        return 9;
    }
    std::cout << "Document ownership and isolated semantic input OK\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
