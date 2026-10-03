#include "agas/model/BnfLowering.h"

#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include "agas/model/Validation.h"
#include "ebnf/EbnfGrammar.h"
#include "regex/RegexParser.h"

namespace agas::model {
namespace {

auto repetitionOf(Quantifier quantifier) -> zbik::Repetition {
  switch (quantifier) {
  case Quantifier::One:
    return zbik::Repetition::One;
  case Quantifier::Optional:
    return zbik::Repetition::Optional;
  case Quantifier::ZeroOrMore:
    return zbik::Repetition::ZeroOrMore;
  case Quantifier::OneOrMore:
    return zbik::Repetition::OneOrMore;
  }
  throw std::logic_error("unknown parser quantifier");
}

auto appendExactCodePoints(const zbik::RegexAst &expression,
                           std::vector<zbik::CodePoint> &result) -> bool {
  if (expression.kind() == zbik::RegexAst::Kind::Epsilon) {
    return true;
  }
  if (expression.kind() == zbik::RegexAst::Kind::CodePointClass) {
    const auto &ranges = expression.codePoints().ranges();
    if (ranges.size() != 1 || ranges.front().first != ranges.front().last) {
      return false;
    }
    result.push_back(ranges.front().first);
    return true;
  }
  if (expression.kind() == zbik::RegexAst::Kind::Concatenation) {
    for (const zbik::RegexAst &element : expression.elements()) {
      if (!appendExactCodePoints(element, result)) {
        return false;
      }
    }
    return true;
  }
  return false;
}

auto literalCodePoints(std::string_view spelling)
    -> std::optional<std::vector<zbik::CodePoint>> {
  const zbik::RegexAst expression = zbik::RegexParser{}.parse(spelling);
  std::vector<zbik::CodePoint> result;
  if (!appendExactCodePoints(expression, result)) {
    return std::nullopt;
  }
  return result;
}

auto exactLexerLiteral(const LexerRule &rule)
    -> std::optional<std::vector<zbik::CodePoint>> {
  if (rule.fragment || !rule.commands.empty() ||
      rule.alternatives.size() != 1 ||
      rule.alternatives.front().explicitEmpty) {
    return std::nullopt;
  }
  std::vector<zbik::CodePoint> result;
  for (const LexerElement &element : rule.alternatives.front().elements) {
    if (element.quantifier != LexerQuantifier::One || element.lazy ||
        element.atom.kind != LexerAtomKind::Literal) {
      return std::nullopt;
    }
    const auto literal = literalCodePoints(element.atom.spelling);
    if (!literal) {
      return std::nullopt;
    }
    result.insert(result.end(), literal->begin(), literal->end());
  }
  return result;
}

class TerminalInterner {
public:
  explicit TerminalInterner(const SyntaxDocument &document) {
    for (const LexerRule &rule : document.lexerRules) {
      if (rule.fragment) {
        continue;
      }
      names_.emplace(rule.name, rule.name);
      bindings_.push_back({rule.name, TerminalBindingKind::LexerRule, rule.name,
                           std::nullopt, rule.span});
      if (const auto literal = exactLexerLiteral(rule)) {
        literalNames_.try_emplace(*literal, rule.name);
      }
    }
  }

  auto nameFor(const ParserSymbol &symbol) -> std::string {
    if (symbol.kind == ParserSymbolKind::TokenReference) {
      return symbol.name;
    }
    if (symbol.kind == ParserSymbolKind::Literal) {
      if (const auto literal = literalCodePoints(symbol.name)) {
        if (const auto existing = literalNames_.find(*literal);
            existing != literalNames_.end()) {
          return existing->second;
        }
      }
      return intern("literal:" + symbol.name, TerminalBindingKind::Literal,
                    symbol.name, std::nullopt, symbol.span);
    }
    if (symbol.kind == ParserSymbolKind::QualifiedReference) {
      const std::string key =
          "qualified:" + *symbol.qualifier + "." + symbol.name;
      return intern(key, TerminalBindingKind::QualifiedReference, symbol.name,
                    symbol.qualifier, symbol.span);
    }
    return symbol.name;
  }

  auto bindings() const noexcept -> const std::vector<TerminalBinding> & {
    return bindings_;
  }

  auto takeBindings() && -> std::vector<TerminalBinding> {
    return std::move(bindings_);
  }

private:
  auto intern(std::string key, TerminalBindingKind kind, std::string spelling,
              std::optional<std::string> qualifier, const SourceSpan &span)
      -> std::string {
    if (const auto existing = names_.find(key); existing != names_.end()) {
      return existing->second;
    }
    const std::string internalName =
        "__agas_terminal_" + std::to_string(generatedCount_++);
    names_.emplace(std::move(key), internalName);
    bindings_.push_back(
        {internalName, kind, std::move(spelling), std::move(qualifier), span});
    return internalName;
  }

  std::size_t generatedCount_{};
  std::unordered_map<std::string, std::string> names_;
  std::map<std::vector<zbik::CodePoint>, std::string> literalNames_;
  std::vector<TerminalBinding> bindings_;
};

auto isStructuralEof(std::size_t ruleIndex, std::size_t elementIndex,
                     const ParserAlternative &alternative) -> bool {
  const ParserElement &element = alternative.elements[elementIndex];
  return ruleIndex == 0 && elementIndex + 1 == alternative.elements.size() &&
         element.symbol.kind == ParserSymbolKind::TokenReference &&
         element.symbol.name == "EOF" && element.quantifier == Quantifier::One;
}

auto makeSpecification(const SyntaxDocument &document,
                       TerminalInterner &terminals) -> zbik::EbnfGrammarSpec {
  std::vector<zbik::EbnfRuleSpec> rules;
  rules.reserve(document.parserRules.size());
  for (std::size_t ruleIndex = 0; ruleIndex < document.parserRules.size();
       ++ruleIndex) {
    const ParserRule &sourceRule = document.parserRules[ruleIndex];
    std::vector<zbik::EbnfAlternative> alternatives;
    alternatives.reserve(sourceRule.alternatives.size());
    for (const ParserAlternative &sourceAlternative : sourceRule.alternatives) {
      std::vector<zbik::EbnfElement> elements;
      elements.reserve(sourceAlternative.elements.size());
      for (std::size_t elementIndex = 0;
           elementIndex < sourceAlternative.elements.size(); ++elementIndex) {
        const ParserElement &sourceElement =
            sourceAlternative.elements[elementIndex];
        if (isStructuralEof(ruleIndex, elementIndex, sourceAlternative)) {
          continue;
        }
        elements.emplace_back(terminals.nameFor(sourceElement.symbol),
                              repetitionOf(sourceElement.quantifier));
      }
      alternatives.emplace_back(std::move(elements));
    }
    rules.emplace_back(sourceRule.name, std::move(alternatives));
  }
  return zbik::EbnfGrammarSpec{std::move(rules)};
}

auto sourceSpanFor(const SyntaxDocument &document,
                   const zbik::EbnfRuleOrigin &origin) -> SourceSpan {
  const ParserAlternative &alternative =
      document.parserRules.at(origin.sourceRuleIndex)
          .alternatives.at(origin.alternativeIndex);
  if (origin.elementIndex.has_value()) {
    return alternative.elements.at(*origin.elementIndex).span;
  }
  return alternative.span;
}

} // namespace

BnfModel::BnfModel(zbik::Grammar grammar,
                   std::vector<BnfProductionOrigin> origins,
                   std::vector<TerminalBinding> terminals)
    : grammar_(std::move(grammar)), origins_(std::move(origins)),
      terminals_(std::move(terminals)) {
  if (grammar_.ruleCount() != origins_.size()) {
    throw std::invalid_argument(
        "every BNF production must have one Agas source origin");
  }
}

auto BnfModel::grammar() const noexcept -> const zbik::Grammar & {
  return grammar_;
}

auto BnfModel::origins() const noexcept
    -> const std::vector<BnfProductionOrigin> & {
  return origins_;
}

auto BnfModel::origin(zbik::RuleId rule) const -> const BnfProductionOrigin & {
  return origins_.at(zbik::toIndex(rule));
}

auto BnfModel::terminals() const noexcept
    -> const std::vector<TerminalBinding> & {
  return terminals_;
}

auto lowerToBnf(const SyntaxDocument &document) -> BnfModel {
  if (!validateSyntaxModel(document).valid()) {
    throw std::invalid_argument("cannot lower an invalid Agas syntax model");
  }
  if (document.parserRules.empty()) {
    throw std::invalid_argument("cannot lower a grammar without parser rules");
  }

  TerminalInterner terminals{document};
  const zbik::EbnfGrammarSpec specification =
      makeSpecification(document, terminals);
  std::vector<std::string> terminalNames;
  for (const TerminalBinding &terminal : terminals.bindings()) {
    terminalNames.push_back(terminal.internalName);
  }
  std::vector<TerminalBinding> bindings = std::move(terminals).takeBindings();

  zbik::EbnfConversionResult conversion =
      zbik::EbnfToBnfConverter{}.convertWithOrigins(specification,
                                                    terminalNames);
  std::vector<BnfProductionOrigin> origins;
  origins.reserve(conversion.origins().size());
  for (const zbik::EbnfRuleOrigin &origin : conversion.origins()) {
    origins.push_back({origin.generatedRule, origin.sourceRuleIndex,
                       origin.alternativeIndex, origin.elementIndex,
                       origin.repetition, origin.role, origin.helperName,
                       sourceSpanFor(document, origin)});
  }
  return BnfModel{std::move(conversion).takeGrammar(), std::move(origins),
                  std::move(bindings)};
}

} // namespace agas::model
