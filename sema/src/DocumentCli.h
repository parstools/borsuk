#pragma once
#include "agsem/Projection.h"
#include "agsem/SemanticBinding.h"
#include "agsem/SemanticCompleteness.h"
#include <functional>
#include <set>
namespace agsem::cli {
enum class Operation {
  Check,
  CheckComplete,
  Calls,
  EmitAg,
  EmitSema,
  EmitRust,
  InspectModel,
  EmitTemplate
};
struct Artifacts {
  std::optional<ProjectionResult> projection;
  std::map<std::string, std::string> rust;
  std::set<std::string> calls;
  std::string model;
  std::string generationProvenance;
};
using Processor = std::function<Outcome<Artifacts>(
    const ParsedDocument &, const ContractEnvironment &,
    const DocumentFrontend &, Operation)>;
auto processSema(const ParsedDocument &, const ContractEnvironment &,
                 const DocumentFrontend &, Operation) -> Outcome<Artifacts>;
using TemplateProcessor = std::function<Outcome<ProjectionResult>(
    std::string_view, bool, const std::vector<std::string> &,
    const ContractEnvironment &, const DocumentFrontend &)>;
using CompletenessProcessor = std::function<Outcome<CompletenessReport>(
    const ParsedDocument &, const ContractEnvironment &,
    std::vector<CompletenessTarget>)>;
auto run(int argc, char **argv, std::string_view program,
         const Processor &processor, const TemplateProcessor &templates = {},
         const CompletenessProcessor &completeness = {}) -> int;
auto generationProvenance(const CheckedSemantics &) -> std::string;
void collectCalls(const agas::runtime::AstValue &, std::set<std::string> &);
} // namespace agsem::cli
