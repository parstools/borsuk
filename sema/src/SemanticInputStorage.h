#pragma once

#include "agas/model/SyntaxModel.h"
#include "agas/runtime/ReductionRuntime.h"
#include "agsem/CheckedSemantics.h"
#include "agsem/ContractEnvironment.h"
#include "agsem/ModelBindings.h"
#include "agsem/SemanticInput.h"

namespace agsem {

struct SemanticInput::Impl {
  agas::runtime::AstValue root;
  agas::model::SyntaxDocument grammar;
  std::string identity;
  std::string contractsIdentity;
  std::vector<ContractSymbol> contractSymbols;
  std::shared_ptr<const CheckedModelBindings> modelBindings;
};

struct SemanticInputAccess {
  [[nodiscard]] static auto fromAst(const agas::runtime::AstValue &root,
                                    const agas::model::SyntaxDocument &grammar,
                                    std::string identity = {}) -> SemanticInput;
  [[nodiscard]] static auto withContracts(const SemanticInput &input,
                                          const ContractEnvironment &contracts)
      -> SemanticInput;
  [[nodiscard]] static auto
  withModelBindings(const SemanticInput &,
                    std::shared_ptr<const CheckedModelBindings>)
      -> SemanticInput;
  [[nodiscard]] static auto modelBindings(const SemanticInput &)
      -> const std::shared_ptr<const CheckedModelBindings> &;
  [[nodiscard]] static auto contractsIdentity(const SemanticInput &input)
      -> const std::string &;
  [[nodiscard]] static auto identity(const SemanticInput &input)
      -> const std::string &;
  [[nodiscard]] static auto contracts(const SemanticInput &input)
      -> const std::vector<ContractSymbol> &;
  [[nodiscard]] static auto root(const SemanticInput &input)
      -> const agas::runtime::AstValue &;
  [[nodiscard]] static auto grammar(const SemanticInput &input)
      -> const agas::model::SyntaxDocument &;
};

[[nodiscard]] auto prepareSemanticModel(const SemanticInput &input)
    -> std::shared_ptr<const CheckedSemantics>;
[[nodiscard]] auto tryPrepareSemanticModel(const SemanticInput &input)
    -> Outcome<std::shared_ptr<const CheckedSemantics>>;
[[nodiscard]] auto
prepareSemanticModel(const agas::runtime::AstValue &root,
                     const agas::model::SyntaxDocument &sourceGrammar)
    -> std::shared_ptr<const CheckedSemantics>;

class BoundSemantics;
[[nodiscard]] auto boundSemanticInput(const BoundSemantics &) -> SemanticInput;

void validateSemanticPolicies(const SemanticInput &input);
void validateSemanticPolicies(const agas::runtime::AstValue &root,
                              const CheckedModelBindings *bindings);

} // namespace agsem
