#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "agas/artifact/ArtifactManifest.h"
#include "agas/artifact/GrammarSections.h"
#include "agas/generator/ReductionProgram.h"

namespace agas::artifact {

struct ArtifactReductions {
  std::uint32_t version{};
  generator::AstReductionProgram program;
};

void validateReductionSection(const ArtifactProductions &productions,
                              const ArtifactReductions &reductions);

[[nodiscard]] auto
dumpReductionsJson(const ArtifactProductions &productions,
                   const generator::AstReductionProgram &program)
    -> std::string;

[[nodiscard]] auto
parseReductionsJson(std::string_view text,
                    const ArtifactProductions &productions,
                    const ArtifactLoadLimits &limits = ArtifactLoadLimits{})
    -> ArtifactReductions;

} // namespace agas::artifact
