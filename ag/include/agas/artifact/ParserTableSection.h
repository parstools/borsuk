#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "agas/artifact/ArtifactManifest.h"
#include "agas/artifact/GrammarSections.h"

namespace agas::artifact {

enum class ArtifactParserAlgorithm { Lr, Lalr, Slr };

struct ArtifactLookaheadSymbol {
  std::optional<std::uint32_t> terminal;
  auto operator==(const ArtifactLookaheadSymbol &) const -> bool = default;
};

using ArtifactLookaheadWord = std::vector<ArtifactLookaheadSymbol>;

struct ArtifactShift {
  std::uint32_t state{};
  auto operator==(const ArtifactShift &) const -> bool = default;
};

struct ArtifactReduce {
  std::uint32_t production{};
  auto operator==(const ArtifactReduce &) const -> bool = default;
};

struct ArtifactAccept {
  auto operator==(const ArtifactAccept &) const -> bool = default;
};

using ArtifactParserAction =
    std::variant<ArtifactShift, ArtifactReduce, ArtifactAccept>;

struct ArtifactActionEntry {
  ArtifactLookaheadWord lookahead;
  ArtifactParserAction action;
  auto operator==(const ArtifactActionEntry &) const -> bool = default;
};

struct ArtifactActionRow {
  std::uint32_t id{};
  std::vector<ArtifactActionEntry> entries;
  std::optional<ArtifactReduce> fallback;
  auto operator==(const ArtifactActionRow &) const -> bool = default;
};

struct ArtifactGotoEntry {
  std::uint32_t nonterminal{};
  std::uint32_t state{};
  auto operator==(const ArtifactGotoEntry &) const -> bool = default;
};

struct ArtifactGotoRow {
  std::uint32_t id{};
  std::vector<ArtifactGotoEntry> entries;
  auto operator==(const ArtifactGotoRow &) const -> bool = default;
};

struct ArtifactParserTable {
  ArtifactParserAlgorithm algorithm{};
  std::uint32_t lookahead{};
  std::uint32_t startState{};
  std::vector<ArtifactActionRow> actionRows;
  std::vector<std::uint32_t> actionStateRows;
  std::vector<ArtifactGotoRow> gotoRows;
  std::vector<std::uint32_t> gotoStateRows;
  auto operator==(const ArtifactParserTable &) const -> bool = default;
};

struct ParserTableSectionLimits {
  std::uint32_t maximumStates{10'000'000};
  std::uint32_t maximumRows{10'000'000};
  std::uint64_t maximumEntries{100'000'000};
  std::uint32_t maximumNameBytes{1U << 20U};
};

enum class ParserTableSectionErrorCode {
  InvalidDsl,
  InvalidAlgorithm,
  InvalidId,
  DuplicateEntry,
  InvalidReference,
  ResourceLimit,
};

class ParserTableSectionError final : public std::runtime_error {
public:
  ParserTableSectionError(ParserTableSectionErrorCode code, std::size_t offset,
                          std::string message);

  [[nodiscard]] auto code() const noexcept -> ParserTableSectionErrorCode;
  [[nodiscard]] auto offset() const noexcept -> std::size_t;

private:
  ParserTableSectionErrorCode code_;
  std::size_t offset_{};
};

void validateParserTableSection(
    const ArtifactParserTable &table, const ArtifactSymbols &symbols,
    const ArtifactProductions &productions,
    const ArtifactLoadLimits &loadLimits = {},
    const ParserTableSectionLimits &sectionLimits = {});

[[nodiscard]] auto dumpParserTableDsl(const ArtifactParserTable &table,
                                      const ArtifactSymbols &symbols,
                                      const ArtifactProductions &productions)
    -> std::string;

[[nodiscard]] auto
parseParserTableDsl(std::string_view text, const ArtifactSymbols &symbols,
                    const ArtifactProductions &productions,
                    const ArtifactLoadLimits &loadLimits = {},
                    const ParserTableSectionLimits &sectionLimits = {})
    -> ArtifactParserTable;

} // namespace agas::artifact
