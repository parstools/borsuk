#include "agas/runtime/AgSyntaxAdapter.h"
#include "agas/bootstrap/AntlrFrontend.h"
#include "agas/generator/LexerGeneration.h"
#include "agas/generator/ParserGeneration.h"
#include "agas/runtime/AstParser.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

auto readFile(const std::filesystem::path &path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read " + path.string());
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

} // namespace

int main() {
  try {
    const std::filesystem::path grammarDirectory = AGAS_TEST_GRAMMAR_DIR;
    const std::string agSource = readFile(grammarDirectory / "Ag.ag");
    const auto bootstrapAg = agas::bootstrap::parseAgas(agSource);
    if (!bootstrapAg.accepted()) {
      throw std::runtime_error("bootstrap frontend must parse Ag.ag");
    }
    const auto generated =
        agas::generator::generateParserTable(*bootstrapAg.document);
    const auto lexer = agas::generator::compileLexerAutomaton(
        *bootstrapAg.document, generated.bnf());
    const agas::runtime::GeneratedAstParser parser{generated};

    std::vector<std::filesystem::path> grammarFiles;
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(grammarDirectory)) {
      if (entry.is_regular_file() && entry.path().extension() == ".ag") {
        grammarFiles.push_back(entry.path());
      }
    }
    std::sort(grammarFiles.begin(), grammarFiles.end());
    if (grammarFiles.empty()) {
      throw std::runtime_error("no Agas grammar fixtures found");
    }

    for (const std::filesystem::path &path : grammarFiles) {
      const std::string source = readFile(path);
      const auto expected = agas::bootstrap::parseAgas(source);
      if (!expected.accepted()) {
        throw std::runtime_error("bootstrap rejected " + path.string());
      }
      const auto neutral = parser.parse(lexer.tokenize(source), source.size());
      if (!neutral.accepted()) {
        throw std::runtime_error("own LR parser rejected " + path.string() +
                                 ": " + neutral.error->message);
      }
      const auto actual = agas::runtime::adaptAgSyntaxDocument(
          *neutral.root, source, generated.bnf().grammar());
      if (actual != *expected.document) {
        throw std::runtime_error("SyntaxDocument differs for " + path.string());
      }
    }

    std::cout << "Agas SyntaxDocument oracle files=" << grammarFiles.size()
              << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
