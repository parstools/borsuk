#pragma once

#include "RustGenerator.h"
#include "SemanticInputStorage.h"
#include "agsem/CheckedSemantics.h"
#include "coge/CheckedGeneration.h"

namespace agsem {

using PreparedSemanticModel = CheckedSemantics;
using PreparedGenerationModel = coge::CheckedGeneration;

[[nodiscard]] auto
prepareGenerationModel(const PreparedSemanticModel &model,
                       const agas::runtime::AstValue &legacyRoot)
    -> PreparedGenerationModel;
[[nodiscard]] auto emitRust(const PreparedSemanticModel &model,
                            const PreparedGenerationModel &generation)
    -> RustFiles;
[[nodiscard]] auto emitRust(const PreparedSemanticModel &model,
                            const agas::runtime::AstValue &legacyRoot)
    -> RustFiles;

} // namespace agsem
