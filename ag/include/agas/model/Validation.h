#pragma once

#include <string>
#include <vector>

#include "agas/model/SyntaxModel.h"

namespace agas::model {

enum class DiagnosticSeverity { Warning, Error };

struct ValidationIssue {
  DiagnosticSeverity severity{};
  SourceSpan span;
  std::string message;

  auto operator==(const ValidationIssue &) const -> bool = default;
};

struct ValidationResult {
  std::vector<ValidationIssue> issues;

  [[nodiscard]] auto valid() const noexcept -> bool;
};

[[nodiscard]] auto validateSyntaxModel(const SyntaxDocument &document)
    -> ValidationResult;

} // namespace agas::model
