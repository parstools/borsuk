#include "agas/model/AstChain.h"

#include "regex/RegexParser.h"

namespace agas::model {
namespace {

auto singleCharacter(const SyntaxDocument &document, const ParserElement &element,
                     char character) -> bool {
  if (element.fieldName || element.quantifier != Quantifier::One) return false;
  std::string spelling;
  if (element.symbol.kind == ParserSymbolKind::Literal) {
    spelling = element.symbol.name;
  } else if (element.symbol.kind == ParserSymbolKind::TokenReference) {
    for (const auto &rule : document.lexerRules) {
      if (rule.name != element.symbol.name || rule.fragment ||
          !rule.commands.empty() || rule.alternatives.size() != 1 ||
          rule.alternatives[0].elements.size() != 1) continue;
      const auto &part = rule.alternatives[0].elements[0];
      if (part.quantifier == LexerQuantifier::One && !part.lazy &&
          (part.atom.kind == LexerAtomKind::Literal ||
           part.atom.kind == LexerAtomKind::CharacterSet))
        spelling = part.atom.spelling;
      break;
    }
  }
  if (spelling.empty()) return false;
  const auto regex = zbik::RegexParser{}.parse(spelling);
  if (regex.kind() != zbik::RegexAst::Kind::CodePointClass) return false;
  const auto &ranges = regex.codePoints().ranges();
  const auto codePoint = static_cast<zbik::CodePoint>(character);
  return ranges.size() == 1 && ranges[0].first == codePoint &&
         ranges[0].last == codePoint;
}

} // namespace

auto astChainOperand(const SyntaxDocument &document,
                     const ParserAlternative &alternative)
    -> std::optional<std::size_t> {
  const auto &elements = alternative.elements;
  // Only round grouping with a required value is transparent. A call's
  // optional argument list and an index's square brackets stay meaningful.
  if (elements.size() == 3 && elements[1].fieldName &&
      elements[1].quantifier == Quantifier::One &&
      elements[1].symbol.kind == ParserSymbolKind::RuleReference &&
      singleCharacter(document, elements[0], '(') &&
      singleCharacter(document, elements[2], ')'))
    return 1;

  std::optional<std::size_t> operand;
  for (std::size_t index = 0; index < elements.size(); ++index) {
    const auto &element = elements[index];
    if (!element.fieldName ||
        element.symbol.kind != ParserSymbolKind::RuleReference)
      return std::nullopt;
    if (element.quantifier == Quantifier::One) {
      if (operand) return std::nullopt;
      operand = index;
    } else if (element.quantifier != Quantifier::Optional &&
               element.quantifier != Quantifier::ZeroOrMore) {
      return std::nullopt;
    }
  }
  return operand;
}

} // namespace agas::model
