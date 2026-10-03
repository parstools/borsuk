#include "agas/artifact/ArtifactPackage.h"
#include "agas/artifact/Sha256.h"
#include "agas/runtime/ArtifactAstParser.h"
#include "agas/runtime/ArtifactLexerRuntime.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string{message});
}

auto sections() -> agas::artifact::ArtifactPackageSections {
  using namespace agas::artifact;
  using namespace agas::generator;

  const ArtifactSymbols symbols{1, {{0, "id"}}, {{0, "document"}}, {}};
  const ArtifactProductions productions{
      1, {{0, 0, {{ArtifactSymbolKind::Terminal, 0}}}}};
  const ArtifactLexer lexer{1,
                            {{"Id", 0, std::nullopt, false}},
                            {{std::nullopt, {{{{'x', 'x'}}, 1}}}, {0, {}}},
                            {}};
  const ArtifactParserTable table{
      ArtifactParserAlgorithm::Lr,
      1,
      0,
      {{0, {{{{0}}, ArtifactShift{1}}}, std::nullopt},
       {1, {}, ArtifactReduce{0}},
       {2, {{{{std::nullopt}}, ArtifactAccept{}}}, std::nullopt}},
      {0, 1, 2},
      {{0, {{0, 2}}}, {1, {}}},
      {0, 1, 1}};
  const AstReductionProgram reductions{{
      {zbik::RuleId{0},
       1,
       ReductionOpcode::Forward,
       ReductionSpanPolicy::MatchedRhs,
       std::nullopt,
       std::nullopt,
       {0},
       {}},
  }};
  const AstType tokenType{AstTypeKind::Token, "id", {}};
  const AstSchema schema{{
      {"document",
       agas::model::TreeModifier::Node,
       {tokenType},
       {},
       {{0, std::nullopt, tokenType, {}}}},
  }};
  return {dumpSymbolsJson(symbols),
          dumpLexerJson(lexer, 1, 0),
          dumpParserTableDsl(table, symbols, productions),
          dumpProductionsJson(symbols, productions),
          dumpReductionsJson(productions, reductions),
          dumpAstSchemaJson(schema),
          std::nullopt};
}

auto identity() -> agas::artifact::ArtifactManifest {
  const std::string sourceHash = agas::artifact::sha256Hex("grammar source");
  return {1,
          "canonical-lr",
          1,
          "document",
          "document",
          "agas-test",
          "zbik-test",
          "15.1",
          sourceHash,
          sourceHash,
          agas::artifact::sha256Hex("settings"),
          {}};
}

struct TemporaryDirectory {
  std::filesystem::path path;
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};

} // namespace

int main() {
  try {
    require(agas::artifact::sha256Hex("") ==
                    "e3b0c44298fc1c149afbf4c8996fb924"
                    "27ae41e4649b934ca495991b7852b855" &&
                agas::artifact::sha256Hex("abc") ==
                    "ba7816bf8f01cfea414140de5dae2223"
                    "b00361a396177a9cb410ff61f20015ad",
            "SHA-256 must match standard test vectors");

    const auto package =
        agas::artifact::makeArtifactPackage(identity(), sections());
    const auto loaded = agas::artifact::loadArtifactPackage(
        package.manifestJson, package.sections);
    const agas::runtime::ArtifactLexerRuntime lexer{
        loaded.lexer, loaded.symbols.terminals.size(),
        loaded.symbols.channels.size()};
    const agas::runtime::ArtifactAstParser parser{
        loaded.parserTable, loaded.symbols, loaded.productions,
        loaded.reductions};
    const auto tokens = lexer.tokenize("x");
    const auto parsed = parser.parse(tokens, 1);
    require(parsed.accepted() && parsed.root->tokenText == "x",
            "loaded package must execute without generator state");

    auto tampered = package.sections;
    tampered.lexer.front() = '[';
    try {
      static_cast<void>(
          agas::artifact::loadArtifactPackage(package.manifestJson, tampered));
      throw std::runtime_error("tampered package was accepted");
    } catch (const agas::artifact::ArtifactPackageError &error) {
      require(error.code() ==
                  agas::artifact::ArtifactPackageErrorCode::HashMismatch,
              "tampered section must fail its SHA-256 check");
    }

    auto wrongProfile = package.manifest;
    wrongProfile.lookahead = 2;
    try {
      static_cast<void>(agas::artifact::loadArtifactPackage(
          agas::artifact::dumpArtifactManifestJson(wrongProfile),
          package.sections));
      throw std::runtime_error("inconsistent parser profile was accepted");
    } catch (const agas::artifact::ArtifactPackageError &error) {
      require(
          error.code() ==
              agas::artifact::ArtifactPackageErrorCode::InconsistentSections,
          "manifest and parser table profiles must agree");
    }

    const TemporaryDirectory temporary{std::filesystem::current_path() /
                                       "agas-artifact-package-test.tmp"};
    std::filesystem::remove_all(temporary.path);
    agas::artifact::writeArtifactPackageDirectory(package, temporary.path);
    const auto fromDirectory =
        agas::artifact::loadArtifactPackageDirectory(temporary.path);
    require(fromDirectory.parserTable == loaded.parserTable,
            "directory loader must preserve the parser table");

    std::filesystem::remove(temporary.path / "lexer.json");
    std::filesystem::create_symlink("symbols.json",
                                    temporary.path / "lexer.json");
    try {
      static_cast<void>(
          agas::artifact::loadArtifactPackageDirectory(temporary.path));
      throw std::runtime_error("symlinked package section was accepted");
    } catch (const agas::artifact::ArtifactPackageError &error) {
      require(error.code() ==
                  agas::artifact::ArtifactPackageErrorCode::UnsafeFile,
              "directory loader must reject symlinked sections");
    }

    std::cout << "artifact package sections="
              << package.manifest.sections.size() << " runtime=accepted\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
