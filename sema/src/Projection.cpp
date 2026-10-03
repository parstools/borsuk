#include "AstAccess.h"
#include "DocumentStorage.h"
#include "ModelBindingsInternal.h"
#include "ProjectionStorage.h"
#include "agas/artifact/Sha256.h"
#include <algorithm>
#include <stdexcept>
namespace agsem {
auto applySourceEdits(std::string_view source, std::vector<SourceEdit> edits)
    -> ProjectedText {
  std::ranges::sort(edits, {},
                    [](const auto &edit) { return edit.location.beginByte; });
  ProjectedText result;
  std::size_t cursor = 0;
  const auto append = [&](std::uint64_t begin, std::uint64_t end,
                          std::string_view text, bool replaced) {
    if (text.empty())
      return;
    const auto offset = result.text.size();
    result.text += text;
    result.origins.push_back(
        {offset, result.text.size(), begin, end, replaced});
  };
  for (const auto &edit : edits) {
    const auto begin = edit.location.beginByte, end = edit.location.endByte;
    if (begin < cursor || end < begin || end > source.size())
      throw std::runtime_error("invalid or overlapping projection edits");
    append(cursor, begin, source.substr(cursor, begin - cursor), false);
    append(begin, end, edit.replacement, true);
    cursor = end;
  }
  append(cursor, source.size(), source.substr(cursor), false);
  return result;
}
auto applyProjection(const ParsedDocument &document,
                     std::vector<SourceEdit> edits)
    -> Outcome<ProjectionResult> {
  Outcome<ProjectionResult> result;
  try {
    auto text = applySourceEdits(document.source().text, std::move(edits));
    ProjectionResult projection;
    projection.sourceIdentity = document.identity();
    const auto &root = DocumentAccess::root(document);
    if (const auto *model = ast::optional(ast::field(root, "model")))
      for (const auto &declaration :
           ast::field(*model, "declarations").elements)
        if (declaration.typeName == "modelSchemaDeclaration") {
          const auto name = ast::token(ast::field(declaration, "name"));
          if (name == "standard_semantic_v1")
            projection.bindingSchema =
                BindingSchemaIdentity{name, "1", standardBindingSchemaHash(),
                                      ast::location(declaration)};
        }
    projection.specificationName = document.specificationName();
    projection.grammarName = document.grammar().grammar.grammarName;
    projection.text = std::move(text.text);
    projection.origins = std::move(text.origins);
    projection.outputIdentity = agas::artifact::sha256Hex(projection.text);
    result.value = std::move(projection);
  } catch (const std::exception &error) {
    result.diagnostics.push_back(
        {Severity::Error, "projection.invalid_range", error.what(), {}, {}});
  }
  return result;
}
auto projectAg(const ParsedDocument &document) -> Outcome<ProjectionResult> {
  Outcome<ProjectionResult> result;
  result.diagnostics = validateDocumentSections(document);
  if (!result.diagnostics.empty())
    return result;
  std::vector<SourceEdit> edits;
  for (const auto &element : document.elements())
    if (element.owner != DocumentOwner::Ag)
      edits.push_back({element.location, {}});
  return applyProjection(document, std::move(edits));
}
auto composeOriginMaps(const std::vector<OriginFragment> &parent,
                       const std::vector<OriginFragment> &child)
    -> std::vector<OriginFragment> {
  std::vector<OriginFragment> result;
  for (const auto &fragment : child) {
    std::uint64_t cursor = fragment.inputBegin;
    for (const auto &origin : parent) {
      const auto begin = std::max(cursor, origin.outputBegin),
                 end = std::min(fragment.inputEnd, origin.outputEnd);
      if (begin >= end)
        continue;
      if (begin != cursor)
        throw std::invalid_argument("origin maps contain a gap");
      if (fragment.outputEnd - fragment.outputBegin !=
              fragment.inputEnd - fragment.inputBegin ||
          origin.outputEnd - origin.outputBegin !=
              origin.inputEnd - origin.inputBegin)
        throw std::invalid_argument(
            "partial replacement mapping is not supported");
      result.push_back({fragment.outputBegin + begin - fragment.inputBegin,
                        fragment.outputBegin + end - fragment.inputBegin,
                        origin.inputBegin + begin - origin.outputBegin,
                        origin.inputBegin + end - origin.outputBegin,
                        fragment.replacement || origin.replacement});
      cursor = end;
    }
    if (cursor != fragment.inputEnd)
      throw std::invalid_argument("origin map does not cover its input");
  }
  return result;
}
} // namespace agsem
