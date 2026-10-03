#include "AstAccess.h"
#include "SemanticInputStorage.h"

#include <memory>

namespace agsem {

auto SemanticInputAccess::fromAst(const agas::runtime::AstValue &root,
                                  const agas::model::SyntaxDocument &grammar,
                                  std::string identity) -> SemanticInput {
  auto semantic = root;
  for (const auto *name : {"execution", "contract", "lowering", "backendC",
                           "backendLlvm", "settings", "obligations"})
    if (ast::find(semantic, name))
      ast::field(semantic, name).elements.clear();
  const auto remove = [&](auto &&self, ast::Value &value) -> void {
    if (value.typeName == "parserAlternative")
      std::erase_if(ast::field(value, "actions").elements,
                    [](const auto &action) {
                      return action.typeName == "executionResult";
                    });
    for (auto &child : value.elements)
      self(self, child);
  };
  remove(remove, semantic);
  return SemanticInput{std::make_shared<const SemanticInput::Impl>(
      std::move(semantic), grammar, std::move(identity), std::string{},
      std::vector<ContractSymbol>{})};
}

auto SemanticInputAccess::withContracts(const SemanticInput &input,
                                        const ContractEnvironment &contracts)
    -> SemanticInput {
  auto data = *input.storage_;
  data.modelBindings.reset();
  data.contractsIdentity = contracts.fingerprint();
  data.contractSymbols = contracts.symbols();
  std::erase_if(data.contractSymbols, [](const auto &symbol) {
    return symbol.owner == SymbolOwner::Execution;
  });
  return SemanticInput{
      std::make_shared<const SemanticInput::Impl>(std::move(data))};
}
auto SemanticInputAccess::withModelBindings(
    const SemanticInput &input,
    std::shared_ptr<const CheckedModelBindings> bindings) -> SemanticInput {
  auto data = *input.storage_;
  data.modelBindings = std::move(bindings);
  return SemanticInput{
      std::make_shared<const SemanticInput::Impl>(std::move(data))};
}
auto SemanticInputAccess::modelBindings(const SemanticInput &input)
    -> const std::shared_ptr<const CheckedModelBindings> & {
  return input.storage_->modelBindings;
}
auto SemanticInputAccess::contractsIdentity(const SemanticInput &input)
    -> const std::string & {
  return input.storage_->contractsIdentity;
}

auto SemanticInputAccess::identity(const SemanticInput &input)
    -> const std::string & {
  return input.storage_->identity;
}

auto SemanticInputAccess::contracts(const SemanticInput &input)
    -> const std::vector<ContractSymbol> & {
  return input.storage_->contractSymbols;
}

auto SemanticInputAccess::root(const SemanticInput &input)
    -> const agas::runtime::AstValue & {
  return input.storage_->root;
}

auto SemanticInputAccess::grammar(const SemanticInput &input)
    -> const agas::model::SyntaxDocument & {
  return input.storage_->grammar;
}

} // namespace agsem
