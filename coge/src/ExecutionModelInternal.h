#pragma once
#include "agsem/SemanticBinding.h"
#include "coge/ExecutionModel.h"
namespace coge {
struct ExecutionProfileRequirement {
  ExecutionDependency contract;
  bool eitherEffect = false;
  bool localBody = false;
};
struct ExecutionProfileSpec {
  std::vector<ExecutionProfileRequirement> requirements;
  std::vector<ExecutionFunction> functions;
  std::vector<ExecutionHandler> operations, expressions;
};
[[nodiscard]] auto sharedValueProfile() -> const ExecutionProfileSpec &;
[[nodiscard]] auto executionProfileHash() -> std::string;
[[nodiscard]] auto expandExecutionModel(const agas::runtime::AstValue &,
                                        std::string_view, std::string,
                                        std::string) -> ExpandedExecutionModel;
[[nodiscard]] auto serializeProfileRequirements(const ExecutionProfileSpec &)
    -> std::string;
void applyExecutionProfile(ExpandedExecutionModel &);
[[nodiscard]] auto executionInterpreter(const ExpandedExecutionModel &)
    -> InterpreterGenerationPlan;
[[nodiscard]] auto prepareCheckedExecution(ExpandedExecutionModel,
                                           const agsem::BoundSemantics &)
    -> agsem::Outcome<std::shared_ptr<const CheckedExecutionModel>>;
[[nodiscard]] auto checkExpandedExecution(const ExpandedExecutionModel &,
                                          const agsem::BoundSemantics &,
                                          std::vector<ExecutionDependency> &,
                                          bool partial = false)
    -> std::vector<agsem::Diagnostic>;
[[nodiscard]] auto serializeExecutionModel(const ExpandedExecutionModel &,
                                           bool origins = true) -> std::string;
} // namespace coge
