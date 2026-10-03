#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "agas/artifact/ArtifactManifest.h"

namespace agas::artifact {

struct ArtifactNamedSymbol {
  std::uint32_t id{};
  std::string name;

  auto operator==(const ArtifactNamedSymbol &) const -> bool = default;
};

struct ArtifactSymbols {
  std::uint32_t version{};
  std::vector<ArtifactNamedSymbol> terminals;
  std::vector<ArtifactNamedSymbol> nonterminals;
  std::vector<ArtifactNamedSymbol> channels;

  auto operator==(const ArtifactSymbols &) const -> bool = default;
};

enum class ArtifactSymbolKind { Terminal, Nonterminal };

struct ArtifactSymbolReference {
  ArtifactSymbolKind kind{};
  std::uint32_t id{};

  auto operator==(const ArtifactSymbolReference &) const -> bool = default;
};

struct ArtifactProduction {
  std::uint32_t id{};
  std::uint32_t lhs{};
  std::vector<ArtifactSymbolReference> rhs;

  auto operator==(const ArtifactProduction &) const -> bool = default;
};

struct ArtifactProductions {
  std::uint32_t version{};
  std::vector<ArtifactProduction> productions;

  auto operator==(const ArtifactProductions &) const -> bool = default;
};

struct GrammarSectionLimits {
  std::uint32_t maximumSymbols{1'000'000};
  std::uint32_t maximumProductions{1'000'000};
  std::uint64_t maximumRhsSymbols{10'000'000};
  std::uint32_t maximumNameBytes{1U << 20U};
};

enum class GrammarSectionErrorCode {
  UnsupportedVersion,
  InvalidJson,
  UnknownField,
  InvalidId,
  DuplicateName,
  InvalidReference,
  ResourceLimit,
};

class GrammarSectionError final : public std::runtime_error {
public:
  GrammarSectionError(GrammarSectionErrorCode code, std::string message);

  [[nodiscard]] auto code() const noexcept -> GrammarSectionErrorCode;

private:
  GrammarSectionErrorCode code_;
};

void validateGrammarSections(
    const ArtifactSymbols &symbols, const ArtifactProductions &productions,
    const GrammarSectionLimits &limits = GrammarSectionLimits{});

[[nodiscard]] auto dumpSymbolsJson(const ArtifactSymbols &symbols)
    -> std::string;
[[nodiscard]] auto parseSymbolsJson(
    std::string_view text,
    const ArtifactLoadLimits &loadLimits = ArtifactLoadLimits{},
    const GrammarSectionLimits &sectionLimits = GrammarSectionLimits{})
    -> ArtifactSymbols;

[[nodiscard]] auto dumpProductionsJson(const ArtifactSymbols &symbols,
                                       const ArtifactProductions &productions)
    -> std::string;
[[nodiscard]] auto parseProductionsJson(
    std::string_view text, const ArtifactSymbols &symbols,
    const ArtifactLoadLimits &loadLimits = ArtifactLoadLimits{},
    const GrammarSectionLimits &sectionLimits = GrammarSectionLimits{})
    -> ArtifactProductions;

} // namespace agas::artifact
