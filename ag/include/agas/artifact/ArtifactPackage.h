#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "agas/artifact/ArtifactManifestJson.h"
#include "agas/artifact/AstSchemaSection.h"
#include "agas/artifact/GrammarSections.h"
#include "agas/artifact/LexerSection.h"
#include "agas/artifact/ParserTableSection.h"
#include "agas/artifact/ReductionSection.h"

namespace agas::artifact {

struct ArtifactPackageSections {
  std::string symbols;
  std::string lexer;
  std::string parserTable;
  std::string productions;
  std::string reductions;
  std::string astSchema;
  std::optional<std::string> diagnostics;
};

struct ArtifactPackage {
  ArtifactManifest manifest;
  std::string manifestJson;
  ArtifactPackageSections sections;
};

struct LoadedArtifactPackage {
  ArtifactManifest manifest;
  ArtifactSymbols symbols;
  ArtifactLexer lexer;
  ArtifactParserTable parserTable;
  ArtifactProductions productions;
  ArtifactReductions reductions;
  ArtifactAstSchema astSchema;
  std::optional<std::string> diagnostics;
};

enum class ArtifactPackageErrorCode {
  InvalidPackage,
  MissingSection,
  LengthMismatch,
  HashMismatch,
  InconsistentSections,
  UnsafeFile,
  IoError,
  ResourceLimit,
};

class ArtifactPackageError final : public std::runtime_error {
public:
  ArtifactPackageError(ArtifactPackageErrorCode code, std::string message);

  [[nodiscard]] auto code() const noexcept -> ArtifactPackageErrorCode;

private:
  ArtifactPackageErrorCode code_;
};

// `identity.sections` must be empty. The returned manifest describes the exact
// canonical bytes supplied in `sections`.
[[nodiscard]] auto makeArtifactPackage(ArtifactManifest identity,
                                       ArtifactPackageSections sections)
    -> ArtifactPackage;

[[nodiscard]] auto
loadArtifactPackage(std::string_view manifestJson,
                    const ArtifactPackageSections &sections,
                    const ArtifactLoadLimits &limits = ArtifactLoadLimits{})
    -> LoadedArtifactPackage;

// The directory itself and every loaded file must be real directories/files,
// not symbolic links. Section names come only from the validated manifest.
[[nodiscard]] auto loadArtifactPackageDirectory(
    const std::filesystem::path &directory,
    const ArtifactLoadLimits &limits = ArtifactLoadLimits{})
    -> LoadedArtifactPackage;

// Writes through a sibling temporary directory and atomically renames it.
// Refuses to replace an existing output or temporary directory.
void writeArtifactPackageDirectory(const ArtifactPackage &package,
                                   const std::filesystem::path &directory);

} // namespace agas::artifact
