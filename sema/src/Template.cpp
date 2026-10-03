#include "agsem/Template.h"
#include "ProjectionStorage.h"
#include "agas/artifact/Sha256.h"
#include <map>
namespace agsem {
auto templateFromAg(std::string_view source, const DocumentFrontend &frontend)
    -> Outcome<ProjectionResult> {
  Outcome<ProjectionResult> result;
  auto grammar = frontend.parseAg(source);
  result.diagnostics = std::move(grammar.diagnostics);
  if (!grammar.value)
    return result;
  try {
    const auto &syntax = grammar.value->grammar;
    for (const auto &option : syntax.options)
      if (option.name == "generate_interpreter") {
        result.diagnostics.push_back(
            {Severity::Error, "document.forbidden_option",
             "generate_interpreter belongs in coge generation, not Ag options",
             SourceLocation{0, option.span.begin.offset,
                            option.span.end.offset},
             option.name});
        return result;
      }
    std::vector<SourceEdit> edits{
        {{0, 0, 0}, "sema " + syntax.grammarName + "; format 1;\n"}};
    std::map<std::string, std::size_t> counters;
    for (const auto &r : syntax.parserRules)
      for (const auto &a : r.alternatives) {
        const auto hash = alternativeSyntaxHash(r, a);
        const auto prefix = alternativeIdPrefix(syntax.grammarName, hash);
        const auto id = prefix + std::to_string(counters[prefix]++);
        const auto offset = a.span.end.offset;
        edits.push_back({{0, offset, offset},
                         "\n    analysis_status {\n      id \"" + id +
                             "\";\n      syntax_sha256 \"" + hash +
                             "\";\n      state pending;\n    }\n    analysis "
                             "{\n    }\n  "});
      }
    auto edited = applySourceEdits(source, std::move(edits));
    auto parsed = frontend.parse(edited.text);
    if (!parsed.value) {
      result.diagnostics = std::move(parsed.diagnostics);
      return result;
    }
    ProjectionResult projection;
    projection.text = std::move(edited.text);
    projection.origins = std::move(edited.origins);
    projection.sourceIdentity = agas::artifact::sha256Hex(source);
    projection.outputIdentity = parsed.value->identity();
    projection.grammarName = syntax.grammarName;
    projection.specificationName = syntax.grammarName;
    result.value = std::move(projection);
  } catch (const std::exception &error) {
    result.diagnostics.push_back(
        {Severity::Error, "template.invalid", error.what(), {}, {}});
  }
  return result;
}
} // namespace agsem
