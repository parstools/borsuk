#include "agas/runtime/SelfHostedAgFrontend.h"
#include "agas/bootstrap/AntlrFrontend.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string{message});
}

auto readFile(const std::filesystem::path &path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read " + path.string());
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

auto grammarCorpus(const std::filesystem::path &directory)
    -> std::vector<std::filesystem::path> {
  std::vector<std::filesystem::path> result;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(directory)) {
    if (entry.is_regular_file() && entry.path().extension() == ".ag")
      result.push_back(entry.path());
  }
  std::sort(result.begin(), result.end());
  return result;
}

auto nextRandom(std::uint32_t &state) -> std::uint32_t {
  state ^= state << 13U;
  state ^= state >> 17U;
  state ^= state << 5U;
  return state;
}

auto mutate(std::string source, std::uint32_t &randomState,
            std::size_t mutationIndex) -> std::string {
  static constexpr std::array<std::string_view, 9> insertions{
      "@", ";", "node ", "(", ")", "|", "?", "`", "\xC0"};
  const std::size_t position =
      source.empty() ? 0 : nextRandom(randomState) % (source.size() + 1);
  switch (mutationIndex % 3) {
  case 0:
    if (position < source.size())
      source.erase(position, 1);
    else
      source.push_back(';');
    break;
  case 1: {
    const std::string_view insertion =
        insertions[nextRandom(randomState) % insertions.size()];
    source.insert(position, insertion);
    break;
  }
  case 2:
    if (position < source.size())
      source[position] = source[position] == ';' ? ':' : ';';
    else
      source.push_back(':');
    break;
  }
  return source;
}

void compareFrontends(const agas::runtime::SelfHostedAgFrontend &frontend,
                      std::string_view source, std::string_view fixture) {
  using agas::runtime::AgFrontendIssueKind;

  const auto oracle = agas::bootstrap::parseAgas(source);
  const auto actual = frontend.parse(source);
  require(actual.accepted() == oracle.accepted(),
          std::string{fixture} + ": frontend acceptance differs");
  if (oracle.accepted()) {
    require(actual.document == oracle.document,
            std::string{fixture} + ": SyntaxDocument differs");
    return;
  }

  require(!oracle.issues.empty(),
          std::string{fixture} + ": rejected oracle has no issue");
  require(actual.issues.size() == 1,
          std::string{fixture} + ": own frontend must return one issue");
  const auto &issue = actual.issues.front();
  require(issue.span.beginByte <= issue.span.endByte &&
              issue.span.endByte <= source.size() && !issue.message.empty(),
          std::string{fixture} + ": own diagnostic is incomplete");
  if (issue.kind == AgFrontendIssueKind::Syntax) {
    require(issue.state.has_value() && issue.lookahead.has_value() &&
                !issue.expected.empty(),
            std::string{fixture} + ": syntax diagnostic lacks LR(k) context");
  }
}

} // namespace

int main() {
  using agas::runtime::AgFrontendIssueKind;

  try {
    const std::filesystem::path grammarDirectory = AGAS_TEST_GRAMMAR_DIR;
    const std::vector<std::filesystem::path> corpus =
        grammarCorpus(grammarDirectory);
    require(!corpus.empty(), "the .ag grammar corpus must not be empty");
    const std::string agSource = readFile(grammarDirectory / "Ag.ag");
    const auto bootstrapDefinition = agas::bootstrap::parseAgas(agSource);
    require(bootstrapDefinition.accepted(),
            "bootstrap must load the self-hosted Agas definition");
    const agas::runtime::SelfHostedAgFrontend frontend{
        *bootstrapDefinition.document};

    std::vector<std::string> corpusSources;
    corpusSources.reserve(corpus.size());
    for (const std::filesystem::path &path : corpus) {
      corpusSources.push_back(readFile(path));
      compareFrontends(frontend, corpusSources.back(), path.string());
    }

    const std::vector<std::string> invalidSyntax{
        "grammar Broken node start : EOF ;",
        "grammar Broken; node start : TOKEN EOF",
        "grammar Broken; node start : empty | ;",
        "grammar Broken; node start : value=( EOF ;",
    };
    for (const std::string &source : invalidSyntax) {
      const auto oracle = agas::bootstrap::parseAgas(source);
      const auto actual = frontend.parse(source);
      require(!oracle.accepted() && !oracle.issues.empty(),
              "ANTLR oracle must reject each syntax fixture");
      require(!actual.accepted() && actual.issues.size() == 1 &&
                  actual.issues.front().kind == AgFrontendIssueKind::Syntax,
              "the self-hosted frontend must return one syntax issue");
      const auto &issue = actual.issues.front();
      require(issue.state.has_value() && issue.lookahead.has_value() &&
                  !issue.expected.empty() &&
                  issue.span.beginByte <= issue.span.endByte &&
                  issue.span.endByte <= source.size(),
              "syntax diagnostics must retain state, token range and "
              "expected LR(2) words");
      if (oracle.issues.front().line == 1) {
        require(issue.span.beginByte <= oracle.issues.front().column,
                "LR(2) may detect an error earlier than ANTLR but not after "
                "the oracle position in one-line fixtures");
      }
    }

    const std::string invalidCharacter = "grammar Broken; `";
    const auto oracleLexical = agas::bootstrap::parseAgas(invalidCharacter);
    const auto actualLexical = frontend.parse(invalidCharacter);
    require(!oracleLexical.accepted() && !actualLexical.accepted() &&
                actualLexical.issues.size() == 1 &&
                actualLexical.issues.front().kind ==
                    AgFrontendIssueKind::Lexical &&
                actualLexical.issues.front().span.beginByte == 16 &&
                actualLexical.issues.front().span.endByte == 17,
            "lexical errors must retain the offending byte range");

    std::string invalidUtf8 = "grammar Broken; ";
    invalidUtf8.append("\xC0\xAF", 2);
    const auto invalidEncoding = frontend.parse(invalidUtf8);
    require(!invalidEncoding.accepted() &&
                invalidEncoding.issues.front().kind ==
                    AgFrontendIssueKind::Lexical &&
                invalidEncoding.issues.front().span.beginByte == 16,
            "invalid UTF-8 must be reported as a lexical issue");

    constexpr std::size_t mutationCount = 256;
    std::uint32_t randomState = 0xA6A52023U;
    for (std::size_t index = 0; index < mutationCount; ++index) {
      const std::size_t sourceIndex = nextRandom(randomState) % corpus.size();
      const std::string changed =
          mutate(corpusSources[sourceIndex], randomState, index);
      compareFrontends(frontend, changed,
                       "mutation " + std::to_string(index) + " of " +
                           corpus[sourceIndex].string());
    }

    std::cout << "self-hosted frontend corpus=" << corpus.size()
              << " mutations=" << mutationCount
              << " invalid-syntax=" << invalidSyntax.size()
              << " invalid-lexical=2\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
