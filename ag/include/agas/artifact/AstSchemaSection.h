#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "agas/artifact/ArtifactManifest.h"
#include "agas/artifact/GrammarSections.h"
#include "agas/generator/AstSchema.h"

namespace agas::artifact {

struct ArtifactAstSchema {
  std::uint32_t version{};
  generator::AstSchema schema;
};

void validateAstSchemaSection(const ArtifactAstSchema &section);

[[nodiscard]] auto dumpAstSchemaJson(const generator::AstSchema &schema)
    -> std::string;
[[nodiscard]] auto
parseAstSchemaJson(std::string_view text,
                   const ArtifactLoadLimits &limits = ArtifactLoadLimits{})
    -> ArtifactAstSchema;

} // namespace agas::artifact
