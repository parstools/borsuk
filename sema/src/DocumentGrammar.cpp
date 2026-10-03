#include "agsem/DocumentGrammar.h"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace agsem {
namespace {
using namespace agas::model;

auto reference(std::string name, std::optional<std::string> field = {},
               Quantifier quantifier = Quantifier::One) -> ParserElement {
  return {std::move(field),
          {ParserSymbolKind::RuleReference, std::move(name), {}, {}},
          quantifier,
          {}};
}

auto rule(SyntaxDocument &document, std::string_view name) -> ParserRule & {
  const auto found =
      std::ranges::find(document.parserRules, name, &ParserRule::name);
  if (found == document.parserRules.end())
    throw std::runtime_error("missing grammar extension point: " +
                             std::string{name});
  return *found;
}
} // namespace

auto composeDocumentGrammar(const SyntaxDocument &ag,
                            const SyntaxDocument &actions,
                            const SyntaxDocument &envelope) -> SyntaxDocument {
  SyntaxDocument result = ag;
  result.grammarName = "SemaDocument";
  result.options = envelope.options;
  result.lexerClasses = actions.lexerClasses;
  result.lexerClasses.push_back(
      {"AG", {OptionValueKind::Boolean, "true", {}}, {}});
  std::set<std::string> baseRules;
  for (const auto &entry : ag.parserRules)
    baseRules.insert(entry.name);
  std::set<std::string> ignored{"document", "semaDeclaration",
                                "legacyGrammarDeclaration"};
  for (auto entry : actions.parserRules) {
    if (ignored.contains(entry.name))
      continue;
    if (entry.name == "identifier")
      entry.name = "actionIdentifier";
    else if (entry.name == "optionEntry")
      entry.name = "generationOptionEntry";
    else if (baseRules.contains(entry.name))
      continue;
    for (auto &alternative : entry.alternatives)
      for (auto &element : alternative.elements) {
        if (element.symbol.name == "identifier")
          element.symbol.name = "actionIdentifier";
        if (element.symbol.name == "NOT")
          element.symbol.name = "ACTION_NOT";
        if (element.symbol.name == "ARROW")
          element.symbol.name = "RARROW";
        if (element.symbol.name == "STRING_LITERAL") {
          element.symbol.name = "actionString";
          element.symbol.kind = ParserSymbolKind::RuleReference;
        }
      }
    if (!entry.lexerContext.empty())
      entry.lexerContext.push_back({"disable", "AG", {}});
    result.parserRules.push_back(std::move(entry));
  }
  for (const auto &entry : envelope.parserRules) {
    if (entry.name == "document")
      rule(result, "document") = entry;
    else
      result.parserRules.push_back(entry);
  }
  for (const auto *name : {"semanticModel", "executionModel", "loweringModel",
                           "cBackend", "llvmBackend", "executionContract",
                           "generationOptions", "executionObligations"})
    rule(result, "topLevelItem")
        .alternatives.push_back({false, {reference(name, "value")}, {}, {}});
  // Parse extension suffixes as items so their common identifier prefixes are
  // shifted before deciding between an Ag symbol and an action. The document
  // adapter checks label/action ordering and restores the canonical Ag shape.
  for (auto &alternative : rule(result, "parserAlternative").alternatives) {
    const bool empty = alternative.label == "EmptyAlternative";
    if (empty) {
      alternative.elements.back() =
          reference("alternativeSuffix", "items", Quantifier::ZeroOrMore);
    } else {
      alternative.elements.front() = reference("alternativeSequence", "items");
      alternative.elements.pop_back();
    }
  }
  ParserRule sequence{TreeModifier::Node, "alternativeSequence", {}, {}};
  sequence.alternatives.push_back(
      {false,
       {reference("parserElement", "item"),
        reference("alternativeSequence", "rest", Quantifier::Optional)},
       "Element",
       {}});
  sequence.alternatives.push_back(
      {false,
       {reference("alternativeLabel", "item"),
        reference("ruleAction", "rest", Quantifier::ZeroOrMore)},
       "Label",
       {}});
  sequence.alternatives.push_back(
      {false,
       {reference("ruleAction", "item"),
        reference("ruleAction", "rest", Quantifier::ZeroOrMore)},
       "Action",
       {}});
  result.parserRules.push_back(std::move(sequence));
  ParserRule suffix{TreeModifier::Inline, "alternativeSuffix", {}, {}};
  for (const auto *child : {"alternativeLabel", "ruleAction"})
    suffix.alternatives.push_back({false, {reference(child, "value")}, {}, {}});
  result.parserRules.push_back(std::move(suffix));
  // Expand the optional label without changing the language. This avoids an
  // epsilon reduction before the shared `execution` identifier prefix.
  auto &element = rule(result, "parserElement");
  auto labeled = element.alternatives.front();
  labeled.elements.front().quantifier = Quantifier::One;
  labeled.label = "Labeled";
  auto bare = element.alternatives.front();
  bare.elements.erase(bare.elements.begin());
  bare.label = "Bare";
  element.alternatives = {std::move(labeled), std::move(bare)};

  // Extension words remain usable as Ag rule references and field names.
  ParserRule names{TreeModifier::Inline, "agRuleReference", {}, {}};
  std::set<std::string> baseTokens;
  for (const auto &entry : ag.lexerRules)
    baseTokens.insert(entry.name);
  std::vector<LexerRule> extraTokens;
  for (auto entry : actions.lexerRules) {
    if (entry.name == "NOT")
      entry.name = "ACTION_NOT";
    if (entry.name == "ARROW" || baseTokens.contains(entry.name))
      continue;
    if (entry.fragment && entry.name == "SingleStringCharacter")
      continue;
    extraTokens.push_back(std::move(entry));
  }
  for (const auto &entry : envelope.lexerRules)
    extraTokens.push_back(entry);
  for (const auto &entry : extraTokens)
    if (!entry.fragment && entry.commands.empty() &&
        !entry.alternatives.empty()) {
      const auto &elements = entry.alternatives.front().elements;
      if (elements.size() == 1 &&
          elements.front().atom.kind == LexerAtomKind::Literal &&
          elements.front().atom.spelling.size() > 2 &&
          elements.front().atom.spelling[1] >= 'a' &&
          elements.front().atom.spelling[1] <= 'z')
        names.alternatives.push_back(
            {false,
             {{"value",
               {ParserSymbolKind::TokenReference, entry.name, {}, {}},
               Quantifier::One,
               {}}},
             {},
             {}});
    }
  names.alternatives.push_back(
      {false,
       {{"value",
         {ParserSymbolKind::TokenReference, "RULE_REF", {}, {}},
         Quantifier::One,
         {}}},
       {},
       {}});
  for (auto &entry : result.parserRules)
    if (baseRules.contains(entry.name))
      for (auto &alternative : entry.alternatives)
        for (auto &element : alternative.elements)
          if (element.symbol.name == "RULE_REF") {
            element.symbol.name = "agRuleReference";
            element.symbol.kind = ParserSymbolKind::RuleReference;
          }
  result.parserRules.push_back(std::move(names));

  // Ag-only and envelope keywords were ordinary identifiers in the action
  // language. Keep them usable there after combining the lexer alphabets.
  std::set<std::string> actionTokens;
  for (const auto &entry : actions.lexerRules)
    actionTokens.insert(entry.name);
  const auto preserveActionName = [&](const LexerRule &entry) {
    if (entry.fragment || actionTokens.contains(entry.name) ||
        entry.alternatives.size() != 1)
      return;
    const auto &elements = entry.alternatives.front().elements;
    if (elements.size() != 1 ||
        elements.front().atom.kind != LexerAtomKind::Literal)
      return;
    const auto &spelling = elements.front().atom.spelling;
    if (spelling.size() > 2 && spelling[1] >= 'a' && spelling[1] <= 'z')
      rule(result, "actionIdentifier")
          .alternatives.push_back(
              {false,
               {{"value",
                 {ParserSymbolKind::TokenReference, entry.name, {}, {}},
                 Quantifier::One,
                 {}}},
               {},
               {}});
  };
  for (const auto &entry : ag.lexerRules)
    preserveActionName(entry);
  for (const auto &entry : envelope.lexerRules)
    preserveActionName(entry);

  // Single quoted Ag literals keep their canonical rule; double quoted text
  // is available only in the action/model lexer class.
  const auto strings =
      std::ranges::find(actions.lexerRules, "STRING_LITERAL", &LexerRule::name);
  if (strings == actions.lexerRules.end() || strings->alternatives.size() != 2)
    throw std::runtime_error("missing action string extension");
  LexerRule doubleString{false,
                         "ACTION_STRING_LITERAL",
                         {strings->alternatives.back()},
                         {{"require", "ACTION", {}}},
                         {}};
  extraTokens.push_back(std::move(doubleString));
  ParserRule stringRule{TreeModifier::Inline, "actionString", {}, {}};
  for (const auto *name : {"STRING_LITERAL", "ACTION_STRING_LITERAL"})
    stringRule.alternatives.push_back(
        {false,
         {{"value",
           {ParserSymbolKind::TokenReference, name, {}, {}},
           Quantifier::One,
           {}}},
         {},
         {}});
  result.parserRules.push_back(std::move(stringRule));
  for (auto &entry : result.lexerRules)
    if (entry.name == "LEXER_CHAR_SET")
      entry.commands.push_back({"require", "AG", {}});
  // Keywords must win equal-length matches against generic identifiers.
  result.lexerRules.insert(result.lexerRules.begin(), extraTokens.begin(),
                           extraTokens.end());
  return result;
}

} // namespace agsem
