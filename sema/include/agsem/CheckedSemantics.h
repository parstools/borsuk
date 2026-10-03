#pragma once

#include "agas/model/SyntaxModel.h"
#include "agas/runtime/ReductionRuntime.h"
#include "agsem/Diagnostics.h"
#include "agsem/ModelBindings.h"
#include "agsem/SemanticInput.h"

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace agsem {

class CheckedSemantics;

struct CheckedEnumConstant {
  std::string type;
  std::string variant;
  std::string rust;
  SourceLocation source;
};

struct CheckedCollectionOperation {
  std::string operation;
  std::string elementType;
  std::string resultType;
  std::vector<std::string> operandTypes;
  std::vector<std::string> operandOwnership;
  std::string ownership;
  std::string rustElementType;
  std::string rust;
  SourceLocation source;
};

[[nodiscard]] auto checkedCollectionOperations(const CheckedSemantics &model)
    -> const std::vector<CheckedCollectionOperation> &;

[[nodiscard]] auto checkedEnumConstants(const CheckedSemantics &model)
    -> const std::vector<CheckedEnumConstant> &;
// Inspection covers semantic interfaces and checked semantic expansions.
[[nodiscard]] auto inspectSemanticModel(const CheckedSemantics &model) -> std::string;

struct SemanticRustFiles {
  std::string sema;
  std::string semaLib;
  std::map<std::string, std::string> modules;
};

class SemanticPreparationError : public std::runtime_error {
public:
  explicit SemanticPreparationError(std::vector<Diagnostic> diagnostics)
      : std::runtime_error(diagnostics.empty() ? "semantic preparation failed"
                                               : diagnostics.front().message),
        diagnostics_(std::move(diagnostics)) {}

  [[nodiscard]] auto diagnostics() const -> const std::vector<Diagnostic> & {
    return diagnostics_;
  }

private:
  std::vector<Diagnostic> diagnostics_;
};

[[nodiscard]] auto semanticInput(const CheckedSemantics &model)
    -> const SemanticInput &;
[[nodiscard]] auto semanticContextType(const CheckedSemantics &model)
    -> std::string_view;
[[nodiscard]] auto emitCheckedSemanticsRust(const CheckedSemantics &model)
    -> SemanticRustFiles;

} // namespace agsem
