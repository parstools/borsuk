#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace agas::artifact {

enum class ArtifactSectionKind {
  Symbols,
  Lexer,
  ParserTable,
  Productions,
  Reductions,
  AstSchema,
  Diagnostics,
};

struct ArtifactSectionDescriptor {
  ArtifactSectionKind kind{};
  std::uint32_t version{};
  std::string file;
  bool required{};
  std::uint64_t byteLength{};
  std::string sha256;

  auto operator==(const ArtifactSectionDescriptor &) const -> bool = default;
};

struct ArtifactManifest {
  std::uint32_t formatVersion{};
  std::string parserAlgorithm;
  std::uint32_t lookahead{};
  std::string startSymbol;
  std::string rootType;
  std::string generatorVersion;
  std::string zbikRevision;
  std::string unicodeVersion;
  std::string exactSourceSha256;
  std::string expandedSourceSha256;
  std::string settingsSha256;
  std::vector<ArtifactSectionDescriptor> sections;

  auto operator==(const ArtifactManifest &) const -> bool = default;
};

struct ArtifactLoadLimits {
  std::uint32_t maximumLookahead{64};
  std::uint64_t maximumManifestBytes{1ULL << 20U};
  std::uint64_t maximumSectionBytes{1ULL << 30U};
  std::uint64_t maximumPackageBytes{2ULL << 30U};
};

enum class ArtifactManifestErrorCode {
  UnsupportedVersion,
  InvalidAlgorithm,
  InvalidLookahead,
  MissingIdentity,
  InvalidHash,
  InvalidSection,
  DuplicateSection,
  MissingSection,
  ResourceLimit,
  InvalidJson,
  UnknownField,
};

class ArtifactManifestError final : public std::runtime_error {
public:
  ArtifactManifestError(ArtifactManifestErrorCode code, std::string message);

  [[nodiscard]] auto code() const noexcept -> ArtifactManifestErrorCode;

private:
  ArtifactManifestErrorCode code_;
};

[[nodiscard]] auto sectionKindName(ArtifactSectionKind kind)
    -> std::string_view;
[[nodiscard]] auto sectionFileName(ArtifactSectionKind kind)
    -> std::string_view;

void validateArtifactManifest(
    const ArtifactManifest &manifest,
    const ArtifactLoadLimits &limits = ArtifactLoadLimits{});

} // namespace agas::artifact
