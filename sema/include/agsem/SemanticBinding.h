#pragma once
#include "agsem/CheckedSemantics.h"
#include "agsem/ContractEnvironment.h"
#include "agsem/DocumentModel.h"
namespace agsem {
using SymbolId = std::size_t;
struct ResolvedUse {
  SymbolId symbol;
  SourceLocation location;
};
struct BoundSymbol {
  ContractSymbol contract;
  SourceLocation declaration;
};
struct BindingAccess;
class BoundSemantics {
public:
  [[nodiscard]] auto document() const -> const SemaDocument &;
  [[nodiscard]] auto contracts() const -> const ContractEnvironment &;
  [[nodiscard]] auto symbols() const -> const std::vector<BoundSymbol> &;
  [[nodiscard]] auto uses() const -> const std::vector<ResolvedUse> &;

private:
  struct Impl;
  explicit BoundSemantics(std::shared_ptr<const Impl> data)
      : data_(std::move(data)) {}
  std::shared_ptr<const Impl> data_;
  friend struct BindingAccess;
};
struct SemanticValidation {};
[[nodiscard]] auto bindSemantics(const SemaDocument &document,
                                 const ContractEnvironment &contracts)
    -> Outcome<BoundSemantics>;
[[nodiscard]] auto checkSemantics(const BoundSemantics &document)
    -> Outcome<SemanticValidation>;
[[nodiscard]] auto prepareSemanticModel(const BoundSemantics &document)
    -> Outcome<std::shared_ptr<const CheckedSemantics>>;
} // namespace agsem
