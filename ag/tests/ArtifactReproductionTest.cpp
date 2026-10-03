#include "agas/artifact/ArtifactPackage.h"
#include "agas/generator/ArtifactGeneration.h"
#include "agas/generator/LexerGeneration.h"
#include "agas/generator/ParserGeneration.h"
#include "agas/model/Validation.h"
#include "agas/runtime/AgSyntaxAdapter.h"
#include "agas/runtime/ArtifactAstParser.h"
#include "agas/runtime/ArtifactLexerRuntime.h"
#include "regex/UnicodeProperties.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string{message});
}

auto readFile(const std::filesystem::path &path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read reproduction input");
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

void requireSamePackage(
    const agas::artifact::ArtifactPackage &actual,
    std::string_view expectedManifest,
    const agas::artifact::ArtifactPackageSections &expected) {
  require(actual.manifestJson == expectedManifest,
          "regenerated manifest differs from pinned artifact");
  require(actual.sections.symbols == expected.symbols &&
              actual.sections.lexer == expected.lexer &&
              actual.sections.parserTable == expected.parserTable &&
              actual.sections.productions == expected.productions &&
              actual.sections.reductions == expected.reductions &&
              actual.sections.astSchema == expected.astSchema &&
              actual.sections.diagnostics == expected.diagnostics,
          "regenerated sections differ from pinned artifact");
}

struct TemporaryDirectory {
  std::filesystem::path path;
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};

} // namespace

int main(int argc, char **argv) {
  try {
    const bool full = argc == 2 && std::string_view{argv[1]} == "--full";
    require(argc == 1 || full, "usage: agas_artifact_reproduction_tests [--full]");
    const std::filesystem::path artifactDirectory = AGAS_PINNED_ARTIFACT_DIR;
    const std::string source = readFile(AGAS_TEST_GRAMMAR_FILE);
    const auto loaded =
        agas::artifact::loadArtifactPackageDirectory(artifactDirectory);
    const agas::runtime::ArtifactLexerRuntime lexer{
        loaded.lexer, loaded.symbols.terminals.size(),
        loaded.symbols.channels.size()};
    const agas::runtime::ArtifactAstParser parser{
        loaded.parserTable, loaded.symbols, loaded.productions,
        loaded.reductions};
    const auto parsed = parser.parse(lexer.tokenize(source), source.size());
    require(parsed.accepted(), "pinned artifact must parse its Ag.ag source");
    const auto document = agas::runtime::adaptAgSyntaxDocument(
        *parsed.root, source, loaded.symbols);
    require(agas::model::validateSyntaxModel(document).valid(),
            "pinned artifact must reproduce a valid syntax model");

    if (!full) {
      std::cout << "pinned artifact parses Ag.ag\n";
      return 0;
    }

    const auto generated = agas::generator::generateParserTable(document);
    const auto generatedLexer =
        agas::generator::compileLexerAutomaton(document, generated.bnf());
    const auto reproduced = agas::generator::buildParserArtifactPackage(
        document, generated, generatedLexer, source, source,
        {AGAS_GENERATOR_VERSION, AGAS_ZBIK_REVISION,
         zbik::unicodeDataVersion()});

    agas::artifact::ArtifactPackageSections pinnedSections;
    pinnedSections.symbols = readFile(artifactDirectory / "symbols.json");
    pinnedSections.lexer = readFile(artifactDirectory / "lexer.json");
    pinnedSections.parserTable = readFile(artifactDirectory / "parser.dsl");
    pinnedSections.productions =
        readFile(artifactDirectory / "productions.json");
    pinnedSections.reductions = readFile(artifactDirectory / "reductions.json");
    pinnedSections.astSchema = readFile(artifactDirectory / "ast-schema.json");
    pinnedSections.diagnostics = readFile(artifactDirectory / "diagnostics.json");
    const std::string pinnedManifest =
        readFile(artifactDirectory / "manifest.json");
    requireSamePackage(reproduced, pinnedManifest, pinnedSections);

    const TemporaryDirectory temporary{std::filesystem::current_path() /
                                       "agas-artifact-reproduction.tmp"};
    std::error_code ignored;
    std::filesystem::remove_all(temporary.path, ignored);
    agas::artifact::writeArtifactPackageDirectory(reproduced, temporary.path);
    const auto reloaded =
        agas::artifact::loadArtifactPackageDirectory(temporary.path);
    require(reloaded.manifest == loaded.manifest &&
                reloaded.parserTable == loaded.parserTable,
            "rewritten artifact package differs after directory reload");

    std::cout << "reproduced artifact files=8 bytes="
              << reproduced.manifestJson.size() +
                     reproduced.sections.symbols.size() +
                     reproduced.sections.lexer.size() +
                     reproduced.sections.parserTable.size() +
                     reproduced.sections.productions.size() +
                     reproduced.sections.reductions.size() +
                     reproduced.sections.astSchema.size() +
                     reproduced.sections.diagnostics->size()
              << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
