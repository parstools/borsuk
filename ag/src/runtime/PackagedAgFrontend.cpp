#include "agas/runtime/PackagedAgFrontend.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "agas/runtime/AgSyntaxAdapter.h"
#include "agas/runtime/SourceText.h"
#include "first/LookaheadSymbols.h"

namespace agas::runtime {
namespace {

auto lookahead(const artifact::ArtifactLookaheadWord &source)
    -> zbik::LookaheadWord {
  std::vector<zbik::LookaheadSymbol> result;
  result.reserve(source.size());
  for (const auto &symbol : source) {
    if (symbol.terminal)
      result.emplace_back(zbik::TerminalId{*symbol.terminal});
    else
      result.emplace_back(zbik::endOfInput);
  }
  return zbik::LookaheadWord{std::move(result)};
}

} // namespace

PackagedAgFrontend::PackagedAgFrontend(
    const std::filesystem::path &artifactDirectory)
    : package_(artifact::loadArtifactPackageDirectory(artifactDirectory)),
      lexer_(package_.lexer, package_.symbols.terminals.size(),
             package_.symbols.channels.size()),
      parser_(package_.parserTable, package_.symbols, package_.productions,
              package_.reductions) {}

auto PackagedAgFrontend::parse(std::string_view source) const
    -> AgFrontendResult {
  ArtifactAstParseResult parsed;
  try {
    parsed = parser_.parse(source, lexer_);
  } catch (const ArtifactLexerError &error) {
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

  if (!parsed.accepted()) {
    ArtifactAstParseError &error = *parsed.error;
    std::vector<zbik::LookaheadWord> expected;
    expected.reserve(error.expected.size());
    for (const auto &word : error.expected)
      expected.push_back(lookahead(word));
    return {std::nullopt,
            {{AgFrontendIssueKind::Syntax, error.span,
              zbik::StateId{error.state}, lookahead(error.lookahead),
              std::move(expected), std::move(error.message)}}};
  }
  try {
    return {adaptAgSyntaxDocument(*parsed.root, source, package_.symbols), {}};
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

auto PackagedAgFrontend::package() const noexcept
    -> const artifact::LoadedArtifactPackage & {
  return package_;
}

auto PackagedAgFrontend::parseDocument(std::string_view source) const
    -> GrammarDocumentResult {
  auto parsed = parse(source);
  if (!parsed.accepted()) return {std::nullopt, std::move(parsed.issues)};
  const auto tokens = lexer_.tokenize(source, package_.parserTable, package_.productions);
  return {model::GrammarDocument{std::move(*parsed.document),
             captureSourceText(std::string{source}, tokens)}, {}};
}

} // namespace agas::runtime
