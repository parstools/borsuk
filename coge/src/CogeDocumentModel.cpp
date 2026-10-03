#include "AstAccess.h"
#include "CogeDocumentStorage.h"
#include "DocumentStorage.h"
namespace coge {
auto ExecutionInput::identity() const -> const std::string & {
  return data_->identity;
}
auto CogeDocument::identity() const -> const std::string & {
  return data_->execution.identity();
}
auto CogeDocument::semantics() const -> const agsem::SemaDocument & {
  return data_->semantics;
}
auto CogeDocument::execution() const -> const ExecutionInput & {
  return data_->execution;
}
auto DocumentAccess::legacyInput(const agas::runtime::AstValue &root)
    -> ExecutionInput {
  return ExecutionInput{
      std::make_shared<const ExecutionInput::Impl>(std::string{}, root)};
}
auto DocumentAccess::root(const ExecutionInput &input)
    -> const agsem::ast::Value & {
  return input.data_->root;
}
auto DocumentAccess::syntax(const CogeDocument &document)
    -> const agsem::ParsedDocument & {
  return document.data_->syntax;
}
auto DocumentAccess::make(const agsem::ParsedDocument &document)
    -> CogeDocument {
  auto root = agsem::DocumentAccess::root(document);
  for (const auto *name : {"model", "rules", "header", "legacy"})
    agsem::ast::field(root, name).elements.clear();
  auto input = ExecutionInput{std::make_shared<const ExecutionInput::Impl>(
      document.identity(), std::move(root))};
  return CogeDocument{std::make_shared<const CogeDocument::Impl>(
      agsem::DocumentAccess::sema(document), std::move(input), document)};
}
auto makeCogeDocument(const agsem::ParsedDocument &document)
    -> agsem::Outcome<CogeDocument> {
  agsem::Outcome<CogeDocument> result;
  if (document.kind() != agsem::DocumentKind::Coge)
    result.diagnostics.push_back(
        {agsem::Severity::Error,
         "document.wrong_kind",
         "expected coge header; use sema for this document",
         agsem::DocumentAccess::kindLocation(document),
         {}});
  auto errors = agsem::validateDocumentSections(document);
  result.diagnostics.insert(result.diagnostics.end(), errors.begin(),
                            errors.end());
  if (result.diagnostics.empty())
    result.value = DocumentAccess::make(document);
  return result;
}
} // namespace coge
