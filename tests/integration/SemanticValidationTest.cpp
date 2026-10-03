#include "SemanticValidation.h"
#include "agas/artifact/ArtifactPackage.h"
#include "agas/runtime/ArtifactAstParser.h"
#include "agas/runtime/ArtifactLexerRuntime.h"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

auto readFile(const char *path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot open semantic fixture");
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

auto field(const agas::runtime::AstValue &value, std::string_view name)
    -> const agas::runtime::AstValue & {
  for (std::size_t index = 0; index < value.fieldNames.size(); ++index)
    if (value.fieldNames[index] == name)
      return value.elements[index];
  throw std::runtime_error("missing semantic model");
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
  for (int index = 2; index < 5; ++index) {
    const auto parsed = parser.parse(readFile(argv[index]), lexer);
    if (!parsed.accepted())
      return 3;
    const auto &model = field(*parsed.root, "model");
    if (model.elements.empty())
      return 4;
    try {
      agsem::checkFunctionCycles(model.elements.front());
      if (index != 2)
        return 5;
    } catch (const std::runtime_error &error) {
      if (index == 2)
        return 6;
      const std::string_view message{error.what()};
      if (index == 3 && !message.starts_with("function call cycle:"))
        return 7;
      if (index == 4 && !message.starts_with("unresolved call in function"))
        return 8;
    }
  }
  return 0;
}
