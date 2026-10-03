#include "agas/bootstrap/AntlrFrontend.h"

#include <antlr4-runtime.h>

#include <any>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

#include "AgBaseVisitor.h"
#include "AgLexer.h"
#include "AgParser.h"

namespace agas::bootstrap {
namespace {

class SourceMap {
public:
  explicit SourceMap(std::string_view source) {
    std::size_t byteOffset = 0;
    std::size_t line = 1;
    std::size_t column = 0;
    while (byteOffset < source.size()) {
      positions_.push_back({byteOffset, line, column});
      const auto first = static_cast<unsigned char>(source[byteOffset]);
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
          throw std::invalid_argument("invalid UTF-8 in Agas source");
        }
        if (byteOffset + length > source.size()) {
          throw std::invalid_argument("incomplete UTF-8 in Agas source");
        }
        for (std::size_t index = 1; index < length; ++index) {
          const auto continuation =
              static_cast<unsigned char>(source[byteOffset + index]);
          if ((continuation & 0xC0U) != 0x80U) {
            throw std::invalid_argument("invalid UTF-8 in Agas source");
          }
          value = (value << 6U) | (continuation & 0x3FU);
        }
        if (value < minimum || value > 0x10FFFF ||
            (value >= 0xD800 && value <= 0xDFFF)) {
          throw std::invalid_argument("invalid UTF-8 scalar in Agas source");
        }
      }
      byteOffset += length;
      if (value == U'\n') {
        ++line;
        column = 0;
      } else {
        ++column;
      }
    }
    positions_.push_back({source.size(), line, column});
  }

  auto position(std::size_t codePointIndex) const -> model::SourcePosition {
    if (codePointIndex >= positions_.size()) {
      throw std::out_of_range("ANTLR source index exceeds the UTF-8 source");
    }
    return positions_[codePointIndex];
  }

private:
  std::vector<model::SourcePosition> positions_;
};

class SyntaxIssueListener final : public antlr4::BaseErrorListener {
public:
  explicit SyntaxIssueListener(std::vector<SyntaxIssue> &issues)
      : issues_(issues) {}

  void syntaxError(antlr4::Recognizer *, antlr4::Token *, std::size_t line,
                   std::size_t charPositionInLine, const std::string &message,
                   std::exception_ptr) override {
    issues_.push_back(SyntaxIssue{line, charPositionInLine, message});
  }

private:
  std::vector<SyntaxIssue> &issues_;
};

auto positionAtStart(const antlr4::Token &token, const SourceMap &sourceMap)
    -> model::SourcePosition {
  return sourceMap.position(token.getStartIndex());
}

auto positionAfter(const antlr4::Token &token, const SourceMap &sourceMap)
    -> model::SourcePosition {
  if (token.getType() == antlr4::Token::EOF) {
    return positionAtStart(token, sourceMap);
  }
  return sourceMap.position(token.getStopIndex() + 1);
}

auto spanOf(const antlr4::ParserRuleContext &context,
            const SourceMap &sourceMap) -> model::SourceSpan {
  return {positionAtStart(*context.getStart(), sourceMap),
          positionAfter(*context.getStop(), sourceMap)};
}

auto spanOf(const antlr4::tree::TerminalNode &node,
            const SourceMap &sourceMap) -> model::SourceSpan {
  return {positionAtStart(*node.getSymbol(), sourceMap),
          positionAfter(*node.getSymbol(), sourceMap)};
}

class SyntaxModelBuilder final : public AgBaseVisitor {
public:
  explicit SyntaxModelBuilder(const SourceMap &sourceMap)
      : sourceMap_(sourceMap) {}

  std::any visitDocument(AgParser::DocumentContext *context) override {
    model::SyntaxDocument document;
    document.grammarName =
        context->grammarDeclaration()->TOKEN_REF()->getText();
    document.span = spanOf(*context, sourceMap_);

    for (AgParser::TopLevelItemContext *item : context->topLevelItem()) {
      if (AgParser::OptionsBlockContext *options = item->optionsBlock();
          options != nullptr) {
        appendOptions(document, *options);
      } else if (auto *classes = item->lexerClassesBlock(); classes != nullptr) {
        for (auto *entry : classes->optionEntry()) {
          document.lexerClasses.push_back({entry->identifier()->getText(),
              makeOptionValue(*entry->optionValue()), spanOf(*entry, sourceMap_)});
        }
      } else if (AgParser::ChannelsBlockContext *channels =
                     item->channelsBlock();
                 channels != nullptr) {
        appendChannels(document, *channels);
      } else if (AgParser::ConflictsBlockContext *conflicts =
                     item->conflictsBlock();
                 conflicts != nullptr) {
        appendConflicts(document, *conflicts);
      } else if (AgParser::ParserRuleSpecContext *rule = item->parserRuleSpec();
                 rule != nullptr) {
        document.parserRules.push_back(makeParserRule(*rule));
      } else if (AgParser::LexerRuleSpecContext *rule = item->lexerRuleSpec();
                 rule != nullptr) {
        document.lexerRules.push_back(makeLexerRule(*rule));
      }
    }
    return document;
  }

private:
  auto makeOptionValue(AgParser::OptionValueContext &context)
      -> model::OptionValue {
    model::OptionValueKind kind = model::OptionValueKind::Identifier;
    if (context.INTEGER() != nullptr) {
      kind = model::OptionValueKind::Integer;
    } else if (context.STRING_LITERAL() != nullptr) {
      kind = model::OptionValueKind::String;
    } else if (context.TRUE() != nullptr || context.FALSE() != nullptr) {
      kind = model::OptionValueKind::Boolean;
    }
    return {kind, context.getText(), spanOf(context, sourceMap_)};
  }

  void appendOptions(model::SyntaxDocument &document,
                     AgParser::OptionsBlockContext &context) {
    for (AgParser::OptionEntryContext *entry : context.optionEntry()) {
      document.options.push_back(model::Option{
          entry->identifier()->getText(),
          makeOptionValue(*entry->optionValue()), spanOf(*entry, sourceMap_)});
    }
  }

  void appendChannels(model::SyntaxDocument &document,
                      AgParser::ChannelsBlockContext &context) {
    AgParser::ChannelListContext *channels = context.channelList();
    if (channels == nullptr) {
      return;
    }
    antlr4::tree::TerminalNode *first = channels->TOKEN_REF();
    document.channels.push_back(
        model::Channel{first->getText(), spanOf(*first, sourceMap_)});
    for (AgParser::ChannelTailContext *tail : channels->channelTail()) {
      antlr4::tree::TerminalNode *name = tail->TOKEN_REF();
      document.channels.push_back(
          model::Channel{name->getText(), spanOf(*name, sourceMap_)});
    }
  }

  void appendConflicts(model::SyntaxDocument &document,
                       AgParser::ConflictsBlockContext &context) {
    for (AgParser::ConflictEntryContext *entry : context.conflictEntry()) {
      const bool preferShift = entry->SHIFT()->getSymbol()->getTokenIndex() <
                               entry->REDUCE()->getSymbol()->getTokenIndex();
      document.conflictPreferences.push_back(
          {preferShift ? model::ConflictPreference::Choice::Shift
                       : model::ConflictPreference::Choice::Reduce,
           entry->TOKEN_REF(preferShift ? 0 : 1)->getText(),
           entry->RULE_REF()->getText(),
           entry->TOKEN_REF(preferShift ? 1 : 0)->getText(),
           spanOf(*entry, sourceMap_)});
    }
  }

  auto makeSymbol(AgParser::ParserSymbolContext &context)
      -> model::ParserSymbol {
    if (context.RULE_REF() != nullptr) {
      return {model::ParserSymbolKind::RuleReference,
              context.RULE_REF()->getText(), std::nullopt,
              spanOf(context, sourceMap_)};
    }
    if (context.TOKEN_REF() != nullptr) {
      return {model::ParserSymbolKind::TokenReference,
              context.TOKEN_REF()->getText(), std::nullopt,
              spanOf(context, sourceMap_)};
    }
    if (context.STRING_LITERAL() != nullptr) {
      return {model::ParserSymbolKind::Literal,
              context.STRING_LITERAL()->getText(), std::nullopt,
              spanOf(context, sourceMap_)};
    }

    AgParser::QualifiedReferenceContext *qualified =
        context.qualifiedReference();
    return {model::ParserSymbolKind::QualifiedReference,
            qualified->identifier()->getText(),
            qualified->TOKEN_REF()->getText(),
            spanOf(context, sourceMap_)};
  }

  auto makeElement(AgParser::ParserElementContext &context)
      -> model::ParserElement {
    model::ParserElement element;
    if (context.elementLabel() != nullptr) {
      element.fieldName = context.elementLabel()->RULE_REF()->getText();
    }
    element.symbol = makeSymbol(*context.parserSymbol());
    element.span = spanOf(context, sourceMap_);

    if (AgParser::ParserSuffixContext *suffix = context.parserSuffix();
        suffix != nullptr) {
      if (suffix->QUESTION() != nullptr) {
        element.quantifier = model::Quantifier::Optional;
      } else if (suffix->STAR() != nullptr) {
        element.quantifier = model::Quantifier::ZeroOrMore;
      } else {
        element.quantifier = model::Quantifier::OneOrMore;
      }
    }
    return element;
  }

  auto makeAlternative(AgParser::ParserAlternativeContext &context)
      -> model::ParserAlternative {
    model::ParserAlternative alternative;
    alternative.explicitEmpty = context.EMPTY() != nullptr;
    alternative.span = spanOf(context, sourceMap_);
    for (AgParser::ParserElementContext *element : context.parserElement()) {
      alternative.elements.push_back(makeElement(*element));
    }
    if (context.alternativeLabel() != nullptr) {
      alternative.label = context.alternativeLabel()->TOKEN_REF()->getText();
    }
    return alternative;
  }

  auto makeParserRule(AgParser::ParserRuleSpecContext &context)
      -> model::ParserRule {
    model::ParserRule rule;
    rule.name = context.RULE_REF()->getText();
    rule.span = spanOf(context, sourceMap_);
    AgParser::TreeModifierContext *modifier = context.treeModifier();
    rule.treeModifier = modifier->NODE() != nullptr
                            ? model::TreeModifier::Node
                            : model::TreeModifier::Inline;
    if (auto *commands = context.lexerCommands()) {
      rule.lexerContext.push_back(makeLexerCommand(*commands->lexerCommand()));
      for (auto *tail : commands->lexerCommandTail())
        rule.lexerContext.push_back(makeLexerCommand(*tail->lexerCommand()));
    }
    rule.alternatives.push_back(makeAlternative(*context.parserAlternative()));
    for (AgParser::ParserAlternativeTailContext *tail :
         context.parserAlternativeTail()) {
      rule.alternatives.push_back(makeAlternative(*tail->parserAlternative()));
    }
    return rule;
  }

  auto makeLexerAtom(AgParser::LexerAtomContext &context)
      -> model::LexerAtom {
    model::LexerAtom atom;
    atom.span = spanOf(context, sourceMap_);
    if (context.TOKEN_REF() != nullptr) {
      atom.kind = model::LexerAtomKind::TokenReference;
      atom.spelling = context.TOKEN_REF()->getText();
    } else if (context.STRING_LITERAL() != nullptr) {
      atom.kind = model::LexerAtomKind::Literal;
      atom.spelling = context.STRING_LITERAL()->getText();
    } else if (context.LEXER_CHAR_SET() != nullptr) {
      atom.kind = model::LexerAtomKind::CharacterSet;
      atom.spelling = context.LEXER_CHAR_SET()->getText();
    } else if (context.DOT() != nullptr) {
      atom.kind = model::LexerAtomKind::Wildcard;
      atom.spelling = context.DOT()->getText();
    } else if (AgParser::LexerNegatableContext *negated =
                   context.lexerNegatable();
               negated != nullptr) {
      if (negated->STRING_LITERAL() != nullptr) {
        atom.kind = model::LexerAtomKind::NegatedLiteral;
        atom.spelling = negated->STRING_LITERAL()->getText();
      } else {
        atom.kind = model::LexerAtomKind::NegatedCharacterSet;
        atom.spelling = negated->LEXER_CHAR_SET()->getText();
      }
    } else {
      atom.kind = model::LexerAtomKind::Group;
      atom.groupAlternatives =
          makeLexerAlternatives(*context.lexerGroup()->lexerAltList());
    }
    return atom;
  }

  auto makeLexerElement(AgParser::LexerElementContext &context)
      -> model::LexerElement {
    model::LexerElement element;
    element.atom = makeLexerAtom(*context.lexerAtom());
    element.span = spanOf(context, sourceMap_);
    if (AgParser::LexerSuffixContext *suffix = context.lexerSuffix();
        suffix != nullptr) {
      if (suffix->STAR() != nullptr) {
        element.quantifier = model::LexerQuantifier::ZeroOrMore;
        element.lazy = suffix->QUESTION() != nullptr;
      } else if (suffix->PLUS() != nullptr) {
        element.quantifier = model::LexerQuantifier::OneOrMore;
        element.lazy = suffix->QUESTION() != nullptr;
      } else {
        element.quantifier = model::LexerQuantifier::Optional;
      }
    }
    return element;
  }

  auto makeLexerAlternative(AgParser::LexerAlternativeContext &context)
      -> model::LexerAlternative {
    model::LexerAlternative alternative;
    alternative.explicitEmpty = context.EMPTY() != nullptr;
    alternative.span = spanOf(context, sourceMap_);
    for (AgParser::LexerElementContext *element : context.lexerElement()) {
      alternative.elements.push_back(makeLexerElement(*element));
    }
    return alternative;
  }

  auto makeLexerAlternatives(AgParser::LexerAltListContext &context)
      -> std::vector<model::LexerAlternative> {
    std::vector<model::LexerAlternative> alternatives;
    alternatives.push_back(makeLexerAlternative(*context.lexerAlternative()));
    for (AgParser::LexerAlternativeTailContext *tail :
         context.lexerAlternativeTail()) {
      alternatives.push_back(makeLexerAlternative(*tail->lexerAlternative()));
    }
    return alternatives;
  }

  auto makeLexerCommand(AgParser::LexerCommandContext &context)
      -> model::LexerCommand {
    model::LexerCommand command;
    command.name = context.identifier()->getText();
    command.span = spanOf(context, sourceMap_);
    if (context.lexerCommandArgument() != nullptr) {
      command.argument =
          context.lexerCommandArgument()->identifier()->getText();
    }
    return command;
  }

  auto makeLexerRule(AgParser::LexerRuleSpecContext &context)
      -> model::LexerRule {
    model::LexerRule rule;
    rule.fragment = context.FRAGMENT() != nullptr;
    rule.name = context.TOKEN_REF()->getText();
    rule.alternatives = makeLexerAlternatives(*context.lexerAltList());
    rule.span = spanOf(context, sourceMap_);
    if (AgParser::LexerCommandsContext *commands = context.lexerCommands();
        commands != nullptr) {
      rule.commands.push_back(makeLexerCommand(*commands->lexerCommand()));
      for (AgParser::LexerCommandTailContext *tail :
           commands->lexerCommandTail()) {
        rule.commands.push_back(makeLexerCommand(*tail->lexerCommand()));
      }
    }
    return rule;
  }

  const SourceMap &sourceMap_;
};

} // namespace

auto parseAgas(std::string_view source) -> ParseResult {
  ParseResult result;
  SyntaxIssueListener issueListener(result.issues);
  std::optional<SourceMap> sourceMap;
  try {
    sourceMap.emplace(source);
  } catch (const std::invalid_argument &error) {
    result.issues.push_back({1, 0, error.what()});
    return result;
  }

  antlr4::ANTLRInputStream input{std::string(source)};
  AgLexer lexer(&input);
  lexer.removeErrorListeners();
  lexer.addErrorListener(&issueListener);

  antlr4::CommonTokenStream tokens(&lexer);
  AgParser parser(&tokens);
  parser.removeErrorListeners();
  parser.addErrorListener(&issueListener);

  AgParser::DocumentContext *document = parser.document();
  if (!result.issues.empty() || document == nullptr ||
      document->grammarDeclaration() == nullptr) {
    return result;
  }

  SyntaxModelBuilder builder(*sourceMap);
  result.document =
      std::any_cast<model::SyntaxDocument>(document->accept(&builder));
  return result;
}

auto parseAgasFile(const std::filesystem::path &path) -> ParseResult {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("cannot open Agas grammar: " + path.string());
  }
  const std::string source{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
  return parseAgas(source);
}

auto lexAgasWithBootstrapAntlr(std::string_view source)
    -> BootstrapLexResult {
  BootstrapLexResult result;
  SyntaxIssueListener issueListener(result.issues);
  std::optional<SourceMap> sourceMap;
  try {
    sourceMap.emplace(source);
  } catch (const std::invalid_argument &error) {
    result.issues.push_back({1, 0, error.what()});
    return result;
  }

  antlr4::ANTLRInputStream input{std::string(source)};
  AgLexer lexer(&input);
  lexer.removeErrorListeners();
  lexer.addErrorListener(&issueListener);
  for (const auto &token : lexer.getAllTokens()) {
    const std::string_view symbolicName =
        lexer.getVocabulary().getSymbolicName(token->getType());
    std::optional<std::string> channel;
    if (token->getChannel() != antlr4::Token::DEFAULT_CHANNEL) {
      const auto &channelNames = lexer.getChannelNames();
      if (token->getChannel() < channelNames.size()) {
        channel = channelNames[token->getChannel()];
      } else {
        channel = std::to_string(token->getChannel());
      }
    }
    result.tokens.push_back(
        {std::string{symbolicName}, std::move(channel),
         sourceMap->position(token->getStartIndex()).offset,
         token->getText()});
  }
  return result;
}

} // namespace agas::bootstrap
