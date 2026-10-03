#include "CompletenessInternal.h"
#include "AssignmentPolicies.h"
#include "CollectionOperations.h"
#include "ConditionPolicies.h"
#include "ModelBindingsInternal.h"
#include "SemanticInputStorage.h"
#include "SemanticPolicies.h"
#include "agsem/CheckedSemantics.h"

#include "agas/model/AstChain.h"
#include "agas/model/BnfLowering.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace agsem {
namespace {

using Value = agas::runtime::AstValue;

auto field(const Value &value, std::string_view name) -> const Value & {
  for (std::size_t index = 0; index < value.fieldNames.size(); ++index)
    if (value.fieldNames[index] == name)
      return value.elements[index];
  std::string available;
  for (const auto &entry : value.fieldNames)
    available += (available.empty() ? "" : ", ") + entry;
  throw std::runtime_error("missing AST field " + std::string{name} + " in " +
                           value.typeName + " (available: " + available + ")");
}

auto optional(const Value &value) -> const Value * {
  if (value.kind != agas::runtime::AstValueKind::Optional)
    throw std::runtime_error("expected optional AST value");
  return value.elements.empty() ? nullptr : &value.elements.front();
}

auto token(const Value &value) -> std::string {
  if (value.kind != agas::runtime::AstValueKind::Token)
    throw std::runtime_error("expected token in typed typed subset");
  return value.tokenText;
}

auto typeName(const Value &value) -> std::string {
  if (value.kind == agas::runtime::AstValueKind::Token)
    return token(value);
  if (value.typeName != "typeExpression")
    throw std::runtime_error("unsupported type syntax");
  const auto name = token(field(value, "name"));
  const auto *arguments = optional(field(value, "arguments"));
  if (!arguments)
    return name;
  const auto &rest = field(*arguments, "rest");
  if (!rest.elements.empty())
    throw std::runtime_error("type accepts one argument in typed subset: " +
                             name);
  return name + "<" + typeName(field(*arguments, "first")) + ">";
}

auto simpleToken(const Value &value) -> std::string {
  if (value.kind == agas::runtime::AstValueKind::Token)
    return token(value);
  if (value.typeName == "analysisEnding")
    return simpleToken(field(value, "value"));
  if (value.typeName == "actionExpression" &&
      field(value, "rest").elements.empty() &&
      !optional(field(value, "choice")))
    return simpleToken(field(value, "first"));
  if (value.typeName == "actionUnary" &&
      field(value, "prefixes").elements.empty() &&
      field(value, "suffixes").elements.empty())
    return simpleToken(field(value, "atom"));
  throw std::runtime_error("expected a simple token in program analysis");
}

auto quotedText(const Value &value) -> std::string {
  const auto spelling = simpleToken(value);
  if (spelling.size() < 2 || spelling.front() != '"' ||
      spelling.back() != '"' || spelling.find('\\') != std::string::npos)
    throw std::runtime_error(
        "expected a plain string literal in program analysis");
  return spelling;
}

auto rustCallBinding(std::string_view name, std::string_view code)
    -> std::string {
  const std::string prefix = "    let " + std::string{name} + " = ";
  if (code.starts_with("match "))
    return prefix + std::string{code} + ";\n";
  if (prefix.size() + code.size() + 1 <= 100)
    return prefix + std::string{code} + ";\n";
  if (8 + code.size() + 1 <= 100)
    return "    let " + std::string{name} + " =\n        " + std::string{code} +
           ";\n";
  const auto open = code.find('(');
  const auto close = code.rfind(')');
  // Only split the arguments of a single outer call. Collection expansions
  // can contain grouped receivers, macros and chained calls.
  std::size_t firstClose = std::string_view::npos;
  std::size_t nesting = 0;
  bool quoted = false;
  bool escaped = false;
  for (std::size_t index = open; index < code.size(); ++index) {
    const auto character = code[index];
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (character == '\\')
        escaped = true;
      else if (character == '"')
        quoted = false;
    } else if (character == '"')
      quoted = true;
    else if (character == '(')
      ++nesting;
    else if (character == ')' && --nesting == 0) {
      firstClose = index;
      break;
    }
  }
  if (open == std::string_view::npos || firstClose != close || close <= open)
    return "    let " + std::string{name} + " =\n        " + std::string{code} + ";\n";
  std::ostringstream output;
  output << prefix << code.substr(0, open + 1) << '\n';
  std::size_t start = open + 1;
  std::size_t depth = 0;
  for (std::size_t index = start; index < close; ++index) {
    if (code[index] == '(')
      ++depth;
    else if (code[index] == ')')
      --depth;
    else if (code[index] == ',' && depth == 0) {
      output << "        " << code.substr(start, index - start) << ",\n";
      start = index + 2;
      ++index;
    }
  }
  output << "        " << code.substr(start, close - start) << ",\n"
         << "    )" << code.substr(close + 1) << ";\n";
  return output.str();
}

auto referencesName(const Value &value, std::string_view name) -> bool {
  if (value.kind == agas::runtime::AstValueKind::Token &&
      value.tokenText == name)
    return true;
  for (const auto &element : value.elements)
    if (referencesName(element, name))
      return true;
  return false;
}

auto assignsAnalyzeDestination(const Value &value, std::string_view name)
    -> bool {
  if (value.typeName == "analyzeStatement") {
    const auto *destination = optional(field(value, "destination"));
    if (destination && token(field(*destination, "name")) == name)
      return true;
  }
  for (const auto &element : value.elements)
    if (assignsAnalyzeDestination(element, name))
      return true;
  return false;
}

struct RuleAlternative {
  std::string label;
  std::vector<std::string> tokenFields;
  std::vector<std::string> optionalTokenFields;
  std::vector<std::pair<std::string, std::string>> childFields;
  std::vector<std::pair<std::string, std::string>> optionalChildFields;
  std::vector<std::pair<std::string, std::string>> childListFields;
  std::vector<const Value *> bodies;
  std::set<std::string> usedInputs;
  std::optional<std::string> forwardedRule;
  std::optional<std::string> actionDispatchType;
  std::optional<std::pair<std::string, std::string>> collapsedChild;
  bool inlineRecord{};
  bool noActionUnit{};
  std::optional<std::uint32_t> tokenDispatchKind;
  std::optional<std::string> tokenDispatchField;
};

enum class RuleAstKind { Node, Record, Token, Mixed };

struct RuleAnalyses {
  std::string name;
  std::string resultType;
  std::vector<std::pair<std::string, std::string>> inputs;
  std::vector<RuleAlternative> alternatives;
  RuleAstKind astKind{RuleAstKind::Node};
  bool inlineRule{};
};

struct AnalyzerSignature {
  std::string resultType;
  std::vector<std::pair<std::string, std::string>> inputs;
};

struct CodegenContract {
  std::string contextType;
  std::map<std::string, AnalyzerSignature> analyzers;
  std::set<std::string> explicitAnalyzers;
  std::vector<std::pair<std::string, std::string>> inherited;
};

auto codegenContract(const Value &root, bool partial = false)
    -> CodegenContract {
  const auto *model = optional(field(root, "model"));
  if (!model) {
    if (partial)
      return {};
    throw std::runtime_error("Rust generation needs a semantic_model block");
  }
  CodegenContract result;
  for (const auto &declaration : field(*model, "declarations").elements) {
    if (declaration.typeName == "rustContextDeclaration") {
      if (!result.contextType.empty())
        throw std::runtime_error("duplicate rust_context declaration");
      result.contextType = token(field(declaration, "name"));
    } else if (declaration.typeName == "inheritedDeclaration") {
      const auto name = token(field(declaration, "name"));
      if (std::ranges::any_of(result.inherited, [&](const auto &item) {
            return item.first == name;
          }))
        throw std::runtime_error("duplicate inherited attribute: " + name);
      result.inherited.emplace_back(name, typeName(field(declaration, "type")));
    } else if (declaration.typeName == "analyzerSignature") {
      const auto name = token(field(declaration, "name"));
      AnalyzerSignature signature{
          typeName(field(field(declaration, "result"), "type")), {}};
      const auto *parameters = optional(field(declaration, "parameters"));
      if (parameters) {
        const auto add = [&](const Value &parameter) {
          const auto parameterName = token(field(parameter, "name"));
          const auto *annotation = optional(field(parameter, "annotation"));
          if (!annotation)
            throw std::runtime_error("analyzer parameter needs a type: " +
                                     name + "." + parameterName);
          if (std::ranges::any_of(signature.inputs, [&](const auto &input) {
                return input.first == parameterName;
              }))
            throw std::runtime_error("duplicate analyzer parameter: " + name +
                                     "." + parameterName);
          signature.inputs.emplace_back(parameterName,
                                        typeName(field(*annotation, "value")));
        };
        if (parameters->typeName == "functionParameter") {
          add(*parameters);
        } else {
          add(field(*parameters, "first"));
          for (const auto &entry : field(*parameters, "rest").elements)
            add(entry);
        }
      }
      result.explicitAnalyzers.insert(name);
      if (!result.analyzers.emplace(name, std::move(signature)).second)
        throw std::runtime_error("duplicate analyzer signature: " + name);
    }
  }
  if (result.contextType.empty() && !partial)
    throw std::runtime_error("Rust generation needs rust_context declaration");
  return result;
}

auto checkedRuleAnalyses(const Value &root,
                         const agas::model::SyntaxDocument &grammar,
                         const CodegenContract &contract)
    -> std::vector<RuleAnalyses> {
  const auto &rules = field(root, "rules").elements;
  if (rules.size() != grammar.parserRules.size())
    throw std::runtime_error(
        "parser rule count differs from source grammar: sema has " +
        std::to_string(rules.size()) + ", grammar has " +
        std::to_string(grammar.parserRules.size()));
  const auto bnf = agas::model::lowerToBnf(grammar);

  std::vector<RuleAnalyses> result;
  for (std::size_t ruleIndex = 0; ruleIndex < rules.size(); ++ruleIndex) {
    const auto &rule = rules[ruleIndex];
    const auto &sourceRule = grammar.parserRules[ruleIndex];
    const auto context = "rule " + sourceRule.name;
    if (rule.typeName != "parserRuleSpec" ||
        token(field(rule, "name")) != sourceRule.name ||
        token(field(rule, "kind")) !=
            (sourceRule.treeModifier == agas::model::TreeModifier::Node
                 ? "node"
                 : "inline"))
      throw std::runtime_error(context +
                               " differs from source grammar in name or kind");
    std::vector<const Value *> alternatives{&field(rule, "first")};
    for (const auto &rest : field(rule, "rest").elements)
      alternatives.push_back(&rest);
    if (alternatives.size() != sourceRule.alternatives.size())
      throw std::runtime_error(
          context +
          " alternative count differs from source grammar: sema has " +
          std::to_string(alternatives.size()) + ", grammar has " +
          std::to_string(sourceRule.alternatives.size()));
    const auto declared = contract.analyzers.find(sourceRule.name);
    if (declared == contract.analyzers.end())
      throw std::runtime_error("missing analyzer signature for rule " +
                               sourceRule.name);
    RuleAnalyses checkedRule{sourceRule.name,
                             declared->second.resultType,
                             declared->second.inputs,
                             {}};
    checkedRule.inlineRule =
        sourceRule.treeModifier == agas::model::TreeModifier::Inline;
    std::set<std::string> labels;
    std::set<std::string> forwardedTypes;
    std::set<std::uint32_t> forwardedTokens;
    for (std::size_t alternativeIndex = 0;
         alternativeIndex < alternatives.size(); ++alternativeIndex) {
      const auto &alternative = *alternatives[alternativeIndex];
      const auto &source = sourceRule.alternatives[alternativeIndex];
      const auto alternativeContext =
          context + " alternative " + std::to_string(alternativeIndex + 1);
      if (alternative.variantName != "NonemptyAlternative")
        throw std::runtime_error(alternativeContext +
                                 " must be nonempty for Rust generation");
      const auto *semaLabel = optional(field(alternative, "label"));
      bool inlineRecord = false;
      bool inlineToken = false;
      if (!source.label) {
        if (semaLabel)
          throw std::runtime_error(alternativeContext +
                                   " has a label absent from source grammar");
        const bool singleNodeAlternative =
            sourceRule.treeModifier == agas::model::TreeModifier::Node &&
            sourceRule.alternatives.size() == 1 &&
            (source.elements.size() > 1 ||
             !agas::model::astChainOperand(grammar, source));
        if (!singleNodeAlternative) {
          const auto forwardedChild =
              [](const agas::model::ParserElement &element) {
                return element.fieldName &&
                       element.symbol.kind ==
                           agas::model::ParserSymbolKind::RuleReference &&
                       element.quantifier == agas::model::Quantifier::One;
              };
          const auto forwardedToken =
              [](const agas::model::ParserElement &element) {
                return element.fieldName &&
                       element.symbol.kind ==
                           agas::model::ParserSymbolKind::TokenReference &&
                       element.quantifier == agas::model::Quantifier::One;
              };
          const auto anonymousToken =
              [](const agas::model::ParserElement &element) {
                return !element.fieldName &&
                       element.symbol.kind ==
                           agas::model::ParserSymbolKind::TokenReference &&
                       element.quantifier == agas::model::Quantifier::One;
              };
          const auto fieldCount =
              std::ranges::count_if(source.elements, [](const auto &element) {
                return element.fieldName.has_value();
              });
          const bool singleChild =
              fieldCount == 1 &&
              std::ranges::all_of(source.elements, [&](const auto &element) {
                return forwardedChild(element) || anonymousToken(element);
              });
          inlineToken =
              fieldCount == 1 &&
              std::ranges::all_of(source.elements, [&](const auto &element) {
                return forwardedToken(element) || anonymousToken(element);
              });
          inlineRecord =
              fieldCount >= 2 &&
              std::ranges::all_of(source.elements, [&](const auto &element) {
                return element.fieldName.has_value() || anonymousToken(element);
              });
          if (sourceRule.treeModifier != agas::model::TreeModifier::Inline ||
              (!singleChild && !inlineToken && !inlineRecord))
            throw std::runtime_error(
                alternativeContext +
                " is unlabeled; Rust generation needs a "
                "single node alternative, forwarded child, forwarded token, "
                "or one inline record alternative");
        }
      } else if (!semaLabel ||
                 token(field(*semaLabel, "name")) != *source.label ||
                 !labels.insert(*source.label).second)
        throw std::runtime_error(alternativeContext +
                                 " label differs from source grammar");
      const auto &elements = field(alternative, "elements").elements;
      if (elements.size() != source.elements.size())
        throw std::runtime_error(
            alternativeContext +
            " differs from source grammar in element count");
      RuleAlternative checked{};
      checked.label = source.label.value_or("");
      checked.inlineRecord = inlineRecord;
      std::set<std::string> fields;
      for (std::size_t index = 0; index < elements.size(); ++index) {
        const auto &element = elements[index];
        const auto &sourceElement = source.elements[index];
        const auto &fieldName = sourceElement.fieldName;
        const auto &symbol = sourceElement.symbol.name;
        const auto quantifier = sourceElement.quantifier;
        const bool repeated =
            quantifier == agas::model::Quantifier::ZeroOrMore ||
            quantifier == agas::model::Quantifier::OneOrMore;
        const bool optionalElement =
            quantifier == agas::model::Quantifier::Optional;
        const auto *suffix = element.kind == agas::runtime::AstValueKind::Token
                                 ? nullptr
                                 : optional(field(element, "suffix"));
        const bool suffixMatches =
            quantifier == agas::model::Quantifier::One
                ? !suffix
                : (optionalElement && suffix && token(*suffix) == "?") ||
                      (quantifier == agas::model::Quantifier::ZeroOrMore &&
                       suffix && token(*suffix) == "*") ||
                      (quantifier == agas::model::Quantifier::OneOrMore &&
                       suffix && token(*suffix) == "+");
        const bool semaMatches =
            !fieldName ? (element.kind == agas::runtime::AstValueKind::Token
                              ? token(element) == symbol
                              : element.variantName == "Bare" &&
                                    token(field(element, "symbol")) == symbol)
                       : element.variantName == "Labeled" &&
                             token(field(element, "field")) == *fieldName &&
                             token(field(element, "symbol")) == symbol;
        if (!semaMatches || !suffixMatches ||
            (sourceElement.symbol.kind !=
                 agas::model::ParserSymbolKind::TokenReference &&
             sourceElement.symbol.kind !=
                 agas::model::ParserSymbolKind::RuleReference) ||
            (repeated && (sourceElement.symbol.kind !=
                              agas::model::ParserSymbolKind::RuleReference ||
                          !fieldName)))
          throw std::runtime_error(alternativeContext +
                                   " differs from source grammar in element " +
                                   std::to_string(index + 1));
        if (fieldName) {
          if (!fields.insert(*fieldName).second)
            throw std::runtime_error("duplicate AST field: " + *fieldName);
          if (repeated)
            checked.childListFields.emplace_back(*fieldName, symbol);
          else if (optionalElement &&
                   sourceElement.symbol.kind ==
                       agas::model::ParserSymbolKind::TokenReference)
            checked.optionalTokenFields.push_back(*fieldName);
          else if (optionalElement)
            checked.optionalChildFields.emplace_back(*fieldName, symbol);
          else if (sourceElement.symbol.kind ==
                   agas::model::ParserSymbolKind::TokenReference)
            checked.tokenFields.push_back(*fieldName);
          else
            checked.childFields.emplace_back(*fieldName, symbol);
        }
      }
      const auto &actions = field(alternative, "actions").elements;
      for (const auto &action : actions) {
        if (action.typeName == "analysisBlock")
          checked.bodies.push_back(&field(action, "body"));
        else if (action.typeName != "executionResult" &&
                 action.typeName != "analysisStatus")
          throw std::runtime_error(alternativeContext +
                                   " has an unsupported action block");
      }
      for (const auto &action : actions)
        if (action.typeName == "analysisStatus" &&
            readAnalysisStatus(action).declaredState ==
                ObligationState::NoAction) {
          if (!contract.explicitAnalyzers.contains(sourceRule.name) ||
              checkedRule.resultType != "Unit" ||
              std::ranges::any_of(checked.bodies, [](const Value *body) {
                return !field(*body, "statements").elements.empty();
              }))
            throw std::runtime_error("no_action requires an explicit Unit "
                                     "interface and empty instructions");
          checked.noActionUnit = true;
        }
      if (checked.bodies.empty() && !checked.noActionUnit)
        throw std::runtime_error(
            alternativeContext +
            " needs an analysis block for Rust generation");
      if (!source.label &&
          sourceRule.treeModifier == agas::model::TreeModifier::Inline &&
          !inlineRecord && !inlineToken)
        checked.actionDispatchType = checked.childFields.front().second;
      else if (inlineToken) {
        const auto tokenField =
            std::ranges::find_if(source.elements, [](const auto &element) {
              return element.fieldName.has_value();
            });
        const auto terminal =
            bnf.grammar().findTerminal(tokenField->symbol.name);
        if (!terminal)
          throw std::runtime_error(alternativeContext +
                                   " refers to an unknown token kind");
        checked.tokenDispatchKind = terminal->value;
        checked.tokenDispatchField = *tokenField->fieldName;
        if (!forwardedTokens.insert(*checked.tokenDispatchKind).second)
          throw std::runtime_error(alternativeContext +
                                   " repeats a forwarded token kind");
      } else if (agas::model::astChainOperand(grammar, source) &&
                 checked.childFields.size() == 1 &&
                 checked.childListFields.empty() &&
                 checked.optionalChildFields.empty() &&
                 checked.optionalTokenFields.empty() &&
                 checked.tokenFields.empty()) {
        if (std::ranges::any_of(checked.bodies, [](const Value *body) {
              return !field(*body, "statements").elements.empty();
            }))
          checked.actionDispatchType = checked.childFields.front().second;
        else if (!checked.noActionUnit)
          checked.forwardedRule = checked.childFields.front().second;
      }
      if (sourceRule.treeModifier == agas::model::TreeModifier::Node &&
          sourceRule.alternatives.size() == 1 && !source.label &&
          agas::model::astChainOperand(grammar, source) &&
          checked.childFields.size() == 1 &&
          (!checked.childListFields.empty() ||
           !checked.optionalChildFields.empty()) &&
          checked.tokenFields.empty() && checked.optionalTokenFields.empty())
        checked.collapsedChild = checked.childFields.front();
      if (checked.forwardedRule || checked.actionDispatchType) {
        const auto &type = checked.forwardedRule ? *checked.forwardedRule
                                                 : *checked.actionDispatchType;
        if (!forwardedTypes.insert(type).second)
          throw std::runtime_error(
              alternativeContext +
              " cannot distinguish alternatives forwarding child AST type " +
              type);
      }
      checkedRule.alternatives.push_back(std::move(checked));
    }
    if (std::ranges::all_of(checkedRule.alternatives,
                            [](const auto &alternative) {
                              return alternative.tokenDispatchKind.has_value();
                            }))
      checkedRule.astKind = RuleAstKind::Token;
    else if (checkedRule.alternatives.size() == 1 &&
             checkedRule.alternatives.front().inlineRecord)
      checkedRule.astKind = RuleAstKind::Record;
    else if (std::ranges::any_of(
                 checkedRule.alternatives, [](const auto &alternative) {
                   return alternative.tokenDispatchKind.has_value() ||
                          alternative.inlineRecord;
                 }))
      checkedRule.astKind = RuleAstKind::Mixed;
    result.push_back(std::move(checkedRule));
  }
  if (contract.analyzers.size() != grammar.parserRules.size())
    throw std::runtime_error(
        "analyzer signatures contain rules absent from source grammar");
  return result;
}

auto rustType(std::string_view type) -> std::string {
  if (type == "Bool" || type == "Int")
    return type == "Bool" ? "bool" : "i64";
  if (type == "Text")
    return "&str";
  if (type == "OwnedText")
    return "String";
  if (type == "Unit")
    return "()";
  if (type.starts_with("Node<") && type.ends_with('>'))
    return "ast::AstValue";
  if (type.starts_with("Option<") && type.ends_with('>'))
    return "Option<" + rustType(type.substr(7, type.size() - 8)) + ">";
  if (type.starts_with("List<") && type.ends_with('>'))
    return "Vec<" + rustType(type.substr(5, type.size() - 6)) + ">";
  if (type.starts_with("Result<") && type.ends_with('>'))
    return "Result<" + rustType(type.substr(7, type.size() - 8)) +
           ", &'static str>";
  if (!type.empty() &&
      std::ranges::all_of(type,
                          [](char character) {
                            return (character >= 'A' && character <= 'Z') ||
                                   (character >= 'a' && character <= 'z') ||
                                   (character >= '0' && character <= '9') ||
                                   character == '_';
                          }) &&
      ((type.front() >= 'A' && type.front() <= 'Z') || type.front() == '_'))
    return std::string{type};
  throw std::runtime_error("unsupported type in typed Rust subset: " +
                           std::string{type});
}

auto rustParameterType(std::string_view type) -> std::string {
  if (type.starts_with("List<Node<") && type.ends_with(">>"))
    return "&[ast::AstValue]";
  if (type.starts_with("List<") && type.ends_with('>'))
    return "&[" + rustType(type.substr(5, type.size() - 6)) + "]";
  return rustType(type);
}

void collectRustNamedTypes(std::string_view type,
                           std::set<std::string> &names) {
  if (type == "Bool" || type == "Int" || type == "Text" ||
      type == "OwnedText" || type == "Unit")
    return;
  if (type.starts_with("Node<") && type.ends_with('>'))
    return;
  for (const std::string_view wrapper : {"Option<", "List<", "Result<"})
    if (type.starts_with(wrapper) && type.ends_with('>')) {
      collectRustNamedTypes(
          type.substr(wrapper.size(), type.size() - wrapper.size() - 1), names);
      return;
    }
  static_cast<void>(rustType(type));
  names.insert(std::string{type});
}

auto rustImports(const std::set<std::string> &names) -> std::string {
  if (names.empty())
    return {};
  if (names.size() == 1)
    return "use crate::" + *names.begin() + ";\n";
  std::string result = "use crate::{";
  bool first = true;
  for (const auto &name : names) {
    if (!first)
      result += ", ";
    first = false;
    result += name;
  }
  return result + "};\n";
}

auto resultValueType(std::string_view type) -> std::string {
  if (!type.starts_with("Result<") || !type.ends_with('>'))
    throw std::runtime_error("expected Result<T> type: " + std::string{type});
  return std::string{type.substr(7, type.size() - 8)};
}

void checkRustIdentifier(const std::string &name) {
  if (name.empty() ||
      !(name.front() == '_' || (name.front() >= 'a' && name.front() <= 'z')))
    throw std::runtime_error("unsupported Rust identifier: " + name);
  for (const auto character : name)
    if (!(character == '_' || (character >= 'a' && character <= 'z') ||
          (character >= 'A' && character <= 'Z') ||
          (character >= '0' && character <= '9')))
      throw std::runtime_error("unsupported Rust identifier: " + name);
  static const std::set<std::string> keywords{
      "as",     "async",  "await", "break",  "const",  "continue", "crate",
      "dyn",    "else",   "enum",  "extern", "false",  "fn",       "for",
      "if",     "impl",   "in",    "let",    "loop",   "match",    "mod",
      "move",   "mut",    "pub",   "ref",    "return", "self",     "Self",
      "static", "struct", "super", "trait",  "true",   "type",     "unsafe",
      "use",    "where",  "while"};
  if (keywords.contains(name) && name != "type")
    throw std::runtime_error("Rust keyword used as identifier: " + name);
}

void checkRustTypeIdentifier(const std::string &name) {
  if (name.empty() || name == "Self")
    throw std::runtime_error("unsupported Rust type identifier: " + name);
  if (name.front() >= 'A' && name.front() <= 'Z')
    checkRustIdentifier("_" + name);
  else
    checkRustIdentifier(name);
}

auto rustLocalName(std::string_view name) -> std::string {
  if (name == "type")
    return "r#type";
  std::string result;
  for (const auto character : name) {
    if (character >= 'A' && character <= 'Z') {
      result.push_back('_');
      result.push_back(static_cast<char>(character - 'A' + 'a'));
    } else {
      result.push_back(character);
    }
  }
  return result;
}

auto rustRuleName(std::string_view name) -> std::string {
  std::string result;
  for (const auto character : name) {
    if (character >= 'A' && character <= 'Z') {
      if (!result.empty() && result.back() != '_')
        result += '_';
      result += static_cast<char>(character - 'A' + 'a');
    } else {
      result += character;
    }
  }
  return result;
}

struct Parameter {
  std::string name;
  std::string type;
};

struct Signature {
  std::string name;
  std::vector<Parameter> parameters;
  std::string result;
  bool intrinsic{};
  bool mutates{};
  const Value *body{};
  bool returnPolicy{};
  bool assignmentPolicy{};
  bool conditionPolicy{};
  bool statementIrPolicy{};
  bool flowActions{};
  bool selectionPolicy{};
  bool selectionCandidates{};
  bool selectionCompletion{};
  std::string externalPath;
  bool externalContext{};
  bool needsExternalBinding{};
};

auto parameters(const Value &declaration, std::string_view functionName)
    -> std::vector<Parameter> {
  std::vector<Parameter> result;
  const auto *list = optional(field(declaration, "parameters"));
  if (!list)
    return result;
  const auto add = [&](const Value &value) {
    if (value.typeName != "functionParameter")
      throw std::runtime_error(
          "Rust generation needs typed parameters in function " +
          std::string{functionName});
    const auto name = token(field(value, "name"));
    checkRustIdentifier(name);
    const auto *annotation = optional(field(value, "annotation"));
    if (!annotation)
      throw std::runtime_error("parameter needs a type: " + name);
    const auto type = typeName(field(*annotation, "value"));
    static_cast<void>(rustType(type));
    if (std::ranges::any_of(result, [&](const Parameter &parameter) {
          return parameter.name == name;
        }))
      throw std::runtime_error("duplicate parameter: " + name);
    result.push_back({name, type});
  };
  if (list->typeName == "functionParameter") {
    add(*list);
  } else {
    add(field(*list, "first"));
    for (const auto &entry : field(*list, "rest").elements)
      add(entry);
  }
  return result;
}

auto signature(const Value &declaration) -> Signature {
  Signature result;
  result.name = token(field(declaration, "name"));
  checkRustIdentifier(result.name);
  result.parameters = parameters(declaration, result.name);
  const auto *resultType = optional(field(declaration, "result"));
  if (!resultType)
    throw std::runtime_error("function needs a result type: " + result.name);
  result.result = typeName(field(*resultType, "type"));
  static_cast<void>(rustType(result.result));
  result.intrinsic = declaration.typeName == "intrinsicDeclaration";
  result.mutates = optional(field(declaration, "effect")) != nullptr;
  if (result.intrinsic) {
    if (field(declaration, "tail").variantName != "External")
      throw std::runtime_error(
          "only external intrinsic declarations can be exported: " +
          result.name);
  } else {
    result.body = &field(declaration, "body");
  }
  return result;
}

void collectSignatures(const Value &value,
                       std::map<std::string, Signature> &signatures) {
  if (value.typeName == "functionDeclaration" ||
      value.typeName == "intrinsicDeclaration") {
    auto item = signature(value);
    const auto name = item.name;
    if (!signatures.emplace(name, std::move(item)).second)
      throw std::runtime_error("duplicate typed function: " + name);
  }
  for (const auto &element : value.elements)
    collectSignatures(element, signatures);
}

// Enum values are resolved only through explicit external type contracts.
using EnumSchemas = std::map<std::string, ContractSymbol>;

auto enumVariant(const EnumSchemas &schemas, const std::string &type,
                 const std::string &variant) -> std::string {
  const auto &symbol = schemas.at(type);
  const auto &schema = *symbol.type;
  if (schema.arity != 0)
    throw std::runtime_error(
        "generic enum constants require type arguments: " + type);
  const auto found = schema.variants.find(variant);
  if (found == schema.variants.end())
    throw std::runtime_error("unknown enum variant: " + type + "." + variant);
  if (!found->second.empty())
    throw std::runtime_error("enum variant requires payload: " + type + "." +
                             variant);
  std::string path;
  for (const auto &segment : symbol.rust)
    path += (path.empty() ? "" : "::") + segment;
  if (path.empty())
    path = "crate::" + type;
  return path + "::" + variant;
}

// Infer rule interfaces before the existing typed action checker runs.
// Type variables represent synthesized results; inherited types are explicit.
class AnalyzerInference {
public:
  AnalyzerInference(const std::map<std::string, Signature> &functions,
                    const CodegenContract &contract, const EnumSchemas &enums)
      : functions_(functions), enums_(enums), declared_(contract.analyzers) {
    for (const auto &[name, type] : contract.inherited)
      inherited_.emplace(name, type);
    for (const auto &[name, signature] : declared_) {
      unify("$" + name, signature.resultType);
      for (const auto &[input, type] : signature.inputs) {
        if (inherited_.contains(input) && inherited_.at(input) != type)
          throw std::runtime_error("inherited type mismatch: " + name + "." +
                                   input);
        inputs_[name].insert(input);
      }
    }
  }

  void run(const std::vector<RuleAnalyses> &rules, CodegenContract &contract,
           std::vector<Diagnostic> *diagnostics = nullptr) {
    for (const auto &rule : rules) {
      current_ = rule.name;
      for (const auto &alternative : rule.alternatives) {
        Environment environment;
        if (const auto declared = declared_.find(rule.name);
            declared != declared_.end())
          for (const auto &[name, type] : declared->second.inputs)
            environment.emplace(name, type);
        for (const auto &name : alternative.tokenFields)
          environment[name] = "Token";
        for (const auto &name : alternative.optionalTokenFields)
          environment[name] = "Option<Token>";
        for (const auto &[name, target] : alternative.childFields)
          environment[name] = "Node<" + target + ">";
        for (const auto &[name, target] : alternative.optionalChildFields)
          environment[name] = "Option<Node<" + target + ">>";
        for (const auto &[name, target] : alternative.childListFields)
          environment[name] = "List<Node<" + target + ">>";
        if (alternative.forwardedRule) {
          unify("$" + current_, "$" + *alternative.forwardedRule);
          calls_.push_back(
              {current_, *alternative.forwardedRule, {}, environment});
        }
        for (const auto *body : alternative.bodies) {
          try {
            block(*body, environment);
          } catch (const std::runtime_error &error) {
            if (!diagnostics)
              throw;
            diagnostics->push_back({Severity::Error, "action.invalid_inference",
                                    error.what(), ast::location(*body),
                                    rule.name});
          }
        }
      }
    }
    // OwnedText can be passed as Text. Resolve other anchors first so a Text
    // use does not erase an independently established OwnedText result.
    for (const bool textPass : {false, true})
      for (const auto &constraint : arguments_)
        if ((constraint.expected == "Text") == textPass) {
          current_ = constraint.rule;
          const auto actual = resolve(constraint.actual);
          try {
            if (!(actual == "OwnedText" && constraint.expected == "Text"))
              unify(actual, constraint.expected);
          } catch (const std::runtime_error &error) {
            if (!diagnostics)
              throw;
            diagnostics->push_back({Severity::Error,
                                    "action.invalid_inference",
                                    error.what(),
                                    {},
                                    current_});
          }
        }
    bool changed = true;
    while (changed) {
      changed = false;
      for (const auto &call : calls_) {
        current_ = call.caller;
        for (const auto &name : inputs_[call.callee]) {
          if (call.supplied.contains(name) || call.locals.contains(name))
            continue;
          if (!inherited_.contains(name))
            throw std::runtime_error("missing inherited attribute in " +
                                     current_ + ": " + name);
          changed |= inputs_[call.caller].insert(name).second;
        }
      }
    }
    for (const auto &rule : rules) {
      current_ = rule.name;
      const auto result = resolve("$" + rule.name);
      if (result.find('$') != std::string::npos || result == "None") {
        if (diagnostics)
          continue;
        throw std::runtime_error("cannot infer analyzer result for " +
                                 rule.name + "; add an analyzer signature");
      }
      if (const auto declared = declared_.find(rule.name);
          declared != declared_.end()) {
        for (const auto &input : inputs_[rule.name])
          if (std::ranges::none_of(
                  declared->second.inputs,
                  [&](const auto &item) { return item.first == input; }))
            throw std::runtime_error(
                "analyzer signature omits inherited attribute: " + rule.name +
                "." + input);
        continue;
      }
      AnalyzerSignature inferred{result, {}};
      for (const auto &[name, type] : contract.inherited)
        if (inputs_[rule.name].contains(name))
          inferred.inputs.emplace_back(name, type);
      contract.analyzers.emplace(rule.name, std::move(inferred));
    }
  }

private:
  using Environment = std::map<std::string, std::string>;
  struct Call {
    std::string caller;
    std::string callee;
    std::set<std::string> supplied;
    Environment locals;
  };
  struct ArgumentConstraint {
    std::string actual;
    std::string expected;
    std::string rule;
  };

  auto resolve(std::string type) const -> std::string {
    while (types_.contains(type))
      type = types_.at(type);
    if (type.starts_with("Option<") && type.ends_with('>'))
      return "Option<" + resolve(type.substr(7, type.size() - 8)) + ">";
    if (type.starts_with("List<") && type.ends_with('>'))
      return "List<" + resolve(type.substr(5, type.size() - 6)) + ">";
    return type;
  }

  void unify(std::string left, std::string right) {
    left = resolve(std::move(left));
    right = resolve(std::move(right));
    if (left == right)
      return;
    if (left.starts_with("List<") && left.ends_with('>') &&
        right.starts_with("List<") && right.ends_with('>')) {
      unify(left.substr(5, left.size() - 6), right.substr(5, right.size() - 6));
      return;
    }
    if (left.starts_with("Option<") && left.ends_with('>') &&
        right.starts_with("Option<") && right.ends_with('>')) {
      unify(left.substr(7, left.size() - 8),
            right.substr(7, right.size() - 8));
      return;
    }
    if ((left.starts_with('$') && right.find("<" + left + ">") != std::string::npos) ||
        (right.starts_with('$') && left.find("<" + right + ">") != std::string::npos))
      throw std::runtime_error("recursive inferred collection type in " + current_);
    if (left.starts_with('$'))
      types_[left] = right;
    else if (right.starts_with('$'))
      types_[right] = left;
    else
      throw std::runtime_error("inferred type mismatch in " + current_ + ": " +
                               left + " and " + right);
  }

  auto lookup(const std::string &name, const Environment &environment)
      -> std::string {
    if (environment.contains(name))
      return environment.at(name);
    if (inherited_.contains(name)) {
      inputs_[current_].insert(name);
      return inherited_.at(name);
    }
    throw std::runtime_error("unbound identifier in " + current_ + ": " + name);
  }

  auto expression(const Value &value, const Environment &environment)
      -> std::string {
    if (value.kind == agas::runtime::AstValueKind::Token) {
      const auto name = token(value);
      if (name == "true" || name == "false")
        return "Bool";
      if (name == "unit")
        return "Unit";
      if (name == "none")
        return "None";
      if (!name.empty() && (name.front() == '\"' || name.front() == '\''))
        return "Text";
      if (!name.empty() && name.front() >= '0' && name.front() <= '9')
        return "Int";
      return lookup(name, environment);
    }
    if (value.typeName == "actionExpression") {
      auto result = expression(field(value, "first"), environment);
      if (optional(field(value, "choice")))
        throw std::runtime_error(
            "unsupported conditional expression in inference");
      for (const auto &part : field(value, "rest").elements) {
        const auto right = expression(field(part, "value"), environment);
        const auto op = token(field(part, "operator"));
        if (op == "and") {
          unify(result, "Bool");
          unify(right, "Bool");
          result = "Bool";
        } else if (op == "==" || op == "!=") {
          if (result != "None" && right != "None")
            unify(result, right);
          result = "Bool";
        } else {
          unify(result, "Int");
          unify(right, "Int");
          result = "Int";
        }
      }
      return result;
    }
    if (value.typeName != "actionUnary" ||
        !field(value, "prefixes").elements.empty())
      throw std::runtime_error(
          "unsupported expression in analyzer inference: " + value.typeName);
    const auto &suffixes = field(value, "suffixes").elements;
    if (suffixes.empty())
      return expression(field(value, "atom"), environment);
    if (!environment.contains("List") && !inherited_.contains("List")) {
      if (const auto operation = collectionOperation(value)) {
        if (operation->name == "empty")
          return "List<" + collectionElementName(*operation->arguments[0]) + ">";
        const auto first = expression(*operation->arguments[0], environment);
        if (operation->name == "single")
          return "List<" + first + ">";
        const auto element = "$collection_" + std::to_string(value.sourceSpan.beginByte);
        arguments_.push_back({first, "List<" + element + ">", current_});
        arguments_.push_back({expression(*operation->arguments[1], environment), element, current_});
        return "List<" + element + ">";
      }
    }
    if (suffixes.size() != 1)
      throw std::runtime_error(
          "unsupported postfix expression in analyzer inference");
    const auto &suffix = suffixes.front();
    const auto name = token(field(value, "atom"));
    if (suffix.variantName == "Field") {
      const auto member = token(field(suffix, "name"));
      if (!environment.contains(name) && enums_.contains(name)) {
        enumVariant(enums_, name, member);
        return name;
      }
      if (name == "self" && member == "source")
        return "SourceRange";
      const auto base = lookup(name, environment);
      if (member == "text" && base == "Token")
        return "Text";
      if (member == "source" && (base == "Token" || base.starts_with("Node<")))
        return "SourceRange";
      if (member == "present" && base.starts_with("Option<"))
        return "Bool";
      throw std::runtime_error("unsupported attribute in analyzer inference: " +
                               name + "." + member);
    }
    if (suffix.variantName != "Call" || !functions_.contains(name))
      throw std::runtime_error("unresolved typed call: " + name);
    std::vector<std::string> arguments;
    if (const auto *list = optional(field(suffix, "arguments"))) {
      if (list->typeName == "argumentList") {
        arguments.push_back(expression(field(*list, "first"), environment));
        for (const auto &item : field(*list, "rest").elements)
          arguments.push_back(expression(item, environment));
      } else
        arguments.push_back(expression(*list, environment));
    }
    const auto &function = functions_.at(name);
    if (arguments.size() != function.parameters.size())
      throw std::runtime_error("wrong argument count for " + name);
    for (std::size_t index = 0; index < arguments.size(); ++index)
      arguments_.push_back(
          {arguments[index], function.parameters[index].type, current_});
    return function.result.starts_with("Result<")
               ? resultValueType(function.result)
               : function.result;
  }

  void block(const Value &body, Environment &environment) {
    for (const auto &statement : field(body, "statements").elements) {
      if (statement.typeName == "letStatement") {
        const auto type = expression(field(statement, "value"), environment);
        environment[token(field(statement, "name"))] = type;
      } else if (statement.typeName == "expressionOrAssignment") {
        if (const auto *assignment = optional(field(statement, "assignment"))) {
          const auto target = simpleToken(field(statement, "target"));
          const auto type =
              expression(field(*assignment, "value"), environment);
          if (target == "result") {
            unify("$" + current_, type);
            environment["result"] = "$" + current_;
          } else
            unify(lookup(target, environment), type);
        } else {
          static_cast<void>(
              expression(field(statement, "target"), environment));
        }
      } else if (statement.typeName == "analyzeStatement") {
        const auto type = lookup(token(field(statement, "child")), environment);
        const auto optionalChild =
            optional(field(statement, "optional")) != nullptr;
        const std::string prefix = optionalChild ? "Option<Node<" : "Node<";
        const std::string suffix = optionalChild ? ">>" : ">";
        if (!type.starts_with(prefix) || !type.ends_with(suffix))
          throw std::runtime_error("analyze requires a child rule node in " +
                                   current_);
        const auto child = type.substr(
            prefix.size(), type.size() - prefix.size() - suffix.size());
        Call call{current_, child, {}, environment};
        if (const auto *context = optional(field(statement, "context"))) {
          const auto collect = [&](const Value &argument) {
            const auto name = token(field(argument, "name"));
            if (!call.supplied.insert(name).second)
              throw std::runtime_error("duplicate analyze context: " + name);
            std::string expected;
            if (inherited_.contains(name))
              expected = inherited_.at(name);
            else if (declared_.contains(child))
              for (const auto &[parameter, parameterType] :
                   declared_.at(child).inputs)
                if (parameter == name)
                  expected = parameterType;
            if (expected.empty())
              throw std::runtime_error("unknown inherited attribute for " +
                                       child + ": " + name);
            // Explicit named call arguments also define the child's interface,
            // including deliberately unused inputs of some alternatives.
            inputs_[child].insert(name);
            arguments_.push_back(
                {expression(field(argument, "value"), environment), expected,
                 current_});
          };
          collect(field(*context, "first"));
          for (const auto &item : field(*context, "rest").elements)
            collect(item);
        }
        calls_.push_back(std::move(call));
        if (const auto *fallback = optional(field(statement, "fallback"))) {
          auto inner = environment;
          if (simpleToken(field(statement, "ending")) == "continue_on_error")
            inner["failure_message"] = "Text";
          unify("$" + child, expression(field(*fallback, "value"), inner));
        }
        if (const auto *destination =
                optional(field(statement, "destination"))) {
          const auto name = token(field(*destination, "name"));
          const auto result = optionalChild && !optional(field(statement, "fallback"))
                                  ? "Option<$" + child + ">"
                                  : "$" + child;
          if (environment.contains(name))
            unify(environment.at(name), result);
          else
            environment[name] = result;
        }
      } else if (statement.typeName == "foreachStatement") {
        const auto collection =
            expression(field(statement, "collection"), environment);
        if (!collection.starts_with("List<") || !collection.ends_with('>'))
          throw std::runtime_error("foreach requires a list in " + current_);
        auto inner = environment;
        inner[token(field(statement, "item"))] =
            collection.substr(5, collection.size() - 6);
        block(field(statement, "body"), inner);
      } else if (statement.typeName == "ifStatement") {
        unify(expression(field(statement, "condition"), environment), "Bool");
        auto inner = environment;
        block(field(statement, "thenBranch"), inner);
        if (const auto *otherwise = optional(field(statement, "otherwise"))) {
          inner = environment;
          block(*otherwise, inner);
        }
      } else if (statement.typeName == "requireStatement") {
        unify(expression(field(statement, "condition"), environment), "Bool");
        const auto &failure = field(statement, "failure");
        if (failure.typeName == "errorResponse") {
          static_cast<void>(expression(field(failure, "message"), environment));
          if (const auto *location = optional(field(failure, "location")))
            static_cast<void>(
                expression(field(*location, "value"), environment));
        }
      } else {
        throw std::runtime_error("unsupported analyzer inference statement: " +
                                 statement.typeName);
      }
    }
  }

  const std::map<std::string, Signature> &functions_;
  EnumSchemas enums_;
  std::map<std::string, AnalyzerSignature> declared_;
  Environment inherited_;
  Environment types_;
  std::map<std::string, std::set<std::string>> inputs_;
  std::vector<Call> calls_;
  std::vector<ArgumentConstraint> arguments_;
  std::string current_;
};

void inferAnalyzers(const Value &root,
                    const agas::model::SyntaxDocument &grammar,
                    const std::map<std::string, Signature> &functions,
                    CodegenContract &contract, const EnumSchemas &enums) {
  if (contract.inherited.empty())
    return;
  auto provisional = contract;
  for (const auto &rule : grammar.parserRules)
    provisional.analyzers.try_emplace(rule.name,
                                      AnalyzerSignature{"$" + rule.name, {}});
  const auto rules = checkedRuleAnalyses(root, grammar, provisional);
  AnalyzerInference{functions, contract, enums}.run(rules, contract);
}

struct Expression {
  std::string code;
  std::string type;
  std::string directResultCode{};
};

using Environment = std::map<std::string, std::string>;

struct MissingAnalyzerInterface : std::runtime_error {
  std::string rule;
  explicit MissingAnalyzerInterface(std::string name)
      : std::runtime_error("unresolved analyzer interface: " + name),
        rule(std::move(name)) {}
};
class Compiler {
public:
  Compiler(const std::map<std::string, Signature> &signatures,
           const std::vector<RuleAnalyses> &rules, std::string contextType,
           const EnumSchemas &enums = {}, bool partial = false)
      : signatures_(signatures), enums_(enums),
        contextType_(std::move(contextType)) {
    for (const auto &rule : rules) {
      if (!rule.resultType.empty())
        analysisResults_.emplace(rule.name, rule.resultType);
      analysisInputs_.emplace(rule.name, rule.inputs);
      astKinds_.emplace(rule.name, rule.astKind);
      acceptedNodeTypes_[rule.name].insert(rule.name);
      for (const auto &alternative : rule.alternatives)
        if (alternative.collapsedChild)
          acceptedNodeTypes_[rule.name].insert(
              alternative.collapsedChild->second);
    }
    for (const auto &rule : rules)
      for (const auto &alternative : rule.alternatives)
        if (alternative.forwardedRule || alternative.actionDispatchType) {
          const auto &targetName = alternative.forwardedRule
                                       ? *alternative.forwardedRule
                                       : *alternative.actionDispatchType;
          const auto target = analysisResults_.find(targetName);
          if (partial &&
              (target == analysisResults_.end() || rule.resultType.empty()))
            continue;
          if (partial && alternative.forwardedRule &&
              target->second != rule.resultType)
            continue;
          if (target == analysisResults_.end() ||
              (alternative.forwardedRule &&
               target->second != rule.resultType))
            throw std::runtime_error(
                "forwarded rule has an incompatible result");
          if (partial && alternative.forwardedRule &&
              analysisInputs_.at(targetName) != rule.inputs)
            continue;
          if (alternative.forwardedRule &&
              analysisInputs_.at(targetName) != rule.inputs)
            throw std::runtime_error(
                "forwarded rule has incompatible inherited parameters");
          acceptedNodeTypes_[rule.name].insert(targetName);
        }
    bool changed = true;
    while (changed) {
      changed = false;
      for (const auto &rule : rules)
        for (const auto &alternative : rule.alternatives) {
          auto targetName = alternative.forwardedRule
                                ? alternative.forwardedRule
                                : alternative.actionDispatchType;
          if (!targetName && alternative.collapsedChild)
            targetName = alternative.collapsedChild->second;
          if (!targetName)
            continue;
          const auto targetTypes = acceptedNodeTypes_.at(*targetName);
          for (const auto &name : targetTypes)
            changed |= acceptedNodeTypes_[rule.name].insert(name).second;
        }
    }
  }

  auto acceptedNodeTypes(std::string_view ruleName) const
      -> const std::set<std::string> & {
    return acceptedNodeTypes_.at(std::string{ruleName});
  }

  auto acceptsForwardedNodes(std::string_view ruleName) const -> bool {
    const auto found = acceptedNodeTypes_.find(std::string{ruleName});
    return found != acceptedNodeTypes_.end() && found->second.size() > 1;
  }

  auto astKind(std::string_view ruleName) const -> RuleAstKind {
    const auto found = astKinds_.find(std::string{ruleName});
    if (found == astKinds_.end())
      throw std::runtime_error("unknown rule AST kind: " +
                               std::string{ruleName});
    return found->second;
  }

  void setFunction(const Signature &signature) {
    currentMutates_ = signature.mutates;
    currentFallible_ = signature.result.starts_with("Result<");
  }

  void setAnalysis() {
    currentMutates_ = true;
    currentFallible_ = true;
  }

  enum class ExpressionKind {
    Integer, None, Unit, Boolean, Local, Binary, Presence, BooleanComparison,
    Source, OptionalPresence, TokenText, EnumConstant, Collection, Call
  };

  std::vector<CheckedEnumConstant> enumConstants;
  std::vector<CheckedCollectionOperation> collectionOperations;

  enum class ArgumentConversion { Identity, TextView, NodeSlice, ListBorrow, ListCopy };

  struct CheckedExpression {
    ExpressionKind kind;
    std::string type;
    agas::runtime::InputSpan source;
    std::string name;
    std::vector<CheckedExpression> children;
    std::vector<ArgumentConversion> conversions;
    bool intrinsic{};
    bool mutates{};
    bool fallible{};
    bool callerMutates{};
    std::string externalPath;
    bool externalContext{};
  };

  auto checkExpression(const Value &value, const Environment &environment)
      -> CheckedExpression {
    const auto make = [&](ExpressionKind kind, std::string type,
                          std::string name = {}) -> CheckedExpression {
      return {kind, std::move(type), value.sourceSpan, std::move(name)};
    };
    if (value.kind == agas::runtime::AstValueKind::Token) {
      const auto name = token(value);
      if (!name.empty() && std::ranges::all_of(name, [](char character) {
            return character >= '0' && character <= '9';
          }))
        return make(ExpressionKind::Integer, "Int", name);
      if (name == "none")
        return make(ExpressionKind::None, "None");
      if (name == "unit")
        return make(ExpressionKind::Unit, "Unit");
      if (name == "true" || name == "false")
        return make(ExpressionKind::Boolean, "Bool", name);
      checkRustIdentifier(name);
      const auto found = environment.find(name);
      if (found == environment.end())
        throw std::runtime_error("unbound identifier in action: " + name);
      return make(ExpressionKind::Local, found->second, name);
    }
    if (value.typeName == "actionExpression") {
      auto left = checkExpression(field(value, "first"), environment);
      const auto &parts = field(value, "rest").elements;
      if (optional(field(value, "choice")) || parts.size() > 1)
        throw std::runtime_error("unsupported expression in typed Rust subset");
      if (parts.empty())
        return left;
      const auto &part = parts.front();
      const auto operatorName = token(field(part, "operator"));
      auto right = checkExpression(field(part, "value"), environment);
      auto result = make(ExpressionKind::Binary, "Bool", operatorName);
      if (operatorName == "and") {
        if (left.type != "Bool" || right.type != "Bool")
          throw std::runtime_error("and requires Bool operands");
      } else if (left.type == "Int" && right.type == "Int" &&
                 (operatorName == "+" || operatorName == "-" ||
                  operatorName == "*" || operatorName == "/")) {
        result.type = "Int";
      } else {
        if (operatorName != "!=" && operatorName != "==")
          throw std::runtime_error("only equality comparisons are supported");
        if ((left.type.starts_with("Option<") && right.type == "None") ||
            (right.type.starts_with("Option<") && left.type == "None")) {
          result.kind = ExpressionKind::Presence;
        } else if (left.type == right.type && left.type != "Unit" &&
                   left.type != "None") {
          if (left.type == "Bool" &&
              ((left.kind == ExpressionKind::Boolean) !=
               (right.kind == ExpressionKind::Boolean)))
            result.kind = ExpressionKind::BooleanComparison;
        } else {
          throw std::runtime_error("incompatible equality operands: " +
                                   left.type + " and " + right.type);
        }
      }
      result.children.push_back(std::move(left));
      result.children.push_back(std::move(right));
      return result;
    }
    if (value.typeName != "actionUnary")
      throw std::runtime_error("unsupported expression node: " +
                               value.typeName);
    if (!field(value, "prefixes").elements.empty())
      throw std::runtime_error(
          "unary prefixes are not supported in typed subset");
    const auto &suffixes = field(value, "suffixes").elements;
    if (suffixes.empty())
      return checkExpression(field(value, "atom"), environment);
    if (!environment.contains("List")) {
      if (const auto operation = collectionOperation(value)) {
        auto result = make(ExpressionKind::Collection, "", operation->name);
        std::string element;
        if (operation->name == "empty") {
          element = collectionElementName(*operation->arguments[0]);
        } else {
          for (const auto *argument : operation->arguments)
            result.children.push_back(checkExpression(*argument, environment));
          if (operation->name == "single")
            element = result.children[0].type;
          else {
            const auto &list = result.children[0].type;
            if (!list.starts_with("List<") || !list.ends_with('>'))
              throw std::runtime_error("List.append requires a List operand");
            element = list.substr(5, list.size() - 6);
            if (result.children[1].type != element)
              throw std::runtime_error("element type mismatch for List.append: expected " + element);
          }
        }
        if (element == "None" || element == "Token" ||
            element.find("Node<") != std::string::npos ||
            element.find("<Token>") != std::string::npos)
          throw std::runtime_error("collection operation requires a value element type");
        result.type = "List<" + element + ">";
        result.externalPath = rustType(element);
        // List-valued operands may be borrowed function parameters. Materialize
        // their owned representation before inserting them into another list.
        for (const auto &child : result.children)
          result.conversions.push_back(child.type.starts_with("List<")
                                           ? ArgumentConversion::ListCopy
                                           : ArgumentConversion::Identity);
        std::vector<std::string> operands;
        std::vector<std::string> ownership;
        for (const auto &child : result.children)
          operands.push_back(child.type);
        for (std::size_t index = 0; index < result.children.size(); ++index)
          ownership.push_back(operation->name == "append" && index == 0
                                  ? "borrow and clone elements"
                                  : result.conversions[index] == ArgumentConversion::ListCopy
                                        ? "clone list value" : "value");
        collectionOperations.push_back({operation->name, element, result.type, operands, ownership,
            operation->name == "append" ? "borrow list, clone elements, own result" : "own result",
            result.externalPath, renderExpression(result).code,
            {0, value.sourceSpan.beginByte, value.sourceSpan.endByte}});
        return result;
      }
    }
    if (suffixes.size() == 1 && suffixes.front().variantName == "Field") {
      const auto name = token(field(value, "atom"));
      const auto member = token(field(suffixes.front(), "name"));
      if (!environment.contains(name) && enums_.contains(name)) {
        const auto path = enumVariant(enums_, name, member);
        enumConstants.push_back({name, member, path,
                                 {0, value.sourceSpan.beginByte,
                                  value.sourceSpan.endByte}});
        auto result = make(ExpressionKind::EnumConstant, name, member);
        result.externalPath = path;
        return result;
      }
      if (name == "self" && member == "source")
        return make(ExpressionKind::Source, "SourceRange", name);
      const auto found = environment.find(name);
      if (found != environment.end() && found->second.starts_with("Option<") &&
          member == "present")
        return make(ExpressionKind::OptionalPresence, "Bool", name);
      if (found != environment.end() &&
          (found->second == "Token" || found->second.starts_with("Node<")) &&
          member == "source")
        return make(ExpressionKind::Source, "SourceRange", name);
      if (found == environment.end() || found->second != "Token" ||
          member != "text")
        throw std::runtime_error(
            "only AST source and Token.text are supported in analyzer");
      return make(ExpressionKind::TokenText, "Text", name);
    }
    if (suffixes.size() != 1 || suffixes.front().variantName != "Call")
      throw std::runtime_error(
          "only direct calls are supported in typed subset");
    const auto name = token(field(value, "atom"));
    auto result = make(ExpressionKind::Call, "", name);
    const auto *list = optional(field(suffixes.front(), "arguments"));
    if (list) {
      if (list->typeName == "argumentList") {
        result.children.push_back(
            checkExpression(field(*list, "first"), environment));
        for (const auto &entry : field(*list, "rest").elements)
          result.children.push_back(checkExpression(entry, environment));
      } else {
        result.children.push_back(checkExpression(*list, environment));
      }
    }
    const auto found = signatures_.find(name);
    if (found == signatures_.end())
      throw std::runtime_error("unresolved typed call: " + name);
    const auto &callee = found->second;
    if (callee.mutates && !currentMutates_)
      throw std::runtime_error("mutating call from read-only function: " +
                               name);
    if (result.children.size() != callee.parameters.size())
      throw std::runtime_error("wrong argument count for " + name);
    for (std::size_t index = 0; index < result.children.size(); ++index) {
      const auto &actual = result.children[index].type;
      const auto &expected = callee.parameters[index].type;
      if (actual == "OwnedText" && expected == "Text")
        result.conversions.push_back(ArgumentConversion::TextView);
      else if (actual.starts_with("List<Node<") && actual == expected)
        result.conversions.push_back(ArgumentConversion::NodeSlice);
      else if (actual.starts_with("List<") && actual == expected)
        result.conversions.push_back(ArgumentConversion::ListBorrow);
      else if (actual == expected)
        result.conversions.push_back(ArgumentConversion::Identity);
      else
        throw std::runtime_error("argument type mismatch for " + name);
    }
    result.externalPath = callee.externalPath;
    result.externalContext = callee.externalContext;
    result.intrinsic = callee.intrinsic;
    result.mutates = callee.mutates;
    result.callerMutates = currentMutates_;
    result.fallible = callee.result.starts_with("Result<");
    if (result.fallible && !currentFallible_)
      throw std::runtime_error("fallible call in infallible function: " + name);
    result.type =
        result.fallible ? resultValueType(callee.result) : callee.result;
    return result;
  }

  auto renderExpression(const CheckedExpression &plan) const -> Expression {
    switch (plan.kind) {
    case ExpressionKind::Integer:
      return {plan.name, plan.type};
    case ExpressionKind::None:
      return {"None", plan.type};
    case ExpressionKind::Unit:
      return {"()", plan.type};
    case ExpressionKind::Boolean:
      return {plan.name, plan.type};
    case ExpressionKind::EnumConstant:
      return {plan.externalPath, plan.type};
    case ExpressionKind::Collection: {
      if (plan.name == "empty")
        return {"Vec::<" + plan.externalPath + ">::new()", plan.type};
      const auto index = plan.name == "single" ? 0u : 1u;
      auto element = renderExpression(plan.children[index]).code;
      if (plan.conversions[index] == ArgumentConversion::ListCopy)
        element = "(" + element + ").to_vec()";
      if (plan.name == "single")
        return {"vec![" + element + "]", plan.type};
      return {"(" + renderExpression(plan.children[0]).code +
                  ").iter().cloned().chain(std::iter::once(" +
                  element +
                  ")).collect::<Vec<" + plan.externalPath + ">>()", plan.type};
    }
    case ExpressionKind::Local:
      return {rustLocalName(plan.name), plan.type};
    case ExpressionKind::Binary: {
      const auto left = renderExpression(plan.children[0]);
      const auto right = renderExpression(plan.children[1]);
      const auto op = plan.name == "and" ? "&&" : plan.name.c_str();
      return {left.code + " " + op + " " + right.code, plan.type};
    }
    case ExpressionKind::Presence: {
      const auto &option = plan.children[0].type.starts_with("Option<")
                               ? plan.children[0] : plan.children[1];
      return {renderExpression(option).code +
                  (plan.name == "!=" ? ".is_some()" : ".is_none()"),
              plan.type};
    }
    case ExpressionKind::BooleanComparison: {
      const auto leftLiteral = plan.children[0].kind == ExpressionKind::Boolean;
      const auto &literal = leftLiteral ? plan.children[0] : plan.children[1];
      const auto &operand = leftLiteral ? plan.children[1] : plan.children[0];
      const auto code = renderExpression(operand).code;
      const auto negate = (plan.name == "==") == (literal.name == "false");
      const auto simple = code.find(' ') == std::string::npos;
      return {negate ? "!" + (simple ? code : "(" + code + ")") : code,
              plan.type};
    }
    case ExpressionKind::Source:
      return {plan.name == "self" ? "node.source_span"
                                  : rustLocalName(plan.name) + ".source_span",
              plan.type};
    case ExpressionKind::OptionalPresence:
      return {rustLocalName(plan.name) + ".is_some()", plan.type};
    case ExpressionKind::TokenText:
      return {rustLocalName(plan.name) + ".token_text.as_str()", plan.type};
    case ExpressionKind::Call: {
      std::string code = plan.intrinsic
                             ? "ctx." + plan.name + "("
                             : "crate::sema_lib_gen::" + plan.name + "(";
      if (!plan.externalPath.empty())
        code = plan.externalPath + "(";
      if (!plan.intrinsic || plan.externalContext)
        code += plan.mutates ? "&mut *ctx"
                : plan.callerMutates ? "&*ctx"
                                     : "ctx";
      for (std::size_t index = 0; index < plan.children.size(); ++index) {
        auto argumentCode = renderExpression(plan.children[index]).code;
        switch (plan.conversions[index]) {
        case ArgumentConversion::Identity:
          break;
        case ArgumentConversion::TextView:
          argumentCode += ".as_str()";
          break;
        case ArgumentConversion::NodeSlice:
          argumentCode += ".elements.as_slice()";
          break;
        case ArgumentConversion::ListBorrow:
          argumentCode = "&" + argumentCode;
          break;
        case ArgumentConversion::ListCopy:
          argumentCode = "(" + argumentCode + ").to_vec()";
          break;
        }
        if (index || !plan.intrinsic || plan.externalContext)
          code += ", ";
        code += argumentCode;
      }
      code += ")";
      return plan.fallible ? Expression{code + "?", plan.type, code}
                           : Expression{code, plan.type};
    }
    }
    throw std::logic_error("unknown checked action expression");
  }

  auto expression(const Value &value, const Environment &environment)
      -> Expression {
    return renderExpression(checkExpression(value, environment));
  }

  enum class StatementKind { Let, Return, Error, Expression, If, Foreach };

  struct CheckedStatement {
    StatementKind kind;
    agas::runtime::InputSpan source;
    std::string name;
    std::string itemType;
    std::string message;
    std::optional<CheckedExpression> value;
    std::optional<CheckedExpression> location;
    std::vector<CheckedStatement> thenBranch;
    std::vector<CheckedStatement> elseBranch;
    bool hasElse{};
    bool fallible{};
    bool unitReturn{};
  };

  auto checkBlock(const Value &value, Environment environment,
                  std::string_view resultType, bool expressionTail = false)
      -> std::vector<CheckedStatement> {
    if (value.typeName != "actionBlock")
      throw std::runtime_error("expected action block");
    std::vector<CheckedStatement> plan;
    const auto &statements = field(value, "statements").elements;
    for (std::size_t index = 0; index < statements.size(); ++index)
      plan.push_back(checkStatement(statements[index], environment, resultType,
                                    expressionTail && index + 1 == statements.size()));
    return plan;
  }

  auto renderBlock(const std::vector<CheckedStatement> &plan,
                   std::size_t indent, bool expressionTail = false) const
      -> std::string {
    std::string output;
    for (std::size_t index = 0; index < plan.size(); ++index)
      output += renderStatement(plan[index], indent,
                                expressionTail && index + 1 == plan.size());
    return output;
  }

  auto block(const Value &value, Environment environment,
             std::string_view resultType, std::size_t indent,
             bool expressionTail = false) -> std::string {
    return renderBlock(checkBlock(value, std::move(environment), resultType,
                                  expressionTail), indent, expressionTail);
  }

  struct CheckedAnalyze {
    agas::runtime::InputSpan source;
    std::string childName;
    std::string ruleName;
    std::string destinationName;
    std::string resultType;
    std::vector<std::pair<std::string, std::optional<CheckedExpression>>>
        arguments;
    std::optional<CheckedExpression> fallback;
    bool recover{};
    bool optionalChild{};
    bool existing{};
    bool usedLater{};
    bool reassignedLater{};
  };

  auto checkAnalyzeStatement(const Value &statement, Environment &environment,
                             bool usedLater, bool reassignedLater = false)
      -> CheckedAnalyze {
    const auto *fallback = optional(field(statement, "fallback"));
    const auto ending = simpleToken(field(statement, "ending"));
    const bool recover = ending == "continue_on_error";
    const bool optionalChild = optional(field(statement, "optional")) != nullptr;
    if ((ending != ";" && !recover) ||
        (!optionalChild && recover != (fallback != nullptr)) ||
        (optionalChild && recover))
      throw std::runtime_error("unsupported analyze clause in analyzer");
    const auto childName = token(field(statement, "child"));
    const auto child = environment.find(childName);
    const auto expectedPrefix = optionalChild ? "Option<Node<" : "Node<";
    const auto expectedSuffix = optionalChild ? ">>" : ">";
    if (child == environment.end() ||
        !child->second.starts_with(expectedPrefix) ||
        !child->second.ends_with(expectedSuffix))
      throw std::runtime_error("analyze requires a child rule node: " +
                               childName);
    const auto ruleName = child->second.substr(
        std::string_view(expectedPrefix).size(),
        child->second.size() - std::string_view(expectedPrefix).size() -
            std::string_view(expectedSuffix).size());
    const auto foundResult = analysisResults_.find(ruleName);
    if (foundResult == analysisResults_.end() && astKinds_.contains(ruleName))
      throw MissingAnalyzerInterface(ruleName);
    if (foundResult == analysisResults_.end())
      throw std::runtime_error("analyze references an unknown rule: " +
                               ruleName);
    CheckedAnalyze plan{statement.sourceSpan};
    plan.childName = childName;
    plan.ruleName = ruleName;
    plan.resultType = foundResult->second;
    plan.recover = recover;
    plan.optionalChild = optionalChild;
    plan.usedLater = usedLater;
    plan.reassignedLater = reassignedLater;
    const auto *context = optional(field(statement, "context"));
    std::map<std::string, CheckedExpression> arguments;
    const auto collect = [&](const Value &argument) {
      const auto name = token(field(argument, "name"));
      auto value = checkExpression(field(argument, "value"), environment);
      if (!arguments.emplace(name, std::move(value)).second)
        throw std::runtime_error("duplicate analyze context: " + name);
    };
    if (context) {
      collect(field(*context, "first"));
      for (const auto &item : field(*context, "rest").elements)
        collect(item);
    }
    const auto &inputs = analysisInputs_.at(ruleName);
    for (const auto &[inputName, inputType] : inputs) {
      auto argument = arguments.find(inputName);
      if (argument == arguments.end()) {
        const auto inherited = environment.find(inputName);
        if (inherited == environment.end() || inherited->second != inputType)
          throw std::runtime_error("missing analyze context: " + inputName);
        plan.arguments.emplace_back(inputName, std::nullopt);
      } else {
        if (argument->second.type != inputType)
          throw std::runtime_error("analyze context type mismatch: " +
                                   inputName);
        plan.arguments.emplace_back(inputName, std::move(argument->second));
      }
    }
    if (arguments.size() > inputs.size() ||
        std::ranges::any_of(arguments, [&](const auto &argument) {
          return std::ranges::none_of(inputs, [&](const auto &input) {
            return input.first == argument.first;
          });
        }))
      throw std::runtime_error("unknown analyze context for rule " + ruleName);
    if (recover) {
      auto fallbackEnvironment = environment;
      fallbackEnvironment.emplace("failure_message", "Text");
      auto replacement =
          checkExpression(field(*fallback, "value"), fallbackEnvironment);
      if (replacement.type != foundResult->second)
        throw std::runtime_error("analyze fallback type mismatch: " +
                                 childName);
      plan.fallback = std::move(replacement);
    } else if (optionalChild && fallback) {
      auto replacement = checkExpression(field(*fallback, "value"), environment);
      if (replacement.type != foundResult->second)
        throw std::runtime_error("optional analyze fallback type mismatch: " +
                                 childName);
      plan.fallback = std::move(replacement);
    }
    const auto *destination = optional(field(statement, "destination"));
    if (!destination)
      throw std::runtime_error("analyze requires a result destination");
    const auto name = token(field(*destination, "name"));
    checkRustIdentifier(name);
    if (name == "ctx" || name == "result" || name == "token" ||
        name == "child" || name == "children" || name == "flow" ||
        name == "scope")
      throw std::runtime_error("reserved analyzer destination: " + name);
    const auto destinationType = optionalChild && !fallback
                                     ? "Option<" + foundResult->second + ">"
                                     : foundResult->second;
    const auto existing = environment.find(name);
    if (existing != environment.end()) {
      if (existing->second != destinationType)
        throw std::runtime_error("analyze destination type mismatch: " + name);
      plan.existing = true;
    } else {
      environment.emplace(name, destinationType);
    }
    plan.destinationName = name;
    plan.resultType = destinationType;
    return plan;
  }

  auto renderAnalyzeStatement(const CheckedAnalyze &plan,
                              std::size_t indent) const -> std::string {
    std::string call = "analyze_" + rustRuleName(plan.ruleName) + "(&mut *ctx";
    for (const auto &[name, value] : plan.arguments)
      call += ", " + (value ? renderExpression(*value).code : name);
    call += plan.optionalChild
                ? ", child)"
                : ", " + rustLocalName(plan.childName) + ")";
    if (plan.recover) {
      call = "match " + call +
             " { Ok(value) => value, Err(failure_message) => " +
             renderExpression(*plan.fallback).code + " }";
    } else if (plan.optionalChild) {
      if (plan.fallback) {
        call = "match " + rustLocalName(plan.childName) +
               " { Some(child) => " + call + "?, None => " +
               renderExpression(*plan.fallback).code + " }";
      } else {
        call = "match " + rustLocalName(plan.childName) +
               " { Some(child) => Some(" + call + "?), None => None }";
      }
    } else {
      call += "?";
    }
    const auto pad = std::string(indent * 4, ' ');
    if (plan.existing) {
      if (plan.resultType == "Unit")
        return pad + "let _ = " + call + ";\n";
      return pad + plan.destinationName + " = " + call + ";\n";
    }
    const auto binding = plan.reassignedLater
                             ? "mut " + plan.destinationName
                         : plan.usedLater ? plan.destinationName
                                          : "_" + plan.destinationName;
    if (indent != 1)
      return pad + "let " + binding + " = " + call + ";\n";
    return rustCallBinding(binding, call);
  }

  auto compileAnalyzeStatement(const Value &statement, Environment &environment,
                               std::size_t indent, bool usedLater,
                               bool reassignedLater = false) -> std::string {
    return renderAnalyzeStatement(checkAnalyzeStatement(
                                      statement, environment, usedLater,
                                      reassignedLater),
                                  indent);
  }

  enum class AnalyzerBranchStepKind { Analyze, Let, Result };

  struct CheckedAnalyzerBranchStep {
    AnalyzerBranchStepKind kind;
    std::optional<CheckedAnalyze> analyze;
    std::optional<CheckedStatement> binding;
    std::optional<CheckedExpression> result;
  };

  auto checkAnalyzerBranch(const Value &body, Environment environment,
                           std::string_view resultType)
      -> std::vector<CheckedAnalyzerBranchStep> {
    const auto &statements = field(body, "statements").elements;
    if (statements.empty() ||
        statements.back().typeName != "expressionOrAssignment")
      throw std::runtime_error("analyzer if branch must assign result");
    std::vector<CheckedAnalyzerBranchStep> plan;
    for (std::size_t index = 0; index < statements.size(); ++index) {
      const auto &statement = statements[index];
      if (statement.typeName == "analyzeStatement") {
        const auto *destination = optional(field(statement, "destination"));
        bool usedLater = false;
        bool reassignedLater = false;
        if (destination) {
          const auto name = token(field(*destination, "name"));
          for (std::size_t later = index + 1; later < statements.size(); ++later) {
            usedLater |= referencesName(statements[later], name);
            reassignedLater |= assignsAnalyzeDestination(statements[later], name);
          }
        }
        CheckedAnalyzerBranchStep step{AnalyzerBranchStepKind::Analyze};
        step.analyze = checkAnalyzeStatement(statement, environment, usedLater,
                                             reassignedLater);
        plan.push_back(std::move(step));
        continue;
      }
      if (statement.typeName == "letStatement") {
        CheckedAnalyzerBranchStep step{AnalyzerBranchStepKind::Let};
        step.binding = checkStatement(
            statement, environment, "Result<" + std::string(resultType) + ">",
            false);
        plan.push_back(std::move(step));
        continue;
      }
      if (statement.typeName != "expressionOrAssignment" ||
          index + 1 != statements.size())
        throw std::runtime_error("unsupported statement in analyzer if branch");
      const auto *assignment = optional(field(statement, "assignment"));
      if (!assignment || simpleToken(field(statement, "target")) != "result" ||
          optional(field(*assignment, "context")) ||
          optional(field(*assignment, "failure")))
        throw std::runtime_error("analyzer if branch must assign result");
      auto value = checkExpression(field(*assignment, "value"), environment);
      if (!resultType.empty() && value.type != resultType)
        throw std::runtime_error("analyzer result must have type " +
                                 std::string(resultType));
      CheckedAnalyzerBranchStep step{AnalyzerBranchStepKind::Result};
      step.result = std::move(value);
      plan.push_back(std::move(step));
    }
    return plan;
  }

  auto renderAnalyzerBranch(const std::vector<CheckedAnalyzerBranchStep> &plan,
                            std::size_t indent) const -> std::string {
    std::string output;
    for (const auto &step : plan) {
      switch (step.kind) {
      case AnalyzerBranchStepKind::Analyze:
        output += renderAnalyzeStatement(*step.analyze, indent);
        break;
      case AnalyzerBranchStepKind::Let:
        output += renderStatement(*step.binding, indent, false);
        break;
      case AnalyzerBranchStepKind::Result:
        output += std::string(indent * 4, ' ') +
                  renderExpression(*step.result).code + "\n";
        break;
      }
    }
    return output;
  }

  auto compileAnalyzerBranch(const Value &body, Environment environment,
                             std::string_view resultType, std::size_t indent)
      -> std::string {
    return renderAnalyzerBranch(
        checkAnalyzerBranch(body, std::move(environment), resultType), indent);
  }

  auto checkedAlternativeEnvironment(const RuleAnalyses &rule,
                                     const RuleAlternative &alternative) const
      -> Environment {
    Environment environment;
    for (const auto &[name, type] : rule.inputs) {
      checkRustIdentifier(name);
      if (name == "ctx" || name == "node" || name == "result" ||
          name == "token" || name == "child" || name == "children" ||
          !environment.emplace(name, type).second)
        throw std::runtime_error("reserved analyzer parameter: " + name);
    }
    const auto insertToken = [&](const std::string &name) {
      checkRustIdentifier(name);
      if (name == "ctx" || name == "scope" || name == "flow" ||
          name == "result" || name == "token" ||
          !environment.emplace(name, "Token").second)
        throw std::runtime_error("reserved token field in analyzer: " + name);
    };
    if (!alternative.tokenDispatchKind)
      for (const auto &name : alternative.tokenFields)
        insertToken(name);
    if (alternative.tokenDispatchKind)
      insertToken(*alternative.tokenDispatchField);
    for (const auto &name : alternative.optionalTokenFields) {
      checkRustIdentifier(name);
      if (!environment.emplace(name, "Option<Token>").second)
        throw std::runtime_error("duplicate optional token field: " + name);
    }
    const auto insertChild = [&](const std::string &name,
                                 const std::string &type) {
      checkRustIdentifier(name);
      if (name == "ctx" || name == "scope" || name == "flow" ||
          name == "result" || name == "token" || name == "child" ||
          !environment.emplace(name, "Node<" + type + ">").second)
        throw std::runtime_error("reserved child field in analyzer: " + name);
    };
    if (!alternative.actionDispatchType)
      for (const auto &[name, type] : alternative.childFields)
        insertChild(name, type);
    if (alternative.actionDispatchType) {
      const auto &[name, type] = alternative.childFields.front();
      insertChild(name, type);
    }
    for (const auto &[name, type] : alternative.optionalChildFields) {
      checkRustIdentifier(name);
      if (!environment.emplace(name, "Option<Node<" + type + ">>").second)
        throw std::runtime_error("duplicate optional child field: " + name);
    }
    for (const auto &[name, type] : alternative.childListFields) {
      checkRustIdentifier(name);
      if (name == "ctx" || name == "scope" || name == "flow" ||
          name == "result" || name == "token" || name == "child" ||
          name == "children" ||
          !environment.emplace(name, "List<Node<" + type + ">>").second)
        throw std::runtime_error("reserved child list field in analyzer: " +
                                 name);
    }
    return environment;
  }

  enum class AlternativeStepKind {
    Analyze,
    Let,
    Foreach,
    Require,
    If,
    Expression,
    Result,
    NoActionUnit
  };

  struct CheckedAlternativeStep {
    AlternativeStepKind kind;
    std::string name;
    std::string binding;
    std::string message;
    std::optional<CheckedExpression> value;
    std::optional<CheckedExpression> location;
    std::optional<CheckedAnalyze> analyze;
    std::vector<CheckedAnalyzerBranchStep> thenBranch;
    std::vector<CheckedAnalyzerBranchStep> elseBranch;
  };

  auto checkAlternativeBody(const RuleAnalyses &rule,
                            const RuleAlternative &alternative,
                            Environment environment,
                            AlternativeReview *review = nullptr)
      -> std::vector<CheckedAlternativeStep> {
    setAnalysis();
    std::vector<const Value *> statements;
    for (const auto *body : alternative.bodies)
      for (const auto &statement : field(*body, "statements").elements)
        statements.push_back(&statement);
    std::vector<CheckedAlternativeStep> plan;
    bool hasResult = false;
    if (alternative.noActionUnit) {
      CheckedAlternativeStep step{AlternativeStepKind::NoActionUnit};
      CheckedExpression unit;
      unit.kind = ExpressionKind::Unit;
      unit.type = "Unit";
      step.value = std::move(unit);
      plan.push_back(std::move(step));
      if (review) {
        review->hasResult = true;
        review->noAction = true;
      }
      return plan;
    }
    std::set<std::string> blockedNames;
    for (std::size_t statementIndex = 0; statementIndex < statements.size();
         ++statementIndex) {
      const auto &statement = *statements[statementIndex];
      const auto blockDestination = [&] {
        if (statement.typeName == "analyzeStatement") {
          if (const auto *destination =
                  optional(field(statement, "destination")))
            blockedNames.insert(token(field(*destination, "name")));
        } else if (statement.typeName == "letStatement")
          blockedNames.insert(token(field(statement, "name")));
      };
      if (review && std::ranges::any_of(blockedNames, [&](const auto &name) {
            return referencesName(statement, name);
          })) {
        blockDestination();
        continue;
      }
      try {
        if (statement.typeName == "analyzeStatement") {
          const auto *destination = optional(field(statement, "destination"));
          bool usedLater = false;
          bool reassignedLater = false;
          if (destination) {
            const auto name = token(field(*destination, "name"));
            for (std::size_t later = statementIndex + 1;
                 later < statements.size(); ++later) {
              usedLater |= referencesName(*statements[later], name);
              reassignedLater |=
                  assignsAnalyzeDestination(*statements[later], name);
            }
          }
          CheckedAlternativeStep step{AlternativeStepKind::Analyze};
          step.analyze = checkAnalyzeStatement(statement, environment,
                                               usedLater, reassignedLater);
          plan.push_back(std::move(step));
          continue;
        }
        if (statement.typeName == "letStatement") {
          if (optional(field(statement, "context")) ||
              optional(field(statement, "failure")))
            throw std::runtime_error("unsupported let clause in analyzer");
          const auto name = token(field(statement, "name"));
          checkRustIdentifier(name);
          if (name == "ctx" || name == "result" || name == "token" ||
              environment.contains(name))
            throw std::runtime_error(
                "duplicate or reserved analyzer binding: " + name);
          auto value = checkExpression(field(statement, "value"), environment);
          if (value.type == "None")
            throw std::runtime_error("cannot infer type of none binding: " +
                                     name);
          environment.emplace(name, value.type);
          bool usedLater = false;
          bool reassigned = false;
          for (std::size_t later = statementIndex + 1;
               later < statements.size(); ++later) {
            usedLater |= referencesName(*statements[later], name);
            reassigned |= assignsAnalyzeDestination(*statements[later], name);
          }
          CheckedAlternativeStep step{AlternativeStepKind::Let};
          step.binding = reassigned && value.type != "Unit" ? "mut " + name
                         : usedLater                        ? name
                                                            : "_" + name;
          step.value = std::move(value);
          plan.push_back(std::move(step));
          continue;
        }
        if (statement.typeName == "foreachStatement") {
          if (optional(field(statement, "filter")))
            throw std::runtime_error(
                "foreach filter is not supported in analyzer");
          auto collection =
              checkExpression(field(statement, "collection"), environment);
          if (!collection.type.starts_with("List<Node<") ||
              !collection.type.ends_with(">>"))
            throw std::runtime_error("foreach requires a child node list");
          const auto type =
              collection.type.substr(10, collection.type.size() - 12);
          const auto name = token(field(statement, "item"));
          checkRustIdentifier(name);
          if (name == "ctx" || name == "result" || name == "token" ||
              name == "child" || name == "children" ||
              environment.contains(name))
            throw std::runtime_error("duplicate or reserved foreach item: " +
                                     name);
          auto inner = environment;
          inner.emplace(name, "Node<" + type + ">");
          const auto &body = field(statement, "body");
          const auto &bodyStatements = field(body, "statements").elements;
          if (bodyStatements.size() != 1 ||
              bodyStatements.front().typeName != "analyzeStatement")
            throw std::runtime_error(
                "foreach body needs one analyze statement");
          const auto *destination =
              optional(field(bodyStatements.front(), "destination"));
          if (!destination ||
              !environment.contains(token(field(*destination, "name"))))
            throw std::runtime_error("foreach must update an existing state");
          CheckedAlternativeStep step{AlternativeStepKind::Foreach};
          step.name = name;
          step.value = std::move(collection);
          step.analyze =
              checkAnalyzeStatement(bodyStatements.front(), inner, true);
          plan.push_back(std::move(step));
          continue;
        }
        if (statement.typeName == "requireStatement") {
          auto condition =
              checkExpression(field(statement, "condition"), environment);
          if (condition.type != "Bool")
            throw std::runtime_error("require condition must have type Bool");
          const auto &failure = field(statement, "failure");
          if (failure.typeName != "errorResponse" ||
              failure.variantName != "Error" ||
              token(field(failure, "kind")) != "error")
            throw std::runtime_error("analyzer requires an error response");
          CheckedAlternativeStep step{AlternativeStepKind::Require};
          step.value = std::move(condition);
          step.message = quotedText(field(failure, "message"));
          if (const auto *location = optional(field(failure, "location"))) {
            auto span = checkExpression(field(*location, "value"), environment);
            if (span.type != "SourceRange")
              throw std::runtime_error("error location must be SourceRange");
            step.location = std::move(span);
          }
          plan.push_back(std::move(step));
          continue;
        }
        if (statement.typeName == "ifStatement") {
          if (hasResult || statementIndex + 1 != statements.size())
            throw std::runtime_error("analyzer if must be the final statement");
          auto condition =
              checkExpression(field(statement, "condition"), environment);
          if (condition.type != "Bool")
            throw std::runtime_error(
                "analyzer if condition must have type Bool");
          const auto *otherwise = optional(field(statement, "otherwise"));
          if (!otherwise)
            throw std::runtime_error("analyzer if must have an else branch");
          CheckedAlternativeStep step{AlternativeStepKind::If};
          step.value = std::move(condition);
          step.thenBranch = checkAnalyzerBranch(field(statement, "thenBranch"),
                                                environment, rule.resultType);
          step.elseBranch =
              checkAnalyzerBranch(*otherwise, environment, rule.resultType);
          plan.push_back(std::move(step));
          hasResult = true;
          continue;
        }
        if (statement.typeName == "expressionOrAssignment") {
          const auto *tail = optional(field(statement, "assignment"));
          if (!tail) {
            auto checked =
                checkStatement(statement, environment,
                               "Result<" + rule.resultType + ">", false);
            CheckedAlternativeStep step{AlternativeStepKind::Expression};
            step.value = std::move(checked.value);
            plan.push_back(std::move(step));
            continue;
          }
          if (hasResult ||
              simpleToken(field(statement, "target")) != "result" ||
              optional(field(*tail, "context")) ||
              optional(field(*tail, "failure")))
            throw std::runtime_error("unsupported analyzer result assignment");
          auto value = checkExpression(field(*tail, "value"), environment);
          if (!rule.resultType.empty() && value.type != rule.resultType)
            throw std::runtime_error("analyzer result must have type " +
                                     rule.resultType);
          CheckedAlternativeStep step{AlternativeStepKind::Result};
          step.value = std::move(value);
          plan.push_back(std::move(step));
          environment.emplace("result", rule.resultType);
          hasResult = true;
          continue;
        }
        throw std::runtime_error("unsupported statement in analyzer: " +
                                 statement.typeName);
      } catch (const MissingAnalyzerInterface &error) {
        if (!review)
          throw;
        review->dependencies.push_back(error.rule);
        blockDestination();
      } catch (const std::runtime_error &error) {
        if (!review)
          throw;
        review->diagnostics.push_back(
            {Severity::Error, "action.invalid_alternative", error.what(),
             ast::location(statement), rule.name});
        blockDestination();
      }
    }
    if (review) {
      review->hasResult = hasResult;
      return plan;
    }
    if (!hasResult)
      throw std::runtime_error("analyzer must assign result");
    return plan;
  }

  auto renderAlternativeBody(const std::vector<CheckedAlternativeStep> &plan) const
      -> std::string {
    std::ostringstream output;
    if (plan.size() == 1 &&
        plan.front().kind == AlternativeStepKind::NoActionUnit)
      return "    Ok(())\n}\n";
    for (const auto &step : plan) {
      switch (step.kind) {
      case AlternativeStepKind::NoActionUnit:
        throw std::logic_error(
            "NoActionUnit must be the entire alternative plan");
      case AlternativeStepKind::Analyze:
        output << renderAnalyzeStatement(*step.analyze, 1);
        break;
      case AlternativeStepKind::Let:
        output << rustCallBinding(step.binding,
                                  renderExpression(*step.value).code);
        break;
      case AlternativeStepKind::Foreach:
        output << "    for " << step.name << " in "
               << renderExpression(*step.value).code
               << ".elements.iter() {\n"
               << renderAnalyzeStatement(*step.analyze, 2)
               << "    }\n";
        break;
      case AlternativeStepKind::Require: {
        const auto condition = renderExpression(*step.value).code;
        std::string negated;
        if (condition.ends_with(".is_some()"))
          negated = condition.substr(0, condition.size() - 10) + ".is_none()";
        else if (condition.ends_with(".is_none()"))
          negated = condition.substr(0, condition.size() - 10) + ".is_some()";
        else if (condition.starts_with("!(") && condition.ends_with(')'))
          negated = condition.substr(2, condition.size() - 3);
        else if (condition.starts_with('!'))
          negated = condition.substr(1);
        else
          negated = "!(" + condition + ")";
        output << "    if " << negated << " {\n";
        if (step.location)
          output << "        ctx.note_error("
                 << renderExpression(*step.location).code << ");\n";
        output << "        return Err(" << step.message
               << ");\n"
                  "    }\n";
        break;
      }
      case AlternativeStepKind::If:
        output << "    let result = if " << renderExpression(*step.value).code
               << " {\n"
               << renderAnalyzerBranch(step.thenBranch, 2)
               << "    } else {\n"
               << renderAnalyzerBranch(step.elseBranch, 2)
               << "    };\n";
        break;
      case AlternativeStepKind::Expression:
        output << "    " << renderExpression(*step.value).code << ";\n";
        break;
      case AlternativeStepKind::Result:
        output << rustCallBinding("result", renderExpression(*step.value).code);
        break;
      }
    }
    output << "    Ok(result)\n}\n";
    return output.str();
  }

  auto ruleAnalysis(const RuleAnalyses &rule,
                    const RuleAlternative &alternative, std::size_t index,
                    const std::vector<CheckedAlternativeStep> &checkedBody)
      -> std::string {
    std::ostringstream output;
    output << "fn analyze_" << rustRuleName(rule.name) << "_alt_" << index
           << "(\n"
              "    ctx: &mut "
           << contextType_ << ",\n";
    for (const auto &[name, type] : rule.inputs) {
      output << "    " << name << ": " << rustType(type) << ",\n";
    }
    output << "    node: &AstValue,\n"
           << ") -> Result<" << rustType(rule.resultType)
           << ", &'static str> {\n";
    if (alternative.tokenFields.empty() &&
        alternative.optionalTokenFields.empty() &&
        alternative.childFields.empty() &&
        alternative.optionalChildFields.empty() &&
        alternative.childListFields.empty() && !alternative.tokenDispatchKind &&
        !alternative.actionDispatchType)
      output << "    let _ = node;\n";
    for (const auto &[name, type] : rule.inputs) {
      static_cast<void>(type);
      if (!alternative.usedInputs.contains(name))
        output << "    let _ = " << name << ";\n";
    }
    if (!alternative.tokenFields.empty() && !alternative.tokenDispatchKind) {
      output
          << "    let token = |field_name: &str| {\n"
             "        node.field_names\n"
             "            .iter()\n"
             "            .position(|field| field == field_name)\n"
             "            .and_then(|index| node.elements.get(index))\n"
             "            .filter(|value| value.kind == AstValueKind::Token)\n"
             "            .ok_or(\"missing identifier token\")\n"
             "    };\n";
      for (const auto &name : alternative.tokenFields) {
        output << "    let " << rustLocalName(name) << " = token(\"" << name
               << "\")?;\n";
      }
    }
    if (alternative.tokenDispatchKind) {
      const auto &name = *alternative.tokenDispatchField;
      output << "    let " << rustLocalName(name) << " = node;\n";
    }
    for (const auto &name : alternative.optionalTokenFields) {
      const auto binding = rustLocalName(name);
      output
          << "    let " << binding << " = node.field_names.iter()\n"
          << "        .position(|field| field == \"" << name << "\")\n"
          << "        .and_then(|index| node.elements.get(index))\n"
          << "        .filter(|value| value.kind == AstValueKind::Optional)\n"
          << "        .ok_or(\"missing optional token\")?;\n"
          << "    let " << binding << " = " << binding << ".elements.first();\n"
          << "    if " << binding
          << ".is_some_and(|value| value.kind != AstValueKind::Token) {\n"
          << "        return Err(\"unexpected optional token\");\n"
          << "    }\n";
    }
    if (!alternative.childFields.empty() && !alternative.actionDispatchType) {
      output << "    let child = |field_name: &str| {\n"
                "        node.field_names\n"
                "            .iter()\n"
                "            .position(|field| field == field_name)\n"
                "            .and_then(|index| node.elements.get(index))\n"
                "            .ok_or(\"missing child node\")\n"
                "    };\n";
      for (const auto &[name, type] : alternative.childFields) {
        const auto binding = rustLocalName(name);
        output << "    let " << binding << " = child(\"" << name << "\")?;\n";
        output << "    if !is_" << rustRuleName(type) << "_ast(" << binding
               << ") {\n";
        output << "        return Err(\"unexpected child AST node\");\n"
                  "    }\n";
      }
    }
    if (alternative.actionDispatchType) {
      const auto &[name, type] = alternative.childFields.front();
      output << "    let " << rustLocalName(name) << " = node;\n";
    }
    for (const auto &[name, type] : alternative.optionalChildFields) {
      const auto binding = rustLocalName(name);
      output
          << "    let " << binding << " = node.field_names.iter()\n"
          << "        .position(|field| field == \"" << name << "\")\n"
          << "        .and_then(|index| node.elements.get(index))\n"
          << "        .filter(|value| value.kind == AstValueKind::Optional)\n"
          << "        .ok_or(\"missing optional child\")?;\n"
          << "    let " << binding << " = " << binding
          << ".elements.first();\n";
      const auto condition = "!is_" + rustRuleName(type) + "_ast(value)";
      output << "    if " << binding << ".is_some_and(|value| " << condition
             << ") {\n"
             << "        return Err(\"unexpected optional child\");\n"
             << "    }\n";
    }
    if (!alternative.childListFields.empty()) {
      output
          << "    let children = |field_name: &str| {\n"
             "        node.field_names\n"
             "            .iter()\n"
             "            .position(|field| field == field_name)\n"
             "            .and_then(|index| node.elements.get(index))\n"
             "            .filter(|value| value.kind == AstValueKind::List)\n"
             "            .ok_or(\"missing child list\")\n"
             "    };\n";
      for (const auto &[name, type] : alternative.childListFields) {
        const auto binding = rustLocalName(name);
        output << "    let " << binding << " = children(\"" << name
               << "\")?;\n";
        const auto helper = "is_" + rustRuleName(type) + "_ast";
        output << "    if " << binding << "\n"
               << "        .elements\n"
                  "        .iter()\n"
                  "        .any(|value| !"
               << helper << "(value))\n"
               << "    {\n";
        output << "        return Err(\"unexpected child list item\");\n"
                  "    }\n";
      }
    }
    output << renderAlternativeBody(checkedBody);
    return output.str();
  }

private:
  auto checkStatement(const Value &value, Environment &environment,
                      std::string_view resultType, bool last)
      -> CheckedStatement {
    if (value.typeName == "letStatement") {
      if (optional(field(value, "context")) ||
          optional(field(value, "failure")))
        throw std::runtime_error(
            "context and failure clauses are not supported");
      const auto name = token(field(value, "name"));
      checkRustIdentifier(name);
      if (environment.contains(name))
        throw std::runtime_error("duplicate local binding: " + name);
      auto checked = checkExpression(field(value, "value"), environment);
      if (checked.type == "None")
        throw std::runtime_error("cannot infer type of none binding: " + name);
      environment.emplace(name, checked.type);
      CheckedStatement result{StatementKind::Let, value.sourceSpan};
      result.name = name;
      result.value = std::move(checked);
      return result;
    }
    if (value.typeName == "returnStatement") {
      auto checked = checkExpression(field(value, "value"), environment);
      const auto expectedType = currentFallible_ ? resultValueType(resultType)
                                                 : std::string{resultType};
      if (checked.type != expectedType &&
          !(checked.type == "None" && expectedType.starts_with("Option<")))
        throw std::runtime_error("return type mismatch: expected " +
                                 expectedType + ", got " + checked.type);
      CheckedStatement result{StatementKind::Return, value.sourceSpan};
      result.unitReturn = !currentFallible_ && expectedType == "Unit" &&
                          checked.kind == ExpressionKind::Unit;
      result.fallible = currentFallible_;
      result.value = std::move(checked);
      return result;
    }
    if (value.typeName == "errorStatement") {
      if (!currentFallible_ || token(field(value, "kind")) != "error")
        throw std::runtime_error("unsupported error statement");
      CheckedStatement result{StatementKind::Error, value.sourceSpan};
      result.message = quotedText(field(value, "message"));
      if (const auto *location = optional(field(value, "location"))) {
        if (!currentMutates_)
          throw std::runtime_error("located errors require mutates");
        auto span = checkExpression(field(*location, "value"), environment);
        if (span.type != "SourceRange")
          throw std::runtime_error("error location must be SourceRange");
        result.location = std::move(span);
      }
      return result;
    }
    if (value.typeName == "expressionOrAssignment") {
      if (optional(field(value, "assignment")))
        throw std::runtime_error("assignment is not supported in functions");
      auto checked = checkExpression(field(value, "target"), environment);
      if (checked.type != "Unit")
        throw std::runtime_error("only Unit calls can be statements");
      CheckedStatement result{StatementKind::Expression, value.sourceSpan};
      result.value = std::move(checked);
      return result;
    }
    if (value.typeName == "ifStatement") {
      auto condition = checkExpression(field(value, "condition"), environment);
      if (condition.type != "Bool")
        throw std::runtime_error("if condition must have type Bool");
      CheckedStatement result{StatementKind::If, value.sourceSpan};
      result.value = std::move(condition);
      result.thenBranch = checkBlock(field(value, "thenBranch"), environment,
                                     resultType);
      if (const auto *otherwise = optional(field(value, "otherwise"))) {
        result.hasElse = true;
        result.elseBranch = checkBlock(*otherwise, environment, resultType);
      }
      return result;
    }
    if (value.typeName == "foreachStatement") {
      if (optional(field(value, "filter")))
        throw std::runtime_error("foreach filter is not supported");
      auto collection = checkExpression(field(value, "collection"), environment);
      if (!collection.type.starts_with("List<") ||
          !collection.type.ends_with('>'))
        throw std::runtime_error("foreach requires a list type");
      const auto name = token(field(value, "item"));
      checkRustIdentifier(name);
      if (environment.contains(name))
        throw std::runtime_error("duplicate foreach binding: " + name);
      auto inner = environment;
      const auto itemType =
          collection.type.substr(5, collection.type.size() - 6);
      inner[name] = itemType;
      CheckedStatement result{StatementKind::Foreach, value.sourceSpan};
      result.name = name;
      result.itemType = itemType;
      result.value = std::move(collection);
      result.thenBranch = checkBlock(field(value, "body"), std::move(inner),
                                     resultType);
      return result;
    }
    throw std::runtime_error("unsupported statement in typed Rust subset: " +
                             value.typeName);
  }

  auto renderStatement(const CheckedStatement &plan, std::size_t indent,
                       bool last) const -> std::string {
    const std::string pad(indent * 4, ' ');
    switch (plan.kind) {
    case StatementKind::Let:
      return pad + "let " + plan.name + " = " +
             renderExpression(*plan.value).code + ";\n";
    case StatementKind::Return: {
      if (plan.unitReturn)
        return last ? "" : pad + "return;\n";
      const auto value = renderExpression(*plan.value);
      const auto code = !value.directResultCode.empty()
                            ? value.directResultCode
                        : plan.fallible ? "Ok(" + value.code + ")" : value.code;
      return last ? pad + code + "\n" : pad + "return " + code + ";\n";
    }
    case StatementKind::Error: {
      std::string locationCode;
      if (plan.location)
        locationCode = pad + "ctx.note_error(" +
                       renderExpression(*plan.location).code + ");\n";
      return locationCode + (last ? pad + "Err(" + plan.message + ")\n"
                                  : pad + "return Err(" + plan.message + ");\n");
    }
    case StatementKind::Expression:
      return pad + renderExpression(*plan.value).code + ";\n";
    case StatementKind::If: {
      std::string result = pad + "if " + renderExpression(*plan.value).code +
                           " {\n";
      result += renderBlock(plan.thenBranch, indent + 1);
      result += pad + "}";
      if (plan.hasElse) {
        result += " else {\n";
        result += renderBlock(plan.elseBranch, indent + 1);
        result += pad + "}";
      }
      return result + "\n";
    }
    case StatementKind::Foreach: {
      const auto collection = renderExpression(*plan.value).code;
      const auto iterable = plan.itemType == "ParameterId"
                                ? collection + ".iter().copied()"
                            : plan.itemType == "ScopeId" ||
                                      plan.itemType == "SymbolId"
                                ? collection
                                : collection + ".iter().cloned()";
      return pad + "for " + plan.name + " in " + iterable + " {\n" +
             renderBlock(plan.thenBranch, indent + 1) + pad + "}\n";
    }
    }
    throw std::logic_error("unknown checked action statement");
  }

  auto compileStatement(const Value &value, Environment &environment,
                        std::string_view resultType, std::size_t indent,
                        bool last) -> std::string {
    return renderStatement(checkStatement(value, environment, resultType, last),
                           indent, last);
  }

  const std::map<std::string, Signature> &signatures_;
  EnumSchemas enums_;
  std::string contextType_;
  std::map<std::string, std::string> analysisResults_;
  std::map<std::string, std::vector<std::pair<std::string, std::string>>>
      analysisInputs_;
  std::map<std::string, RuleAstKind> astKinds_;
  std::map<std::string, std::set<std::string>> acceptedNodeTypes_;
  bool currentMutates_{};
  bool currentFallible_{};
};

struct StatementIrPolicy {
  std::map<std::string, std::string> fields;
  std::map<std::string, std::string> bindings;
};

auto statementEntries(const Value &declaration,
                      const std::set<std::string> &allowed,
                      std::string_view kind)
    -> std::map<std::string, std::string> {
  std::map<std::string, std::string> result;
  for (const auto &entry : field(declaration, "entries").elements) {
    const auto key = token(field(entry, "name"));
    if (!allowed.contains(key))
      throw std::runtime_error(std::string{kind} + " unknown field " + key);
    const auto value = token(field(entry, "value"));
    if (key.ends_with("_ir"))
      checkRustTypeIdentifier(value);
    else
      checkRustIdentifier(value);
    if (!result.emplace(key, value).second)
      throw std::runtime_error(std::string{kind} + " duplicate field " + key);
  }
  for (const auto &key : allowed)
    if (!result.contains(key))
      throw std::runtime_error(std::string{kind} + " missing field " + key);
  return result;
}

auto parseStatementIr(const Value &root,
                      const CheckedModelBindings *checked = nullptr)
    -> std::optional<StatementIrPolicy> {
  const auto *model = optional(field(root, "model"));
  if (!model)
    return std::nullopt;
  static const std::set<std::string> fields{
      "append",       "call",       "if_without_else",
      "if_with_else", "loop_while", "for_initialization",
      "loop_for",     "call_ir",    "if_ir",
      "while_ir",     "block_ir",   "for_ir"};
  static const std::set<std::string> bindings{"scopes", "emit"};
  const Value *policy = nullptr;
  const Value *binding = nullptr;
  for (const auto &declaration : field(*model, "declarations").elements) {
    if (declaration.typeName == "statementIrPolicy") {
      if (policy)
        throw std::runtime_error("only one statement_ir is supported");
      policy = &declaration;
    } else if (declaration.typeName == "statementIrBindings") {
      if (binding)
        throw std::runtime_error("only one statement_bindings is supported");
      binding = &declaration;
    }
  }
  if (!policy && !binding)
    return std::nullopt;
  if (!policy || (!binding && !checked))
    throw std::runtime_error(policy ? "statement_ir needs statement_bindings"
                                    : "statement_bindings needs statement_ir");
  if (binding &&
      token(field(*policy, "name")) != token(field(*binding, "name")))
    throw std::runtime_error(
        "statement_ir and statement_bindings names differ");
  StatementIrPolicy parsed{
      statementEntries(*policy, fields, "statement_ir"),
      checked ? checked->names("statement", token(field(*policy, "name")))
              : statementEntries(*binding, bindings, "statement_bindings")};
  const std::set<std::string> keys{"append",          "call",
                                   "if_without_else", "if_with_else",
                                   "loop_while",      "for_initialization",
                                   "loop_for"};
  std::set<std::string> functionNames;
  for (const auto &key : keys)
    if (!functionNames.insert(parsed.fields.at(key)).second)
      throw std::runtime_error("statement_ir function names must differ");
  return parsed;
}

auto emitStatementIrBody(const StatementIrPolicy &policy, std::string_view key)
    -> std::string {
  const auto &f = policy.fields;
  const auto &scopes = policy.bindings.at("scopes");
  const auto &emit = policy.bindings.at("emit");
  const auto variant = [&](std::string_view name) {
    return "crate::model::Operation::" + f.at(std::string{name});
  };
  if (key == "append")
    return "    ctx." + scopes + "[scope.0].operations.push(operation);\n";
  const auto write = [&](const std::string &operation) {
    return "    ctx." + emit + "(" + operation + ", source)\n";
  };
  if (key == "call")
    return write(variant("call_ir") + "(value)");
  if (key == "if_without_else")
    return write(variant("if_ir") +
                 " { condition, then_branch, else_branch: None }");
  if (key == "if_with_else")
    return write(variant("if_ir") +
                 " { condition, then_branch, else_branch: Some(else_branch) }");
  if (key == "loop_while")
    return write(variant("while_ir") + " { condition, body }");
  if (key == "for_initialization")
    return "    let body = ctx." + scopes + "[scope.0].operations.clone();\n" +
           write(variant("block_ir") + " { scope, body }");
  if (key == "loop_for")
    return write(variant("for_ir") +
                 " { scope, initialization, condition, body, update }");
  throw std::runtime_error("unknown statement_ir function key");
}

struct FlowActions {
  std::map<std::string, std::string> functions;
  std::map<std::string, std::string> bindings;
};

auto parseFlowActions(const Value &root,
                      const CheckedModelBindings *checked = nullptr)
    -> std::optional<FlowActions> {
  const auto *model = optional(field(root, "model"));
  if (!model)
    return std::nullopt;
  static const std::set<std::string> actions{
      "snapshot", "restore", "merge_snapshot", "recover"};
  static const std::set<std::string> bindings{
      "current", "snapshots", "merge", "record_error", "emit", "error_ir"};
  const Value *action = nullptr;
  const Value *binding = nullptr;
  for (const auto &declaration : field(*model, "declarations").elements) {
    if (declaration.typeName == "flowActions") {
      if (action)
        throw std::runtime_error("only one flow_actions is supported");
      action = &declaration;
    } else if (declaration.typeName == "flowBindings") {
      if (binding)
        throw std::runtime_error("only one flow_bindings is supported");
      binding = &declaration;
    }
  }
  if (!action && !binding)
    return std::nullopt;
  if (!action || (!binding && !checked))
    throw std::runtime_error(action ? "flow_actions needs flow_bindings"
                                    : "flow_bindings needs flow_actions");
  if (binding &&
      token(field(*action, "name")) != token(field(*binding, "name")))
    throw std::runtime_error("flow_actions and flow_bindings names differ");
  FlowActions parsed{
      statementEntries(*action, actions, "flow_actions"),
      checked ? checked->names("flow", token(field(*action, "name")))
              : statementEntries(*binding, bindings, "flow_bindings")};
  std::set<std::string> names;
  for (const auto &[key, name] : parsed.functions) {
    static_cast<void>(key);
    if (!names.insert(name).second)
      throw std::runtime_error("flow_actions function names must differ");
  }
  return parsed;
}

auto emitFlowActionBody(const FlowActions &policy, std::string_view key)
    -> std::string {
  const auto &bindings = policy.bindings;
  const auto &current = bindings.at("current");
  const auto &snapshots = bindings.at("snapshots");
  if (key == "snapshot")
    return "    FlowId(agsem_runtime::snapshot_flow(&ctx." + current +
           ", &mut ctx." + snapshots + "))\n";
  if (key == "restore")
    return "    ctx." + current + " = agsem_runtime::restore_flow(&ctx." +
           snapshots + ", snapshot.0);\n";
  if (key == "merge_snapshot")
    return "    ctx." + current + " = agsem_runtime::merge_flow_snapshot(&ctx." +
           current + ", &ctx." + snapshots + ", snapshot.0, " +
           "|left, right| ctx." + bindings.at("merge") + "(left, right));\n";
  if (key == "recover")
    return "    ctx." + bindings.at("record_error") +
           "(message, source);\n" +
           "    ctx." + current + " = agsem_runtime::restore_flow(&ctx." +
           snapshots + ", snapshot.0);\n" +
           "    ctx." + bindings.at("emit") +
           "(crate::model::Operation::" + bindings.at("error_ir") +
           ", source)\n";
  throw std::runtime_error("unknown flow_actions function key");
}

struct SelectionPolicy {
  std::string function;
  std::string collector;
  std::string completion;
  std::string candidateType;
  std::map<std::string, std::string> bindings;
  std::string missing;
  std::string ambiguous;
  std::string inaccessible;
  std::string nonclass;
  std::string missingDefault;
  std::string incompatible;
};

auto parseSelectionPolicy(const Value &root,
                          const CheckedModelBindings *checked = nullptr)
    -> std::optional<SelectionPolicy> {
  const auto *model = optional(field(root, "model"));
  if (!model)
    return std::nullopt;
  const Value *declaration = nullptr;
  for (const auto &entry : field(*model, "declarations").elements)
    if (entry.typeName == "selectionPolicy") {
      if (declaration)
        throw std::runtime_error("only one selection_policy is supported");
      declaration = &entry;
    }
  if (!declaration)
    return std::nullopt;
  static const std::set<std::string> allowed{
      "choose", "collect", "complete", "candidate", "receiver", "ranking",
      "access", "fallback", "missing", "ambiguous", "inaccessible",
      "nonclass", "missing_default", "incompatible"};
  std::map<std::string, std::string> fields;
  for (const auto &entry : field(*declaration, "entries").elements) {
    const auto key = token(field(entry, "name"));
    if (!allowed.contains(key))
      throw std::runtime_error("selection_policy unknown field " + key);
    const auto value = token(field(entry, "value"));
    if (!fields.emplace(key, value).second)
      throw std::runtime_error("selection_policy duplicate field " + key);
  }
  for (const auto &key : allowed)
    if (!fields.contains(key))
      throw std::runtime_error("selection_policy missing field " + key);
  checkRustIdentifier(fields.at("choose"));
  checkRustIdentifier(fields.at("collect"));
  checkRustIdentifier(fields.at("complete"));
  checkRustTypeIdentifier(fields.at("candidate"));
  const auto requireValue = [&](std::string_view key, std::string_view expected) {
    if (fields.at(std::string{key}) != expected)
      throw std::runtime_error("selection_policy unsupported " +
                               std::string{key} + ": " +
                               fields.at(std::string{key}));
  };
  requireValue("ranking", "fewest_conversions");
  requireValue("receiver", "first_parameter");
  requireValue("access", "after_ranking");
  requireValue("fallback", "no_declared_and_no_arguments");
  const auto message = [&](std::string_view key) {
    const auto &value = fields.at(std::string{key});
    if (value.size() < 2 || value.front() != '"' || value.back() != '"' ||
        value.find('\\') != std::string::npos)
      throw std::runtime_error("selection_policy " + std::string{key} +
                               " needs a plain string literal");
    return value;
  };
  const Value *binding = nullptr;
  for (const auto &entry : field(*model, "declarations").elements)
    if (entry.typeName == "selectionBindings") {
      if (binding)
        throw std::runtime_error("only one selection_bindings is supported");
      binding = &entry;
    }
  if (!binding && !checked)
    throw std::runtime_error("selection_policy needs selection_bindings");
  if (binding &&
      token(field(*binding, "name")) != token(field(*declaration, "name")))
    throw std::runtime_error("selection_bindings name differs from selection_policy");
  static const std::set<std::string> bindingFields{
      "structs", "constructors", "functions", "parameters", "expressions",
      "expression_type", "visibility", "accessible", "convertible",
      "variables", "variable_type", "variable_place", "fields", "field_type",
      "default_constructible", "convert", "construction_ir", "add_operation",
      "scopes", "scope_operations", "current_flow", "mark_initialized",
      "active_initializer"};
  auto bindings =
      checked ? checked->names("selection", token(field(*declaration, "name")))
              : statementEntries(*binding, bindingFields, "selection_bindings");
  return SelectionPolicy{fields.at("choose"), fields.at("collect"),
                         fields.at("complete"),
                         fields.at("candidate"), std::move(bindings),
                         message("missing"), message("ambiguous"),
                         message("inaccessible"), message("nonclass"),
                         message("missing_default"), message("incompatible")};
}

auto emitSelectionCandidatesBody(const SelectionPolicy &policy,
                                 std::string_view contextType) -> std::string {
  const auto &b = policy.bindings;
  return "    ctx." + b.at("structs") + "[owner.0]." + b.at("constructors") +
         ".iter().copied().filter_map(|constructor| {\n"
         "        let expected = &ctx." + b.at("functions") +
         "[constructor.0]." + b.at("parameters") + "[1..];\n"
         "        if expected.len() != arguments.len() { return None; }\n"
         "        let mut rank = 0usize;\n"
         "        for (argument, ty) in arguments.iter().zip(expected) {\n"
         "            let actual = &ctx." + b.at("expressions") +
         "[argument.0]." + b.at("expression_type") + ";\n"
         "            if actual == ty { continue; }\n"
         "            if !" + std::string{contextType} + "::" + b.at("convertible") +
         "(actual, ty) { return None; }\n"
         "            rank += 1;\n"
         "        }\n"
         "        Some(agsem_runtime::RankedCandidate {\n"
         "            id: constructor, conversions: expected.to_vec(), rank,\n"
         "            accessible: ctx." + b.at("accessible") + "(ctx." +
         b.at("functions") + "[constructor.0]." + b.at("visibility") +
         ", owner),\n"
         "        })\n"
         "    }).collect()\n";
}

auto emitSelectionCompletionBody(const SelectionPolicy &policy) -> std::string {
  const auto &b = policy.bindings;
  std::ostringstream out;
  out << "    let ty = ctx." << b.at("variables") << "[symbol.0]."
      << b.at("variable_type") << ".clone();\n"
      << "    let crate::model::Type::Struct(owner) = ty else { return Err("
      << policy.nonclass << "); };\n"
      << "    if ctx." << b.at("structs") << "[owner.0]." << b.at("fields")
      << ".iter().any(|field| !ctx." << b.at("default_constructible")
      << "(&ctx." << b.at("fields") << "[field.0]." << b.at("field_type")
      << ")) { return Err(" << policy.missingDefault << "); }\n"
      << "    let candidates = " << policy.collector
      << "(ctx, owner, arguments);\n"
      << "    let selected = " << policy.function << "(ctx, &candidates, !ctx."
      << b.at("structs") << "[owner.0]." << b.at("constructors")
      << ".is_empty(), !arguments.is_empty())?;\n"
      << "    let converted = if let Some(candidate) = &selected {\n"
      << "        arguments.iter().zip(&candidate.conversions)\n"
      << "            .map(|(&argument, ty)| ctx." << b.at("convert")
      << "(argument, ty.clone()))\n"
      << "            .collect::<Result<Vec<_>, _>>()\n"
      << "            .map_err(|_| " << policy.incompatible << ")?\n"
      << "    } else { arguments.to_vec() };\n"
      << "    let operation = ctx." << b.at("add_operation")
      << "(crate::model::Operation::" << b.at("construction_ir")
      << "(symbol, selected.map(|candidate| candidate.id), converted), "
         "source);\n"
      << "    ctx." << b.at("scopes") << "[scope.0]."
      << b.at("scope_operations") << ".push(operation);\n"
      << "    let place = ctx." << b.at("variables") << "[symbol.0]."
      << b.at("variable_place") << ";\n"
      << "    let mut flow = ctx." << b.at("current_flow") << ".clone();\n"
      << "    ctx." << b.at("mark_initialized") << "(&mut flow, place);\n"
      << "    ctx." << b.at("current_flow") << " = flow;\n"
      << "    ctx." << b.at("active_initializer") << " = None;\n"
      << "    Ok(())\n";
  return out.str();
}

auto emitSelectionPolicyBody(const SelectionPolicy &policy) -> std::string {
  return "    let _ = ctx;\n"
         "    let allow_implicit_default = !has_declared && !has_arguments;\n"
         "    "
         "    agsem_runtime::select_ranked_candidate(candidates.iter().cloned(), "
         "allow_implicit_default)\n"
         "        .map_err(|error| match error {\n"
         "            agsem_runtime::SelectionFailure::NoMatch => " +
         policy.missing + ",\n" +
         "            agsem_runtime::SelectionFailure::Ambiguous => " +
         policy.ambiguous + ",\n" +
         "            agsem_runtime::SelectionFailure::Inaccessible => " +
         policy.inaccessible + ",\n" +
         "        })\n";
}

struct ModuleAssignments {
  std::map<std::string, std::string> rules;
  std::map<std::string, std::string> functions;
  std::set<std::string> names;
};

auto parseModules(const Value &root,
                  const std::map<std::string, Signature> &signatures,
                  const std::vector<RuleAnalyses> &analyses)
    -> ModuleAssignments {
  ModuleAssignments result;
  const auto *model = optional(field(root, "model"));
  if (!model)
    return result;
  const Value *declaration = nullptr;
  for (const auto &entry : field(*model, "declarations").elements)
    if (entry.typeName == "modulesDeclaration") {
      if (declaration)
        throw std::runtime_error("only one modules block is supported");
      declaration = &entry;
    }
  if (!declaration)
    return result;
  std::set<std::string> knownRules;
  for (const auto &rule : analyses)
    knownRules.insert(rule.name);
  for (const auto &module : field(*declaration, "modules").elements) {
    const auto name = token(field(module, "name"));
    checkRustIdentifier(name);
    if (name == "lib" || name == "type" || name == "_")
      throw std::runtime_error("modules name collides with Rust or generated file: " +
                               name);
    if (!result.names.insert(name).second)
      throw std::runtime_error("modules duplicate module: " + name);
    std::set<std::string> seenFields;
    for (const auto &entry : field(module, "fields").elements) {
      const auto kind = token(field(entry, "name"));
      if (kind != "rules" && kind != "functions")
        throw std::runtime_error("modules unknown field: " + kind);
      if (!seenFields.insert(kind).second)
        throw std::runtime_error("modules duplicate field: " + kind);
      const auto *list = optional(field(entry, "values"));
      if (!list)
        continue;
      std::vector<std::string> members;
      if (list->kind == agas::runtime::AstValueKind::Token) {
        members.push_back(token(*list));
      } else if (list->fieldNames.empty() && list->elements.size() == 1 &&
                 list->elements.front().kind ==
                     agas::runtime::AstValueKind::Token) {
        members.push_back(token(list->elements.front()));
      } else {
        members.push_back(token(field(*list, "first")));
        for (const auto &tail : field(*list, "rest").elements)
          members.push_back(tail.kind == agas::runtime::AstValueKind::Token
                                ? token(tail)
                                : token(field(tail, "value")));
      }
      for (const auto &member : members) {
        if (kind == "rules" && !knownRules.contains(member))
          throw std::runtime_error("modules unknown rule: " + member);
        if (kind == "functions" &&
            (!signatures.contains(member) || signatures.at(member).intrinsic))
          throw std::runtime_error("modules unknown function: " + member);
        auto &owners = kind == "rules" ? result.rules : result.functions;
        if (!owners.emplace(member, name).second)
          throw std::runtime_error("modules duplicate owner for " + member);
      }
    }
    if (!seenFields.contains("rules") && !seenFields.contains("functions"))
      throw std::runtime_error("modules entry needs rules or functions: " +
                               name);
    const auto ownedBy = [&](const auto &owners) {
      return std::ranges::any_of(owners, [&](const auto &entry) {
        return entry.second == name;
      });
    };
    if (!ownedBy(result.rules) && !ownedBy(result.functions))
      throw std::runtime_error("modules empty module: " + name);
  }
  return result;
}

} // namespace

namespace {
template <class Binding>
auto bindingNames(const CheckedModelBindings &checked, std::string_view family)
    -> std::vector<Binding> {
  std::vector<Binding> result;
  for (const auto &policy : checked.policies())
    if (policy.family == family)
      result.push_back({policy.policy, policy.names});
  return result;
}
} // namespace
void validateSemanticPolicies(const SemanticInput &input) {
  validateSemanticPolicies(SemanticInputAccess::root(input),
                           SemanticInputAccess::modelBindings(input).get());
}
void validateSemanticPolicies(const Value &root,
                              const CheckedModelBindings *checked) {
  static_cast<void>(parseReturnPolicies(root));
  if (!checked)
    static_cast<void>(parseReturnModelBindings(root));
  static_cast<void>(parseAssignmentPolicies(root));
  if (!checked)
    static_cast<void>(parseAssignmentBindings(root));
  static_cast<void>(parseConditionPolicies(root));
  if (!checked)
    static_cast<void>(parseConditionBindings(root));
  static_cast<void>(parseStatementIr(root, checked));
  static_cast<void>(parseFlowActions(root, checked));
  static_cast<void>(parseSelectionPolicy(root, checked));
}

struct SemanticPreparation {
  explicit SemanticPreparation(SemanticInput ownedInput)
      : input(std::move(ownedInput)) {}

  SemanticInput input;
  CodegenContract contract;
  EnumSchemas enums;
  std::vector<CheckedEnumConstant> enumConstants;
  std::vector<CheckedCollectionOperation> collectionOperations;
  std::map<std::string, Signature> signatures;
  std::vector<ReturnPolicy> returnPolicies;
  std::vector<ReturnModelBindings> modelBindings;
  std::map<std::string, const ReturnPolicy *> policyByName;
  std::vector<AssignmentPolicy> assignmentPolicies;
  std::vector<AssignmentBindings> assignmentBindings;
  std::map<std::string, const AssignmentPolicy *> assignmentByFunction;
  std::map<std::string, const AssignmentBindings *> assignmentBindingByName;
  std::vector<ConditionPolicy> conditionPolicies;
  std::vector<ConditionBindings> conditionBindings;
  std::map<std::string, const ConditionPolicy *> conditionByName;
  std::map<std::string, const ConditionBindings *> conditionBindingByName;
  std::optional<StatementIrPolicy> statementIr;
  std::map<std::string, std::string> statementByFunction;
  std::optional<FlowActions> flowActions;
  std::map<std::string, std::string> flowByFunction;
  std::optional<SelectionPolicy> selectionPolicy;
  std::map<std::string, const ReturnModelBindings *> bindingByName;
  std::vector<RuleAnalyses> analyses;
  ModuleAssignments modules;
  std::map<std::string, std::vector<Compiler::CheckedStatement>> functionPlans;
  std::map<std::string, std::vector<std::vector<Compiler::CheckedAlternativeStep>>> alternativeBodies;
};

struct CheckedSemantics : SemanticPreparation {
  explicit CheckedSemantics(SemanticPreparation &&prepared)
      : SemanticPreparation(std::move(prepared)) {}
};

auto checkedModelBindings(const CheckedSemantics &model)
    -> const CheckedModelBindings & {
  static const CheckedModelBindings empty;
  const auto &bindings = SemanticInputAccess::modelBindings(model.input);
  return bindings ? *bindings : empty;
}
auto checkedEnumConstants(const CheckedSemantics &model)
    -> const std::vector<CheckedEnumConstant> & {
  return model.enumConstants;
}

auto checkedCollectionOperations(const CheckedSemantics &model)
    -> const std::vector<CheckedCollectionOperation> & {
  return model.collectionOperations;
}

auto inspectSemanticModel(const CheckedSemantics &model) -> std::string {
  nlohmann::ordered_json output{
      {"format", "agsem-checked-semantic-expansion-v3"},
      {"scope", "semantic interfaces, enum constants, collection operations "
                "and model bindings"},
      {"source_sha256", SemanticInputAccess::identity(model.input)},
      {"contracts_sha256",
       SemanticInputAccess::contractsIdentity(model.input)}};
  const auto bindings = nlohmann::ordered_json::parse(
      inspectModelBindings(checkedModelBindings(model)));
  for (const auto &[key, value] : bindings.items())
    output[key] = value;
  output["functions"] = nlohmann::ordered_json::array();
  for (const auto &[name, signature] : model.signatures) {
    nlohmann::ordered_json parameters = nlohmann::ordered_json::array();
    for (const auto &parameter : signature.parameters)
      parameters.push_back({{"name", parameter.name}, {"type", parameter.type}});
    output["functions"].push_back({{"name", name}, {"parameters", parameters},
                                    {"result", signature.result}});
  }
  output["analyzers"] = nlohmann::ordered_json::array();
  for (const auto &rule : model.analyses) {
    nlohmann::ordered_json inputs = nlohmann::ordered_json::array();
    for (const auto &[name, type] : rule.inputs)
      inputs.push_back({{"name", name}, {"type", type}});
    output["analyzers"].push_back({{"name", rule.name}, {"inputs", inputs},
                                    {"result", rule.resultType}});
  }
  output["enum_schemas"] = nlohmann::ordered_json::array();
  for (const auto &[name, symbol] : model.enums) {
    nlohmann::ordered_json variants = nlohmann::ordered_json::object();
    for (const auto &[variant, payload] : symbol.type->variants) {
      variants[variant] = nlohmann::ordered_json::array();
      for (const auto &type : payload)
        variants[variant].push_back(typeSpelling(type));
    }
    output["enum_schemas"].push_back({{"name", name}, {"arity", symbol.type->arity},
                                     {"variants", variants}, {"rust", symbol.rust}});
  }
  output["enum_constants"] = nlohmann::ordered_json::array();
  for (const auto &constant : model.enumConstants)
    output["enum_constants"].push_back({{"type", constant.type},
        {"variant", constant.variant}, {"rust", constant.rust},
        {"begin_byte", constant.source.beginByte}, {"end_byte", constant.source.endByte}});
  output["collection_operations"] = nlohmann::ordered_json::array();
  for (const auto &operation : model.collectionOperations)
    output["collection_operations"].push_back({{"operation", operation.operation},
        {"element_type", operation.elementType}, {"result_type", operation.resultType},
        {"operand_types", operation.operandTypes}, {"operand_ownership", operation.operandOwnership},
        {"ownership", operation.ownership}, {"rust", operation.rust},
        {"rust_element_type", operation.rustElementType},
        {"begin_byte", operation.source.beginByte}, {"end_byte", operation.source.endByte}});
  return output.dump(2) + '\n';
}

static auto prepareSemanticContext(const SemanticInput &input, bool partial)
    -> std::shared_ptr<SemanticPreparation> {
  auto prepared = std::make_shared<SemanticPreparation>(input);
  const auto *checkedBindings = SemanticInputAccess::modelBindings(input).get();
  const auto &root = SemanticInputAccess::root(prepared->input);
  auto &contract = prepared->contract;
  contract = codegenContract(root, partial);
  for (const auto &symbol : SemanticInputAccess::contracts(input))
    if (symbol.kind == SymbolKind::Type && symbol.type &&
        symbol.type->kind == TypeContract::Kind::Enum)
      prepared->enums.emplace(symbol.name, symbol);
  auto &signatures = prepared->signatures;
  if (const auto *model = optional(field(root, "model")))
    collectSignatures(*model, signatures);
  for (const auto &symbol : SemanticInputAccess::contracts(input)) {
    if (symbol.kind != SymbolKind::Function ||
        symbol.owner == SymbolOwner::Execution ||
        signatures.contains(symbol.name))
      continue;
    const auto &function = *symbol.function;
    Signature imported;
    imported.name = symbol.name;
    imported.result = typeSpelling(function.result);
    imported.intrinsic = true;
    imported.mutates = function.mutates;
    for (std::size_t i = 0; i < function.parameters.size(); ++i)
      imported.parameters.push_back({"argument_" + std::to_string(i),
                                     typeSpelling(function.parameters[i])});
    for (const auto &segment : symbol.rust)
      imported.externalPath +=
          (imported.externalPath.empty() ? "" : "::") + segment;
    imported.externalContext = !function.context.empty();
    imported.needsExternalBinding = symbol.rust.empty();
    signatures.emplace(symbol.name, std::move(imported));
  }
  auto &returnPolicies = prepared->returnPolicies;
  returnPolicies = parseReturnPolicies(root);
  if (returnPolicies.size() > 1)
    throw std::runtime_error(
        "only one return_policy is supported per semantic_model");
  auto &modelBindings = prepared->modelBindings;
  modelBindings =
      checkedBindings
          ? bindingNames<ReturnModelBindings>(*checkedBindings, "return")
          : parseReturnModelBindings(root);
  auto &policyByName = prepared->policyByName;
  for (const auto &policy : returnPolicies) {
    checkRustIdentifier(policy.name);
    if (signatures.contains(policy.name))
      throw std::runtime_error("return_policy name conflicts with function: " +
                               policy.name);
    signatures.emplace(policy.name, Signature{policy.name,
                                              {{"value", "Option<ExprId>"},
                                               {"source", "SourceRange"}},
                                              "Result<OpId>",
                                              false,
                                              true,
                                              nullptr,
                                              true});
    policyByName.emplace(policy.name, &policy);
  }
  auto &assignmentPolicies = prepared->assignmentPolicies;
  assignmentPolicies = parseAssignmentPolicies(root);
  if (assignmentPolicies.size() > 1)
    throw std::runtime_error(
        "only one assignment_policy is supported per semantic_model");
  auto &assignmentBindings = prepared->assignmentBindings;
  assignmentBindings =
      checkedBindings
          ? bindingNames<AssignmentBindings>(*checkedBindings, "assignment")
          : parseAssignmentBindings(root);
  auto &assignmentByFunction = prepared->assignmentByFunction;
  for (const auto &policy : assignmentPolicies) {
    const auto addFunction = [&](const std::string &name,
                                 std::vector<Parameter> parameters,
                                 std::string result, bool mutates) {
      checkRustIdentifier(name);
      if (!signatures
               .emplace(name, Signature{name, std::move(parameters),
                                        std::move(result), false, mutates,
                                        nullptr, false, true})
               .second)
        throw std::runtime_error(
            "assignment_policy name conflicts with function: " + name);
      assignmentByFunction.emplace(name, &policy);
    };
    addFunction(policy.checkName,
                {{"place", "PlaceId"}, {"operator", "AssignmentOp"}},
                "Result<Unit>", false);
    addFunction(policy.name,
                {{"place", "PlaceId"},
                 {"operator", "AssignmentOp"},
                 {"value", "ExprId"},
                 {"source", "SourceRange"}},
                "Result<OpId>", true);
    addFunction(policy.incrementName,
                {{"place", "PlaceId"},
                 {"increment", "Bool"},
                 {"source", "SourceRange"}},
                "Result<OpId>", true);
  }
  auto &assignmentBindingByName = prepared->assignmentBindingByName;
  for (const auto &binding : assignmentBindings) {
    if (std::ranges::none_of(assignmentPolicies, [&](const auto &policy) {
          return policy.name == binding.policyName;
        }))
      throw std::runtime_error(
          "assignment_bindings references unknown policy: " +
          binding.policyName);
    assignmentBindingByName.emplace(binding.policyName, &binding);
  }
  for (const auto &policy : assignmentPolicies)
    if (!assignmentBindingByName.contains(policy.name))
      throw std::runtime_error("assignment_policy needs assignment_bindings: " +
                               policy.name);
  auto &conditionPolicies = prepared->conditionPolicies;
  conditionPolicies = parseConditionPolicies(root);
  if (conditionPolicies.size() > 1)
    throw std::runtime_error(
        "only one condition_policy is supported per semantic_model");
  auto &conditionBindings = prepared->conditionBindings;
  conditionBindings =
      checkedBindings
          ? bindingNames<ConditionBindings>(*checkedBindings, "condition")
          : parseConditionBindings(root);
  auto &conditionByName = prepared->conditionByName;
  for (const auto &policy : conditionPolicies) {
    checkRustIdentifier(policy.name);
    if (!signatures
             .emplace(policy.name, Signature{policy.name,
                                             {{"value", "ExprId"}},
                                             "Result<ExprId>",
                                             false,
                                             true,
                                             nullptr,
                                             false,
                                             false,
                                             true})
             .second)
      throw std::runtime_error(
          "condition_policy name conflicts with function: " + policy.name);
    conditionByName.emplace(policy.name, &policy);
  }
  auto &conditionBindingByName = prepared->conditionBindingByName;
  for (const auto &binding : conditionBindings) {
    if (!conditionByName.contains(binding.policyName))
      throw std::runtime_error(
          "condition_bindings references unknown policy: " +
          binding.policyName);
    conditionBindingByName.emplace(binding.policyName, &binding);
  }
  for (const auto &policy : conditionPolicies)
    if (!conditionBindingByName.contains(policy.name))
      throw std::runtime_error("condition_policy needs condition_bindings: " +
                               policy.name);
  auto &statementIr = prepared->statementIr;
  statementIr = parseStatementIr(root, checkedBindings);
  auto &statementByFunction = prepared->statementByFunction;
  if (statementIr) {
    const auto addFunction = [&](std::string key,
                                 std::vector<Parameter> parameters,
                                 std::string result) {
      const auto name = statementIr->fields.at(key);
      if (!signatures
               .emplace(name, Signature{name, std::move(parameters),
                                         std::move(result), false, true, nullptr,
                                         false, false, false, true})
               .second)
        throw std::runtime_error("statement_ir name conflicts with function: " +
                                 name);
      statementByFunction.emplace(name, std::move(key));
    };
    addFunction("append", {{"scope", "ScopeId"}, {"operation", "OpId"}},
                "Unit");
    addFunction("call", {{"value", "ExprId"}, {"source", "SourceRange"}},
                "OpId");
    addFunction("if_without_else",
                {{"condition", "ExprId"},
                 {"then_branch", "OpId"},
                 {"source", "SourceRange"}},
                "OpId");
    addFunction("if_with_else",
                {{"condition", "ExprId"},
                 {"then_branch", "OpId"},
                 {"else_branch", "OpId"},
                 {"source", "SourceRange"}},
                "OpId");
    addFunction("loop_while",
                {{"condition", "ExprId"},
                 {"body", "OpId"},
                 {"source", "SourceRange"}},
                "OpId");
    addFunction("for_initialization",
                {{"scope", "ScopeId"}, {"source", "SourceRange"}}, "OpId");
    addFunction("loop_for",
                {{"scope", "ScopeId"},
                 {"initialization", "OpId"},
                 {"condition", "ExprId"},
                 {"body", "OpId"},
                 {"update", "OpId"},
                 {"source", "SourceRange"}},
                "OpId");
  }
  auto &flowActions = prepared->flowActions;
  flowActions = parseFlowActions(root, checkedBindings);
  auto &flowByFunction = prepared->flowByFunction;
  if (flowActions) {
    const auto addFunction = [&](std::string key,
                                 std::vector<Parameter> parameters,
                                 std::string result) {
      const auto name = flowActions->functions.at(key);
      Signature signature{name, std::move(parameters), std::move(result)};
      signature.mutates = true;
      signature.flowActions = true;
      if (!signatures.emplace(name, std::move(signature)).second)
        throw std::runtime_error("flow_actions name conflicts with function: " +
                                 name);
      flowByFunction.emplace(name, std::move(key));
    };
    addFunction("snapshot", {}, "FlowId");
    addFunction("restore", {{"snapshot", "FlowId"}}, "Unit");
    addFunction("merge_snapshot", {{"snapshot", "FlowId"}}, "Unit");
    addFunction("recover",
                {{"snapshot", "FlowId"}, {"message", "Text"},
                 {"source", "SourceRange"}},
                "OpId");
  }
  auto &selectionPolicy = prepared->selectionPolicy;
  selectionPolicy = parseSelectionPolicy(root, checkedBindings);
  if (selectionPolicy) {
    const auto &name = selectionPolicy->function;
    const auto candidate = selectionPolicy->candidateType;
    Signature signature{name,
                        {{"candidates", "List<" + candidate + ">"},
                         {"has_declared", "Bool"},
                         {"has_arguments", "Bool"}},
                        "Result<Option<" + candidate + ">>"};
    signature.selectionPolicy = true;
    if (!signatures.emplace(name, std::move(signature)).second)
      throw std::runtime_error("selection_policy name conflicts with function: " +
                               name);
    Signature collector{selectionPolicy->collector,
                        {{"owner", "StructId"}, {"arguments", "List<ExprId>"}},
                        "List<" + candidate + ">"};
    collector.selectionCandidates = true;
    if (!signatures.emplace(collector.name, std::move(collector)).second)
      throw std::runtime_error("selection_policy collector conflicts with function");
    Signature completion{selectionPolicy->completion,
                         {{"scope", "ScopeId"}, {"symbol", "SymbolId"},
                          {"arguments", "List<ExprId>"},
                          {"source", "SourceRange"}},
                         "Result<Unit>"};
    completion.mutates = true;
    completion.selectionCompletion = true;
    if (!signatures.emplace(completion.name, std::move(completion)).second)
      throw std::runtime_error("selection_policy completion conflicts with function");
  }
  auto &bindingByName = prepared->bindingByName;
  for (const auto &binding : modelBindings) {
    if (!policyByName.contains(binding.policyName))
      throw std::runtime_error("model_bindings references unknown policy: " +
                               binding.policyName);
    bindingByName.emplace(binding.policyName, &binding);
  }
  for (const auto &policy : returnPolicies)
    if (!bindingByName.contains(policy.name))
      throw std::runtime_error("return_policy needs model_bindings: " +
                               policy.name);
  for (const auto &policy : returnPolicies) {
    const auto &fields = bindingByName.at(policy.name)->fields;
    const bool hasCleanup = fields.at("cleanup_scopes") != "no_scopes";
    if (hasCleanup !=
        (policy.cleanup == ReturnCleanup::ExitedAutomaticLifetimes))
      throw std::runtime_error("return_policy cleanup and model_bindings "
                               "cleanup_scopes disagree: " +
                               policy.name);
    if (hasCleanup && fields.at("return_ir") != "record")
      throw std::runtime_error("return_policy cleanup needs record IR: " +
                               policy.name);
  }
  return prepared;
}

static auto parseTypeSpelling(std::string_view text) -> TypeRef {
  const auto open = text.find('<');
  if (open == std::string_view::npos)
    return {std::string(text), {}};
  TypeRef result{std::string(text.substr(0, open)), {}};
  std::size_t begin = open + 1, depth = 0;
  for (std::size_t i = begin; i < text.size(); ++i) {
    if (text[i] == '<')
      ++depth;
    else if (text[i] == '>' && depth)
      --depth;
    else if ((text[i] == ',' || text[i] == '>') && depth == 0) {
      result.arguments.push_back(
          parseTypeSpelling(text.substr(begin, i - begin)));
      begin = i + 1;
    }
  }
  return result;
}
// Logical alternatives retain unresolved interfaces; they are never emission
// plans.
static auto logicalAnalyses(const Value &root,
                            const agas::model::SyntaxDocument &grammar,
                            const CodegenContract &contract)
    -> std::vector<RuleAnalyses> {
  std::vector<RuleAnalyses> rules;
  const auto &authored = field(root, "rules").elements;
  for (std::size_t ri = 0; ri < grammar.parserRules.size(); ++ri) {
    const auto &source = grammar.parserRules[ri];
    RuleAnalyses rule{source.name, "", {}, {}};
    if (const auto it = contract.analyzers.find(source.name);
        it != contract.analyzers.end()) {
      rule.resultType = it->second.resultType;
      rule.inputs = it->second.inputs;
    }
    std::vector<const Value *> alternatives{&field(authored.at(ri), "first")};
    for (const auto &a : field(authored.at(ri), "rest").elements)
      alternatives.push_back(&a);
    for (std::size_t ai = 0; ai < source.alternatives.size(); ++ai) {
      const auto &a = source.alternatives[ai];
      RuleAlternative alternative;
      alternative.label = a.label.value_or("");
      for (const auto &element : a.elements) {
        if (!element.fieldName)
          continue;
        const auto &name = *element.fieldName;
        const bool tokenField = element.symbol.kind ==
                                agas::model::ParserSymbolKind::TokenReference;
        switch (element.quantifier) {
        case agas::model::Quantifier::One:
          if (tokenField)
            alternative.tokenFields.push_back(name);
          else
            alternative.childFields.emplace_back(name, element.symbol.name);
          break;
        case agas::model::Quantifier::Optional:
          if (tokenField)
            alternative.optionalTokenFields.push_back(name);
          else
            alternative.optionalChildFields.emplace_back(name,
                                                         element.symbol.name);
          break;
        default:
          alternative.childListFields.emplace_back(name, element.symbol.name);
        }
      }
      if (const auto *actions = ast::find(*alternatives.at(ai), "actions"))
        for (const auto &action : actions->elements) {
          if (action.typeName == "analysisBlock")
            alternative.bodies.push_back(&field(action, "body"));
          else if (action.typeName == "analysisStatus" &&
                   readAnalysisStatus(action).declaredState ==
                       ObligationState::NoAction &&
                   rule.resultType == "Unit")
            alternative.noActionUnit = true;
        }
      const bool empty =
          std::ranges::all_of(alternative.bodies, [](const Value *body) {
            return field(*body, "statements").elements.empty();
          });
      alternative.noActionUnit &= empty;
      if (empty && !alternative.noActionUnit && !alternative.bodies.empty() &&
          agas::model::astChainOperand(grammar, a) &&
          alternative.childFields.size() == 1 &&
          alternative.tokenFields.empty() &&
          alternative.optionalTokenFields.empty() &&
          alternative.optionalChildFields.empty() &&
          alternative.childListFields.empty())
        alternative.forwardedRule = alternative.childFields.front().second;
      rule.alternatives.push_back(std::move(alternative));
    }
    rules.push_back(std::move(rule));
  }
  return rules;
}
auto reviewSemanticFragments(const SemanticInput &input) -> SemanticReview {
  SemanticReview review;
  const auto &root = SemanticInputAccess::root(input);
  const auto &grammar = SemanticInputAccess::grammar(input);
  std::shared_ptr<SemanticPreparation> preparation;
  try {
    preparation = prepareSemanticContext(input, true);
  } catch (const std::runtime_error &error) {
    review.diagnostics.push_back({Severity::Error,
                                  "sema.invalid_contract",
                                  error.what(),
                                  ast::location(root),
                                  {}});
    // Declaration checking is independent of policy preparation.
    preparation = std::make_shared<SemanticPreparation>(input);
    try {
      preparation->contract = codegenContract(root, true);
      if (const auto *model = optional(field(root, "model")))
        collectSignatures(*model, preparation->signatures);
    } catch (const std::runtime_error &declarationError) {
      review.diagnostics.push_back({Severity::Error,
                                    "sema.invalid_contract",
                                    declarationError.what(),
                                    ast::location(root),
                                    {}});
    }
  }
  auto &contract = preparation->contract;
  // Preserve the established inference path for complete documents.
  bool fullyInferred = false;
  try {
    inferAnalyzers(root, grammar, preparation->signatures, contract,
                   preparation->enums);
    fullyInferred = true;
  } catch (const std::runtime_error &) {
  }
  auto rules = logicalAnalyses(root, grammar, contract);
  if (!fullyInferred && !contract.inherited.empty()) {
    try {
      AnalyzerInference{preparation->signatures, contract, preparation->enums}
          .run(rules, contract, &review.diagnostics);
    } catch (const std::runtime_error &error) {
      review.diagnostics.push_back({Severity::Error,
                                    "action.invalid_inference",
                                    error.what(),
                                    ast::location(root),
                                    {}});
    }
    rules = logicalAnalyses(root, grammar, contract);
  }
  for (const auto &[name, signature] : contract.analyzers) {
    FunctionContract interface;
    interface.result = parseTypeSpelling(signature.resultType);
    for (const auto &[inputName, type] : signature.inputs) {
      interface.parameters.push_back(parseTypeSpelling(type));
      review.inputNames[name].push_back(inputName);
    }
    review.interfaces.emplace(name, std::move(interface));
  }
  try {
    rules = checkedRuleAnalyses(root, grammar, contract);
  } catch (const std::runtime_error &) {
  }
  std::optional<Compiler> checker;
  try {
    checker.emplace(preparation->signatures, rules, contract.contextType,
                    preparation->enums, true);
  } catch (const std::runtime_error &error) {
    review.diagnostics.push_back({Severity::Error,
                                  "action.invalid_forwarding",
                                  error.what(),
                                  ast::location(root),
                                  {}});
  }
  for (const auto &[name, signature] : preparation->signatures) {
    if (signature.needsExternalBinding)
      review.canEmit = false;
    if (signature.intrinsic || signature.returnPolicy ||
        signature.assignmentPolicy || signature.conditionPolicy ||
        signature.statementIrPolicy || signature.flowActions ||
        signature.selectionPolicy || signature.selectionCandidates ||
        signature.selectionCompletion) {
      review.functions[name] = true;
      continue;
    }
    review.functions[name] = false;
    if (!signature.body || !checker)
      continue;
    try {
      checker->setFunction(signature);
      Environment environment;
      for (const auto &parameter : signature.parameters)
        environment.emplace(parameter.name, parameter.type);
      static_cast<void>(checker->checkBlock(
          *signature.body, std::move(environment), signature.result, true));
      review.functions[name] = true;
    } catch (const std::runtime_error &error) {
      review.diagnostics.push_back({Severity::Error, "action.invalid_function",
                                    error.what(),
                                    ast::location(*signature.body), name});
    }
  }
  for (const auto &rule : rules) {
    for (std::size_t ai = 0; ai < rule.alternatives.size(); ++ai) {
      const auto &a = rule.alternatives[ai];
      AlternativeReview alternative;
      alternative.rule = rule.name;
      alternative.ordinal = ai;
      alternative.resultType = rule.resultType;
      alternative.inputs = rule.inputs;
      alternative.forwarding = a.forwardedRule.has_value();
      if (a.forwardedRule) {
        alternative.dependencies.push_back(*a.forwardedRule);
        alternative.bodyChecked = checker.has_value() &&
                                  !rule.resultType.empty() &&
                                  contract.analyzers.contains(*a.forwardedRule);
        alternative.hasResult = alternative.bodyChecked;
        if (const auto target = contract.analyzers.find(*a.forwardedRule);
            target != contract.analyzers.end() && !rule.resultType.empty() &&
            (target->second.resultType != rule.resultType ||
             target->second.inputs != rule.inputs)) {
          alternative.bodyChecked = false;
          alternative.hasResult = false;
          alternative.diagnostics.push_back(
              {Severity::Error,
               "action.invalid_forwarding",
               "forwarded rule has incompatible result or inherited parameters",
               {},
               rule.name});
        }
      } else if (checker) {
        try {
          static_cast<void>(checker->checkAlternativeBody(
              rule, a, checker->checkedAlternativeEnvironment(rule, a),
              &alternative));
          alternative.bodyChecked = alternative.diagnostics.empty() &&
                                    alternative.dependencies.empty();
        } catch (const std::runtime_error &error) {
          alternative.diagnostics.push_back({Severity::Error,
                                             "action.invalid_alternative",
                                             error.what(),
                                             {},
                                             rule.name});
        }
      }
      review.alternatives.push_back(std::move(alternative));
    }
  }
  // Emitter limitations are separate from logical completeness.
  try {
    const auto emissionRules = checkedRuleAnalyses(root, grammar, contract);
    for (auto &a : review.alternatives)
      a.representation = true;
    // Invalid module assignments are errors, not unsupported AST shapes.
    try {
      static_cast<void>(
          parseModules(root, preparation->signatures, emissionRules));
    } catch (const std::runtime_error &error) {
      review.canEmit = false;
      auto location = ast::location(root);
      if (const auto *model = optional(field(root, "model")))
        for (const auto &declaration : field(*model, "declarations").elements)
          if (declaration.typeName == "modulesDeclaration") {
            location = ast::location(declaration);
            break;
          }
      review.diagnostics.push_back({Severity::Error, "sema.invalid_modules",
                                    error.what(), location, {}});
    }
  } catch (const std::runtime_error &) {
    review.canEmit = false;
  }
  return review;
}

auto prepareSemanticModel(const Value &inputRoot,
                          const agas::model::SyntaxDocument &inputGrammar)
    -> std::shared_ptr<const CheckedSemantics> {
  return prepareSemanticModel(
      SemanticInputAccess::fromAst(inputRoot, inputGrammar));
}

auto prepareSemanticModel(const SemanticInput &input)
    -> std::shared_ptr<const CheckedSemantics> {
  auto pending = incompleteAnalysisDiagnostics(
      SemanticInputAccess::root(input), SemanticInputAccess::grammar(input));
  if (!pending.empty())
    throw SemanticPreparationError(std::move(pending));
  if (hasModelBindingSchema(SemanticInputAccess::root(input)) &&
      !SemanticInputAccess::modelBindings(input))
    throw SemanticPreparationError(
        {{Severity::Error,
          "sema.missing_binding",
          "model_schema requires bound model contracts",
          {},
          {}}});
  auto prepared = prepareSemanticContext(input, false);
  const auto &root = SemanticInputAccess::root(input);
  const auto &sourceGrammar = SemanticInputAccess::grammar(input);
  auto &contract = prepared->contract;
  auto &signatures = prepared->signatures;
  inferAnalyzers(root, sourceGrammar, signatures, contract, prepared->enums);
  auto &analyses = prepared->analyses;
  analyses = checkedRuleAnalyses(root, sourceGrammar, contract);
  prepared->modules = parseModules(root, signatures, analyses);
  Compiler actionChecker{signatures, analyses, contract.contextType,
                         prepared->enums};
  std::vector<Diagnostic> actionErrors;
  for (const auto &[name, signature] : signatures) {
    if (signature.intrinsic)
      continue;
    if (!signature.returnPolicy && !signature.assignmentPolicy &&
        !signature.conditionPolicy && !signature.statementIrPolicy &&
        !signature.flowActions && !signature.selectionPolicy &&
        !signature.selectionCandidates && !signature.selectionCompletion &&
        signature.body == nullptr)
      throw std::runtime_error("missing body for " + name);
    if (!signature.body)
      continue;
    actionChecker.setFunction(signature);
    Environment environment;
    for (const auto &parameter : signature.parameters) {
      if (parameter.name == "ctx")
        throw std::runtime_error("reserved parameter name: ctx");
      environment.emplace(parameter.name, parameter.type);
    }
    try {
      prepared->functionPlans.emplace(
          name, actionChecker.checkBlock(*signature.body, std::move(environment),
                                         signature.result, true));
    } catch (const std::runtime_error &error) {
      const auto span = signature.body->sourceSpan;
      actionErrors.push_back(
          {Severity::Error, "action.invalid_function", error.what(),
           SourceLocation{0, span.beginByte, span.endByte}, name});
    }
  }
  for (const auto &rule : analyses) {
    auto &bodies = prepared->alternativeBodies[rule.name];
    bodies.resize(rule.alternatives.size());
    for (std::size_t index = 0; index < rule.alternatives.size(); ++index) {
      if (rule.alternatives[index].forwardedRule)
        continue;
      const auto &alternative = rule.alternatives[index];
      try {
        auto environment = actionChecker.checkedAlternativeEnvironment(
            rule, alternative);
        bodies[index] = actionChecker.checkAlternativeBody(
            rule, alternative, std::move(environment));
      } catch (const std::runtime_error &error) {
        const auto source = alternative.bodies.empty()
                                ? root.sourceSpan
                                : alternative.bodies.front()->sourceSpan;
        actionErrors.push_back(
            {Severity::Error, "action.invalid_alternative", error.what(),
             SourceLocation{0, source.beginByte, source.endByte},
             rule.name + "#" + std::to_string(index + 1)});
      }
    }
  }
  if (!actionErrors.empty())
    throw SemanticPreparationError(std::move(actionErrors));
  prepared->enumConstants = std::move(actionChecker.enumConstants);
  prepared->collectionOperations = std::move(actionChecker.collectionOperations);
  // Emission uses only checked plans and source independent rule metadata.
  for (auto &rule : analyses)
    for (auto &alternative : rule.alternatives) {
      for (const auto &[name, type] : rule.inputs) {
        static_cast<void>(type);
        if (std::ranges::any_of(alternative.bodies,
                                [&](const Value *body) {
                                  return referencesName(*body, name);
                                }))
          alternative.usedInputs.insert(name);
      }
      alternative.bodies.clear();
    }
  for (auto &[name, signature] : signatures) {
    static_cast<void>(name);
    signature.body = nullptr;
  }
  return std::make_shared<CheckedSemantics>(std::move(*prepared));
}

auto tryPrepareSemanticModel(const SemanticInput &input)
    -> Outcome<std::shared_ptr<const CheckedSemantics>> {
  Outcome<std::shared_ptr<const CheckedSemantics>> result;
  try {
    result.value = prepareSemanticModel(input);
  } catch (const SemanticPreparationError &error) {
    result.diagnostics = error.diagnostics();
  } catch (const std::runtime_error &error) {
    result.diagnostics.push_back(
        {Severity::Error, "sema.invalid_contract", error.what(), std::nullopt,
         {}});
  }
  return result;
}

auto semanticInput(const CheckedSemantics &model) -> const SemanticInput & {
  return model.input;
}

auto semanticContextType(const CheckedSemantics &model) -> std::string_view {
  return model.contract.contextType;
}

auto emitCheckedSemanticsRust(const CheckedSemantics &model)
    -> SemanticRustFiles {
  const auto pending =
      incompleteAnalysisDiagnostics(SemanticInputAccess::root(model.input),
                                    SemanticInputAccess::grammar(model.input));
  if (!pending.empty())
    throw SemanticPreparationError(pending);
  for (const auto &[name, signature] : model.signatures)
    if (signature.needsExternalBinding)
      throw std::runtime_error("document.unsupported_construct: external "
                               "function needs a Rust binding: " +
                               name);

  const auto *prepared = &model;
  const auto &contract = prepared->contract;
  const auto &signatures = prepared->signatures;
  const auto &returnPolicies = prepared->returnPolicies;
  const auto &modelBindings = prepared->modelBindings;
  const auto &policyByName = prepared->policyByName;
  const auto &assignmentPolicies = prepared->assignmentPolicies;
  const auto &assignmentBindings = prepared->assignmentBindings;
  const auto &assignmentByFunction = prepared->assignmentByFunction;
  const auto &assignmentBindingByName = prepared->assignmentBindingByName;
  const auto &conditionPolicies = prepared->conditionPolicies;
  const auto &conditionBindings = prepared->conditionBindings;
  const auto &conditionByName = prepared->conditionByName;
  const auto &conditionBindingByName = prepared->conditionBindingByName;
  const auto &statementIr = prepared->statementIr;
  const auto &statementByFunction = prepared->statementByFunction;
  const auto &flowActions = prepared->flowActions;
  const auto &flowByFunction = prepared->flowByFunction;
  const auto &selectionPolicy = prepared->selectionPolicy;
  const auto &bindingByName = prepared->bindingByName;
  const auto &analyses = prepared->analyses;
  const auto &modules = prepared->modules;
  std::map<std::string, std::ostringstream> moduleLibraryBodies;
  std::map<std::string, std::ostringstream> moduleRuleBodies;
  for (const auto &name : modules.names) {
    moduleLibraryBodies.try_emplace(name);
    moduleRuleBodies.try_emplace(name);
  }
  Compiler compiler{signatures, analyses, contract.contextType};
  std::set<std::string> libraryTypes;
  const bool hasLibraryFunctions = std::ranges::any_of(
      signatures, [](const auto &entry) { return !entry.second.intrinsic; });
  if (hasLibraryFunctions)
    libraryTypes.insert(contract.contextType);
  if (hasLibraryFunctions)
    for (const auto &[name, signature] : signatures) {
      static_cast<void>(name);
      if (signature.intrinsic)
        continue;
      collectRustNamedTypes(signature.result, libraryTypes);
      for (const auto &parameter : signature.parameters)
        collectRustNamedTypes(parameter.type, libraryTypes);
    }
  std::ostringstream libraryRoot;
  libraryRoot << "// Generated by sema from Action function definitions.\n"
              << rustImports(libraryTypes) << '\n';
  for (const auto &name : modules.names) {
    std::vector<std::string> functions;
    for (const auto &[function, owner] : modules.functions)
      if (owner == name)
        functions.push_back(function);
    if (functions.empty())
      continue;
    libraryRoot << "pub use crate::sema_gen::" << name << "::{";
    for (std::size_t index = 0; index < functions.size(); ++index)
      libraryRoot << (index ? ", " : "") << functions[index];
    libraryRoot << "};\n";
  }
  for (const auto &[name, signature] : signatures) {
    if (signature.intrinsic)
      continue;
    std::ostringstream &library = modules.functions.contains(name)
                                    ? moduleLibraryBodies.at(
                                          modules.functions.at(name))
                                    : libraryRoot;
    compiler.setFunction(signature);
    std::vector<std::string> rustParameters{
        "ctx: &" + std::string{signature.mutates ? "mut " : ""} +
        contract.contextType};
    Environment environment;
    for (const auto &parameter : signature.parameters) {
      if (parameter.name == "ctx")
        throw std::runtime_error("reserved parameter name: ctx");
      environment.emplace(parameter.name, parameter.type);
      rustParameters.push_back(parameter.name + ": " +
                               rustParameterType(parameter.type));
    }
    std::string header = "pub fn " + name + "(";
    for (std::size_t index = 0; index < rustParameters.size(); ++index) {
      if (index)
        header += ", ";
      header += rustParameters[index];
    }
    header += signature.result == "Unit"
                  ? ") {"
                  : ") -> " + rustType(signature.result) + " {";
    if (header.size() <= 100) {
      library << header << '\n';
    } else {
      library << "pub fn " << name << "(\n";
      for (const auto &parameter : rustParameters)
        library << "    " << parameter << ",\n";
      if (signature.result == "Unit")
        library << ") {\n";
      else
        library << ") -> " << rustType(signature.result) << " {\n";
    }
    if (signature.returnPolicy)
      library << emitReturnPolicyBody(*policyByName.at(name));
    else if (signature.assignmentPolicy)
      library << emitAssignmentPolicyBody(*assignmentByFunction.at(name), name);
    else if (signature.conditionPolicy)
      library << emitConditionPolicyBody(*conditionByName.at(name));
    else if (signature.statementIrPolicy)
      library << emitStatementIrBody(*statementIr,
                                     statementByFunction.at(name));
    else if (signature.flowActions)
      library << emitFlowActionBody(*flowActions, flowByFunction.at(name));
    else if (signature.selectionPolicy)
      library << emitSelectionPolicyBody(*selectionPolicy);
    else if (signature.selectionCandidates)
      library << emitSelectionCandidatesBody(*selectionPolicy,
                                             contract.contextType);
    else if (signature.selectionCompletion)
      library << emitSelectionCompletionBody(*selectionPolicy);
    else
      library << compiler.renderBlock(prepared->functionPlans.at(name), 1,
                                      true);
    library << "}\n\n";
  }
  for (const auto &policy : returnPolicies)
    libraryRoot << emitReturnModelRust(*bindingByName.at(policy.name),
                                       contract.contextType);
  for (const auto &policy : assignmentPolicies)
    libraryRoot << emitAssignmentModelRust(
        policy, *assignmentBindingByName.at(policy.name), contract.contextType);
  for (const auto &policy : conditionPolicies)
    libraryRoot << emitConditionModelRust(
        *conditionBindingByName.at(policy.name), contract.contextType);
  auto semaLib = libraryRoot.str();
  if (semaLib.ends_with("\n\n"))
    semaLib.pop_back();
  std::ostringstream sema;
  const bool hasFields =
      std::ranges::any_of(analyses, [](const RuleAnalyses &rule) {
        return std::ranges::any_of(
            rule.alternatives, [](const RuleAlternative &alternative) {
              return !alternative.tokenFields.empty() ||
                     !alternative.childFields.empty() ||
                     !alternative.childListFields.empty();
            });
      });
  sema << "// Generated by sema from Action analysis blocks.\n"
          "#![allow(dead_code, unused_variables, clippy::let_unit_value)]\n";
  std::set<std::string> analyzerTypes{contract.contextType};
  for (const auto &rule : analyses) {
    collectRustNamedTypes(rule.resultType, analyzerTypes);
    for (const auto &[name, type] : rule.inputs) {
      static_cast<void>(name);
      collectRustNamedTypes(type, analyzerTypes);
    }
  }
  std::vector<std::string> imports{
      hasFields ? "use crate::ast::{AstValue, AstValueKind};\n"
                : "use crate::ast::AstValue;\n",
      rustImports(analyzerTypes)};
  std::ranges::sort(imports);
  for (const auto &entry : imports)
    sema << entry;
  for (const auto &name : modules.names) {
    sema << "#[path = \"sema_" << name << "_gen.rs\"]\n"
         << "pub(crate) mod " << name << ";\n";
    std::vector<std::string> rules;
    for (const auto &[rule, owner] : modules.rules)
      if (owner == name)
        rules.push_back("analyze_" + rustRuleName(rule));
    if (!rules.empty()) {
      sema << "pub use " << name << "::{";
      for (std::size_t index = 0; index < rules.size(); ++index)
        sema << (index ? ", " : "") << rules[index];
      sema << "};\n";
    }
  }
  for (const auto &rule : analyses) {
    if (rule.astKind == RuleAstKind::Record)
      sema << "\nfn is_" << rustRuleName(rule.name)
           << "_ast(value: &AstValue) -> bool {\n"
              "    value.kind == AstValueKind::Record && value.type_name == \""
           << rule.name << "\"\n}\n";
    else if (rule.astKind == RuleAstKind::Token) {
      sema << "\nfn is_" << rustRuleName(rule.name)
           << "_ast(value: &AstValue) -> bool {\n"
              "    value.kind == AstValueKind::Token && "
              "matches!(value.token_kind, ";
      bool consecutive = rule.alternatives.size() >= 3;
      for (std::size_t index = 1; index < rule.alternatives.size(); ++index)
        consecutive &= *rule.alternatives[index].tokenDispatchKind ==
                       *rule.alternatives[index - 1].tokenDispatchKind + 1;
      if (consecutive) {
        sema << *rule.alternatives.front().tokenDispatchKind
             << "..=" << *rule.alternatives.back().tokenDispatchKind;
      } else {
        for (std::size_t index = 0; index < rule.alternatives.size(); ++index) {
          if (index)
            sema << " | ";
          sema << *rule.alternatives[index].tokenDispatchKind;
        }
      }
      sema << ")\n}\n";
    } else if (rule.astKind == RuleAstKind::Node) {
      const auto &accepted = compiler.acceptedNodeTypes(rule.name);
      sema << "\nfn is_" << rustRuleName(rule.name)
           << "_ast(value: &AstValue) -> bool {\n";
      for (const auto &alternative : rule.alternatives) {
        const auto &target = alternative.forwardedRule
                                 ? alternative.forwardedRule
                                 : alternative.actionDispatchType;
        if (target && (compiler.astKind(*target) != RuleAstKind::Node ||
                       rule.inlineRule))
          sema << "    if value.kind != AstValueKind::Node && is_"
               << rustRuleName(*target)
               << "_ast(value) { return true; }\n";
        if (alternative.collapsedChild)
          sema << "    if value.kind != AstValueKind::Node && is_"
               << rustRuleName(alternative.collapsedChild->second)
               << "_ast(value) { return true; }\n";
      }
      sema << "    value.kind == AstValueKind::Node && "
              "matches!(value.type_name.as_str(), ";
      bool first = true;
      for (const auto &name : accepted) {
        if (!first)
          sema << " | ";
        first = false;
        sema << "\"" << name << "\"";
      }
      sema << ")\n}\n";
    } else {
      sema << "\nfn is_" << rustRuleName(rule.name)
           << "_ast(value: &AstValue) -> bool {\n";
      for (const auto &alternative : rule.alternatives) {
        if (alternative.tokenDispatchKind)
          sema << "    if value.kind == AstValueKind::Token && "
                  "value.token_kind == "
               << *alternative.tokenDispatchKind << " { return true; }\n";
        else if (alternative.inlineRecord)
          sema << "    if value.kind == AstValueKind::Record && "
                  "value.type_name == \""
               << rule.name << "\" { return true; }\n";
        else if (const auto &target = alternative.forwardedRule
                                          ? alternative.forwardedRule
                                          : alternative.actionDispatchType)
          sema << "    if is_" << rustRuleName(*target)
               << "_ast(value) { return true; }\n";
        else
          sema << "    if value.kind == AstValueKind::Node && "
                  "value.type_name == \""
               << rule.name << "\" { return true; }\n";
      }
      sema << "    false\n}\n";
    }
  }
  for (const auto &rule : analyses) {
    const auto &accepted = compiler.acceptedNodeTypes(rule.name);
    if (accepted.size() == 1)
      continue;
    std::string variants = "\"" + rule.name + "\"";
    for (const auto &name : accepted)
      if (name != rule.name)
        variants += " | \"" + name + "\"";
    sema << "\nfn is_" << rustRuleName(rule.name)
         << "_node(value: &AstValue) -> bool {\n";
    const auto condition = "    value.kind == AstValueKind::Node && "
                           "matches!(value.type_name.as_str(), " +
                           variants + ")\n";
    if (condition.size() <= 100) {
      sema << condition;
    } else if (variants.size() <= 40) {
      sema << "    value.kind == AstValueKind::Node\n"
              "        && matches!(value.type_name.as_str(), "
           << variants << ")\n";
    } else {
      sema << "    value.kind == AstValueKind::Node\n"
              "        && matches!(\n"
              "            value.type_name.as_str(),\n"
              "            "
           << variants
           << "\n"
              "        )\n";
    }
    sema << "}\n";
  }
  auto &semaRoot = sema;
  for (const auto &rule : analyses) {
    std::ostringstream &sema = modules.rules.contains(rule.name)
                                   ? moduleRuleBodies.at(modules.rules.at(rule.name))
                                   : semaRoot;
    std::string callArguments = "ctx";
    for (const auto &[name, type] : rule.inputs) {
      static_cast<void>(type);
      callArguments += ", " + name;
    }
    callArguments += ", node";
    sema << "\npub fn analyze_" << rustRuleName(rule.name)
         << "(\n"
            "    ctx: &mut "
         << contract.contextType << ",\n";
    for (const auto &[name, type] : rule.inputs)
      sema << "    " << name << ": " << rustType(type) << ",\n";
    sema << "    node: &AstValue,\n"
            ") -> Result<"
         << rustType(rule.resultType) << ", &'static str> {\n";
    if (rule.astKind == RuleAstKind::Mixed) {
      for (std::size_t index = 0; index < rule.alternatives.size(); ++index) {
        const auto &alternative = rule.alternatives[index];
        std::string condition;
        if (alternative.tokenDispatchKind)
          condition =
              "node.kind == AstValueKind::Token && node.token_kind == " +
              std::to_string(*alternative.tokenDispatchKind);
        else if (alternative.inlineRecord)
          condition =
              "node.kind == AstValueKind::Record && node.type_name == \"" +
              rule.name + "\"";
        else if (const auto &target = alternative.forwardedRule
                                          ? alternative.forwardedRule
                                          : alternative.actionDispatchType)
          condition = "is_" + rustRuleName(*target) + "_ast(node)";
        else
          condition =
              "node.kind == AstValueKind::Node && node.type_name == \"" +
              rule.name + "\" && node.variant_name == \"" + alternative.label +
              "\"";
        sema << "    if " << condition << " {\n"
             << "        return ";
        if (alternative.forwardedRule)
          sema << "analyze_" << rustRuleName(*alternative.forwardedRule);
        else
          sema << "analyze_" << rustRuleName(rule.name) << "_alt_" << index;
        sema << "(" << callArguments
             << ");\n"
                "    }\n";
      }
      sema << "    Err(\"unexpected AST variant\")\n}\n";
      for (std::size_t index = 0; index < rule.alternatives.size(); ++index)
        if (!rule.alternatives[index].forwardedRule)
          sema << '\n'
               << compiler.ruleAnalysis(rule, rule.alternatives[index], index,
                                        prepared->alternativeBodies.at(rule.name).at(index));
      continue;
    }
    if (rule.astKind == RuleAstKind::Token) {
      sema << "    if node.kind != AstValueKind::Token {\n"
              "        return Err(\"unexpected AST token\");\n"
              "    }\n"
              "    match node.token_kind {\n";
      for (std::size_t index = 0; index < rule.alternatives.size(); ++index)
        sema << "        " << *rule.alternatives[index].tokenDispatchKind
             << " => analyze_" << rustRuleName(rule.name) << "_alt_" << index
             << "(" << callArguments << "),\n";
      sema << "        _ => Err(\"unexpected AST token kind\"),\n"
              "    }\n}\n";
      for (std::size_t index = 0; index < rule.alternatives.size(); ++index)
        sema << '\n'
             << compiler.ruleAnalysis(rule, rule.alternatives[index], index,
                                        prepared->alternativeBodies.at(rule.name).at(index));
      continue;
    }
    if (rule.astKind == RuleAstKind::Record) {
      sema << "    if node.kind != AstValueKind::Record || node.type_name != \""
           << rule.name
           << "\" {\n"
              "        return Err(\"unexpected AST record\");\n"
              "    }\n"
              "    analyze_"
           << rustRuleName(rule.name) << "_alt_0(" << callArguments << ")\n}\n"
           << '\n'
           << compiler.ruleAnalysis(rule, rule.alternatives.front(), 0,
                                    prepared->alternativeBodies.at(rule.name).at(0));
      continue;
    }
    const auto forwardedCondition = [&](std::string_view target) {
      std::vector<std::string> names;
      std::set<std::string> competingTargets;
      for (const auto &alternative : rule.alternatives) {
        const auto &other = alternative.forwardedRule
                                ? alternative.forwardedRule
                                : alternative.actionDispatchType;
        if (other && *other != target)
          competingTargets.insert(*other);
      }
      for (const auto &name : compiler.acceptedNodeTypes(target))
        if (name != rule.name && !competingTargets.contains(name))
          names.push_back(name);
      if (names.empty())
        throw std::runtime_error("forwarded rule has no dispatchable AST type");
      if (names.size() == 1)
        return "node.type_name == \"" + names.front() + "\"";
      std::string condition = "matches!(node.type_name.as_str(), ";
      for (std::size_t index = 0; index < names.size(); ++index) {
        if (index)
          condition += " | ";
        condition += "\"" + names[index] + "\"";
      }
      return condition + ")";
    };
    const auto forwardedAstCondition = [&](std::string_view target) {
      const auto helper = "is_" + rustRuleName(target) + "_ast(node)";
      if (compiler.astKind(target) != RuleAstKind::Node)
        return helper;
      const auto nodes = forwardedCondition(target);
      if (!rule.inlineRule)
        return nodes;
      return nodes + " || (node.kind != AstValueKind::Node && " +
             helper + ")";
    };
    for (const auto &alternative : rule.alternatives)
      if (alternative.forwardedRule)
        sema << "    if " << forwardedAstCondition(*alternative.forwardedRule)
             << " {\n"
                "        return analyze_"
             << rustRuleName(*alternative.forwardedRule) << "(" << callArguments
             << ");\n"
                "    }\n";
    for (std::size_t index = 0; index < rule.alternatives.size(); ++index)
      if (const auto &type = rule.alternatives[index].actionDispatchType)
        sema << "    if " << forwardedAstCondition(*type)
             << " {\n"
                "        return analyze_"
             << rustRuleName(rule.name) << "_alt_" << index << "("
             << callArguments
             << ");\n"
                "    }\n";
    for (std::size_t index = 0; index < rule.alternatives.size(); ++index) {
      const auto &alternative = rule.alternatives[index];
      if (!alternative.collapsedChild)
        continue;
      const auto &target = alternative.collapsedChild->second;
      sema << "    if node.type_name != \"" << rule.name << "\" && is_"
           << rustRuleName(target) << "_ast(node) {\n"
           << "        let normalized = AstValue {\n"
              "            kind: AstValueKind::Node,\n"
              "            source_span: node.source_span,\n"
              "            recognized_span: node.recognized_span,\n"
           << "            type_name: \"" << rule.name
           << "\".to_owned(),\n"
              "            variant_name: String::new(),\n"
              "            token_kind: 0,\n"
              "            token_text: String::new(),\n"
              "            field_names: vec![\""
           << alternative.collapsedChild->first << "\".to_owned()";
      for (const auto &[name, type] : alternative.childListFields) {
        static_cast<void>(type);
        sema << ", \"" << name << "\".to_owned()";
      }
      for (const auto &[name, type] : alternative.optionalChildFields) {
        static_cast<void>(type);
        sema << ", \"" << name << "\".to_owned()";
      }
      sema << "],\n"
              "            elements: vec![node.clone()";
      const auto emitAbsent = [&](std::string_view kind) {
        sema << ", AstValue { kind: AstValueKind::" << kind
             << ", source_span: node.source_span, recognized_span: "
                "node.recognized_span, type_name: String::new(), "
                "variant_name: String::new(), token_kind: 0, "
                "token_text: String::new(), field_names: Vec::new(), "
                "elements: Vec::new() }";
      };
      for (std::size_t field = 0; field < alternative.childListFields.size();
           ++field)
        emitAbsent("List");
      for (std::size_t field = 0;
           field < alternative.optionalChildFields.size(); ++field)
        emitAbsent("Optional");
      sema << "],\n"
              "        };\n"
              "        return analyze_"
           << rustRuleName(rule.name) << "_alt_" << index << "(ctx";
      for (const auto &[name, type] : rule.inputs) {
        static_cast<void>(type);
        sema << ", " << name;
      }
      sema << ", &normalized);\n"
              "    }\n";
    }
    sema << "    if node.type_name != \"" << rule.name
         << "\" {\n"
            "        return Err(\"unexpected AST node\");\n"
            "    }\n";
    const bool hasDirectAlternative = std::ranges::any_of(
        rule.alternatives, [](const RuleAlternative &alternative) {
          return !alternative.forwardedRule && !alternative.actionDispatchType;
        });
    if (hasDirectAlternative) {
      sema << "    match node.variant_name.as_str() {\n";
      for (std::size_t index = 0; index < rule.alternatives.size(); ++index)
        if (!rule.alternatives[index].forwardedRule &&
            !rule.alternatives[index].actionDispatchType)
          sema << "        \"" << rule.alternatives[index].label
               << "\" => analyze_" << rustRuleName(rule.name) << "_alt_"
               << index << "(" << callArguments << "),\n";
      sema << "        _ => Err(\"unexpected AST variant\"),\n"
              "    }\n";
    } else {
      sema << "    Err(\"unexpected AST variant\")\n";
    }
    sema << "}\n";
    for (std::size_t index = 0; index < rule.alternatives.size(); ++index)
      if (!rule.alternatives[index].forwardedRule)
        sema << '\n'
             << compiler.ruleAnalysis(rule, rule.alternatives[index], index,
                                        prepared->alternativeBodies.at(rule.name).at(index));
  }
  std::map<std::string, std::string> generatedModules;
  for (const auto &name : modules.names) {
    std::ostringstream output;
    output << "// Generated by sema from module " << name << ".\n"
              "#![allow(dead_code, unused_variables, clippy::let_unit_value)]\n"
              "use super::*;\n";
    if (!moduleLibraryBodies.at(name).str().empty()) {
      std::set<std::string> moduleTypes{contract.contextType};
      for (const auto &[function, owner] : modules.functions) {
        if (owner != name)
          continue;
        const auto &signature = signatures.at(function);
        collectRustNamedTypes(signature.result, moduleTypes);
        for (const auto &parameter : signature.parameters)
          collectRustNamedTypes(parameter.type, moduleTypes);
      }
      output << rustImports(moduleTypes);
    }
    output << '\n' << moduleLibraryBodies.at(name).str()
           << moduleRuleBodies.at(name).str();
    generatedModules.emplace("sema_" + name + "_gen.rs", output.str());
  }
  return {sema.str(), std::move(semaLib), std::move(generatedModules)};
}

} // namespace agsem
