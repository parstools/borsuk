#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "agas/model/BnfLowering.h"
#include "agas/model/SyntaxModel.h"
#include "grammar/Identifiers.h"
#include "lexer/Utf8Lexer.h"
#include "regex/RegexAst.h"

namespace agas::generator {

class LexerGenerationError final : public std::runtime_error {
public:
  LexerGenerationError(model::SourceSpan span, std::string message);

  [[nodiscard]] auto span() const noexcept -> const model::SourceSpan &;

private:
  model::SourceSpan span_;
};

struct CompiledLexerRule {
  std::string name;
  zbik::RegexAst expression;
  std::optional<zbik::TerminalId> terminal;
  std::optional<std::string> channel;
  bool skipped{};
  model::SourceSpan span;
};

struct AgasLexedToken {
  zbik::TerminalId terminal;
  std::optional<std::string> channel;
  std::size_t offset{};
  std::string text;

  auto operator==(const AgasLexedToken &) const -> bool = default;
};

struct AgasLexResult {
  std::vector<AgasLexedToken> tokens;
  std::vector<zbik::TerminalId> parserTerminalIds;

  auto operator==(const AgasLexResult &) const -> bool = default;
};

class GeneratedLexerAutomaton {
public:
  GeneratedLexerAutomaton(std::vector<CompiledLexerRule> rules,
                          zbik::Utf8Lexer lexer);

  [[nodiscard]] auto rules() const noexcept
      -> const std::vector<CompiledLexerRule> &;
  [[nodiscard]] auto dfaStateCount() const noexcept -> std::size_t;
  [[nodiscard]] auto transitionRangeCount() const noexcept -> std::size_t;
  [[nodiscard]] auto runtimeLexer() const noexcept -> const zbik::Utf8Lexer &;
  [[nodiscard]] auto tokenize(std::string_view input) const -> AgasLexResult;

private:
  std::vector<CompiledLexerRule> rules_;
  zbik::Utf8Lexer lexer_;
};

// Compiles the regular eager subset. Lazy quantifiers are rejected until
// their path-priority semantics is implemented explicitly.
// contextualSource is an internal bridge: masks are attached by ContextualGeneration.
// Its result must not be used to tokenize or export a contextual grammar directly.
[[nodiscard]] auto compileLexerAutomaton(
    const model::SyntaxDocument &document, const model::BnfModel &bnf,
    bool contextualSource = false)
    -> GeneratedLexerAutomaton;

} // namespace agas::generator
