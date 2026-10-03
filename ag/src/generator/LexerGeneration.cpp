#include "agas/generator/LexerGeneration.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "lexer/LexerTypes.h"
#include "regex/RegexParser.h"

namespace agas::generator {
namespace {

enum class VisitState { Unvisited, Visiting, Complete };

auto complement(const zbik::CodePointClass &codePoints)
    -> zbik::CodePointClass {
  std::vector<zbik::CodePointRange> ranges;
  zbik::CodePoint next = 0;
  for (const zbik::CodePointRange range : codePoints.ranges()) {
    if (next < range.first) ranges.push_back({next, range.first - 1});
    next = range.last + 1U;
  }
  if (next <= zbik::maxUnicodeCodePoint) {
    ranges.push_back({next, zbik::maxUnicodeCodePoint});
  }
  return zbik::CodePointClass{std::move(ranges)};
}

class Compiler {
public:
  Compiler(const model::SyntaxDocument &document, const model::BnfModel &bnf)
      : document_(document), bnf_(bnf), states_(document.lexerRules.size()),
        compiled_(document.lexerRules.size()) {
    for (std::size_t index = 0; index < document_.lexerRules.size(); ++index) {
      const model::LexerRule &rule = document_.lexerRules[index];
      ruleIndices_.emplace(rule.name, index);
    }
  }

  auto compile() -> GeneratedLexerAutomaton {
    for (std::size_t index = 0; index < document_.lexerRules.size(); ++index) {
      static_cast<void>(compileRule(index));
    }

    std::vector<CompiledLexerRule> rules;
    appendImplicitLiterals(rules);
    for (std::size_t index = 0; index < document_.lexerRules.size(); ++index) {
      const model::LexerRule &source = document_.lexerRules[index];
      if (source.fragment) continue;
      const auto terminal = terminalFor(source.name, source.span);
      bool skipped = false;
      std::optional<std::string> channel;
      for (const model::LexerCommand &command : source.commands) {
        if (command.name == "skip") {
          skipped = true;
        } else if (command.name == "channel") {
          channel = command.argument;
        }
      }
      const std::optional<zbik::TerminalId> emittedTerminal =
          skipped ? std::optional<zbik::TerminalId>{}
                  : std::optional<zbik::TerminalId>{terminal};
      rules.push_back({source.name, *compiled_[index], emittedTerminal,
                       std::move(channel), skipped, source.span});
    }
    if (rules.empty()) {
      throw LexerGenerationError(document_.span,
                                 "at least one non-fragment lexer rule is required");
    }

    std::vector<zbik::LexerRule> runtimeRules;
    std::vector<zbik::RegexAst> expressions;
    runtimeRules.reserve(rules.size());
    expressions.reserve(rules.size());
    for (std::size_t index = 0; index < rules.size(); ++index) {
      if (index > std::numeric_limits<std::uint32_t>::max()) {
        throw LexerGenerationError(document_.span,
                                   "too many generated lexer rules");
      }
      runtimeRules.push_back(
          {rules[index].skipped
               ? std::nullopt
               : std::optional<zbik::TerminalId>{zbik::TerminalId{
                     static_cast<std::uint32_t>(index)}},
           {}});
      expressions.push_back(rules[index].expression);
    }
    try {
      zbik::Utf8Lexer lexer{std::move(runtimeRules), std::move(expressions)};
      return {std::move(rules), std::move(lexer)};
    } catch (const zbik::LexerBuildError &error) {
      if (!error.ruleIndex().has_value()) {
        throw LexerGenerationError(document_.span, error.what());
      }
      const std::size_t generatedIndex = *error.ruleIndex();
      if (generatedIndex < rules.size()) {
        throw LexerGenerationError(rules[generatedIndex].span, error.what());
      }
      throw;
    }
  }

private:
  auto terminalFor(std::string_view internalName,
                   const model::SourceSpan &span) const -> zbik::TerminalId {
    const auto terminal = bnf_.grammar().findTerminal(internalName);
    if (!terminal) {
      throw LexerGenerationError(
          span, "BNF model has no terminal for lexer rule `" +
                    std::string{internalName} + "`");
    }
    return *terminal;
  }

  void appendImplicitLiterals(std::vector<CompiledLexerRule> &rules) {
    for (const model::TerminalBinding &binding : bnf_.terminals()) {
      if (binding.kind == model::TerminalBindingKind::QualifiedReference) {
        throw LexerGenerationError(
            binding.sourceSpan,
            "qualified terminal `" + *binding.qualifier + "." +
                binding.spelling + "` has no local lexer rule");
      }
      if (binding.kind != model::TerminalBindingKind::Literal) continue;
      try {
        rules.push_back({binding.internalName, parser_.parse(binding.spelling),
                         terminalFor(binding.internalName, binding.sourceSpan),
                         std::nullopt, false, binding.sourceSpan});
      } catch (const zbik::RegexParseError &error) {
        throw LexerGenerationError(binding.sourceSpan, error.what());
      }
    }
  }

  auto compileRule(std::size_t index) -> const zbik::RegexAst & {
    if (states_[index] == VisitState::Complete) return *compiled_[index];
    const model::LexerRule &rule = document_.lexerRules[index];
    if (states_[index] == VisitState::Visiting) {
      throw LexerGenerationError(
          rule.span, "recursive lexer-rule reference involving `" + rule.name + "`");
    }
    states_[index] = VisitState::Visiting;
    compiled_[index] = compileAlternatives(rule.alternatives);
    states_[index] = VisitState::Complete;
    return *compiled_[index];
  }

  auto compileAlternatives(
      const std::vector<model::LexerAlternative> &alternatives)
      -> zbik::RegexAst {
    std::vector<zbik::RegexAst> result;
    result.reserve(alternatives.size());
    for (const model::LexerAlternative &alternative : alternatives) {
      if (alternative.explicitEmpty) {
        result.push_back(zbik::RegexAst::epsilon());
        continue;
      }
      std::vector<zbik::RegexAst> elements;
      elements.reserve(alternative.elements.size());
      for (const model::LexerElement &element : alternative.elements) {
        elements.push_back(compileElement(element));
      }
      result.push_back(zbik::RegexAst::concatenate(std::move(elements)));
    }
    return zbik::RegexAst::alternate(std::move(result));
  }

  auto compileElement(const model::LexerElement &element) -> zbik::RegexAst {
    zbik::RegexAst expression = compileAtom(element.atom);
    switch (element.quantifier) {
    case model::LexerQuantifier::One:
      if (element.lazy) {
        throw LexerGenerationError(
            element.span, "an unquantified lexer element cannot be lazy");
      }
      return expression;
    case model::LexerQuantifier::Optional:
      if (element.lazy) {
        throw LexerGenerationError(
            element.span, "lazy optional lexer elements are not supported");
      }
      return zbik::RegexAst::repeat(
          std::move(expression), zbik::RegexQuantifier::ZeroOrOne);
    case model::LexerQuantifier::ZeroOrMore:
      return zbik::RegexAst::repeat(
          std::move(expression),
          element.lazy ? zbik::RegexQuantifier::LazyZeroOrMore
                       : zbik::RegexQuantifier::ZeroOrMore);
    case model::LexerQuantifier::OneOrMore:
      return zbik::RegexAst::repeat(
          std::move(expression),
          element.lazy ? zbik::RegexQuantifier::LazyOneOrMore
                       : zbik::RegexQuantifier::OneOrMore);
    }
    throw LexerGenerationError(element.span, "unknown lexer quantifier");
  }

  auto compileAtom(const model::LexerAtom &atom) -> zbik::RegexAst {
    if (atom.kind == model::LexerAtomKind::TokenReference) {
      const auto found = ruleIndices_.find(atom.spelling);
      if (found == ruleIndices_.end()) {
        throw LexerGenerationError(
            atom.span, "unknown lexer-rule reference `" + atom.spelling + "`");
      }
      return compileRule(found->second);
    }
    if (atom.kind == model::LexerAtomKind::Group) {
      return compileAlternatives(atom.groupAlternatives);
    }
    if (atom.kind == model::LexerAtomKind::Wildcard) {
      return zbik::RegexAst::codePointClass(zbik::CodePointClass({
          {0, zbik::maxUnicodeCodePoint},
      }));
    }
    try {
      zbik::RegexAst expression = parser_.parse(atom.spelling);
      if (atom.kind == model::LexerAtomKind::NegatedLiteral ||
          atom.kind == model::LexerAtomKind::NegatedCharacterSet) {
        if (expression.kind() != zbik::RegexAst::Kind::CodePointClass) {
          throw LexerGenerationError(
              atom.span, "a negated lexer atom must denote one character");
        }
        expression = zbik::RegexAst::codePointClass(
            complement(expression.codePoints()));
      }
      return expression;
    } catch (const zbik::RegexParseError &error) {
      throw LexerGenerationError(atom.span, error.what());
    }
  }

  const model::SyntaxDocument &document_;
  const model::BnfModel &bnf_;
  zbik::RegexParser parser_;
  std::unordered_map<std::string, std::size_t> ruleIndices_;
  std::vector<VisitState> states_;
  std::vector<std::optional<zbik::RegexAst>> compiled_;
};

} // namespace

LexerGenerationError::LexerGenerationError(model::SourceSpan span,
                                           std::string message)
    : std::runtime_error(std::move(message)), span_(span) {}

auto LexerGenerationError::span() const noexcept -> const model::SourceSpan & {
  return span_;
}

GeneratedLexerAutomaton::GeneratedLexerAutomaton(
    std::vector<CompiledLexerRule> rules, zbik::Utf8Lexer lexer)
    : rules_(std::move(rules)), lexer_(std::move(lexer)) {}

auto GeneratedLexerAutomaton::rules() const noexcept
    -> const std::vector<CompiledLexerRule> & {
  return rules_;
}

auto GeneratedLexerAutomaton::dfaStateCount() const noexcept -> std::size_t {
  return lexer_.dfaStateCount();
}

auto GeneratedLexerAutomaton::transitionRangeCount() const noexcept
    -> std::size_t {
  return lexer_.transitionRangeCount();
}

auto GeneratedLexerAutomaton::runtimeLexer() const noexcept
    -> const zbik::Utf8Lexer & {
  return lexer_;
}

auto GeneratedLexerAutomaton::tokenize(std::string_view input) const
    -> AgasLexResult {
  const zbik::LexResult raw = lexer_.tokenize(input);
  AgasLexResult result;
  result.tokens.reserve(raw.tokens.size());
  result.parserTerminalIds.reserve(raw.tokens.size());
  for (const zbik::LexedToken &token : raw.tokens) {
    const CompiledLexerRule &rule = rules_.at(zbik::toIndex(token.terminal));
    if (!rule.terminal) {
      throw std::logic_error("a skipped lexer rule emitted a token");
    }
    result.tokens.push_back(
        {*rule.terminal, rule.channel, token.offset, token.text});
    if (!rule.channel) result.parserTerminalIds.push_back(*rule.terminal);
  }
  return result;
}

auto compileLexerAutomaton(const model::SyntaxDocument &document,
                           const model::BnfModel &bnf, bool contextualSource)
    -> GeneratedLexerAutomaton {
  if (!document.lexerClasses.empty() && !contextualSource)
    throw LexerGenerationError(document.span,
        "lexer classes require parser-directed tokenization");
  return Compiler(document, bnf).compile();
}

} // namespace agas::generator
