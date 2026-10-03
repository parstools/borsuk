#include "agas/runtime/ArtifactAstParser.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <type_traits>
#include <utility>

namespace agas::runtime {
namespace {

auto parserTokens(const ArtifactLexResult &lexed)
    -> std::vector<const ArtifactLexedToken *> {
  std::vector<const ArtifactLexedToken *> result;
  result.reserve(lexed.parserTerminalIds.size());
  for (const auto &token : lexed.tokens)
    if (!token.channel)
      result.push_back(&token);
  if (result.size() != lexed.parserTerminalIds.size())
    throw ArtifactParserRuntimeError{
        ArtifactParserRuntimeErrorCode::InvalidInput,
        "lexer parser-terminal projection has an invalid size"};
  for (std::size_t index = 0; index < result.size(); ++index)
    if (result[index]->terminal != lexed.parserTerminalIds[index])
      throw ArtifactParserRuntimeError{
          ArtifactParserRuntimeErrorCode::InvalidInput,
          "lexer parser-terminal projection does not match emitted tokens"};
  return result;
}

void validateTokenSpans(const std::vector<const ArtifactLexedToken *> &tokens,
                        std::uint64_t inputSize, std::size_t terminalCount) {
  std::uint64_t previousEnd = 0;
  for (const auto *token : tokens) {
    const std::uint64_t begin = token->offset;
    if (token->terminal >= terminalCount ||
        token->text.size() > std::numeric_limits<std::uint64_t>::max() - begin)
      throw ArtifactParserRuntimeError{
          ArtifactParserRuntimeErrorCode::InvalidInput,
          "lexer token has an invalid terminal or byte span"};
    const std::uint64_t end = begin + token->text.size();
    if (begin < previousEnd || end > inputSize)
      throw ArtifactParserRuntimeError{
          ArtifactParserRuntimeErrorCode::InvalidInput,
          "lexer tokens have invalid or unordered byte spans"};
    previousEnd = end;
  }
}

auto makeLookahead(const artifact::ArtifactParserTable &table,
                   const std::vector<const ArtifactLexedToken *> &tokens,
                   std::size_t tokenIndex) -> artifact::ArtifactLookaheadWord {
  artifact::ArtifactLookaheadWord result;
  result.reserve(table.lookahead);
  std::size_t position = tokenIndex;
  while (position < tokens.size() && result.size() < table.lookahead) {
    result.push_back({tokens[position]->terminal});
    ++position;
  }
  if (position == tokens.size() && result.size() < table.lookahead)
    result.push_back({std::nullopt});
  return result;
}

auto findAction(const artifact::ArtifactParserTable &table, std::uint32_t state,
                const artifact::ArtifactLookaheadWord &lookahead)
    -> std::optional<artifact::ArtifactParserAction> {
  const auto &row = table.actionRows.at(table.actionStateRows.at(state));
  const auto found = std::ranges::find(
      row.entries, lookahead, &artifact::ArtifactActionEntry::lookahead);
  if (found != row.entries.end())
    return found->action;
  if (row.fallback)
    return artifact::ArtifactParserAction{*row.fallback};
  return std::nullopt;
}

auto findGoto(const artifact::ArtifactParserTable &table, std::uint32_t state,
              std::uint32_t nonterminal) -> std::optional<std::uint32_t> {
  const auto &row = table.gotoRows.at(table.gotoStateRows.at(state));
  const auto found = std::ranges::find(
      row.entries, nonterminal, &artifact::ArtifactGotoEntry::nonterminal);
  return found == row.entries.end() ? std::nullopt
                                    : std::optional{found->state};
}

auto syntaxError(const artifact::ArtifactParserTable &table,
                 std::uint32_t state, std::size_t tokenIndex, InputSpan span,
                 artifact::ArtifactLookaheadWord lookahead)
    -> ArtifactAstParseError {
  const auto &row = table.actionRows.at(table.actionStateRows.at(state));
  std::vector<artifact::ArtifactLookaheadWord> expected;
  expected.reserve(row.entries.size());
  for (const auto &entry : row.entries)
    expected.push_back(entry.lookahead);
  std::ostringstream message;
  message << "syntax error at byte " << span.beginByte << " in LR state "
          << state;
  return {state,        tokenIndex,           span.beginByte,
          span,         std::move(lookahead), std::move(expected),
          message.str()};
}

[[noreturn]] void executionError(std::string message) {
  throw ArtifactParserRuntimeError{
      ArtifactParserRuntimeErrorCode::InvalidTableExecution,
      std::move(message)};
}

} // namespace

auto ArtifactAstParseResult::accepted() const noexcept -> bool {
  return root.has_value() && !error.has_value();
}

ArtifactParserRuntimeError::ArtifactParserRuntimeError(
    ArtifactParserRuntimeErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

auto ArtifactParserRuntimeError::code() const noexcept
    -> ArtifactParserRuntimeErrorCode {
  return code_;
}

ArtifactAstParser::ArtifactAstParser(artifact::ArtifactParserTable table,
                                     artifact::ArtifactSymbols symbols,
                                     artifact::ArtifactProductions productions,
                                     artifact::ArtifactReductions reductions,
                                     ArtifactParserRuntimeLimits limits)
    : table_(std::move(table)), symbols_(std::move(symbols)),
      productions_(std::move(productions)), reductions_(std::move(reductions)),
      limits_(limits) {
  artifact::validateGrammarSections(symbols_, productions_);
  artifact::validateParserTableSection(table_, symbols_, productions_);
  artifact::validateReductionSection(productions_, reductions_);
  if (limits_.maximumSteps == 0 || limits_.maximumStackDepth == 0)
    throw std::invalid_argument("parser runtime limits must be nonzero");
}

auto ArtifactAstParser::parse(const ArtifactLexResult &lexed,
                              std::uint64_t inputSize) const
    -> ArtifactAstParseResult {
  const auto tokens = parserTokens(lexed);
  validateTokenSpans(tokens, inputSize, symbols_.terminals.size());

  std::vector<std::uint32_t> states{table_.startState};
  std::vector<ReductionStackValue> values;
  std::size_t tokenIndex = 0;
  std::uint64_t steps = 0;
  while (true) {
    if (++steps > limits_.maximumSteps)
      throw ArtifactParserRuntimeError{
          ArtifactParserRuntimeErrorCode::ResourceLimit,
          "parser execution exceeds configured step limit"};
    const std::uint32_t state = states.back();
    auto lookahead = makeLookahead(table_, tokens, tokenIndex);
    const auto action = findAction(table_, state, lookahead);
    const std::uint64_t lookaheadByte =
        tokenIndex < tokens.size() ? tokens[tokenIndex]->offset : inputSize;
    const InputSpan lookaheadSpan =
        tokenIndex < tokens.size()
            ? InputSpan{tokens[tokenIndex]->offset,
                        tokens[tokenIndex]->offset +
                            tokens[tokenIndex]->text.size()}
            : InputSpan{inputSize, inputSize};
    if (!action)
      return {std::nullopt, syntaxError(table_, state, tokenIndex,
                                        lookaheadSpan, std::move(lookahead))};

    if (const auto *shift = std::get_if<artifact::ArtifactShift>(&*action)) {
      if (tokenIndex >= tokens.size())
        executionError("parser table attempts to shift EOF");
      const auto &token = *tokens[tokenIndex];
      const std::uint64_t end = token.offset + token.text.size();
      values.push_back(
          makeTokenValue(token.terminal, token.text, {token.offset, end}));
      states.push_back(shift->state);
      ++tokenIndex;
    } else if (const auto *reduce =
                   std::get_if<artifact::ArtifactReduce>(&*action)) {
      const auto &production = productions_.productions.at(reduce->production);
      const std::size_t rhsLength = production.rhs.size();
      if (states.size() <= rhsLength || values.size() < rhsLength)
        executionError("parser stack underflow during reduction");
      const std::size_t firstValue = values.size() - rhsLength;
      std::vector<ReductionStackValue> rhs;
      rhs.reserve(rhsLength);
      for (std::size_t index = firstValue; index < values.size(); ++index)
        rhs.push_back(std::move(values[index]));
      values.resize(firstValue);
      states.resize(states.size() - rhsLength);
      const auto target = findGoto(table_, states.back(), production.lhs);
      if (!target)
        executionError("missing GOTO after AST reduction");
      values.push_back(executeReduction(
          reductions_.program.instruction(zbik::RuleId{reduce->production}),
          std::move(rhs), lookaheadByte));
      states.push_back(*target);
    } else {
      if (tokenIndex != tokens.size() || values.size() != 1)
        executionError("parser table accepted an incomplete AST");
      return {std::move(values.back().payload), std::nullopt};
    }

    if (states.size() > limits_.maximumStackDepth ||
        values.size() > limits_.maximumStackDepth)
      throw ArtifactParserRuntimeError{
          ArtifactParserRuntimeErrorCode::ResourceLimit,
          "parser execution exceeds configured stack limit"};
    if (states.size() != values.size() + 1)
      executionError("parser state and semantic stacks are inconsistent");
  }
}

auto ArtifactAstParser::parse(std::string_view input, const ArtifactLexerRuntime &lexer) const
    -> ArtifactAstParseResult {
  const auto lexed = lexer.tokenize(input, table_, productions_, limits_.maximumSteps, limits_.maximumStackDepth);
  auto result = parse(lexed, input.size());
  if (result.root && lexer.artifact().context) {
    const auto &originals = lexer.artifact().context->originalTerminals;
    const auto restore = [&](const auto &self, AstValue &value) -> void {
      if (value.kind == AstValueKind::Token) value.tokenKind = originals.at(value.tokenKind);
      for (auto &child : value.elements) self(self, child);
    };
    restore(restore, *result.root);
  }
  return result;
}

} // namespace agas::runtime
