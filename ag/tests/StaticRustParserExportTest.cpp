#include "agas/generator/StaticRustParserExport.h"
#include "agas/generator/ArtifactGeneration.h"
#include "agas/generator/ParserGeneration.h"
#include "agas/generator/TableExport.h"
#include "agas/model/Validation.h"
#include "agas/runtime/PackagedAgFrontend.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string{message});
}

auto readFile(const std::filesystem::path &path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read static Rust export fixture");
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

} // namespace

int main(int argc, char **argv) {
  using namespace agas::artifact;
  try {
    const bool full = argc == 2 && std::string_view{argv[1]} == "--full";
    require(argc == 1 || full, "usage: agas_static_rust_parser_export_tests [--full]");
    const ArtifactSymbols symbols{1, {{0, "a"}}, {{0, "start"}}, {}};
    const ArtifactProductions productions{
        1, {{0, 0, {{ArtifactSymbolKind::Terminal, 0}}}}};
    const ArtifactParserTable table{
        .algorithm = ArtifactParserAlgorithm::Lr,
        .lookahead = 1,
        .startState = 0,
        .actionRows =
            {
                {0, {{{{0}}, ArtifactShift{1}}}, std::nullopt},
                {1, {}, ArtifactReduce{0}},
                {2, {{{{std::nullopt}}, ArtifactAccept{}}}, std::nullopt},
            },
        .actionStateRows = {0, 1, 2},
        .gotoRows = {{0, {{0, 2}}}, {1, {}}},
        .gotoStateRows = {0, 1, 1},
    };

    const std::string first =
        agas::generator::exportStaticRustParser(table, symbols, productions);
    const std::string second =
        agas::generator::exportStaticRustParser(table, symbols, productions);
    require(first == second, "static Rust export must be deterministic");
    require(first.find("pub const PARSER_ALGORITHM: &str = \"LR\";") !=
                std::string::npos,
            "static Rust export must preserve the parser algorithm");
    require(first.find("LookaheadSymbol::EndOfInput") != std::string::npos,
            "static Rust export must preserve structural EOF");
    require(first.find("Action::Shift(1)") != std::string::npos &&
                first.find("Some(Action::Reduce(0))") != std::string::npos &&
                first.find("Action::Accept") != std::string::npos,
            "static Rust export must preserve all action kinds");
    require(first.find("Production { lhs: 0, rhs_len: 1 }") !=
                std::string::npos,
            "static Rust export must preserve production execution data");

    if (!full) {
      require(!readFile(AGAS_GENERATED_RUST_PARSER).empty(),
              "checked-in CompressedTable Rust parser must exist");
      std::cout << "static Rust parser export fixture passed\n";
      return 0;
    }

    const agas::runtime::PackagedAgFrontend frontend{AGAS_PINNED_ARTIFACT_DIR};
    const auto parsed = frontend.parse(readFile(AGAS_COMPRESSED_TABLE_GRAMMAR));
    require(parsed.accepted(), "pinned frontend must parse CompressedTable.ag");
    const auto validation = agas::model::validateSyntaxModel(*parsed.document);
    require(validation.valid(), "CompressedTable.ag must remain valid");
    const auto generated =
        agas::generator::generateParserTable(*parsed.document);
    auto [actualSymbols, actualProductions] =
        agas::generator::buildGrammarArtifactSections(*parsed.document,
                                                      generated);
    const auto actualTable = agas::artifact::parseParserTableDsl(
        agas::generator::exportCompressedTableDsl(generated).text,
        actualSymbols, actualProductions);
    const auto actualLexer = agas::generator::buildLexerArtifactSection(
        agas::generator::compileLexerAutomaton(*parsed.document,
                                               generated.bnf()),
        actualSymbols);
    const std::string actual =
        agas::generator::exportStaticRustLexer(actualLexer, actualSymbols) +
        "\n" +
        agas::generator::exportStaticRustReductions(generated.reductions(),
                                                    actualProductions) +
        "\n" +
        agas::generator::exportStaticRustParser(actualTable, actualSymbols,
                                                actualProductions);
    require(actual == readFile(AGAS_GENERATED_RUST_PARSER),
            "checked-in CompressedTable Rust parser is not reproducible");

    std::cout << "static Rust parser export bytes=" << actual.size() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
