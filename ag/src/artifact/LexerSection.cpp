#include "agas/artifact/LexerSection.h"

#include <algorithm>
#include <stdexcept>
#include <string>

#include "regex/RegexAst.h"

namespace agas::artifact {
namespace {

void validateRanges(const std::vector<ArtifactCodePointRange> &ranges) {
  std::uint32_t previousLast = 0;
  bool firstRange = true;
  for (const ArtifactCodePointRange range : ranges) {
    if (range.first > range.last || range.last > zbik::maxUnicodeCodePoint ||
        (range.first <= zbik::lastLowSurrogate &&
         range.last >= zbik::firstHighSurrogate) ||
        (!firstRange && range.first <= previousLast))
      throw std::invalid_argument(
          "lexer transition ranges must be ordered Unicode scalar intervals");
    previousLast = range.last;
    firstRange = false;
  }
  if (ranges.empty())
    throw std::invalid_argument("lexer transition must contain a range");
}

void validateTransitions(
    const std::vector<ArtifactLexerTransition> &transitions,
    std::size_t stateCount, bool requireDisjoint) {
  std::vector<ArtifactCodePointRange> allRanges;
  for (const ArtifactLexerTransition &transition : transitions) {
    if (transition.target >= stateCount)
      throw std::invalid_argument("lexer transition target is outside states");
    validateRanges(transition.ranges);
    allRanges.insert(allRanges.end(), transition.ranges.begin(),
                     transition.ranges.end());
  }
  if (!requireDisjoint)
    return;
  std::ranges::sort(allRanges, {}, &ArtifactCodePointRange::first);
  for (std::size_t index = 1; index < allRanges.size(); ++index)
    if (allRanges[index].first <= allRanges[index - 1].last)
      throw std::invalid_argument("DFA transitions overlap");
}

} // namespace

void validateLexerSection(const ArtifactLexer &lexer, std::size_t terminalCount,
                          std::size_t channelCount,
                          const LexerSectionLimits &limits) {
  if ((lexer.version != 1 && lexer.version != 2) ||
      (lexer.version == 2) != lexer.context.has_value())
    throw std::invalid_argument("invalid lexer section version/context");
  if (lexer.context) {
    const auto &context = *lexer.context;
    if (context.originalTerminals.size() != terminalCount ||
        context.requiredClasses.size() != lexer.rules.size() || context.rows.empty() ||
        context.rows.size() > limits.maximumStates || lexer.orderedNfas.size() != lexer.rules.size())
      throw std::invalid_argument("invalid lexer context dimensions");
    for (const auto original : context.originalTerminals)
      if (original >= terminalCount || context.originalTerminals[original] != original)
        throw std::invalid_argument("invalid original terminal mapping");
    for (std::size_t i = 0; i < lexer.rules.size(); ++i) {
      const auto &rule = lexer.rules[i];
      if ((rule.skipped || rule.channel) && context.requiredClasses[i])
        throw std::invalid_argument("context-gated hidden rules are unsupported");
      if (rule.terminal && (*rule.terminal >= terminalCount ||
          context.originalTerminals[*rule.terminal] != *rule.terminal))
        throw std::invalid_argument("lexer must emit original terminals");
    }
    std::uint64_t nodes = 0;
    for (const auto &row : context.rows) {
      nodes += row.size();
      if (row.empty() || nodes > limits.maximumStates)
        throw std::invalid_argument("invalid lexer context tree size");
      std::vector<bool> reached(row.size());
      reached[0] = true;
      for (std::size_t i = 0; i < row.size(); ++i) {
        if (!reached[i]) throw std::invalid_argument("unreachable lexer context node");
        std::vector<std::optional<std::uint32_t>> terminals;
        for (const auto &edge : row[i].edges) {
          if (edge.target <= i || edge.target >= row.size() || reached[edge.target] ||
              (edge.terminal && *edge.terminal >= terminalCount) ||
              std::ranges::find(terminals, edge.terminal) != terminals.end())
            throw std::invalid_argument("invalid lexer context edge");
          reached[edge.target] = true;
          terminals.push_back(edge.terminal);
        }
      }
    }
  }
  if (lexer.rules.empty() || lexer.dfaStates.empty())
    throw std::invalid_argument("lexer rules and DFA states must not be empty");
  if (lexer.rules.size() > limits.maximumRules ||
      lexer.dfaStates.size() > limits.maximumStates)
    throw std::length_error("lexer artifact exceeds configured limits");
  std::uint64_t transitionCount = 0;
  std::uint64_t rangeCount = 0;
  for (const ArtifactLexerRule &rule : lexer.rules) {
    if (rule.name.empty() || rule.skipped != !rule.terminal.has_value() ||
        (rule.terminal && *rule.terminal >= terminalCount) ||
        (rule.channel && *rule.channel >= channelCount))
      throw std::invalid_argument("lexer rule metadata is inconsistent");
  }
  for (const ArtifactDfaState &state : lexer.dfaStates) {
    if (state.acceptingRule && *state.acceptingRule >= lexer.rules.size())
      throw std::invalid_argument("DFA accepts an unknown lexer rule");
    validateTransitions(state.transitions, lexer.dfaStates.size(), true);
    transitionCount += state.transitions.size();
    for (const auto &transition : state.transitions)
      rangeCount += transition.ranges.size();
  }
  if (!lexer.orderedNfas.empty() &&
      lexer.orderedNfas.size() != lexer.rules.size())
    throw std::invalid_argument(
        "ordered NFA count must equal the lexer rule count");
  bool hasPriority = false;
  for (const ArtifactOrderedNfa &nfa : lexer.orderedNfas) {
    if (nfa.states.size() > limits.maximumStates)
      throw std::length_error("ordered NFA exceeds configured limits");
    if (nfa.states.empty() || nfa.startState >= nfa.states.size() ||
        nfa.acceptingState >= nfa.states.size())
      throw std::invalid_argument("ordered NFA endpoints are invalid");
    for (const ArtifactNfaState &state : nfa.states) {
      hasPriority = hasPriority || state.activatesPriority;
      if (state.activatesPriority && !state.orderedDecision)
        throw std::invalid_argument(
            "an NFA priority activation must be an ordered decision");
      for (std::uint32_t target : state.epsilonTransitions)
        if (target >= nfa.states.size())
          throw std::invalid_argument("NFA epsilon target is invalid");
      validateTransitions(state.transitions, nfa.states.size(), false);
      transitionCount += state.transitions.size();
      for (const auto &transition : state.transitions)
        rangeCount += transition.ranges.size();
    }
  }
  if (!lexer.orderedNfas.empty() && !hasPriority && !lexer.context)
    throw std::invalid_argument(
        "ordered NFAs require at least one priority-activating decision");
  if (transitionCount > limits.maximumTransitions ||
      rangeCount > limits.maximumRanges)
    throw std::length_error("lexer transitions exceed configured limits");
}

} // namespace agas::artifact
