#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agas/artifact/ArtifactManifest.h"

namespace agas::artifact {

struct ArtifactCodePointRange {
  std::uint32_t first{};
  std::uint32_t last{};
  auto operator==(const ArtifactCodePointRange &) const -> bool = default;
};

struct ArtifactLexerTransition {
  std::vector<ArtifactCodePointRange> ranges;
  std::uint32_t target{};
  auto operator==(const ArtifactLexerTransition &) const -> bool = default;
};

struct ArtifactDfaState {
  std::optional<std::uint32_t> acceptingRule;
  std::vector<ArtifactLexerTransition> transitions;
  auto operator==(const ArtifactDfaState &) const -> bool = default;
};

struct ArtifactNfaState {
  std::vector<std::uint32_t> epsilonTransitions;
  std::vector<ArtifactLexerTransition> transitions;
  bool orderedDecision{};
  bool activatesPriority{};
  auto operator==(const ArtifactNfaState &) const -> bool = default;
};

struct ArtifactOrderedNfa {
  std::uint32_t startState{};
  std::uint32_t acceptingState{};
  std::vector<ArtifactNfaState> states;
  auto operator==(const ArtifactOrderedNfa &) const -> bool = default;
};

struct ArtifactLexerRule {
  std::string name;
  std::optional<std::uint32_t> terminal;
  std::optional<std::uint32_t> channel;
  bool skipped{};
  auto operator==(const ArtifactLexerRule &) const -> bool = default;
};

struct ArtifactContextEdge {
  std::optional<std::uint32_t> terminal;
  std::uint32_t target{};
  auto operator==(const ArtifactContextEdge &) const -> bool = default;
};

struct ArtifactContextNode {
  std::uint64_t active{};
  std::vector<ArtifactContextEdge> edges;
  auto operator==(const ArtifactContextNode &) const -> bool = default;
};

struct ArtifactLexerContext {
  std::vector<std::uint32_t> originalTerminals;
  std::vector<std::uint64_t> requiredClasses;
  std::vector<std::vector<ArtifactContextNode>> rows;
  auto operator==(const ArtifactLexerContext &) const -> bool = default;
};

struct ArtifactLexer {
  std::uint32_t version{};
  std::vector<ArtifactLexerRule> rules;
  std::vector<ArtifactDfaState> dfaStates;
  std::vector<ArtifactOrderedNfa> orderedNfas;
  std::optional<ArtifactLexerContext> context{};
  auto operator==(const ArtifactLexer &) const -> bool = default;
};

struct LexerSectionLimits {
  std::uint32_t maximumRules{1'000'000};
  std::uint32_t maximumStates{10'000'000};
  std::uint64_t maximumTransitions{50'000'000};
  std::uint64_t maximumRanges{100'000'000};
};

void validateLexerSection(const ArtifactLexer &lexer, std::size_t terminalCount,
                          std::size_t channelCount,
                          const LexerSectionLimits &limits = {});

[[nodiscard]] auto dumpLexerJson(const ArtifactLexer &lexer,
                                 std::size_t terminalCount,
                                 std::size_t channelCount) -> std::string;
[[nodiscard]] auto parseLexerJson(std::string_view text,
                                  std::size_t terminalCount,
                                  std::size_t channelCount,
                                  const ArtifactLoadLimits &loadLimits = {},
                                  const LexerSectionLimits &sectionLimits = {})
    -> ArtifactLexer;

} // namespace agas::artifact
