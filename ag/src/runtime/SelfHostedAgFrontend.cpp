#include "agas/runtime/SelfHostedAgFrontend.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "agas/runtime/AgSyntaxAdapter.h"
#include "lexer/Utf8Lexer.h"

namespace agas::runtime {

auto AgFrontendResult::accepted() const noexcept -> bool {
  return issues.empty() && document.has_value();
}

SelfHostedAgFrontend::SelfHostedAgFrontend(
    const model::SyntaxDocument &agasDefinition)
    : generated_(generator::generateParserTable(agasDefinition)),
      lexer_(
          generator::compileLexerAutomaton(agasDefinition, generated_.bnf())),
      parser_(generated_) {}

auto SelfHostedAgFrontend::parse(std::string_view source) const
    -> AgFrontendResult {
  generator::AgasLexResult lexed;
  try {
    lexed = lexer_.tokenize(source);
  } catch (const zbik::Utf8LexerError &error) {
    const std::uint64_t begin = error.tokenStart();
    std::uint64_t end = std::max(error.errorOffset(), error.tokenStart());
    if (end == begin && end < source.size())
      ++end;
    return {std::nullopt,
            {{AgFrontendIssueKind::Lexical,
              {begin, end},
              std::nullopt,
              std::nullopt,
              {},
              error.what()}}};
  }

  AstParseResult parsed = parser_.parse(lexed, source.size());
  if (!parsed.accepted()) {
    AstParseError &error = *parsed.error;
    return {std::nullopt,
            {{AgFrontendIssueKind::Syntax, error.span, error.state,
              std::move(error.lookahead), std::move(error.expected),
              std::move(error.message)}}};
  }
  try {
    return {
        adaptAgSyntaxDocument(*parsed.root, source, generated_.bnf().grammar()),
        {}};
  } catch (const AgSyntaxAdapterError &error) {
    return {std::nullopt,
            {{AgFrontendIssueKind::Adapter,
              error.span(),
              std::nullopt,
              std::nullopt,
              {},
              error.what()}}};
  }
}

auto SelfHostedAgFrontend::generatedParser() const noexcept
    -> const generator::GeneratedParserTable & {
  return generated_;
}

} // namespace agas::runtime
