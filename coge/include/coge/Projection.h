#pragma once
#include "agsem/Projection.h"
#include "agsem/SemanticBinding.h"
#include "coge/DocumentModel.h"
namespace coge {
[[nodiscard]] auto projectSema(const CogeDocument &document,
                               const agsem::BoundSemantics &semantics,
                               const agsem::DocumentFrontend &frontend)
    -> agsem::Outcome<agsem::ProjectionResult>;
} // namespace coge
