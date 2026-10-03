#pragma once

#include "agsem/Diagnostics.h"
#include "agsem/SemanticBinding.h"
#include "coge/CheckedGeneration.h"
#include "coge/DocumentModel.h"
#include "coge/ExecutionModel.h"

#include <string_view>

namespace coge {

class CogeValidation {
public:
  CogeValidation(const CogeValidation &) = default;
  CogeValidation(CogeValidation &&) noexcept = default;
  auto operator=(const CogeValidation &) -> CogeValidation & = default;
  auto operator=(CogeValidation &&) noexcept -> CogeValidation & = default;
  [[nodiscard]] auto execution() const
      -> const std::shared_ptr<const CheckedExecutionModel> & {
    return execution_;
  }
  [[nodiscard]] auto lowering() const -> const CheckedLowering & {
    return lowering_;
  }
  [[nodiscard]] auto backends() const -> const CheckedBackends & {
    return backends_;
  }
  [[nodiscard]] auto completeness() const -> const agsem::CompletenessReport & {
    return completeness_;
  }

private:
  CogeValidation(std::shared_ptr<const CheckedExecutionModel> execution,
                 CheckedLowering lowering, CheckedBackends backends,
                 agsem::CompletenessReport completeness)
      : execution_(std::move(execution)), lowering_(std::move(lowering)),
        backends_(std::move(backends)), completeness_(std::move(completeness)) {
  }
  std::shared_ptr<const CheckedExecutionModel> execution_;
  CheckedLowering lowering_;
  CheckedBackends backends_;
  agsem::CompletenessReport completeness_;
  friend auto checkCoge(const CogeDocument &, const agsem::BoundSemantics &,
                        const agsem::ContractEnvironment &)
      -> agsem::Outcome<CogeValidation>;
};
struct GenerationSelection {
  bool interpreter{};
  bool lowering{};
  bool c{};
  bool llvm{};
};
[[nodiscard]] auto generationSelection(const CogeDocument &document)
    -> GenerationSelection;
[[nodiscard]] auto checkCoge(const CogeDocument &document,
                             const agsem::BoundSemantics &semantics,
                             const agsem::ContractEnvironment &contracts)
    -> agsem::Outcome<CogeValidation>;
[[nodiscard]] auto
prepareGeneration(const CogeDocument &document,
                  const agsem::CheckedSemantics &semantics,
                  const GenerationSelection &selection,
                  const agsem::ContractEnvironment &contracts)
    -> agsem::Outcome<CheckedGeneration>;

[[nodiscard]] auto
prepareGeneration(const CogeDocument &, const agsem::CheckedSemantics &,
                  const GenerationSelection &,
                  const agsem::ContractEnvironment &, const CogeValidation &)
    -> agsem::Outcome<CheckedGeneration>;
} // namespace coge
