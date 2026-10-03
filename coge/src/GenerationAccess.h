#pragma once
#include "coge/GenerationPreparation.h"
namespace coge {
struct GenerationAccess {
  [[nodiscard]] static auto prepare(GenerationInput, const CogeValidation &)
      -> CheckedGeneration;
};
} // namespace coge
