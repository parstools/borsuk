#include "agas/model/Validation.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace agas::model {
namespace {

void addIssue(ValidationResult &result, DiagnosticSeverity severity,
              const SourceSpan &span, std::string message) {
  result.issues.push_back({severity, span, std::move(message)});
}

template <typename Item>
void validateUniqueNames(const std::vector<Item> &items, std::string_view kind,
                         ValidationResult &result) {
  std::unordered_map<std::string_view, const Item *> declarations;
  for (const Item &item : items) {
    if (!declarations.emplace(item.name, &item).second) {
      addIssue(result, DiagnosticSeverity::Error, item.span,
               "duplicate " + std::string(kind) + " `" + item.name + "`");
    }
  }
}

auto sameGrammarElement(const ParserElement &left, const ParserElement &right)
    -> bool {
  return left.symbol.kind == right.symbol.kind &&
         left.symbol.name == right.symbol.name &&
         left.symbol.qualifier == right.symbol.qualifier &&
         left.quantifier == right.quantifier;
}

auto sameProduction(const ParserAlternative &left,
                    const ParserAlternative &right) -> bool {
  return left.explicitEmpty == right.explicitEmpty &&
         left.elements.size() == right.elements.size() &&
         std::equal(left.elements.begin(), left.elements.end(),
                    right.elements.begin(), sameGrammarElement);
}

void validateAlternativeNames(const ParserRule &rule,
                              ValidationResult &result) {
  std::unordered_set<std::string_view> names;
  std::size_t namedCount = 0;
  for (const ParserAlternative &alternative : rule.alternatives) {
    if (!alternative.label.has_value()) {
      continue;
    }
    ++namedCount;
    if (rule.treeModifier == TreeModifier::Inline) {
      addIssue(result, DiagnosticSeverity::Error, alternative.span,
               "inline rule `" + rule.name + "` cannot name alternative `" +
                   *alternative.label + "`");
    }
    if (!names.insert(*alternative.label).second) {
      addIssue(result, DiagnosticSeverity::Error, alternative.span,
               "duplicate alternative name `" + *alternative.label +
                   "` in rule `" + rule.name + "`");
    }
  }

  if (rule.treeModifier != TreeModifier::Inline &&
      rule.alternatives.size() > 1 && namedCount != 0 &&
      namedCount != rule.alternatives.size()) {
    addIssue(result, DiagnosticSeverity::Error, rule.span,
             "multi-alternative node rule `" + rule.name +
                 "` must name either every alternative or none");
  }
}

void validateFields(const ParserRule &rule, ValidationResult &result) {
  for (const ParserAlternative &alternative : rule.alternatives) {
    std::unordered_set<std::string_view> fields;
    for (const ParserElement &element : alternative.elements) {
      if (element.fieldName.has_value() &&
          !fields.insert(*element.fieldName).second) {
        addIssue(result, DiagnosticSeverity::Error, element.span,
                 "duplicate field name `" + *element.fieldName +
                     "` in one alternative of rule `" + rule.name + "`");
      }
    }
  }
}

void reportDuplicateProductions(const ParserRule &rule,
                                ValidationResult &result) {
  for (std::size_t current = 0; current < rule.alternatives.size(); ++current) {
    for (std::size_t previous = 0; previous < current; ++previous) {
      if (sameProduction(rule.alternatives[previous],
                         rule.alternatives[current])) {
        addIssue(result, DiagnosticSeverity::Warning,
                 rule.alternatives[current].span,
                 "alternative duplicates an earlier production in rule `" +
                     rule.name + "` and may cause a reduce/reduce conflict");
        break;
      }
    }
  }
}

void validateParserReferences(
    const SyntaxDocument &document,
    const std::unordered_set<std::string_view> &parserRules,
    const std::unordered_set<std::string_view> &lexerRules,
    const std::unordered_set<std::string_view> &parserTerminals,
    ValidationResult &result) {
  for (std::size_t ruleIndex = 0; ruleIndex < document.parserRules.size();
       ++ruleIndex) {
    const ParserRule &rule = document.parserRules[ruleIndex];
    for (const ParserAlternative &alternative : rule.alternatives) {
      for (std::size_t elementIndex = 0;
           elementIndex < alternative.elements.size(); ++elementIndex) {
        const ParserElement &element = alternative.elements[elementIndex];
        if (element.symbol.kind == ParserSymbolKind::RuleReference &&
            !parserRules.contains(element.symbol.name)) {
          addIssue(result, DiagnosticSeverity::Error, element.symbol.span,
                   "undefined parser rule `" + element.symbol.name + "`");
        } else if (element.symbol.kind == ParserSymbolKind::TokenReference) {
          if (element.symbol.name == "EOF") {
            const bool structuralEof =
                ruleIndex == 0 &&
                elementIndex + 1 == alternative.elements.size() &&
                element.quantifier == Quantifier::One;
            if (!structuralEof) {
              addIssue(result, DiagnosticSeverity::Error, element.span,
                       "EOF is allowed only as the final symbol of a "
                       "start-rule alternative");
            } else if (element.fieldName.has_value()) {
              addIssue(result, DiagnosticSeverity::Error, element.span,
                       "structural EOF cannot be stored in AST field `" +
                           *element.fieldName + "`");
            }
          } else if (!lexerRules.contains(element.symbol.name)) {
            addIssue(result, DiagnosticSeverity::Error, element.symbol.span,
                     "undefined lexer rule `" + element.symbol.name + "`");
          } else if (!parserTerminals.contains(element.symbol.name)) {
            addIssue(result, DiagnosticSeverity::Error, element.symbol.span,
                     "parser rule cannot reference fragment lexer rule `" +
                         element.symbol.name + "`");
          }
        }
      }
    }
  }
}

void reportUnreachableParserRules(const SyntaxDocument &document,
                                  ValidationResult &result) {
  if (document.parserRules.empty())
    return;

  std::unordered_map<std::string_view, std::size_t> ruleIndexes;
  for (std::size_t index = 0; index < document.parserRules.size(); ++index)
    ruleIndexes.emplace(document.parserRules[index].name, index);

  std::vector<bool> reachable(document.parserRules.size());
  std::vector<std::size_t> pending{0};
  reachable[0] = true;
  while (!pending.empty()) {
    const auto index = pending.back();
    pending.pop_back();
    for (const ParserAlternative &alternative :
         document.parserRules[index].alternatives) {
      for (const ParserElement &element : alternative.elements) {
        if (element.symbol.kind != ParserSymbolKind::RuleReference)
          continue;
        const auto found = ruleIndexes.find(element.symbol.name);
        if (found != ruleIndexes.end() && !reachable[found->second]) {
          reachable[found->second] = true;
          pending.push_back(found->second);
        }
      }
    }
  }

  for (std::size_t index = 1; index < document.parserRules.size(); ++index) {
    if (!reachable[index]) {
      const ParserRule &rule = document.parserRules[index];
      addIssue(result, DiagnosticSeverity::Warning, rule.span,
               "parser rule `" + rule.name +
                   "` is unreachable from the start rule `" +
                   document.parserRules.front().name + "`");
    }
  }
}

void validateConflictPreferences(
    const SyntaxDocument &document,
    const std::unordered_set<std::string_view> &parserTerminals,
    const std::unordered_map<std::string_view, const LexerRule *> &lexerRules,
    ValidationResult &result) {
  std::unordered_set<std::string> declarations;
  for (const ConflictPreference &preference : document.conflictPreferences) {
    if (!parserTerminals.contains(preference.shiftTerminal)) {
      addIssue(result, DiagnosticSeverity::Error, preference.span,
               "conflict policy names unknown parser-visible terminal `" +
                   preference.shiftTerminal + "`");
    }
    const auto lexer = lexerRules.find(preference.shiftTerminal);
    if (lexer != lexerRules.end() &&
        std::any_of(lexer->second->commands.begin(), lexer->second->commands.end(),
                    [](const LexerCommand &command) {
                      return command.name == "skip" ||
                             command.name == "channel";
                    })) {
      addIssue(result, DiagnosticSeverity::Error, preference.span,
               "conflict policy terminal `" + preference.shiftTerminal +
                   "` does not emit on the default channel");
    }
    const auto rule = std::find_if(
        document.parserRules.begin(), document.parserRules.end(),
        [&](const ParserRule &candidate) {
          return candidate.name == preference.reduceRule;
        });
    if (rule == document.parserRules.end()) {
      addIssue(result, DiagnosticSeverity::Error, preference.span,
               "conflict policy names unknown parser rule `" +
                   preference.reduceRule + "`");
    } else if (std::none_of(rule->alternatives.begin(), rule->alternatives.end(),
                            [&](const ParserAlternative &alternative) {
                              return alternative.label ==
                                     preference.reduceAlternative;
                            })) {
      addIssue(result, DiagnosticSeverity::Error, preference.span,
               "conflict policy names unknown alternative `" +
                   preference.reduceRule + "#" +
                   preference.reduceAlternative + "`");
    }
    const std::string identity = preference.shiftTerminal + ":" +
                                 preference.reduceRule + "#" +
                                 preference.reduceAlternative;
    if (!declarations.insert(identity).second) {
      addIssue(result, DiagnosticSeverity::Error, preference.span,
               "duplicate conflict policy for `" + identity + "`");
    }
  }
}

void validateLexerAlternatives(
    const std::vector<LexerAlternative> &alternatives,
    const std::unordered_set<std::string_view> &lexerRules,
    ValidationResult &result) {
  for (const LexerAlternative &alternative : alternatives) {
    for (const LexerElement &element : alternative.elements) {
      if (element.atom.kind == LexerAtomKind::TokenReference &&
          !lexerRules.contains(element.atom.spelling)) {
        addIssue(result, DiagnosticSeverity::Error, element.atom.span,
                 "undefined lexer rule `" + element.atom.spelling + "`");
      } else if (element.atom.kind == LexerAtomKind::Group) {
        validateLexerAlternatives(element.atom.groupAlternatives, lexerRules,
                                  result);
      }
    }
  }
}

auto hasLexerCommand(const LexerRule &rule, std::string_view name) -> bool {
  return std::any_of(
      rule.commands.begin(), rule.commands.end(),
      [name](const LexerCommand &command) { return command.name == name; });
}

void validateLexerCommands(
    const SyntaxDocument &document,
    const std::unordered_set<std::string_view> &declaredChannels,
    ValidationResult &result) {
  for (const LexerRule &rule : document.lexerRules) {
    std::unordered_set<std::string_view> commands;
    bool skips = false;
    bool changesChannel = false;

    if (rule.fragment && !rule.commands.empty()) {
      addIssue(result, DiagnosticSeverity::Error, rule.commands.front().span,
               "fragment lexer rule `" + rule.name +
                   "` cannot have lexer commands");
    }

    for (const LexerCommand &command : rule.commands) {
      if (command.name != "require" && !commands.insert(command.name).second) {
        addIssue(result, DiagnosticSeverity::Error, command.span,
                 "duplicate lexer command `" + command.name + "` in rule `" +
                     rule.name + "`");
      }

      if (command.name == "skip") {
        skips = true;
        if (command.argument.has_value()) {
          addIssue(result, DiagnosticSeverity::Error, command.span,
                   "lexer command `skip` does not take an argument");
        }
      } else if (command.name == "require") {
        // Class references are validated together with parser scopes.
      } else if (command.name == "channel") {
        changesChannel = true;
        if (!command.argument.has_value()) {
          addIssue(result, DiagnosticSeverity::Error, command.span,
                   "lexer command `channel` requires an argument");
        } else if (!declaredChannels.contains(*command.argument)) {
          addIssue(result, DiagnosticSeverity::Error, command.span,
                   "undefined lexer channel `" + *command.argument + "`");
        }
      } else {
        addIssue(result, DiagnosticSeverity::Error, command.span,
                 "unsupported lexer command `" + command.name + "`");
      }
    }

    if (skips && changesChannel) {
      addIssue(result, DiagnosticSeverity::Error, rule.span,
               "lexer rule `" + rule.name +
                   "` cannot combine `skip` and `channel`");
    }
  }
}

void validateParserVisibleLexerRules(
    const SyntaxDocument &document,
    const std::unordered_map<std::string_view, const LexerRule *> &lexerRules,
    bool legacyMode, ValidationResult &result) {
  for (const ParserRule &parserRule : document.parserRules) {
    for (const ParserAlternative &alternative : parserRule.alternatives) {
      for (const ParserElement &element : alternative.elements) {
        if (element.symbol.kind != ParserSymbolKind::TokenReference ||
            element.symbol.name == "EOF") {
          continue;
        }
        const auto found = lexerRules.find(element.symbol.name);
        if (found == lexerRules.end() || found->second->fragment) {
          continue;
        }
        if (hasLexerCommand(*found->second, "skip") ||
            hasLexerCommand(*found->second, "channel")) {
          addIssue(result,
                   legacyMode ? DiagnosticSeverity::Warning
                              : DiagnosticSeverity::Error,
                   element.symbol.span,
                   "parser rule cannot reference lexer rule `" +
                       element.symbol.name +
                       "` because it does not emit on the default channel");
        }
      }
    }
  }
}

} // namespace

auto ValidationResult::valid() const noexcept -> bool {
  return std::none_of(issues.begin(), issues.end(),
                      [](const ValidationIssue &issue) {
                        return issue.severity == DiagnosticSeverity::Error;
                      });
}

auto validateSyntaxModel(const SyntaxDocument &document) -> ValidationResult {
  ValidationResult result;
  validateUniqueNames(document.options, "option", result);
  validateUniqueNames(document.lexerClasses, "lexer class", result);
  if (document.lexerClasses.size() > 64)
    addIssue(result, DiagnosticSeverity::Error, document.span,
             "at most 64 lexer classes are supported");
  std::unordered_set<std::string> classes;
  for (const auto &entry : document.lexerClasses) {
    classes.insert(entry.name);
    if (entry.value.kind != OptionValueKind::Boolean)
      addIssue(result, DiagnosticSeverity::Error, entry.span,
               "lexer class initial state must be true or false");
  }
  auto checkClasses = [&](const auto &commands, bool parser) {
    std::unordered_set<std::string> seen;
    for (const auto &command : commands) {
      if (!parser && command.name != "require") continue;
      if (parser && command.name != "enable" && command.name != "disable")
        addIssue(result, DiagnosticSeverity::Error, command.span,
                 "parser lexer context accepts only enable and disable");
      if (!command.argument || !classes.contains(*command.argument))
        addIssue(result, DiagnosticSeverity::Error, command.span,
                 "unknown or missing lexer class in `" + command.name + "`");
      else if (!seen.insert(*command.argument).second)
        addIssue(result, DiagnosticSeverity::Error, command.span,
                 "duplicate or contradictory lexer class `" + *command.argument + "`");
    }
  };
  for (const auto &rule : document.parserRules) checkClasses(rule.lexerContext, true);
  for (const auto &rule : document.lexerRules) checkClasses(rule.commands, false);

  validateUniqueNames(document.channels, "channel", result);
  validateUniqueNames(document.parserRules, "parser rule", result);
  validateUniqueNames(document.lexerRules, "lexer rule", result);

  std::unordered_set<std::string_view> parserRules;
  std::unordered_set<std::string_view> lexerRules;
  std::unordered_set<std::string_view> parserTerminals;
  std::unordered_set<std::string_view> declaredChannels;
  std::unordered_map<std::string_view, const LexerRule *> lexerRuleByName;
  const bool legacyMode =
      std::any_of(document.options.begin(), document.options.end(),
                  [](const Option &option) {
                    return option.name == "legacy" &&
                           option.value.kind == OptionValueKind::Boolean &&
                           option.value.spelling == "true";
                  });
  for (const Channel &channel : document.channels) {
    declaredChannels.insert(channel.name);
  }
  for (const ParserRule &rule : document.parserRules) {
    parserRules.insert(rule.name);
    validateAlternativeNames(rule, result);
    validateFields(rule, result);
    reportDuplicateProductions(rule, result);
  }
  for (const LexerRule &rule : document.lexerRules) {
    lexerRules.insert(rule.name);
    lexerRuleByName.emplace(rule.name, &rule);
    if (!rule.fragment) {
      parserTerminals.insert(rule.name);
    }
  }

  validateParserReferences(document, parserRules, lexerRules, parserTerminals,
                           result);
  reportUnreachableParserRules(document, result);
  validateConflictPreferences(document, parserTerminals, lexerRuleByName,
                              result);
  for (const LexerRule &rule : document.lexerRules) {
    validateLexerAlternatives(rule.alternatives, lexerRules, result);
  }
  validateLexerCommands(document, declaredChannels, result);
  validateParserVisibleLexerRules(document, lexerRuleByName, legacyMode,
                                  result);
  const auto warningsAsErrors = std::find_if(
      document.options.begin(), document.options.end(),
      [](const Option &option) { return option.name == "warningsAsErrors"; });
  if (warningsAsErrors != document.options.end()) {
    if (warningsAsErrors->value.kind != OptionValueKind::Boolean) {
      addIssue(result, DiagnosticSeverity::Error, warningsAsErrors->value.span,
               "option `warningsAsErrors` must be `true` or `false`");
    } else if (warningsAsErrors->value.spelling == "true") {
      for (ValidationIssue &issue : result.issues) {
        if (issue.severity == DiagnosticSeverity::Warning)
          issue.severity = DiagnosticSeverity::Error;
      }
    }
  }
  return result;
}

} // namespace agas::model
