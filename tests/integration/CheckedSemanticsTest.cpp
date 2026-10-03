#include "agsem/CheckedSemantics.h"
#include "SemanticInputStorage.h"
#include "agas/artifact/ArtifactPackage.h"
#include "agas/runtime/ArtifactAstParser.h"
#include "agas/runtime/ArtifactLexerRuntime.h"
#include "agas/runtime/PackagedAgFrontend.h"
#include "agsem/SemanticInput.h"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
auto readFile(const char *path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot open semantic fixture");
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 5)
    return 2;
  const auto package = agas::artifact::loadArtifactPackageDirectory(argv[1]);
  const agas::runtime::ArtifactLexerRuntime lexer{
      package.lexer, package.symbols.terminals.size(),
      package.symbols.channels.size()};
  const agas::runtime::ArtifactAstParser parser{
      package.parserTable, package.symbols, package.productions,
      package.reductions};
  const auto parsed = parser.parse(readFile(argv[3]), lexer);
  const agas::runtime::PackagedAgFrontend grammarFrontend{argv[2]};
  const auto grammar = grammarFrontend.parse(readFile(argv[4]));
  if (!parsed.accepted() || !grammar.accepted())
    return 3;
  const auto checked = [&] {
    const auto input =
        agsem::SemanticInputAccess::fromAst(*parsed.root, *grammar.document);
    return agsem::tryPrepareSemanticModel(input);
  }();
  if (!checked.value || !checked.diagnostics.empty())
    return 4;
  const auto generated = agsem::emitCheckedSemanticsRust(**checked.value);
  if (generated.sema.empty() || generated.semaLib.empty() ||
      generated.sema.find("analyze_program") == std::string::npos)
    return 5;
  return 0;
}
