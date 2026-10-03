#pragma once
#include "agas/runtime/ReductionRuntime.h"
#include "coge/CheckedGeneration.h"
#include "coge/DocumentModel.h"
namespace coge {
struct ExecutionInput::Impl {
  std::string identity;
  agas::runtime::AstValue root;
};
struct CogeDocument::Impl {
  agsem::SemaDocument semantics;
  ExecutionInput execution;
  agsem::ParsedDocument syntax;
};
struct DocumentAccess {
  static auto make(const agsem::ParsedDocument &document) -> CogeDocument;
  static auto legacyInput(const agas::runtime::AstValue &root)
      -> ExecutionInput;
  static auto root(const ExecutionInput &input)
      -> const agas::runtime::AstValue &;
  static auto syntax(const CogeDocument &document)
      -> const agsem::ParsedDocument &;
};
[[nodiscard]] auto
prepareGeneration(const ExecutionInput &input, std::string_view contextType,
                  const std::vector<agsem::ContractSymbol> &contracts = {})
    -> CheckedGeneration;
[[nodiscard]] auto
tryPrepareGeneration(const ExecutionInput &input, std::string_view contextType,
                     const std::vector<agsem::ContractSymbol> &contracts = {})
    -> agsem::Outcome<CheckedGeneration>;

} // namespace coge
