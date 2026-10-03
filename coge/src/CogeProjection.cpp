#include "CogeDocumentStorage.h"
#include "DocumentStorage.h"
#include "coge/Projection.h"
namespace coge {
auto projectSema(const CogeDocument &document,
                 const agsem::BoundSemantics &semantics,
                 const agsem::DocumentFrontend &frontend)
    -> agsem::Outcome<agsem::ProjectionResult> {
  agsem::Outcome<agsem::ProjectionResult> result;
  if (document.identity() != semantics.document().identity()) {
    result.diagnostics.push_back(
        {agsem::Severity::Error,
         "document.identity_mismatch",
         "projection and semantic model belong to different inputs",
         {},
         {}});
    return result;
  }
  const auto &syntax = DocumentAccess::syntax(document);
  std::vector<agsem::SourceEdit> edits{
      {agsem::DocumentAccess::kindLocation(syntax), "sema"}};
  for (const auto &element : syntax.elements())
    if (element.owner == agsem::DocumentOwner::Coge)
      edits.push_back({element.location, {}});
  result = agsem::applyProjection(syntax, std::move(edits));
  if (!result.value)
    return result;
  auto parsed = frontend.parse(result.value->text);
  if (!parsed.value) {
    result.value.reset();
    result.diagnostics = std::move(parsed.diagnostics);
    return result;
  }
  auto sema = agsem::makeSemaDocument(*parsed.value);
  if (!sema.value) {
    result.value.reset();
    result.diagnostics = std::move(sema.diagnostics);
    return result;
  }
  auto bound = agsem::bindSemantics(*sema.value, semantics.contracts());
  if (!bound.value) {
    result.value.reset();
    result.diagnostics = std::move(bound.diagnostics);
    return result;
  }
  result.value->contracts = semantics.contracts().identities();
  return result;
}
} // namespace coge
