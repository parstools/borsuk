#include "agas/artifact/ArtifactManifest.h"
#include "agas/artifact/ArtifactManifestJson.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string{message});
}

auto validManifest() -> agas::artifact::ArtifactManifest {
  using agas::artifact::ArtifactSectionDescriptor;
  using agas::artifact::ArtifactSectionKind;

  const std::string hash(64, 'a');
  const auto section = [&](ArtifactSectionKind kind) {
    return ArtifactSectionDescriptor{
        kind,
        1,
        std::string{agas::artifact::sectionFileName(kind)},
        kind != ArtifactSectionKind::Diagnostics,
        100,
        hash};
  };
  return {1,
          "lalr",
          2,
          "document",
          "document",
          "agas-test",
          "zbik-test",
          "78.2",
          hash,
          hash,
          hash,
          {section(ArtifactSectionKind::Symbols),
           section(ArtifactSectionKind::Lexer),
           section(ArtifactSectionKind::ParserTable),
           section(ArtifactSectionKind::Productions),
           section(ArtifactSectionKind::Reductions),
           section(ArtifactSectionKind::AstSchema),
           section(ArtifactSectionKind::Diagnostics)}};
}

void expectError(agas::artifact::ArtifactManifest manifest,
                 agas::artifact::ArtifactManifestErrorCode expected,
                 std::string_view message) {
  try {
    agas::artifact::validateArtifactManifest(manifest);
  } catch (const agas::artifact::ArtifactManifestError &error) {
    require(error.code() == expected, message);
    return;
  }
  throw std::runtime_error(std::string{message});
}

void expectJsonError(std::string_view json,
                     agas::artifact::ArtifactManifestErrorCode expected,
                     std::string_view message) {
  try {
    static_cast<void>(agas::artifact::parseArtifactManifestJson(json));
  } catch (const agas::artifact::ArtifactManifestError &error) {
    require(error.code() == expected, message);
    return;
  }
  throw std::runtime_error(std::string{message});
}

auto replaceOnce(std::string source, std::string_view from, std::string_view to)
    -> std::string {
  const std::size_t position = source.find(from);
  require(position != std::string::npos, "JSON test fixture pattern is absent");
  source.replace(position, from.size(), to);
  return source;
}

} // namespace

int main() {
  using agas::artifact::ArtifactManifestErrorCode;

  try {
    const auto valid = validManifest();
    agas::artifact::validateArtifactManifest(valid);
    const std::string canonical =
        agas::artifact::dumpArtifactManifestJson(valid);
    const auto parsed = agas::artifact::parseArtifactManifestJson(canonical);
    require(parsed == valid,
            "manifest JSON round-trip must preserve the model");
    require(agas::artifact::dumpArtifactManifestJson(parsed) == canonical,
            "manifest JSON must be byte-for-byte reproducible");

    auto duplicate = valid;
    duplicate.sections.push_back(duplicate.sections.front());
    expectError(duplicate, ArtifactManifestErrorCode::DuplicateSection,
                "duplicate sections must be rejected");

    auto missing = valid;
    missing.sections.erase(missing.sections.begin());
    expectError(missing, ArtifactManifestErrorCode::MissingSection,
                "missing required sections must be rejected");

    auto traversal = valid;
    traversal.sections.front().file = "../symbols.json";
    expectError(traversal, ArtifactManifestErrorCode::InvalidSection,
                "noncanonical section paths must be rejected");

    auto uppercaseHash = valid;
    uppercaseHash.sections.front().sha256.assign(64, 'A');
    expectError(uppercaseHash, ArtifactManifestErrorCode::InvalidHash,
                "noncanonical hashes must be rejected");

    auto unknownVersion = valid;
    unknownVersion.formatVersion = 2;
    expectError(unknownVersion, ArtifactManifestErrorCode::UnsupportedVersion,
                "unknown manifest versions must be rejected");

    auto excessive = valid;
    excessive.sections.front().byteLength = (1ULL << 30U) + 1;
    expectError(excessive, ArtifactManifestErrorCode::ResourceLimit,
                "oversized sections must be rejected before loading");

    agas::artifact::ArtifactLoadLimits aggregateLimit;
    aggregateLimit.maximumSectionBytes = 1'000;
    aggregateLimit.maximumPackageBytes = 650;
    try {
      agas::artifact::validateArtifactManifest(valid, aggregateLimit);
      throw std::runtime_error("oversized packages must be rejected");
    } catch (const agas::artifact::ArtifactManifestError &error) {
      require(error.code() == ArtifactManifestErrorCode::ResourceLimit,
              "aggregate package limit must be enforced");
    }

    expectJsonError(
        replaceOnce(canonical, "{\n  \"formatVersion\": 1,",
                    "{\n  \"formatVersion\": 1,\n  \"formatVersion\": 1,"),
        ArtifactManifestErrorCode::InvalidJson,
        "duplicate JSON fields must be rejected");
    expectJsonError(
        replaceOnce(canonical, "{\n  \"formatVersion\": 1,",
                    "{\n  \"unknown\": 0,\n  \"formatVersion\": 1,"),
        ArtifactManifestErrorCode::UnknownField,
        "unknown JSON fields must be rejected");
    expectJsonError(
        replaceOnce(canonical, "\"lookahead\": 2", "\"lookahead\": -1"),
        ArtifactManifestErrorCode::InvalidJson,
        "signed integers must not be converted to identifiers");
    expectJsonError("{", ArtifactManifestErrorCode::InvalidJson,
                    "truncated JSON must be rejected");

    agas::artifact::ArtifactLoadLimits manifestLimit;
    manifestLimit.maximumManifestBytes = canonical.size() - 1;
    try {
      static_cast<void>(
          agas::artifact::parseArtifactManifestJson(canonical, manifestLimit));
      throw std::runtime_error("oversized manifest JSON must be rejected");
    } catch (const agas::artifact::ArtifactManifestError &error) {
      require(error.code() == ArtifactManifestErrorCode::ResourceLimit,
              "manifest byte limit must be enforced before parsing");
    }

    std::cout << "artifact manifest sections=" << valid.sections.size()
              << " negative-cases=12\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
