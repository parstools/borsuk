#pragma once

#include "coge/BackendGeneration.h"
#include "coge/InterpreterGeneration.h"
#include "coge/LoweringGeneration.h"
#include "coge/PropertyGeneration.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace coge {
class CheckedExecutionModel;
class CogeValidation;
struct GenerationAccess;

struct GenerationInput {
  std::optional<InterpreterGenerationPlan> interpreter;
  std::optional<PropertyGenerationPlan> properties;
  CheckedLowering lowering;
  CheckedBackends backends;
  std::shared_ptr<const CheckedExecutionModel> execution;
};

struct RustArtifacts {
  std::string interpreter;
  std::string properties;
  std::string lowering;
  std::string backendC;
  std::string backendLlvm;
};

class CheckedGeneration {
public:
  CheckedGeneration(const CheckedGeneration &) = default;
  CheckedGeneration(CheckedGeneration &&) noexcept = default;
  auto operator=(const CheckedGeneration &) -> CheckedGeneration & = default;
  auto operator=(CheckedGeneration &&) noexcept
      -> CheckedGeneration & = default;
  [[nodiscard]] auto execution() const
      -> const std::shared_ptr<const CheckedExecutionModel> & {
    return input_.execution;
  }
  [[nodiscard]] auto properties() const
      -> const std::optional<PropertyGenerationPlan> & {
    return input_.properties;
  }
  [[nodiscard]] auto lowering() const -> const CheckedLowering & {
    return input_.lowering;
  }
  [[nodiscard]] auto backends() const -> const CheckedBackends & {
    return input_.backends;
  }

private:
  explicit CheckedGeneration(GenerationInput input)
      : input_(std::move(input)) {}
  GenerationInput input_;
  std::shared_ptr<const CogeValidation> validation_;
  friend struct GenerationAccess;
  friend auto prepareCheckedGeneration(GenerationInput) -> CheckedGeneration;
  friend auto emitCheckedGeneration(const CheckedGeneration &) -> RustArtifacts;
};

[[nodiscard]] auto prepareCheckedGeneration(GenerationInput input)
    -> CheckedGeneration;
[[nodiscard]] auto emitCheckedGeneration(const CheckedGeneration &plan)
    -> RustArtifacts;

} // namespace coge
