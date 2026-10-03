#include "agas/artifact/ArtifactManifest.h"

#include <array>
#include <cctype>
#include <unordered_set>
#include <utility>

namespace agas::artifact {
namespace {

constexpr std::array requiredSections{
    ArtifactSectionKind::Symbols,     ArtifactSectionKind::Lexer,
    ArtifactSectionKind::ParserTable, ArtifactSectionKind::Productions,
    ArtifactSectionKind::Reductions,  ArtifactSectionKind::AstSchema,
};

auto isSha256(std::string_view value) -> bool {
  if (value.size() != 64)
    return false;
  for (const unsigned char character : value) {
    if (!std::isdigit(character) &&
        !(character >= static_cast<unsigned char>('a') &&
          character <= static_cast<unsigned char>('f'))) {
      return false;
    }
  }
  return true;
}

void requireHash(std::string_view value, std::string_view name) {
  if (!isSha256(value)) {
    throw ArtifactManifestError{ArtifactManifestErrorCode::InvalidHash,
                                std::string{name} +
                                    " must be a lowercase SHA-256 digest"};
  }
}

constexpr auto sectionIndex(ArtifactSectionKind kind) -> std::size_t {
  return static_cast<std::size_t>(kind);
}

} // namespace

ArtifactManifestError::ArtifactManifestError(ArtifactManifestErrorCode code,
                                             std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

auto ArtifactManifestError::code() const noexcept -> ArtifactManifestErrorCode {
  return code_;
}

auto sectionKindName(ArtifactSectionKind kind) -> std::string_view {
  switch (kind) {
  case ArtifactSectionKind::Symbols:
    return "symbols";
  case ArtifactSectionKind::Lexer:
    return "lexer";
  case ArtifactSectionKind::ParserTable:
    return "parser-table";
  case ArtifactSectionKind::Productions:
    return "productions";
  case ArtifactSectionKind::Reductions:
    return "reductions";
  case ArtifactSectionKind::AstSchema:
    return "ast-schema";
  case ArtifactSectionKind::Diagnostics:
    return "diagnostics";
  }
  throw ArtifactManifestError{ArtifactManifestErrorCode::InvalidSection,
                              "unknown artifact section kind"};
}

auto sectionFileName(ArtifactSectionKind kind) -> std::string_view {
  switch (kind) {
  case ArtifactSectionKind::Symbols:
    return "symbols.json";
  case ArtifactSectionKind::Lexer:
    return "lexer.json";
  case ArtifactSectionKind::ParserTable:
    return "parser.dsl";
  case ArtifactSectionKind::Productions:
    return "productions.json";
  case ArtifactSectionKind::Reductions:
    return "reductions.json";
  case ArtifactSectionKind::AstSchema:
    return "ast-schema.json";
  case ArtifactSectionKind::Diagnostics:
    return "diagnostics.json";
  }
  throw ArtifactManifestError{ArtifactManifestErrorCode::InvalidSection,
                              "unknown artifact section kind"};
}

void validateArtifactManifest(const ArtifactManifest &manifest,
                              const ArtifactLoadLimits &limits) {
  if (manifest.formatVersion != 1) {
    throw ArtifactManifestError{ArtifactManifestErrorCode::UnsupportedVersion,
                                "artifact manifest version must be 1"};
  }
  if (manifest.parserAlgorithm != "canonical-lr" &&
      manifest.parserAlgorithm != "lalr" &&
      manifest.parserAlgorithm != "slr") {
    throw ArtifactManifestError{ArtifactManifestErrorCode::InvalidAlgorithm,
                                "unknown parser algorithm"};
  }
  if (manifest.lookahead == 0 || manifest.lookahead > limits.maximumLookahead) {
    throw ArtifactManifestError{
        ArtifactManifestErrorCode::InvalidLookahead,
        "artifact lookahead is outside configured limits"};
  }
  if (manifest.startSymbol.empty() || manifest.rootType.empty() ||
      manifest.generatorVersion.empty() || manifest.zbikRevision.empty() ||
      manifest.unicodeVersion.empty()) {
    throw ArtifactManifestError{ArtifactManifestErrorCode::MissingIdentity,
                                "artifact identity fields must not be empty"};
  }
  requireHash(manifest.exactSourceSha256, "exact source hash");
  requireHash(manifest.expandedSourceSha256, "expanded source hash");
  requireHash(manifest.settingsSha256, "settings hash");

  constexpr std::size_t kindCount =
      sectionIndex(ArtifactSectionKind::Diagnostics) + 1;
  std::array<bool, kindCount> seen{};
  std::unordered_set<std::string> files;
  std::uint64_t packageBytes = 0;
  for (const ArtifactSectionDescriptor &section : manifest.sections) {
    const std::size_t index = sectionIndex(section.kind);
    if (index >= seen.size()) {
      throw ArtifactManifestError{ArtifactManifestErrorCode::InvalidSection,
                                  "unknown artifact section kind"};
    }
    if (seen[index] || !files.insert(section.file).second) {
      throw ArtifactManifestError{
          ArtifactManifestErrorCode::DuplicateSection,
          "artifact section kind or file is duplicated"};
    }
    seen[index] = true;
    if ((section.version != 1 && !(section.kind == ArtifactSectionKind::Lexer && section.version == 2)) || section.file != sectionFileName(section.kind) ||
        section.byteLength == 0 ||
        (section.kind == ArtifactSectionKind::Diagnostics &&
         section.required) ||
        (section.kind != ArtifactSectionKind::Diagnostics &&
         !section.required)) {
      throw ArtifactManifestError{ArtifactManifestErrorCode::InvalidSection,
                                  "artifact section descriptor is invalid"};
    }
    requireHash(section.sha256,
                std::string{sectionKindName(section.kind)} + " section hash");
    if (section.byteLength > limits.maximumSectionBytes ||
        section.byteLength > limits.maximumPackageBytes ||
        packageBytes > limits.maximumPackageBytes - section.byteLength) {
      throw ArtifactManifestError{ArtifactManifestErrorCode::ResourceLimit,
                                  "artifact sections exceed configured limits"};
    }
    packageBytes += section.byteLength;
  }
  for (const ArtifactSectionKind kind : requiredSections) {
    if (!seen[sectionIndex(kind)]) {
      throw ArtifactManifestError{ArtifactManifestErrorCode::MissingSection,
                                  "artifact is missing the " +
                                      std::string{sectionKindName(kind)} +
                                      " section"};
    }
  }
}

} // namespace agas::artifact
