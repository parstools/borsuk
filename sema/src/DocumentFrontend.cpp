#include "agsem/DocumentFrontend.h"
#include "CompletenessInternal.h"
#include "DocumentStorage.h"
#include "ProjectionStorage.h"
#include "agas/artifact/Sha256.h"
#include "agas/model/Validation.h"
#include "agas/runtime/SourceText.h"
#include <set>

#include <algorithm>
#include <map>
#include <stdexcept>

namespace agsem {
namespace {
using Value = agas::runtime::AstValue;
using Kind = agas::runtime::AstValueKind;
using Span = agas::runtime::InputSpan;

auto field(const Value &value, std::string_view name) -> const Value & {
  const auto found = std::ranges::find(value.fieldNames, name);
  if (found == value.fieldNames.end())
    throw std::runtime_error("missing document field: " + std::string{name} +
                             " in " + value.typeName);
  return value.elements.at(
      static_cast<std::size_t>(found - value.fieldNames.begin()));
}

auto container(Kind kind) -> Value {
  Value value;
  value.kind = kind;
  return value;
}

void collectActions(const Value &value, std::vector<Span> &removed) {
  if (value.typeName == "analysisBlock" ||
      value.typeName == "executionResult" ||
      value.typeName == "analysisStatus") {
    removed.push_back(value.recognizedSpan);
    return;
  }
  for (const auto &child : value.elements)
    collectActions(child, removed);
}

void flattenAlternative(const Value &value, std::vector<Value> &items) {
  if (value.typeName == "alternativeSequence") {
    items.push_back(field(value, "item"));
    for (const auto &rest : field(value, "rest").elements)
      flattenAlternative(rest, items);
  } else if (value.kind == Kind::List) {
    for (const auto &item : value.elements)
      flattenAlternative(item, items);
  } else
    items.push_back(value);
}

void normalizeAlternatives(Value &value) {
  if (value.typeName == "parserAlternative" ||
      value.typeName == "alternativeSequence") {
    auto elements = container(Kind::List);
    auto label = container(Kind::Optional);
    auto actions = container(Kind::List);
    std::vector<Value> items;
    if (value.typeName == "alternativeSequence") {
      flattenAlternative(value, items);
      value.typeName = "parserAlternative";
      value.variantName = "NonemptyAlternative";
    } else
      flattenAlternative(field(value, "items"), items);
    for (const auto &item : items) {
      if (item.typeName == "alternativeLabel") {
        if (!label.elements.empty() || !actions.elements.empty())
          throw std::runtime_error(
              "alternative label must occur once, before actions");
        label.elements.push_back(item);
      } else if (item.typeName == "analysisBlock" ||
                 item.typeName == "executionResult" ||
                 item.typeName == "analysisStatus") {
        actions.elements.push_back(item);
      } else {
        if (value.variantName == "EmptyAlternative" ||
            !label.elements.empty() || !actions.elements.empty())
          throw std::runtime_error(
              "grammar symbols must precede the label and actions");
        elements.elements.push_back(item);
      }
    }
    if (value.variantName == "NonemptyAlternative" && elements.elements.empty())
      throw std::runtime_error("an empty alternative must use empty");
    value.fieldNames = {"elements", "label", "actions"};
    value.elements = {std::move(elements), std::move(label),
                      std::move(actions)};
    return;
  }
  for (auto &child : value.elements)
    normalizeAlternatives(child);
}

} // namespace

DocumentFrontend::DocumentFrontend(const std::filesystem::path &documentPackage,
                                   const std::filesystem::path &agPackage)
    : package_(agas::artifact::loadArtifactPackageDirectory(documentPackage)),
      lexer_(package_.lexer, package_.symbols.terminals.size(),
             package_.symbols.channels.size()),
      parser_(package_.parserTable, package_.symbols, package_.productions,
              package_.reductions),
      ag_(agPackage) {}

auto DocumentFrontend::parse(std::string_view source) const
    -> Outcome<ParsedDocument> {
  Outcome<ParsedDocument> result;
  Span location{0, source.size()};
  try {
    auto parsed = parser_.parse(source, lexer_);
    if (!parsed.accepted()) {
      result.diagnostics.push_back(
          {Severity::Error,
           "document.syntax",
           parsed.error->message,
           SourceLocation{0, parsed.error->span.beginByte,
                          parsed.error->span.endByte},
           {}});
      return result;
    }
    normalizeAlternatives(*parsed.root);
    const auto &root = *parsed.root;
    const auto &header = field(root, "specification");
    location = header.recognizedSpan;
    unsigned version = 1;
    const auto &versions = field(header, "version").elements;
    if (!versions.empty()) {
      const auto &number = field(versions.front(), "version");
      location = number.sourceSpan;
      if (number.tokenText != "1")
        throw std::runtime_error("unsupported document format version: " +
                                 number.tokenText);
    }
    DocumentAccess::ParsedData document;
    document.identity = agas::artifact::sha256Hex(source);
    document.kindLocation = {0, field(header, "kind").sourceSpan.beginByte,
                             field(header, "kind").sourceSpan.endByte};
    document.elements.push_back(
        {DocumentElementKind::Specification,
         DocumentOwner::Envelope,
         {0, header.recognizedSpan.beginByte, header.recognizedSpan.endByte},
         0});
    const auto &grammarHeader = field(root, "header");
    document.elements.push_back({DocumentElementKind::Grammar,
                                 DocumentOwner::Ag,
                                 {0, grammarHeader.recognizedSpan.beginByte,
                                  grammarHeader.recognizedSpan.endByte},
                                 1});
    document.kind = field(header, "kind").tokenText == "sema"
                        ? DocumentKind::Sema
                        : DocumentKind::Coge;
    document.formatVersion = version;
    document.specificationName = field(header, "name").tokenText;
    document.source = agas::runtime::captureSourceText(
        std::string{source},
        lexer_.tokenize(source, package_.parserTable, package_.productions));
    auto &semantic = document.semanticRoot;
    semantic = container(Kind::Node);
    semantic.typeName = "document";
    semantic.sourceSpan = root.sourceSpan;
    semantic.recognizedSpan = root.recognizedSpan;
    const std::map<std::string, std::string> extensions{
        {"semanticModel", "model"},
        {"executionModel", "execution"},
        {"loweringModel", "lowering"},
        {"cBackend", "backendC"},
        {"llvmBackend", "backendLlvm"},
        {"executionContract", "contract"},
        {"generationOptions", "settings"},
        {"executionObligations", "obligations"}};
    std::map<std::string, Value> sections;
    for (const auto &[type, name] : extensions)
      sections.emplace(name, container(Kind::Optional));
    sections.emplace("header", container(Kind::Optional));
    sections.emplace("legacy", container(Kind::Optional));
    sections.emplace("rules", container(Kind::List));
    std::vector<Span> removed{header.recognizedSpan};
    const std::map<std::string, DocumentElementKind> elementKinds{
        {"semanticModel", DocumentElementKind::SemanticModel},
        {"executionModel", DocumentElementKind::ExecutionModel},
        {"loweringModel", DocumentElementKind::Lowering},
        {"cBackend", DocumentElementKind::BackendC},
        {"llvmBackend", DocumentElementKind::BackendLlvm},
        {"executionContract", DocumentElementKind::ExecutionContract},
        {"generationOptions", DocumentElementKind::Generation},
        {"executionObligations", DocumentElementKind::ExecutionObligation}};
    const auto indexActions = [&](auto &&self, const Value &value) -> void {
      if (value.typeName == "analysisBlock" ||
          value.typeName == "executionResult" ||
          value.typeName == "analysisStatus") {
        const bool status = value.typeName == "analysisStatus";
        const bool analysis = value.typeName == "analysisBlock" || status;
        document.elements.push_back(
            {status     ? DocumentElementKind::SemanticObligation
             : analysis ? DocumentElementKind::Analysis
                        : DocumentElementKind::ExecutionResult,
             analysis ? DocumentOwner::Sema : DocumentOwner::Coge,
             {0, value.recognizedSpan.beginByte, value.recognizedSpan.endByte},
             document.elements.size()});
        return;
      }
      for (const auto &child : value.elements)
        self(self, child);
    };
    for (const auto &item : field(root, "items").elements) {
      const auto kind = elementKinds.find(item.typeName);
      document.elements.push_back(
          {kind == elementKinds.end() ? DocumentElementKind::AgItem
                                      : kind->second,
           kind == elementKinds.end()         ? DocumentOwner::Ag
           : item.typeName == "semanticModel" ? DocumentOwner::Sema
                                              : DocumentOwner::Coge,
           {0, item.recognizedSpan.beginByte, item.recognizedSpan.endByte},
           document.elements.size()});
      if (item.typeName == "parserRuleSpec")
        indexActions(indexActions, item);
      location = item.recognizedSpan;
      if (const auto extension = extensions.find(item.typeName);
          extension != extensions.end()) {
        auto &section = sections.at(extension->second);
        if (!section.elements.empty())
          throw std::runtime_error("duplicate document section: " +
                                   item.typeName);
        section.elements.push_back(item);
        removed.push_back(item.recognizedSpan);
      } else if (item.typeName == "parserRuleSpec") {
        sections.at("rules").elements.push_back(item);
        collectActions(item, removed);
      }
    }
    for (auto &[name, value] : sections) {
      semantic.fieldNames.push_back(name);
      semantic.elements.push_back(std::move(value));
    }
    std::ranges::sort(removed, {}, &Span::beginByte);
    const auto originalOffset = [&](std::uint64_t offset) {
      for (const auto span : removed) {
        if (span.beginByte > offset)
          break;
        offset += span.endByte - span.beginByte;
      }
      return offset;
    };
    std::vector<SourceEdit> edits;
    for (const auto span : removed)
      edits.push_back({{0, span.beginByte, span.endByte}, {}});
    const auto projection = applySourceEdits(source, std::move(edits)).text;
    auto grammar = ag_.parseDocument(projection);
    if (!grammar.document) {
      const auto &issue = grammar.issues.front();
      location = {originalOffset(issue.span.beginByte),
                  originalOffset(issue.span.endByte)};
      throw std::runtime_error("Ag projection is invalid: " + issue.message);
    }
    const auto validation =
        agas::model::validateSyntaxModel(grammar.document->grammar);
    if (!validation.valid()) {
      const auto issue = std::ranges::find(
          validation.issues, agas::model::DiagnosticSeverity::Error,
          &agas::model::ValidationIssue::severity);
      location = {originalOffset(issue->span.begin.offset),
                  originalOffset(issue->span.end.offset)};
      throw std::runtime_error("invalid embedded grammar: " + issue->message);
    }
    for (const auto &option : grammar.document->grammar.options)
      if (option.name == "generate_interpreter")
        document.forbiddenOptions.push_back(
            {0, originalOffset(option.span.begin.offset),
             originalOffset(option.span.end.offset)});
    std::map<std::string, SourceLocation> alternativeIds;
    const auto &authoredRules = field(document.semanticRoot, "rules").elements;
    for (std::size_t ri = 0; ri < authoredRules.size(); ++ri) {
      const auto &r = grammar.document->grammar.parserRules.at(ri);
      std::vector<const Value *> alternatives{
          &field(authoredRules[ri], "first")};
      for (const auto &a : field(authoredRules[ri], "rest").elements)
        alternatives.push_back(&a);
      for (std::size_t ai = 0; ai < alternatives.size(); ++ai) {
        // Ag may fold an unannotated forwarding alternative to its
        // parserElement.
        const auto *actions = ast::find(*alternatives[ai], "actions");
        if (!actions)
          continue;
        std::optional<SourceLocation> previousStatus;
        for (const auto &action : actions->elements) {
          if (action.typeName != "analysisStatus")
            continue;
          location = action.recognizedSpan;
          if (previousStatus) {
            result.diagnostics.push_back({Severity::Error,
                                          "completeness.invalid_metadata",
                                          "duplicate analysis_status",
                                          ast::location(action),
                                          {},
                                          {*previousStatus}});
            return result;
          }
          previousStatus = ast::location(action);
          auto status = readAnalysisStatus(action);
          const auto [previous, inserted] =
              alternativeIds.emplace(status.id.value, status.location);
          if (!inserted) {
            result.diagnostics.push_back(
                {Severity::Error,
                 "completeness.duplicate_alternative_id",
                 "duplicate AlternativeId",
                 status.location,
                 status.id.value,
                 {previous->second}});
            return result;
          }
          status.rule = r.name;
          status.label = r.alternatives.at(ai).label;
          status.ordinal = ai;
          status.alternative = {0, alternatives[ai]->recognizedSpan.beginByte,
                                alternatives[ai]->recognizedSpan.endByte};
          status.currentSyntaxSha256 =
              alternativeSyntaxHash(r, r.alternatives.at(ai));
          document.analysisStatuses.push_back(std::move(status));
        }
      }
    }
    result.diagnostics =
        executionTemplateDiagnostics(document.semanticRoot, false);
    if (!result.diagnostics.empty())
      return result;
    document.grammar = std::move(*grammar.document);
    result.value = DocumentAccess::parsed(std::move(document));
  } catch (const agas::runtime::ArtifactLexerError &error) {
    result.diagnostics.push_back(
        {Severity::Error,
         "document.lexical",
         error.what(),
         SourceLocation{0, error.tokenStart(), error.errorOffset()},
         {}});
  } catch (const std::runtime_error &error) {
    result.diagnostics.push_back(
        {Severity::Error,
         std::string{error.what()}.starts_with("completeness.")
             ? std::string{error.what()}.substr(
                   0, std::string{error.what()}.find(':'))
             : "document.invalid",
         error.what(),
         SourceLocation{0, location.beginByte, location.endByte},
         {}});
  }
  return result;
}

auto DocumentFrontend::parseAg(std::string_view source) const
    -> Outcome<agas::model::GrammarDocument> {
  Outcome<agas::model::GrammarDocument> result;
  try {
    auto parsed = ag_.parseDocument(source);
    for (const auto &issue : parsed.issues)
      result.diagnostics.push_back(
          {Severity::Error,
           "template.invalid_ag",
           issue.message,
           SourceLocation{0, issue.span.beginByte, issue.span.endByte},
           {}});
    if (!parsed.document)
      return result;
    const auto validation =
        agas::model::validateSyntaxModel(parsed.document->grammar);
    for (const auto &issue : validation.issues)
      if (issue.severity == agas::model::DiagnosticSeverity::Error)
        result.diagnostics.push_back(
            {Severity::Error,
             "template.invalid_ag",
             issue.message,
             SourceLocation{0, issue.span.begin.offset, issue.span.end.offset},
             {}});
    if (result.diagnostics.empty())
      result.value = std::move(*parsed.document);
  } catch (const std::exception &error) {
    result.diagnostics.push_back(
        {Severity::Error, "template.invalid_ag", error.what(), {}, {}});
  }
  return result;
}

} // namespace agsem
