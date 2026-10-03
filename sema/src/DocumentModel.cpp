#include "AstAccess.h"
#include "DocumentStorage.h"
#include "SemanticInputStorage.h"
#include <set>

namespace agsem {
auto ParsedDocument::kind() const -> DocumentKind { return data_->kind; }
auto ParsedDocument::formatVersion() const -> unsigned {
  return data_->formatVersion;
}
auto ParsedDocument::specificationName() const -> const std::string & {
  return data_->specificationName;
}
auto ParsedDocument::identity() const -> const std::string & {
  return data_->identity;
}
auto ParsedDocument::source() const -> const agas::model::SourceText & {
  return data_->source;
}
auto ParsedDocument::grammar() const -> const agas::model::GrammarDocument & {
  return data_->grammar;
}
auto ParsedDocument::elements() const -> const std::vector<DocumentElement> & {
  return data_->elements;
}
auto ParsedDocument::analysisStatuses() const
    -> const std::vector<AnalysisStatus> & {
  return data_->analysisStatuses;
}
auto DocumentAccess::parsed(ParsedData data) -> ParsedDocument {
  return ParsedDocument{std::make_shared<const ParsedData>(std::move(data))};
}
auto DocumentAccess::root(const ParsedDocument &document)
    -> const ast::Value & {
  return document.data_->semanticRoot;
}
auto DocumentAccess::forbiddenOptions(const ParsedDocument &document)
    -> const std::vector<SourceLocation> & {
  return document.data_->forbiddenOptions;
}
auto DocumentAccess::kindLocation(const ParsedDocument &document)
    -> SourceLocation {
  return document.data_->kindLocation;
}
auto SemaDocument::identity() const -> const std::string & {
  return data_->identity;
}
auto SemaDocument::source() const -> const agas::model::SourceText & {
  return data_->source;
}
auto SemaDocument::grammar() const -> const agas::model::GrammarDocument & {
  return data_->grammar;
}
auto SemaDocument::specificationName() const -> const std::string & {
  return data_->specificationName;
}
auto SemaDocument::semanticInput() const -> const SemanticInput & {
  return data_->input;
}

auto DocumentAccess::sema(const ParsedDocument &document) -> SemaDocument {
  auto root = DocumentAccess::root(document);
  std::vector<ForeignSymbol> foreign;
  if (const auto *execution = ast::optional(ast::field(root, "execution")))
    for (const auto &declaration :
         ast::field(*execution, "declarations").elements)
      if (const auto *name = ast::find(declaration, "name"))
        foreign.push_back(
            {ast::token(*name),
             declaration.typeName == "typeAlias" ||
                 declaration.typeName == "recordDeclaration" ||
                 declaration.typeName == "executionEnumDeclaration",
             ast::location(declaration)});
  for (const auto *name : {"execution", "contract", "lowering", "backendC",
                           "backendLlvm", "settings", "obligations"})
    ast::field(root, name).elements.clear();
  const auto removeActions = [&](auto &&self, ast::Value &value) -> void {
    if (value.typeName == "parserAlternative") {
      auto &actions = ast::field(value, "actions").elements;
      std::erase_if(actions, [](const auto &item) {
        return item.typeName == "executionResult";
      });
    }
    for (auto &child : value.elements)
      self(self, child);
  };
  removeActions(removeActions, root);
  return SemaDocument{std::make_shared<const SemaDocument::Impl>(
      document.identity(), document.specificationName(), document.source(),
      document.grammar(),
      SemanticInputAccess::fromAst(root, document.grammar().grammar,
                                   document.identity()),
      std::move(foreign))};
}

auto DocumentAccess::foreign(const SemaDocument &document)
    -> const std::vector<ForeignSymbol> & {
  return document.data_->foreign;
}

auto validateDocumentSections(const ParsedDocument &document)
    -> std::vector<Diagnostic> {
  std::vector<Diagnostic> errors;
  if (document.kind() == DocumentKind::Sema)
    for (const auto &element : document.elements())
      if (element.owner == DocumentOwner::Coge)
        errors.push_back({Severity::Error,
                          "document.forbidden_section",
                          "execution construction is not allowed in sema",
                          element.location,
                          {}});
  const auto &root = DocumentAccess::root(document);
  const auto *settings = ast::optional(ast::field(root, "settings"));
  if (settings) {
    std::set<std::string> names;
    for (const auto &entry : ast::field(*settings, "entries").elements) {
      const auto name = ast::token(ast::field(entry, "name"));
      if (name != "generate_interpreter")
        errors.push_back({Severity::Error, "document.unknown_generation_option",
                          "unknown generation option: " + name,
                          ast::location(entry), name});
      else if (!names.insert(name).second ||
               ast::token(ast::field(entry, "value")) != "true")
        errors.push_back(
            {Severity::Error, "document.invalid_generation_option",
             "generate_interpreter must occur once with value true",
             ast::location(entry), name});
    }
  }
  for (const auto &source : DocumentAccess::forbiddenOptions(document))
    errors.push_back(
        {Severity::Error, "document.forbidden_option",
         "generate_interpreter belongs in coge generation, not Ag options",
         source, "generate_interpreter"});
  return errors;
}

auto makeSemaDocument(const ParsedDocument &document) -> Outcome<SemaDocument> {
  Outcome<SemaDocument> result;
  if (document.kind() != DocumentKind::Sema)
    result.diagnostics.push_back(
        {Severity::Error,
         "document.wrong_kind",
         "expected sema header; use coge for this document",
         DocumentAccess::kindLocation(document),
         {}});
  auto errors = validateDocumentSections(document);
  result.diagnostics.insert(result.diagnostics.end(), errors.begin(),
                            errors.end());
  if (result.diagnostics.empty())
    result.value = DocumentAccess::sema(document);
  return result;
}
} // namespace agsem
