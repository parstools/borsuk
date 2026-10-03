#pragma once
#include "coge/GenerationPreparation.h"
namespace coge {
struct ConfigurationReview {
  std::optional<CheckedLowering> lowering;
  std::optional<CheckedBackends> backends;
  std::vector<agsem::Diagnostic> diagnostics;
};
[[nodiscard]] auto reviewConfiguration(const CogeDocument &,
                                       const agsem::BoundSemantics &)
    -> ConfigurationReview;
} // namespace coge
