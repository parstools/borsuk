#pragma once

#include <string>
#include <string_view>

#include "agas/artifact/ArtifactManifest.h"

namespace agas::artifact {

// Produces canonical UTF-8 JSON with a stable field and section order.
[[nodiscard]] auto dumpArtifactManifestJson(const ArtifactManifest &manifest)
    -> std::string;

// Parses a complete JSON document, rejects duplicate or unknown fields, and
// validates the resulting manifest before returning it.
[[nodiscard]] auto parseArtifactManifestJson(
    std::string_view text,
    const ArtifactLoadLimits &limits = ArtifactLoadLimits{})
    -> ArtifactManifest;

} // namespace agas::artifact
