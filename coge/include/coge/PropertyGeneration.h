#pragma once

#include "coge/PropertyExpression.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace coge {

struct PropertyCheck {
  std::vector<std::string> bindings;
  std::string function;
  std::vector<std::string> arguments;
  bool source = false;
  std::optional<PropertyExpression> constraint;
  bool fitsI32 = false;
  bool negatedFits = false;
  std::optional<PropertyExpression> expected;
  std::optional<std::string> errorMessageLiteral;
};

struct PropertyGenerationPlan {
  std::vector<std::int64_t> boundaryCases;
  std::vector<PropertyCheck> checks;
};

[[nodiscard]] auto emitPropertiesRust(
    const std::optional<PropertyGenerationPlan> &plan) -> std::string;

} // namespace coge
