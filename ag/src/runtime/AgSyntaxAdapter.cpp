#include "agas/runtime/AgSyntaxAdapter.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace agas::runtime {
namespace {

class ByteSourceMap {
public:
  explicit ByteSourceMap(std::string_view source)
      : positions_(source.size() + 1), boundaries_(source.size() + 1) {
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 0;
    while (offset < source.size()) {
      positions_[offset] = {offset, line, column};
      boundaries_[offset] = true;
      const auto first = static_cast<unsigned char>(source[offset]);
      std::size_t length = 1;
      char32_t value = first;
      char32_t minimum = 0;
      if (first >= 0x80) {
        if ((first & 0xE0U) == 0xC0U) {
          length = 2;
          value = first & 0x1FU;
          minimum = 0x80;
        } else if ((first & 0xF0U) == 0xE0U) {
          length = 3;
          value = first & 0x0FU;
          minimum = 0x800;
        } else if ((first & 0xF8U) == 0xF0U) {
          length = 4;
          value = first & 0x07U;
          minimum = 0x10000;
        } else {
          throw std::invalid_argument("invalid UTF-8 in Agas source map");
        }
        if (offset + length > source.size()) {
          throw std::invalid_argument("incomplete UTF-8 in Agas source map");
        }
        for (std::size_t index = 1; index < length; ++index) {
          const auto continuation =
              static_cast<unsigned char>(source[offset + index]);
          if ((continuation & 0xC0U) != 0x80U) {
            throw std::invalid_argument("invalid UTF-8 in Agas source map");
          }
          value = (value << 6U) | (continuation & 0x3FU);
        }
        if (value < minimum || value > 0x10FFFF ||
            (value >= 0xD800 && value <= 0xDFFF)) {
          throw std::invalid_argument(
              "invalid UTF-8 scalar in Agas source map");
        }
      }
      offset += length;
      if (value == U'\n') {
        ++line;
        column = 0;
      } else {
        ++column;
      }
    }
    positions_[source.size()] = {source.size(), line, column};
    boundaries_[source.size()] = true;
  }

  auto position(std::uint64_t byteOffset) const -> model::SourcePosition {
    if (byteOffset > std::numeric_limits<std::size_t>::max()) {
      throw std::out_of_range("Agas byte offset exceeds platform size");
    }
    const std::size_t offset = static_cast<std::size_t>(byteOffset);
    if (offset >= boundaries_.size() || !boundaries_[offset]) {
      throw std::out_of_range("Agas byte offset is not a UTF-8 boundary");
    }
    return positions_[offset];
  }

  auto span(InputSpan span) const -> model::SourceSpan {
    return {position(span.beginByte), position(span.endByte)};
  }

private:
  std::vector<model::SourcePosition> positions_;
  std::vector<bool> boundaries_;
};

class Adapter {
public:
  Adapter(std::string_view source, const zbik::Grammar &grammar)
      : source_(source), sourceMap_(source) {
    terminalNames_.reserve(grammar.terminalCount());
    for (std::size_t index = 0; index < grammar.terminalCount(); ++index)
      terminalNames_.push_back(grammar.terminalName(
          zbik::TerminalId{static_cast<std::uint32_t>(index)}));
  }

  Adapter(std::string_view source, const artifact::ArtifactSymbols &symbols)
      : source_(source), sourceMap_(source) {
    terminalNames_.reserve(symbols.terminals.size());
    for (const auto &terminal : symbols.terminals)
      terminalNames_.push_back(terminal.name);
  }

  auto document(const AstValue &root) const -> model::SyntaxDocument {
    expectNode(root, "document");
    model::SyntaxDocument result;
    const AstValue &header = field(root, "header");
    expectNode(header, "grammarDeclaration");
    result.grammarName = token(field(header, "name")).tokenText;

    for (const AstValue &item : list(field(root, "items"))) {
      if (item.typeName == "optionsBlock") {
        appendOptions(item, result);
      } else if (item.typeName == "lexerClassesBlock") {
        model::SyntaxDocument temporary;
        appendOptions(item, temporary);
        result.lexerClasses.insert(result.lexerClasses.end(),
            temporary.options.begin(), temporary.options.end());
      } else if (item.typeName == "channelsBlock") {
        appendChannels(item, result);
      } else if (item.typeName == "conflictsBlock") {
        appendConflicts(item, result);
      } else if (item.typeName == "parserRuleSpec") {
        result.parserRules.push_back(parserRule(item));
      } else if (item.typeName == "lexerRuleSpec") {
        result.lexerRules.push_back(lexerRule(item));
      } else {
        fail(item, "unexpected top-level Agas node");
      }
    }
    model::SourceSpan documentSpan = span(root);
    documentSpan.end = sourceMap_.position(source_.size());
    result.span = documentSpan;
    return result;
  }

private:
  [[noreturn]] void fail(const AstValue &value,
                         const std::string &message) const {
    throw AgSyntaxAdapterError(value.sourceSpan, message);
  }

  void expectNode(const AstValue &value, std::string_view type) const {
    if (value.kind != AstValueKind::Node || value.typeName != type) {
      fail(value, "expected Agas node `" + std::string{type} + "`");
    }
  }

  auto field(const AstValue &value, std::string_view name) const
      -> const AstValue & {
    if (value.fieldNames.size() != value.elements.size()) {
      fail(value, "Agas AST field names and values differ in size");
    }
    for (std::size_t index = 0; index < value.fieldNames.size(); ++index) {
      if (value.fieldNames[index] == name)
        return value.elements[index];
    }
    fail(value, "missing Agas AST field `" + std::string{name} + "`");
  }

  auto token(const AstValue &value) const -> const AstValue & {
    if (value.kind != AstValueKind::Token)
      fail(value, "expected Agas token");
    return value;
  }

  auto list(const AstValue &value) const -> const std::vector<AstValue> & {
    if (value.kind != AstValueKind::List)
      fail(value, "expected Agas list");
    return value.elements;
  }

  auto optional(const AstValue &value) const -> const AstValue * {
    if (value.kind != AstValueKind::Optional || value.elements.size() > 1) {
      fail(value, "expected Agas optional value");
    }
    return value.elements.empty() ? nullptr : &value.elements.front();
  }

  auto span(const AstValue &value) const -> model::SourceSpan {
    return sourceMap_.span(value.sourceSpan);
  }

  auto terminalName(const AstValue &value) const -> const std::string & {
    token(value);
    if (value.tokenKind >= terminalNames_.size()) {
      fail(value, "Agas token kind is outside the grammar alphabet");
    }
    return terminalNames_[value.tokenKind];
  }

  void appendOptions(const AstValue &node,
                     model::SyntaxDocument &document) const {

    for (const AstValue &entry : list(field(node, "entries"))) {
      expectNode(entry, "optionEntry");
      const AstValue &name = token(field(entry, "name"));
      const AstValue &value = token(field(entry, "value"));
      model::OptionValueKind kind = model::OptionValueKind::Identifier;
      const std::string &type = terminalName(value);
      if (type == "INTEGER") {
        kind = model::OptionValueKind::Integer;
      } else if (type == "STRING_LITERAL") {
        kind = model::OptionValueKind::String;
      } else if (type == "TRUE" || type == "FALSE") {
        kind = model::OptionValueKind::Boolean;
      }
      document.options.push_back(
          {name.tokenText, {kind, value.tokenText, span(value)}, span(entry)});
    }
  }

  void appendChannels(const AstValue &node,
                      model::SyntaxDocument &document) const {
    expectNode(node, "channelsBlock");
    const AstValue *names = optional(field(node, "names"));
    if (names == nullptr)
      return;
    const AstValue &first = token(field(*names, "first"));
    document.channels.push_back({first.tokenText, span(first)});
    for (const AstValue &tail : list(field(*names, "rest"))) {
      const AstValue &name = token(tail);
      document.channels.push_back({name.tokenText, span(name)});
    }
  }

  void appendConflicts(const AstValue &node,
                       model::SyntaxDocument &document) const {
    expectNode(node, "conflictsBlock");
    for (const AstValue &entry : list(field(node, "entries"))) {
      expectNode(entry, "conflictEntry");
      if (entry.variantName != "PreferShift" &&
          entry.variantName != "PreferReduce")
        fail(entry, "unexpected conflict policy variant");
      const auto choice = entry.variantName == "PreferReduce"
                              ? model::ConflictPreference::Choice::Reduce
                              : model::ConflictPreference::Choice::Shift;
      document.conflictPreferences.push_back(
          {choice, token(field(entry, "terminal")).tokenText,
           token(field(entry, "rule")).tokenText,
           token(field(entry, "label")).tokenText, span(entry)});
    }
  }

  auto parserSymbol(const AstValue &value) const -> model::ParserSymbol {
    if (value.kind == AstValueKind::Token) {
      model::ParserSymbolKind kind{};
      const std::string &type = terminalName(value);
      if (type == "RULE_REF") {
        kind = model::ParserSymbolKind::RuleReference;
      } else if (type == "TOKEN_REF") {
        kind = model::ParserSymbolKind::TokenReference;
      } else if (type == "STRING_LITERAL") {
        kind = model::ParserSymbolKind::Literal;
      } else {
        fail(value, "unexpected token in parser symbol");
      }
      return {kind, value.tokenText, std::nullopt, span(value)};
    }
    expectNode(value, "qualifiedReference");
    const AstValue &scope = token(field(value, "scope"));
    const AstValue &name = token(field(value, "name"));
    return {model::ParserSymbolKind::QualifiedReference, name.tokenText,
            scope.tokenText, span(value)};
  }

  auto parserElement(const AstValue &node) const -> model::ParserElement {
    model::ParserElement result;
    if (node.typeName != "parserElement") {
      result.symbol = parserSymbol(node);
      result.span = span(node);
      return result;
    }
    if (const AstValue *label = optional(field(node, "field"))) {
      result.fieldName = token(*label).tokenText;
    }
    result.symbol = parserSymbol(field(node, "symbol"));
    if (const AstValue *suffix = optional(field(node, "suffix"))) {
      const std::string &text = token(*suffix).tokenText;
      if (text == "?") {
        result.quantifier = model::Quantifier::Optional;
      } else if (text == "*") {
        result.quantifier = model::Quantifier::ZeroOrMore;
      } else if (text == "+") {
        result.quantifier = model::Quantifier::OneOrMore;
      } else {
        fail(*suffix, "unexpected parser quantifier");
      }
    }
    result.span = span(node);
    return result;
  }

  auto parserAlternative(const AstValue &node) const
      -> model::ParserAlternative {
    expectNode(node, "parserAlternative");
    model::ParserAlternative result;
    result.explicitEmpty = node.variantName == "EmptyAlternative";
    if (!result.explicitEmpty) {
      for (const AstValue &element : list(field(node, "elements"))) {
        result.elements.push_back(parserElement(element));
      }
    }
    if (const AstValue *label = optional(field(node, "label"))) {
      expectNode(*label, "alternativeLabel");
      result.label = token(field(*label, "name")).tokenText;
    }
    result.span = span(node);
    return result;
  }

  auto parserRule(const AstValue &node) const -> model::ParserRule {
    expectNode(node, "parserRuleSpec");
    model::ParserRule result;
    const AstValue &kind = token(field(node, "kind"));
    result.treeModifier = kind.tokenText == "node"
                              ? model::TreeModifier::Node
                              : model::TreeModifier::Inline;
    // Older pinned frontends do not expose the new optional header yet.
    for (std::size_t i = 0; i < node.fieldNames.size(); ++i) {
      if (node.fieldNames[i] != "context") continue;
      if (const auto *commands = optional(node.elements[i])) {
        result.lexerContext.push_back(lexerCommand(field(*commands, "first")));
        for (const auto &tail : list(field(*commands, "rest")))
          result.lexerContext.push_back(lexerCommand(tail));
      }
    }
    result.name = token(field(node, "name")).tokenText;
    result.alternatives.push_back(parserAlternative(field(node, "first")));
    for (const AstValue &tail : list(field(node, "rest"))) {
      result.alternatives.push_back(parserAlternative(tail));
    }
    result.span = span(node);
    return result;
  }

  auto lexerAlternative(const AstValue &node) const -> model::LexerAlternative;
  auto lexerElement(const AstValue &node) const -> model::LexerElement;
  auto lexerAtom(const AstValue &value) const -> model::LexerAtom;

  auto lexerAlternatives(const AstValue &node) const
      -> std::vector<model::LexerAlternative> {
    if (node.typeName == "lexerAlternative")
      return {lexerAlternative(node)};
    expectNode(node, "lexerAltList");
    std::vector<model::LexerAlternative> result;
    result.push_back(lexerAlternative(field(node, "first")));
    for (const AstValue &tail : list(field(node, "rest"))) {
      result.push_back(lexerAlternative(tail));
    }
    return result;
  }

  auto lexerCommand(const AstValue &node) const -> model::LexerCommand {
    model::LexerCommand result;
    if (node.kind == AstValueKind::Token) {
      result.name = token(node).tokenText;
      result.span = span(node);
      return result;
    }
    expectNode(node, "lexerCommand");
    result.name = token(field(node, "name")).tokenText;
    if (const AstValue *argument = optional(field(node, "argument"))) {
      result.argument = token(*argument).tokenText;
    }
    result.span = span(node);
    return result;
  }

  auto lexerRule(const AstValue &node) const -> model::LexerRule {
    expectNode(node, "lexerRuleSpec");
    model::LexerRule result;
    result.fragment = optional(field(node, "isFragment")) != nullptr;
    result.name = token(field(node, "name")).tokenText;
    result.alternatives = lexerAlternatives(field(node, "body"));
    if (const AstValue *commands = optional(field(node, "commands"))) {
      expectNode(*commands, "lexerCommands");
      result.commands.push_back(lexerCommand(field(*commands, "first")));
      for (const AstValue &tail : list(field(*commands, "rest"))) {
        result.commands.push_back(lexerCommand(tail));
      }
    }
    result.span = span(node);
    return result;
  }

  std::string_view source_;
  std::vector<std::string> terminalNames_;
  ByteSourceMap sourceMap_;
};

auto Adapter::lexerAtom(const AstValue &value) const -> model::LexerAtom {
  model::LexerAtom result;
  result.span = span(value);
  if (value.kind == AstValueKind::Token) {
    const std::string &type = terminalName(value);
    if (type == "TOKEN_REF") {
      result.kind = model::LexerAtomKind::TokenReference;
    } else if (type == "STRING_LITERAL") {
      result.kind = model::LexerAtomKind::Literal;
    } else if (type == "LEXER_CHAR_SET") {
      result.kind = model::LexerAtomKind::CharacterSet;
    } else if (type == "DOT") {
      result.kind = model::LexerAtomKind::Wildcard;
    } else {
      fail(value, "unexpected token in lexer atom");
    }
    result.spelling = value.tokenText;
    return result;
  }
  if (value.typeName == "lexerNegation") {
    const AstValue &negated = token(field(value, "value"));
    result.kind = terminalName(negated) == "STRING_LITERAL"
                      ? model::LexerAtomKind::NegatedLiteral
                      : model::LexerAtomKind::NegatedCharacterSet;
    result.spelling = negated.tokenText;
    return result;
  }
  if (value.typeName == "lexerGroup") {
    result.kind = model::LexerAtomKind::Group;
    result.groupAlternatives = lexerAlternatives(field(value, "body"));
    return result;
  }
  if (value.typeName == "lexerAltList" || value.typeName == "lexerAlternative") {
    result.kind = model::LexerAtomKind::Group;
    result.groupAlternatives = lexerAlternatives(value);
    result.span = sourceMap_.span(value.recognizedSpan);
    return result;
  }
  fail(value, "unexpected node in lexer atom");
}

auto Adapter::lexerElement(const AstValue &node) const -> model::LexerElement {
  model::LexerElement result;
  if (node.typeName != "lexerElement") {
    result.atom = lexerAtom(node);
    result.span = sourceMap_.span(node.recognizedSpan);
    return result;
  }
  result.atom = lexerAtom(field(node, "atom"));
  if (const AstValue *suffix = optional(field(node, "suffix"))) {
    if (suffix->kind == AstValueKind::Token) {
      if (suffix->tokenText != "?") {
        fail(*suffix, "unexpected single-token lexer suffix");
      }
      result.quantifier = model::LexerQuantifier::Optional;
    } else {
      const AstValue &kind = token(field(*suffix, "kind"));
      result.quantifier = kind.tokenText == "*"
                              ? model::LexerQuantifier::ZeroOrMore
                              : model::LexerQuantifier::OneOrMore;
      result.lazy = optional(field(*suffix, "lazy")) != nullptr;
    }
  }
  result.span = span(node);
  return result;
}

auto Adapter::lexerAlternative(const AstValue &node) const
    -> model::LexerAlternative {
  expectNode(node, "lexerAlternative");
  model::LexerAlternative result;
  result.explicitEmpty = node.variantName == "EmptyLexerAlternative";
  if (!result.explicitEmpty) {
    for (const AstValue &element : list(field(node, "elements"))) {
      result.elements.push_back(lexerElement(element));
    }
  }
  result.span = span(node);
  return result;
}

} // namespace

AgSyntaxAdapterError::AgSyntaxAdapterError(InputSpan span, std::string message)
    : std::runtime_error(std::move(message)), span_(span) {}

auto AgSyntaxAdapterError::span() const noexcept -> const InputSpan & {
  return span_;
}

auto adaptAgSyntaxDocument(const AstValue &root, std::string_view source,
                           const zbik::Grammar &grammar)
    -> model::SyntaxDocument {
  return Adapter{source, grammar}.document(root);
}

auto adaptAgSyntaxDocument(const AstValue &root, std::string_view source,
                           const artifact::ArtifactSymbols &symbols)
    -> model::SyntaxDocument {
  return Adapter{source, symbols}.document(root);
}

} // namespace agas::runtime
