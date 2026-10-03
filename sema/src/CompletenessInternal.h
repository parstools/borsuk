#pragma once
#include "AstAccess.h"
#include "agsem/Completeness.h"
#include "agsem/SemanticInput.h"
namespace agsem {
[[nodiscard]] auto readAnalysisStatus(const ast::Value &) -> AnalysisStatus;
[[nodiscard]] auto
incompleteAnalysisDiagnostics(const ast::Value &,
                              const agas::model::SyntaxDocument &)
    -> std::vector<Diagnostic>;
[[nodiscard]] auto executionTemplateDiagnostics(const ast::Value &,
                                                bool requireComplete)
    -> std::vector<Diagnostic>;
[[nodiscard]] auto generationDiagnostics(const CompletenessReport &,
                                         bool requireEmitter = true)
    -> std::vector<Diagnostic>;
struct AlternativeReview {
  std::string rule;
  std::size_t ordinal{};
  bool bodyChecked{}, hasResult{}, forwarding{}, noAction{}, representation{};
  std::string resultType;
  std::vector<std::pair<std::string, std::string>> inputs;
  std::vector<std::string> dependencies;
  std::vector<Diagnostic> diagnostics;
};
struct SemanticReview {
  bool canEmit{true};
  std::vector<AlternativeReview> alternatives;
  std::map<std::string, FunctionContract> interfaces;
  std::map<std::string, std::vector<std::string>> inputNames;
  std::map<std::string, bool> functions;
  std::vector<Diagnostic> diagnostics;
};
[[nodiscard]] auto reviewSemanticFragments(const SemanticInput &)
    -> SemanticReview;
} // namespace agsem
