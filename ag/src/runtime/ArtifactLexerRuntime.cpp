#include "agas/runtime/ArtifactLexerRuntime.h"

#include <algorithm>
#include <optional>
#include <span>
#include <unordered_set>
#include <utility>

namespace agas::runtime {
namespace {

constexpr auto isUnicodeScalar(std::uint32_t value) noexcept -> bool {
  return value <= 0x10FFFFU && !(value >= 0xD800U && value <= 0xDFFFU);
}

struct DecodedCodePoint {
  std::uint32_t value{};
  std::size_t nextOffset{};
};

auto decode(std::string_view input, std::size_t offset) -> DecodedCodePoint {
  const std::size_t start = offset;
  const auto first = static_cast<unsigned char>(input[offset++]);
  if (first < 0x80)
    return {first, offset};
  unsigned length = 0;
  std::uint32_t value = 0;
  std::uint32_t minimum = 0;
  if ((first & 0xE0U) == 0xC0U) {
    length = 2;
    value = first & 0x1FU;
    minimum = 0x80;
  } else if ((first & 0xF0U) == 0xE0U) {
    length = 3;
    value = first & 0x0FU;
    minimum = 0x800;
  } else if ((first & 0xF8U) == 0xF0U) {
    length = 4;
    value = first & 0x07U;
    minimum = 0x10000;
  } else {
    throw ArtifactLexerError{ArtifactLexerErrorKind::InvalidEncoding, start,
                             start};
  }
  if (offset + length - 1 > input.size())
    throw ArtifactLexerError{ArtifactLexerErrorKind::InvalidEncoding, start,
                             input.size()};
  for (unsigned index = 1; index < length; ++index) {
    const auto continuation = static_cast<unsigned char>(input[offset++]);
    if ((continuation & 0xC0U) != 0x80U)
      throw ArtifactLexerError{ArtifactLexerErrorKind::InvalidEncoding, start,
                               offset - 1};
    value = (value << 6U) | (continuation & 0x3FU);
  }
  if (value < minimum || !isUnicodeScalar(value))
    throw ArtifactLexerError{ArtifactLexerErrorKind::InvalidEncoding, start,
                             start};
  return {value, offset};
}

auto contains(const std::vector<artifact::ArtifactCodePointRange> &ranges,
              std::uint32_t value) -> bool {
  return std::ranges::any_of(ranges, [value](const auto range) {
    return range.first <= value && value <= range.last;
  });
}

struct Config {
  std::uint32_t state{};
  std::size_t offset{};
  auto operator==(const Config &) const -> bool = default;
};
struct ConfigHash {
  auto operator()(const Config &value) const noexcept -> std::size_t {
    const std::size_t seed = std::hash<std::uint32_t>{}(value.state);
    return seed ^ (std::hash<std::size_t>{}(value.offset) +
                   0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
  }
};

auto preferredAfterPriority(const artifact::ArtifactOrderedNfa &nfa,
                            std::span<const std::uint32_t> input, Config start)
    -> std::optional<std::size_t> {
  std::unordered_set<Config, ConfigHash> visited;
  std::vector<Config> pending{start};
  while (!pending.empty()) {
    const Config current = pending.back();
    pending.pop_back();
    if (!visited.insert(current).second)
      continue;
    if (current.state == nfa.acceptingState)
      return current.offset;
    const auto &state = nfa.states[current.state];
    for (auto transition = state.transitions.rbegin();
         transition != state.transitions.rend(); ++transition)
      if (current.offset < input.size() &&
          contains(transition->ranges, input[current.offset]))
        pending.push_back({transition->target, current.offset + 1});
    for (auto target = state.epsilonTransitions.rbegin();
         target != state.epsilonTransitions.rend(); ++target)
      pending.push_back({*target, current.offset});
  }
  return std::nullopt;
}

auto preferredPrefix(const artifact::ArtifactOrderedNfa &nfa,
                     std::span<const std::uint32_t> input)
    -> std::optional<std::size_t> {
  std::unordered_set<Config, ConfigHash> visited;
  std::vector<Config> pending{{nfa.startState, 0}};
  std::optional<std::size_t> result;
  while (!pending.empty()) {
    const Config current = pending.back();
    pending.pop_back();
    if (!visited.insert(current).second)
      continue;
    const auto &state = nfa.states[current.state];
    if (state.activatesPriority) {
      const auto preferred = preferredAfterPriority(nfa, input, current);
      if (preferred && (!result || *preferred > *result))
        result = preferred;
      continue;
    }
    if (current.state == nfa.acceptingState &&
        (!result || current.offset > *result))
      result = current.offset;
    for (std::uint32_t target : state.epsilonTransitions)
      pending.push_back({target, current.offset});
    if (current.offset < input.size())
      for (const auto &transition : state.transitions)
        if (contains(transition.ranges, input[current.offset]))
          pending.push_back({transition.target, current.offset + 1});
  }
  return result;
}

void emit(ArtifactLexResult &result, const artifact::ArtifactLexerRule &rule,
          std::size_t begin, std::size_t end, std::string_view input) {
  if (rule.skipped)
    return;
  result.tokens.push_back({*rule.terminal, rule.channel, begin,
                           std::string{input.substr(begin, end - begin)}});
  if (!rule.channel)
    result.parserTerminalIds.push_back(*rule.terminal);
}

} // namespace

ArtifactLexerError::ArtifactLexerError(ArtifactLexerErrorKind kind,
                                       std::size_t tokenStart,
                                       std::size_t errorOffset)
    : std::runtime_error(kind == ArtifactLexerErrorKind::InvalidEncoding
                             ? "invalid UTF-8 at byte " +
                                   std::to_string(errorOffset)
                             : "cannot form a token starting at byte " +
                                   std::to_string(tokenStart)),
      kind_(kind), tokenStart_(tokenStart), errorOffset_(errorOffset) {}
auto ArtifactLexerError::kind() const noexcept -> ArtifactLexerErrorKind {
  return kind_;
}
auto ArtifactLexerError::tokenStart() const noexcept -> std::size_t {
  return tokenStart_;
}
auto ArtifactLexerError::errorOffset() const noexcept -> std::size_t {
  return errorOffset_;
}

ArtifactLexerRuntime::ArtifactLexerRuntime(artifact::ArtifactLexer lexer,
                                           std::size_t terminalCount,
                                           std::size_t channelCount)
    : lexer_(std::move(lexer)) {
  artifact::validateLexerSection(lexer_, terminalCount, channelCount);
}

auto ArtifactLexerRuntime::tokenize(std::string_view input) const
    -> ArtifactLexResult {
  if (lexer_.context) throw std::invalid_argument("contextual lexer requires a parser table");
  ArtifactLexResult result;
  if (!lexer_.orderedNfas.empty()) {
    std::vector<std::uint32_t> codePoints;
    std::vector<std::size_t> byteOffsets{0};
    for (std::size_t offset = 0; offset < input.size();) {
      const auto decoded = decode(input, offset);
      codePoints.push_back(decoded.value);
      offset = decoded.nextOffset;
      byteOffsets.push_back(offset);
    }
    for (std::size_t point = 0; point < codePoints.size();) {
      std::optional<std::size_t> matchedRule;
      std::size_t matchedLength = 0;
      const std::span<const std::uint32_t> remaining{
          codePoints.begin() + static_cast<std::ptrdiff_t>(point),
          codePoints.end()};
      for (std::size_t rule = 0; rule < lexer_.orderedNfas.size(); ++rule) {
        const auto length =
            preferredPrefix(lexer_.orderedNfas[rule], remaining);
        if (length && *length > matchedLength) {
          matchedRule = rule;
          matchedLength = *length;
        }
      }
      if (!matchedRule)
        throw ArtifactLexerError{ArtifactLexerErrorKind::NoMatchingRule,
                                 byteOffsets[point], byteOffsets[point]};
      emit(result, lexer_.rules[*matchedRule], byteOffsets[point],
           byteOffsets[point + matchedLength], input);
      point += matchedLength;
    }
    return result;
  }

  for (std::size_t offset = 0; offset < input.size();) {
    std::size_t state = 0;
    std::size_t cursor = offset;
    std::optional<std::uint32_t> matchedRule;
    std::size_t matchedEnd = offset;
    while (cursor < input.size()) {
      const auto decoded = decode(input, cursor);
      const auto &transitions = lexer_.dfaStates[state].transitions;
      const auto found =
          std::ranges::find_if(transitions, [&](const auto &transition) {
            return contains(transition.ranges, decoded.value);
          });
      if (found == transitions.end())
        break;
      state = found->target;
      cursor = decoded.nextOffset;
      if (lexer_.dfaStates[state].acceptingRule) {
        matchedRule = lexer_.dfaStates[state].acceptingRule;
        matchedEnd = cursor;
      }
    }
    if (!matchedRule)
      throw ArtifactLexerError{ArtifactLexerErrorKind::NoMatchingRule, offset,
                               cursor};
    emit(result, lexer_.rules[*matchedRule], offset, matchedEnd, input);
    offset = matchedEnd;
  }
  return result;
}

auto ArtifactLexerRuntime::tokenize(std::string_view input,
    const artifact::ArtifactParserTable &table,
    const artifact::ArtifactProductions &productions,
    std::uint64_t maximumSteps, std::uint64_t maximumStackDepth) const -> ArtifactLexResult {
  if (!lexer_.context) return tokenize(input);
  const auto &context = *lexer_.context;
  if (context.rows.size() != table.actionStateRows.size())
    throw std::invalid_argument("context state count differs from parser");
  std::vector<std::uint32_t> points;
  std::vector<std::size_t> offsets{0};
  for (std::size_t offset = 0; offset < input.size();) {
    const auto decoded = decode(input, offset);
    points.push_back(decoded.value);
    offset = decoded.nextOffset;
    offsets.push_back(offset);
  }
  struct Preview {
    ArtifactLexedToken source;
    ArtifactLexedToken scoped;
    std::vector<ArtifactLexedToken> hidden;
    std::size_t cursor;
  };
  ArtifactLexResult result;
  const auto append = [&](Preview value) {
    result.tokens.insert(result.tokens.end(), value.hidden.begin(), value.hidden.end());
    result.parserTerminalIds.push_back(value.scoped.terminal);
    result.tokens.push_back(std::move(value.scoped));
  };
  const auto next = [&](std::size_t &cursor, std::uint64_t active,
                         std::vector<ArtifactLexedToken> &hidden) -> std::optional<ArtifactLexedToken> {
    while (cursor < points.size()) {
      std::optional<std::size_t> best;
      std::size_t length = 0;
      for (std::size_t id = 0; id < lexer_.rules.size(); ++id) {
        if ((active & context.requiredClasses[id]) != context.requiredClasses[id]) continue;
        const auto size = preferredPrefix(lexer_.orderedNfas[id], std::span<const std::uint32_t>{points}.subspan(cursor));
        if (size && *size > length) { best = id; length = *size; }
      }
      if (!best) throw ArtifactLexerError{ArtifactLexerErrorKind::NoMatchingRule, offsets[cursor], offsets[cursor]};
      const auto begin = cursor;
      cursor += length;
      const auto &rule = lexer_.rules[*best];
      if (rule.skipped) continue;
      ArtifactLexedToken token{*rule.terminal, rule.channel, offsets[begin],
          std::string{input.substr(offsets[begin], offsets[cursor] - offsets[begin])}};
      if (!token.channel) return token;
      hidden.push_back(std::move(token));
    }
    return std::nullopt;
  };
  std::vector<std::uint32_t> states{table.startState};
  std::size_t committed = 0;
  std::vector<ArtifactLexedToken> buffered;
  for (std::uint64_t step = 0; step < maximumSteps; ++step) {
    const auto state = states.back();
    const auto &nodes = context.rows.at(state);
    std::size_t node = 0, cursor = committed;
    artifact::ArtifactLookaheadWord prefix;
    std::vector<Preview> preview;
    std::vector<ArtifactLexedToken> trailing;
    while (prefix.size() < table.lookahead && !nodes[node].edges.empty()) {
      std::vector<ArtifactLexedToken> hidden;
      const auto token = next(cursor, nodes[node].active, hidden);
      if (!token) { trailing = std::move(hidden); prefix.push_back({std::nullopt}); break; }
      std::optional<artifact::ArtifactContextEdge> matched;
      for (const auto &edge : nodes[node].edges)
        if (edge.terminal && context.originalTerminals[*edge.terminal] == token->terminal) {
          if (matched) throw std::logic_error("ambiguous contextual terminal mapping");
          matched = edge;
        }
      if (!matched) {
        for (auto &item : preview) append(std::move(item));
        result.tokens.insert(result.tokens.end(), hidden.begin(), hidden.end());
        result.tokens.push_back(*token);
        result.parserTerminalIds.push_back(token->terminal);
        return result;
      }
      auto scoped = *token;
      scoped.terminal = *matched->terminal;
      prefix.push_back({scoped.terminal});
      preview.push_back({*token, std::move(scoped), std::move(hidden), cursor});
      node = matched->target;
    }
    for (std::size_t i = 0; i < std::min(buffered.size(), preview.size()); ++i)
      if (buffered[i] != preview[i].source) throw std::logic_error("lexer context changes buffered token");
    const auto &row = table.actionRows.at(table.actionStateRows.at(state));
    std::optional<artifact::ArtifactParserAction> action;
    for (const auto &entry : row.entries)
      if (entry.lookahead == prefix) { action = entry.action; break; }
    if (!action && row.fallback) action = *row.fallback;
    if (!action) {
      for (auto &item : preview) append(std::move(item));
      result.tokens.insert(result.tokens.end(), trailing.begin(), trailing.end());
      return result;
    }
    if (const auto shift = std::get_if<artifact::ArtifactShift>(&*action)) {
      if (preview.empty()) throw std::logic_error("contextual shift of EOF");
      committed = preview.front().cursor;
      append(std::move(preview.front()));
      preview.erase(preview.begin());
      states.push_back(shift->state);
    } else if (const auto reduce = std::get_if<artifact::ArtifactReduce>(&*action)) {
      const auto &production = productions.productions.at(reduce->production);
      if (production.rhs.size() >= states.size()) throw std::logic_error("contextual stack underflow");
      states.resize(states.size() - production.rhs.size());
      const auto &gotos = table.gotoRows.at(table.gotoStateRows.at(states.back()));
      const auto target = std::ranges::find(gotos.entries, production.lhs, &artifact::ArtifactGotoEntry::nonterminal);
      if (target == gotos.entries.end()) throw std::logic_error("missing contextual GOTO");
      states.push_back(target->state);
    } else {
      if (prefix.empty() || prefix.front().terminal || cursor != points.size())
        throw std::logic_error("contextual accept before EOF");
      result.tokens.insert(result.tokens.end(), trailing.begin(), trailing.end());
      return result;
    }
    buffered.clear();
    for (const auto &item : preview) buffered.push_back(item.source);
    if (states.size() > maximumStackDepth) throw std::length_error("contextual parser stack limit exceeded");
  }
  throw std::length_error("contextual parser step limit exceeded");
}

auto ArtifactLexerRuntime::artifact() const noexcept
    -> const artifact::ArtifactLexer & {
  return lexer_;
}

} // namespace agas::runtime
