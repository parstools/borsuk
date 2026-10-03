#include "agas/runtime/AstParser.h"

#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <variant>

#include "first/LookaheadSymbols.h"
#include "lr/Action.h"

namespace agas::runtime {
namespace {

auto makeLookahead(const zbik::ParseTable &table,
                   const std::vector<const generator::AgasLexedToken *> &tokens,
                   std::size_t tokenIndex) -> zbik::LookaheadWord {
  std::vector<zbik::LookaheadSymbol> symbols;
  symbols.reserve(table.maxLength());
  std::size_t position = tokenIndex;
  while (position < tokens.size() && symbols.size() < table.maxLength()) {
    symbols.emplace_back(tokens[position]->terminal);
    ++position;
  }
  if (position == tokens.size() && symbols.size() < table.maxLength()) {
    symbols.emplace_back(zbik::endOfInput);
  }
  return zbik::LookaheadWord{std::move(symbols)};
}

auto syntaxError(const zbik::ParseTable &table, zbik::StateId state,
                 std::size_t tokenIndex, InputSpan span,
                 zbik::LookaheadWord lookahead) -> AstParseError {
  std::vector<zbik::LookaheadWord> expected;
  const zbik::ActionRow &row = table.actionRows()[zbik::toIndex(state)];
  expected.reserve(row.size());
  for (const auto &[word, cell] : row) {
    if (!cell.empty())
      expected.push_back(word);
  }
  std::ostringstream message;
  message << "syntax error at byte " << span.beginByte << " in LR state "
          << state.value << " on " << lookahead.dump();
  return {state,        tokenIndex,           span.beginByte,
          span,         std::move(lookahead), std::move(expected),
          message.str()};
}

auto parserTokens(const generator::AgasLexResult &lexed)
    -> std::vector<const generator::AgasLexedToken *> {
  std::vector<const generator::AgasLexedToken *> result;
  result.reserve(lexed.parserTerminalIds.size());
  for (const generator::AgasLexedToken &token : lexed.tokens) {
    if (!token.channel.has_value())
      result.push_back(&token);
  }
  if (result.size() != lexed.parserTerminalIds.size()) {
    throw std::invalid_argument(
        "lexer parser-terminal projection has an invalid size");
  }
  for (std::size_t index = 0; index < result.size(); ++index) {
    if (result[index]->terminal != lexed.parserTerminalIds[index]) {
      throw std::invalid_argument(
          "lexer parser-terminal projection does not match emitted tokens");
    }
  }
  return result;
}

void validateTokenSpans(
    const std::vector<const generator::AgasLexedToken *> &tokens,
    std::uint64_t inputSize) {
  std::uint64_t previousEnd = 0;
  for (const generator::AgasLexedToken *token : tokens) {
    const std::uint64_t begin = token->offset;
    const std::uint64_t end = begin + token->text.size();
    if (end < begin || begin < previousEnd || end > inputSize) {
      throw std::invalid_argument(
          "lexer tokens have invalid or unordered byte spans");
    }
    previousEnd = end;
  }
}

} // namespace

auto AstParseResult::accepted() const noexcept -> bool {
  return root.has_value() && !error.has_value();
}

GeneratedAstParser::GeneratedAstParser(
    const generator::GeneratedParserTable &parser)
    : parser_(parser) {
  if (parser_.table().hasConflicts()) {
    throw std::invalid_argument(
        "GeneratedAstParser requires a conflict-free parsing table");
  }
  if (parser_.reductions().instructions().size() !=
      parser_.table().grammar().ruleCount()) {
    throw std::invalid_argument(
        "AST reduction program does not match the parsing table");
  }
  for (const zbik::Rule &rule : parser_.table().grammar().rules()) {
    const generator::ReductionInstruction &instruction =
        parser_.reductions().instruction(rule.id());
    if (instruction.rhsLength != rule.size()) {
      throw std::invalid_argument(
          "AST reduction RHS length does not match the parsing table");
    }
  }
}

auto GeneratedAstParser::parse(const generator::AgasLexResult &lexed,
                               std::uint64_t inputSize) const
    -> AstParseResult {
  const zbik::ParseTable &table = parser_.table();
  const auto tokens = parserTokens(lexed);
  validateTokenSpans(tokens, inputSize);
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    if (zbik::toIndex(tokens[index]->terminal) >=
        table.grammar().terminalCount()) {
      const zbik::LookaheadWord lookahead{{tokens[index]->terminal}};
      return {std::nullopt,
              syntaxError(table, table.start(), index,
                          {tokens[index]->offset,
                           tokens[index]->offset + tokens[index]->text.size()},
                          lookahead)};
    }
  }

  std::vector<zbik::StateId> states{table.start()};
  std::vector<ReductionStackValue> values;
  std::size_t tokenIndex = 0;
  while (true) {
    const zbik::StateId state = states.back();
    zbik::LookaheadWord lookahead = makeLookahead(table, tokens, tokenIndex);
    const zbik::ActionCell &cell = table.actions(state, lookahead);
    const std::uint64_t lookaheadByte =
        tokenIndex < tokens.size() ? tokens[tokenIndex]->offset : inputSize;
    const InputSpan lookaheadSpan =
        tokenIndex < tokens.size()
            ? InputSpan{tokens[tokenIndex]->offset,
                        tokens[tokenIndex]->offset +
                            tokens[tokenIndex]->text.size()}
            : InputSpan{inputSize, inputSize};
    if (cell.empty()) {
      return {std::nullopt, syntaxError(table, state, tokenIndex, lookaheadSpan,
                                        std::move(lookahead))};
    }
    if (cell.hasConflict()) {
      throw std::logic_error(
          "conflict appeared in a validated Agas parsing table");
    }

    const zbik::Action &action = cell.actions().front();
    if (const auto shift = std::get_if<zbik::Shift>(&action)) {
      if (tokenIndex >= tokens.size()) {
        throw std::logic_error("Agas LR table attempts to shift EOF");
      }
      const generator::AgasLexedToken &token = *tokens[tokenIndex];
      const std::uint64_t end = token.offset + token.text.size();
      values.push_back(makeTokenValue(token.terminal.value, token.text,
                                      {token.offset, end}));
      states.push_back(shift->target);
      ++tokenIndex;
      continue;
    }

    if (const auto reduce = std::get_if<zbik::Reduce>(&action)) {
      const zbik::Rule &rule = table.grammar().rule(reduce->rule);
      if (states.size() <= rule.size() || values.size() < rule.size()) {
        throw std::logic_error("Agas LR stack underflow during reduction");
      }
      const std::size_t firstValue = values.size() - rule.size();
      std::vector<ReductionStackValue> rhs;
      rhs.reserve(rule.size());
      for (std::size_t index = firstValue; index < values.size(); ++index) {
        rhs.push_back(std::move(values[index]));
      }
      values.resize(firstValue);
      states.resize(states.size() - rule.size());
      const auto target = table.goTo(states.back(), rule.lhs());
      if (!target.has_value()) {
        throw std::logic_error("missing GOTO after Agas AST reduction");
      }
      values.push_back(
          executeReduction(parser_.reductions().instruction(reduce->rule),
                           std::move(rhs), lookaheadByte));
      states.push_back(*target);
      continue;
    }

    if (tokenIndex != tokens.size() || values.size() != 1) {
      throw std::logic_error("Agas LR table accepted an incomplete AST");
    }
    return {std::move(values.back().payload), std::nullopt};
  }
}

} // namespace agas::runtime
