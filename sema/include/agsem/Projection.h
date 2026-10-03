#pragma once
#include "agsem/ContractEnvironment.h"
#include "agsem/DocumentFrontend.h"
#include "agsem/ModelBindings.h"
namespace agsem {
struct OriginFragment {
  std::uint64_t outputBegin{}, outputEnd{}, inputBegin{}, inputEnd{};
  bool replacement{};
  auto operator==(const OriginFragment &) const -> bool = default;
};
struct ProjectionResult {
  std::string text;
  std::string sourceIdentity;
  std::string outputIdentity;
  std::string specificationName;
  std::string grammarName;
  std::vector<OriginFragment> origins;
  std::vector<ContractIdentity> contracts;
  std::optional<BindingSchemaIdentity> bindingSchema;
};
struct SourceEdit {
  SourceLocation location;
  std::string replacement;
};
[[nodiscard]] auto applyProjection(const ParsedDocument &document,
                                   std::vector<SourceEdit> edits)
    -> Outcome<ProjectionResult>;
[[nodiscard]] auto projectAg(const ParsedDocument &document)
    -> Outcome<ProjectionResult>;
[[nodiscard]] auto composeOriginMaps(const std::vector<OriginFragment> &parent,
                                     const std::vector<OriginFragment> &child)
    -> std::vector<OriginFragment>;
} // namespace agsem
