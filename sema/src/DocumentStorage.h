#pragma once
#include "agas/runtime/ReductionRuntime.h"
#include "agsem/DocumentModel.h"
namespace agsem {
struct ParsedDocument::Impl {
  DocumentKind kind;
  unsigned formatVersion{1};
  std::string specificationName;
  std::string identity;
  agas::model::SourceText source;
  agas::model::GrammarDocument grammar;
  agas::runtime::AstValue semanticRoot;
  std::vector<DocumentElement> elements;
  std::vector<AnalysisStatus> analysisStatuses;
  SourceLocation kindLocation;
  std::vector<SourceLocation> forbiddenOptions;
};
struct ForeignSymbol {
  std::string name;
  bool type;
  SourceLocation location;
};
struct SemaDocument::Impl {
  std::string identity;
  std::string specificationName;
  agas::model::SourceText source;
  agas::model::GrammarDocument grammar;
  SemanticInput input;
  std::vector<ForeignSymbol> foreign;
};
struct DocumentAccess {
  using ParsedData = ParsedDocument::Impl;
  static auto parsed(ParsedData data) -> ParsedDocument;
  static auto root(const ParsedDocument &document)
      -> const agas::runtime::AstValue &;
  static auto forbiddenOptions(const ParsedDocument &document)
      -> const std::vector<SourceLocation> &;
  static auto kindLocation(const ParsedDocument &document) -> SourceLocation;
  static auto sema(const ParsedDocument &document) -> SemaDocument;
  static auto foreign(const SemaDocument &document)
      -> const std::vector<ForeignSymbol> &;
};
} // namespace agsem
