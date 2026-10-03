#include "agas/runtime/ArtifactLexerRuntime.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

auto fixture() -> agas::artifact::ArtifactLexer {
  using agas::artifact::ArtifactCodePointRange;
  using agas::artifact::ArtifactDfaState;
  using agas::artifact::ArtifactLexer;
  using agas::artifact::ArtifactLexerRule;
  using agas::artifact::ArtifactLexerTransition;

  return ArtifactLexer{
      .version = 1,
      .rules =
          {
              ArtifactLexerRule{"A", 0, std::nullopt, false},
              ArtifactLexerRule{"Whitespace", std::nullopt, std::nullopt, true},
              ArtifactLexerRule{"Greek", 1, 0, false},
          },
      .dfaStates =
          {
              ArtifactDfaState{
                  std::nullopt,
                  {
                      ArtifactLexerTransition{
                          {ArtifactCodePointRange{' ', ' '}}, 2},
                      ArtifactLexerTransition{
                          {ArtifactCodePointRange{'a', 'a'}}, 1},
                      ArtifactLexerTransition{
                          {ArtifactCodePointRange{0x03B1, 0x03B1}}, 3},
                  }},
              ArtifactDfaState{0,
                               {ArtifactLexerTransition{
                                   {ArtifactCodePointRange{'a', 'a'}}, 1}}},
              ArtifactDfaState{1,
                               {ArtifactLexerTransition{
                                   {ArtifactCodePointRange{' ', ' '}}, 2}}},
              ArtifactDfaState{2, {}},
          },
      .orderedNfas = {}};
}

void expectError(const agas::runtime::ArtifactLexerRuntime &lexer,
                 const std::string &input,
                 agas::runtime::ArtifactLexerErrorKind expectedKind) {
  try {
    static_cast<void>(lexer.tokenize(input));
  } catch (const agas::runtime::ArtifactLexerError &error) {
    if (error.kind() == expectedKind)
      return;
    throw std::runtime_error("artifact lexer reported the wrong error kind");
  }
  throw std::runtime_error("artifact lexer accepted invalid input");
}

} // namespace

int main() {
  try {
    const agas::runtime::ArtifactLexerRuntime lexer{fixture(), 2, 1};
    const auto result = lexer.tokenize("aa \xCE\xB1");
    if (result.tokens.size() != 2 || result.parserTerminalIds.size() != 1 ||
        result.tokens[0].terminal != 0 || result.tokens[0].channel ||
        result.tokens[0].offset != 0 || result.tokens[0].text != "aa" ||
        result.tokens[1].terminal != 1 || result.tokens[1].channel != 0 ||
        result.tokens[1].offset != 3 || result.tokens[1].text != "\xCE\xB1" ||
        result.parserTerminalIds[0] != 0)
      throw std::runtime_error("artifact DFA produced unexpected tokens");

    expectError(lexer, "#",
                agas::runtime::ArtifactLexerErrorKind::NoMatchingRule);
    expectError(lexer, std::string{static_cast<char>(0xC0)},
                agas::runtime::ArtifactLexerErrorKind::InvalidEncoding);
    std::cout << "artifact lexer runtime tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
