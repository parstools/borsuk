#pragma once
#include "agas/runtime/ReductionRuntime.h"
#include "agsem/ModelBindings.h"
#include "agsem/SemanticBinding.h"
namespace agsem {
struct BindingRequirement {
  BindingTargetKind kind{BindingTargetKind::Field};
  std::string target;
  std::vector<std::pair<std::string, TypeRef>> fields;
  std::optional<FunctionContract> function;
  std::string invocation;
  std::vector<std::string> argumentPassing;
  std::string access;
};
[[nodiscard]] auto standardBindingRequirements(std::string_view family,
                                               std::string_view context,
                                               bool cleanup)
    -> std::map<std::string, BindingRequirement>;
struct BindingDependency {
  BindingTargetKind kind;
  std::string owner;
  std::string target;
  std::vector<TypeRef> types;
};
[[nodiscard]] auto
standardBindingDependencies(std::string_view family,
                            const std::map<std::string, std::string> &policy,
                            const std::map<std::string, std::string> &bindings,
                            bool cleanup) -> std::vector<BindingDependency>;
[[nodiscard]] auto standardBindingSchemaHash() -> std::string;
[[nodiscard]] auto bindModelBindings(const agas::runtime::AstValue &,
                                     const std::vector<BoundSymbol> &,
                                     const ContractEnvironment &)
    -> Outcome<std::shared_ptr<const CheckedModelBindings>>;
[[nodiscard]] auto hasModelBindingSchema(const agas::runtime::AstValue &)
    -> bool;
} // namespace agsem
