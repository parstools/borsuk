#include "agas/artifact/GrammarSections.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string{message});
}

auto symbols() -> agas::artifact::ArtifactSymbols {
  return {
      1, {{0, "identifier"}, {1, "+"}}, {{0, "expression"}}, {{0, "comments"}}};
}

auto productions() -> agas::artifact::ArtifactProductions {
  using agas::artifact::ArtifactSymbolKind;
  return {1,
          {{0,
            0,
            {{ArtifactSymbolKind::Nonterminal, 0},
             {ArtifactSymbolKind::Terminal, 1},
             {ArtifactSymbolKind::Terminal, 0}}},
           {1, 0, {{ArtifactSymbolKind::Terminal, 0}}}}};
}

void expectError(const auto &operation,
                 agas::artifact::GrammarSectionErrorCode expected,
                 std::string_view message) {
  try {
    operation();
  } catch (const agas::artifact::GrammarSectionError &error) {
    require(error.code() == expected, message);
    return;
  }
  throw std::runtime_error(std::string{message});
}

auto replaceOnce(std::string source, std::string_view from, std::string_view to)
    -> std::string {
  const std::size_t position = source.find(from);
  require(position != std::string::npos, "JSON test fixture pattern is absent");
  source.replace(position, from.size(), to);
  return source;
}

} // namespace

int main() {
  using agas::artifact::GrammarSectionErrorCode;

  try {
    const auto expectedSymbols = symbols();
    const auto expectedProductions = productions();
    agas::artifact::validateGrammarSections(expectedSymbols,
                                            expectedProductions);

    const std::string symbolsJson =
        agas::artifact::dumpSymbolsJson(expectedSymbols);
    const auto actualSymbols = agas::artifact::parseSymbolsJson(symbolsJson);
    require(actualSymbols == expectedSymbols,
            "symbols JSON round-trip must preserve the model");
    require(agas::artifact::dumpSymbolsJson(actualSymbols) == symbolsJson,
            "symbols JSON must be byte-for-byte reproducible");

    const std::string productionsJson = agas::artifact::dumpProductionsJson(
        expectedSymbols, expectedProductions);
    const auto actualProductions =
        agas::artifact::parseProductionsJson(productionsJson, actualSymbols);
    require(actualProductions == expectedProductions,
            "productions JSON round-trip must preserve the model");
    require(agas::artifact::dumpProductionsJson(
                actualSymbols, actualProductions) == productionsJson,
            "productions JSON must be byte-for-byte reproducible");

    auto sparse = expectedSymbols;
    sparse.terminals[1].id = 3;
    expectError(
        [&] {
          agas::artifact::validateGrammarSections(
              sparse, agas::artifact::ArtifactProductions{1, {}});
        },
        GrammarSectionErrorCode::InvalidId,
        "sparse symbol IDs must be rejected");

    auto badReference = expectedProductions;
    badReference.productions[1].rhs[0].id = 9;
    expectError(
        [&] {
          agas::artifact::validateGrammarSections(expectedSymbols,
                                                  badReference);
        },
        GrammarSectionErrorCode::InvalidReference,
        "unknown RHS symbols must be rejected");

    expectError(
        [&] {
          static_cast<void>(agas::artifact::parseSymbolsJson(
              replaceOnce(symbolsJson, "\"version\": 1,",
                          "\"version\": 1,\n  \"version\": 1,")));
        },
        GrammarSectionErrorCode::InvalidJson,
        "duplicate fields must be rejected");
    expectError(
        [&] {
          static_cast<void>(agas::artifact::parseProductionsJson(
              replaceOnce(productionsJson, "\"lhs\": 0",
                          "\"lhs\": 0, \"unknown\": 1"),
              expectedSymbols));
        },
        GrammarSectionErrorCode::UnknownField,
        "unknown fields must be rejected");

    std::cout << "grammar artifact terminals="
              << expectedSymbols.terminals.size()
              << " productions=" << expectedProductions.productions.size()
              << " negative-cases=4\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
