#pragma once

#include "agas/model/GrammarDocument.h"
#include "agsem/Completeness.h"
#include "agsem/Diagnostics.h"
#include "agsem/SemanticInput.h"
#include <memory>

namespace agsem {
struct DocumentAccess;
enum class DocumentKind { Sema, Coge };
enum class DocumentOwner { Envelope, Ag, Sema, Coge };
enum class DocumentElementKind {
  Specification,
  Grammar,
  AgItem,
  SemanticModel,
  Analysis,
  SemanticObligation,
  ExecutionObligation,
  ExecutionModel,
  ExecutionContract,
  ExecutionResult,
  Lowering,
  BackendC,
  BackendLlvm,
  Generation
};
struct DocumentElement {
  DocumentElementKind kind;
  DocumentOwner owner;
  SourceLocation location;
  std::size_t ordinal{};
};

class ParsedDocument {
public:
  [[nodiscard]] auto kind() const -> DocumentKind;
  [[nodiscard]] auto formatVersion() const -> unsigned;
  [[nodiscard]] auto specificationName() const -> const std::string &;
  [[nodiscard]] auto identity() const -> const std::string &;
  [[nodiscard]] auto source() const -> const agas::model::SourceText &;
  [[nodiscard]] auto grammar() const -> const agas::model::GrammarDocument &;
  [[nodiscard]] auto elements() const -> const std::vector<DocumentElement> &;
  [[nodiscard]] auto analysisStatuses() const
      -> const std::vector<AnalysisStatus> &;

private:
  struct Impl;
  explicit ParsedDocument(std::shared_ptr<const Impl> data)
      : data_(std::move(data)) {}
  std::shared_ptr<const Impl> data_;
  friend struct DocumentAccess;
};

class SemaDocument {
public:
  [[nodiscard]] auto identity() const -> const std::string &;
  [[nodiscard]] auto source() const -> const agas::model::SourceText &;
  [[nodiscard]] auto grammar() const -> const agas::model::GrammarDocument &;
  [[nodiscard]] auto specificationName() const -> const std::string &;
  [[nodiscard]] auto semanticInput() const -> const SemanticInput &;

private:
  struct Impl;
  explicit SemaDocument(std::shared_ptr<const Impl> data)
      : data_(std::move(data)) {}
  std::shared_ptr<const Impl> data_;
  friend struct DocumentAccess;
};

[[nodiscard]] auto makeSemaDocument(const ParsedDocument &document)
    -> Outcome<SemaDocument>;
[[nodiscard]] auto validateDocumentSections(const ParsedDocument &document)
    -> std::vector<Diagnostic>;
} // namespace agsem
