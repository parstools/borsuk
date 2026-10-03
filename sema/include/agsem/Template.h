#pragma once
#include "agsem/Projection.h"
namespace agsem {
// Templates preserve source bytes and add pending actions, never
// implementations.
[[nodiscard]] auto templateFromAg(std::string_view source,
                                  const DocumentFrontend &frontend)
    -> Outcome<ProjectionResult>;
} // namespace agsem
