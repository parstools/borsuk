#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace agas::model {

struct SourcePosition {
  std::size_t offset{};
  std::size_t line{};
  std::size_t column{};

  auto operator==(const SourcePosition &) const -> bool = default;
};

struct SourceSpan {
  SourcePosition begin;
  SourcePosition end;

  auto operator==(const SourceSpan &) const -> bool = default;
};

enum class OptionValueKind { Identifier, Integer, String, Boolean };

struct OptionValue {
  OptionValueKind kind{};
  std::string spelling;
  SourceSpan span;

  auto operator==(const OptionValue &) const -> bool = default;
};

struct Option {
  std::string name;
  OptionValue value;
  SourceSpan span;

  auto operator==(const Option &) const -> bool = default;
};

struct Channel {
  std::string name;
  SourceSpan span;

  auto operator==(const Channel &) const -> bool = default;
};

enum class TreeModifier { Node, Inline };
enum class Quantifier { One, Optional, ZeroOrMore, OneOrMore };

enum class ParserSymbolKind {
  RuleReference,
  TokenReference,
  Literal,
  QualifiedReference
};

struct ParserSymbol {
  ParserSymbolKind kind{};
  std::string name;
  std::optional<std::string> qualifier;
  SourceSpan span;

  auto operator==(const ParserSymbol &) const -> bool = default;
};

struct ParserElement {
  std::optional<std::string> fieldName;
  ParserSymbol symbol;
  Quantifier quantifier{Quantifier::One};
  SourceSpan span;

  auto operator==(const ParserElement &) const -> bool = default;
};

struct ParserAlternative {
  bool explicitEmpty{};
  std::vector<ParserElement> elements;
  std::optional<std::string> label;
  SourceSpan span;

  auto operator==(const ParserAlternative &) const -> bool = default;
};

struct LexerCommand {
  std::string name;
  std::optional<std::string> argument;
  SourceSpan span;

  auto operator==(const LexerCommand &) const -> bool = default;
};

struct ParserRule {
  TreeModifier treeModifier{TreeModifier::Node};
  std::string name;
  std::vector<ParserAlternative> alternatives;
  SourceSpan span;

  std::vector<LexerCommand> lexerContext;

  auto operator==(const ParserRule &) const -> bool = default;
};

struct ConflictPreference {
  enum class Choice { Shift, Reduce };
  Choice choice{Choice::Shift};
  std::string shiftTerminal;
  std::string reduceRule;
  std::string reduceAlternative;
  SourceSpan span;

  auto operator==(const ConflictPreference &) const -> bool = default;
};

enum class LexerAtomKind {
  TokenReference,
  Literal,
  CharacterSet,
  Wildcard,
  NegatedLiteral,
  NegatedCharacterSet,
  Group
};

enum class LexerQuantifier { One, Optional, ZeroOrMore, OneOrMore };

struct LexerAlternative;

struct LexerAtom {
  LexerAtomKind kind{};
  std::string spelling;
  std::vector<LexerAlternative> groupAlternatives;
  SourceSpan span;

  auto operator==(const LexerAtom &) const -> bool = default;
};

struct LexerElement {
  LexerAtom atom;
  LexerQuantifier quantifier{LexerQuantifier::One};
  bool lazy{};
  SourceSpan span;

  auto operator==(const LexerElement &) const -> bool = default;
};

struct LexerAlternative {
  bool explicitEmpty{};
  std::vector<LexerElement> elements;
  SourceSpan span;

  auto operator==(const LexerAlternative &) const -> bool = default;
};


struct LexerRule {
  bool fragment{};
  std::string name;
  std::vector<LexerAlternative> alternatives;
  std::vector<LexerCommand> commands;
  SourceSpan span;

  auto operator==(const LexerRule &) const -> bool = default;
};

struct SyntaxDocument {
  std::string grammarName;
  std::vector<Option> options;
  std::vector<Channel> channels;
  std::vector<ConflictPreference> conflictPreferences;
  std::vector<ParserRule> parserRules;
  std::vector<LexerRule> lexerRules;
  SourceSpan span;

  std::vector<Option> lexerClasses;

  auto operator==(const SyntaxDocument &) const -> bool = default;
};

} // namespace agas::model
