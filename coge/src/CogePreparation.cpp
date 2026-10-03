#include "AstAccess.h"
#include "CogeBinding.h"
#include "CogeCompletenessInternal.h"
#include "GenerationAccess.h"
#include "coge/Completeness.h"
#include "CogeDocumentStorage.h"
#include "CompletenessInternal.h"
#include "ExecutionModelInternal.h"
#include "SemanticInputStorage.h"
#include "coge/ExecutionExpression.h"
#include "coge/ExecutionType.h"
#include "coge/GenerationPreparation.h"
#include "coge/PropertyExpression.h"

#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace coge {
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

auto referencesName(const Value &value, std::string_view name) -> bool {
  if (value.kind == agas::runtime::AstValueKind::Token &&
      value.tokenText == name)
    return true;
  for (const auto &element : value.elements)
    if (referencesName(element, name))
      return true;
  return false;
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

auto executionMutatingMethod(std::string_view name) -> bool {
  return name == "push" || name == "pop" || name == "push_bindings" ||
         name == "insert_current" || name == "insert_global" ||
         name == "write_path";
}

auto executionMutatesBinding(const Value &value, std::string_view name)
    -> bool {
  if (value.typeName == "actionUnary" &&
      field(value, "atom").kind == agas::runtime::AstValueKind::Token &&
      token(field(value, "atom")) == name) {
    const auto &parts = field(value, "suffixes").elements;
    for (std::size_t index = 1; index < parts.size(); ++index)
      if (parts[index].variantName == "Call" &&
          parts[index - 1].variantName == "Field" &&
          executionMutatingMethod(token(field(parts[index - 1], "name"))))
        return true;
  }
  for (const auto &element : value.elements)
    if (executionMutatesBinding(element, name))
      return true;
  return false;
}

auto isDirectExecutionCall(const Value &value) -> bool;

auto parseExecutionExpression(const Value &value,
                              const std::set<std::string> &names,
                              bool propagate) -> coge::ExecutionExpression;

auto executionArguments(const Value *arguments,
                        const std::set<std::string> &names)
    -> std::vector<coge::ExecutionExpression> {
  std::vector<coge::ExecutionExpression> result;
  if (!arguments)
    return result;
  const auto append = [&](const Value &argument) {
    result.push_back(parseExecutionExpression(argument, names, true));
  };
  if (arguments->typeName == "argumentList") {
    append(field(*arguments, "first"));
    for (const auto &item : field(*arguments, "rest").elements)
      append(item);
  } else {
    append(*arguments);
  }
  return result;
}

auto parseExecutionExpression(const Value &value,
                              const std::set<std::string> &names,
                              bool propagate) -> coge::ExecutionExpression {
  using Kind = coge::ExecutionExprKind;
  using Expr = coge::ExecutionExpression;
  if (value.kind == agas::runtime::AstValueKind::Token) {
    const auto name = token(value);
    if (names.contains("@state:" + name))
      return {Kind::State, name};
    if (name == "none")
      return {Kind::None, {}};
    if (name == "unit")
      return {Kind::Unit, {}};
    if (name == "true" || name == "false")
      return {Kind::Boolean, name};
    if (!name.empty() && name.front() == '"')
      return {Kind::Text, quotedText(value)};
    if (!name.empty() && name.front() >= '0' && name.front() <= '9' &&
        std::ranges::all_of(name, [](char character) {
          return (character >= '0' && character <= '9') || character == '.';
        }))
      return {Kind::Number, name};
    checkRustIdentifier(name);
    if (!names.contains(name))
      throw std::runtime_error("unbound execution value: " + name);
    return {Kind::Binding, name};
  }
  if (value.typeName == "actionExpression") {
    if (optional(field(value, "choice")))
      throw std::runtime_error("execution ternary is not supported");
    if (field(value, "rest").elements.size() > 1)
      throw std::runtime_error(
          "execution expression needs parentheses for multiple operators");
    auto first =
        parseExecutionExpression(field(value, "first"), names, propagate);
    for (const auto &part : field(value, "rest").elements) {
      const auto op = token(field(part, "operator"));
      if (op != "==" && op != "!=" && op != "<" && op != "<=" && op != ">" &&
          op != ">=" && op != "+" && op != "-" && op != "*" && op != "/")
        throw std::runtime_error("unsupported execution binary operator: " +
                                 op);
      auto right =
          parseExecutionExpression(field(part, "value"), names, propagate);
      first = {
          Kind::Binary, op, {}, false, {std::move(first), std::move(right)}};
    }
    return first;
  }
  if (value.typeName != "actionUnary")
    throw std::runtime_error("unsupported execution expression");
  const auto &prefixes = field(value, "prefixes").elements;
  const auto &suffixes = field(value, "suffixes").elements;
  if (!prefixes.empty()) {
    if (prefixes.size() != 1 || token(prefixes.front()) != "-" ||
        !suffixes.empty())
      throw std::runtime_error("unsupported execution prefix operator");
    return {Kind::Negate,
            {},
            {},
            false,
            {parseExecutionExpression(field(value, "atom"), names, propagate)}};
  }
  if (suffixes.empty())
    return parseExecutionExpression(field(value, "atom"), names, propagate);
  const auto base = token(field(value, "atom"));
  if (names.contains(base) || names.contains("@state:" + base)) {
    auto result = parseExecutionExpression(field(value, "atom"), names, false);
    std::string method;
    for (const auto &suffix : suffixes) {
      if (suffix.variantName == "Field") {
        method = token(field(suffix, "name"));
        checkRustIdentifier(method);
        result = {Kind::MemberField, method, {}, false, {std::move(result)}};
      } else if (suffix.variantName == "Call" && !method.empty()) {
        if (names.contains("@state:" + base) &&
            executionMutatingMethod(method) && !names.contains("@mutates"))
          throw std::runtime_error("runtime mutation needs mutates: " + method);
        auto children =
            executionArguments(optional(field(suffix, "arguments")), names);
        children.insert(children.begin(), std::move(result));
        result = {Kind::MemberCall, {}, {}, false, std::move(children)};
        method.clear();
      } else {
        throw std::runtime_error("unsupported execution member access");
      }
    }
    return result;
  }
  if ((suffixes.size() == 1 || suffixes.size() == 2) &&
      suffixes.front().variantName == "Field") {
    const auto type = token(field(value, "atom"));
    const auto variant = token(field(suffixes.front(), "name"));
    if (type == "AgSemRuntime") {
      if (suffixes.size() != 2 || suffixes.back().variantName != "Call")
        throw std::runtime_error("AgSemRuntime needs a function call");
      checkRustIdentifier(variant);
      return {Kind::RuntimeCall,
              variant,
              {},
              false,
              executionArguments(optional(field(suffixes.back(), "arguments")),
                                 names)};
    }
    checkRustTypeIdentifier(type);
    checkRustTypeIdentifier(variant);
    if (suffixes.size() == 1)
      return {Kind::Variant, type, variant};
    if (suffixes.back().variantName != "Call")
      throw std::runtime_error("unsupported execution constructor suffix");
    return {Kind::Constructor, type, variant, false,
            executionArguments(optional(field(suffixes.back(), "arguments")),
                               names)};
  }
  if (suffixes.size() != 1 || suffixes.front().variantName != "Call")
    throw std::runtime_error("execution expression needs a direct call");
  const auto name = token(field(value, "atom"));
  if (name == "capture") {
    const auto *argument = optional(field(suffixes.front(), "arguments"));
    if (!argument || !isDirectExecutionCall(*argument))
      throw std::runtime_error("capture needs one fallible action call");
    auto child = parseExecutionExpression(*argument, names, true);
    if (child.kind != Kind::DirectCall || !child.propagate)
      throw std::runtime_error("capture needs a fallible action call");
    child.propagate = false;
    return {Kind::Capture, {}, {}, false, {std::move(child)}};
  }
  const bool optionConstructor = name == "Some";
  if (optionConstructor)
    checkRustTypeIdentifier(name);
  else
    checkRustIdentifier(name);
  return {
      Kind::DirectCall,
      name,
      {},
      propagate && !optionConstructor,
      executionArguments(optional(field(suffixes.front(), "arguments")), names),
      optionConstructor};
}

auto executionFunctionPattern(const Value &value, const Value &body,
                              std::set<std::string> &names)
    -> ExecutionPattern {
  const Value *pattern = &value;
  if (pattern->typeName == "actionExpression") {
    if (!field(*pattern, "rest").elements.empty() ||
        optional(field(*pattern, "choice")))
      throw std::runtime_error("execution pattern cannot use operators");
    pattern = &field(*pattern, "first");
  }
  const bool atom = pattern->kind == agas::runtime::AstValueKind::Token;
  const auto name = atom ? token(*pattern) : token(field(*pattern, "atom"));
  ExecutionPattern result;
  result.variant = name;
  const bool bare = atom || field(*pattern, "suffixes").elements.empty();
  if (name == "otherwise" && bare) {
    result.form = ExecutionPatternForm::Wildcard;
    return result;
  }
  if ((name == "none" || name == "None") && bare) {
    result.variant = "None";
    return result;
  }
  if (name == "Continue" && bare) {
    result.owner = "Control";
    return result;
  }
  if (atom || pattern->typeName != "actionUnary" ||
      !field(*pattern, "prefixes").elements.empty())
    throw std::runtime_error("unsupported execution pattern: " + name);
  const auto &suffixes = field(*pattern, "suffixes").elements;
  const Value *arguments = nullptr;
  if (!suffixes.empty() && suffixes.front().variantName == "Field") {
    result.owner = name;
    result.variant = token(field(suffixes.front(), "name"));
    checkRustTypeIdentifier(result.owner);
    checkRustTypeIdentifier(result.variant);
    if (suffixes.size() == 1)
      return result;
    if (suffixes.size() != 2 || suffixes.back().variantName != "Call")
      throw std::runtime_error("unsupported execution pattern");
    arguments = optional(field(suffixes.back(), "arguments"));
  } else {
    if (suffixes.size() != 1 || suffixes.front().variantName != "Call" ||
        (name != "Some" && name != "Return" && name != "Ok" && name != "Err"))
      throw std::runtime_error("unsupported execution pattern: " + name);
    if (name == "Return")
      result.owner = "Control";
    arguments = optional(field(suffixes.front(), "arguments"));
  }
  if (!arguments)
    throw std::runtime_error("execution pattern needs bindings");
  result.form = ExecutionPatternForm::Tuple;
  const auto append = [&](const Value &argument) {
    if (argument.kind != agas::runtime::AstValueKind::Token)
      throw std::runtime_error("execution pattern binding must be a name");
    const auto binding = token(argument);
    checkRustIdentifier(binding);
    if (!names.insert(binding).second)
      throw std::runtime_error("duplicate execution pattern binding: " +
                               binding);
    result.bindings.push_back({{}, binding, referencesName(body, binding)});
  };
  if (arguments->typeName == "argumentList") {
    append(field(*arguments, "first"));
    for (const auto &argument : field(*arguments, "rest").elements)
      append(argument);
  } else
    append(*arguments);
  return result;
}

auto isDirectExecutionCall(const Value &value) -> bool {
  const Value *expression = &value;
  if (expression->typeName == "actionExpression") {
    if (!field(*expression, "rest").elements.empty() ||
        optional(field(*expression, "choice")))
      return false;
    expression = &field(*expression, "first");
  }
  if (expression->typeName != "actionUnary" ||
      !field(*expression, "prefixes").elements.empty())
    return false;
  const auto &suffixes = field(*expression, "suffixes").elements;
  return suffixes.size() == 1 && suffixes.front().variantName == "Call" &&
         token(field(*expression, "atom")) != "Some" &&
         token(field(*expression, "atom")) != "capture";
}

auto checkedExecutionStatement(const Value &statement,
                               std::set<std::string> &names, bool function)
    -> coge::ExecutionStatement;

auto checkedExecutionStatements(const Value &body, std::set<std::string> names,
                                bool function)
    -> std::vector<coge::ExecutionStatement> {
  if (function)
    for (const auto &statement : field(body, "statements").elements)
      if (statement.typeName == "letStatement") {
        const auto name = token(field(statement, "name"));
        if (executionMutatesBinding(body, name))
          names.insert("@mutable:" + name);
      }
  std::vector<coge::ExecutionStatement> result;
  for (const auto &statement : field(body, "statements").elements)
    result.push_back(checkedExecutionStatement(statement, names, function));
  return result;
}

auto checkedExecutionStatement(const Value &statement,
                               std::set<std::string> &names, bool function)
    -> coge::ExecutionStatement {
  using Kind = coge::ExecutionStatementKind;
  if (statement.typeName == "letStatement") {
    if (optional(field(statement, "context")) ||
        optional(field(statement, "failure")))
      throw std::runtime_error(function
                                   ? "unsupported execution function let clause"
                                   : "unsupported execution let clause");
    const auto name = token(field(statement, "name"));
    checkRustIdentifier(name);
    if (names.contains(name))
      throw std::runtime_error(
          std::string{function ? "duplicate execution function binding: "
                               : "duplicate execution binding: "} +
          name);
    coge::ExecutionStatement result{Kind::Let};
    result.binding = name;
    result.mutableBinding = names.contains("@mutable:" + name);
    result.value =
        parseExecutionExpression(field(statement, "value"), names, true);
    names.insert(name);
    return result;
  }
  if (statement.typeName == "returnStatement") {
    coge::ExecutionStatement result{Kind::Return};
    const auto &value = field(statement, "value");
    result.value = parseExecutionExpression(value, names, false);
    result.wrapReturn = function && !isDirectExecutionCall(value);
    return result;
  }
  if (function && statement.typeName == "errorStatement") {
    if (token(field(statement, "kind")) != "runtime_error")
      throw std::runtime_error("execution function needs runtime_error");
    const auto *location = optional(field(statement, "location"));
    if (!location)
      throw std::runtime_error("runtime_error needs a source location");
    coge::ExecutionStatement result{Kind::Error};
    result.value =
        parseExecutionExpression(field(statement, "message"), names, true);
    result.extra =
        parseExecutionExpression(field(*location, "value"), names, true);
    return result;
  }
  if (statement.typeName == "expressionOrAssignment") {
    const auto &target = field(statement, "target");
    if (const auto *assignment = optional(field(statement, "assignment"))) {
      if (!function)
        throw std::runtime_error("execution assignment is not supported");
      if (!names.contains("@mutates"))
        throw std::runtime_error("runtime assignment needs mutates");
      coge::ExecutionStatement result{Kind::Assignment};
      result.value = parseExecutionExpression(target, names, false);
      auto *checkedTarget = &*result.value;
      if (checkedTarget->kind != coge::ExecutionExprKind::MemberField)
        throw std::runtime_error(
            "execution assignment needs a runtime state field");
      while (checkedTarget->kind == coge::ExecutionExprKind::MemberField &&
             !checkedTarget->children.empty())
        checkedTarget = &checkedTarget->children.front();
      if (checkedTarget->kind != coge::ExecutionExprKind::State)
        throw std::runtime_error(
            "execution assignment needs a runtime state field");
      result.extra =
          parseExecutionExpression(field(*assignment, "value"), names, true);
      return result;
    }
    coge::ExecutionStatement result{Kind::Expression};
    result.value = parseExecutionExpression(target, names, true);
    return result;
  }
  if (statement.typeName == "ifStatement") {
    coge::ExecutionStatement result{Kind::If};
    result.value =
        parseExecutionExpression(field(statement, "condition"), names, true);
    result.thenBranch = checkedExecutionStatements(
        field(statement, "thenBranch"), names, function);
    if (const auto *otherwise = optional(field(statement, "otherwise"))) {
      result.hasElse = true;
      result.elseBranch =
          checkedExecutionStatements(*otherwise, names, function);
    }
    return result;
  }
  if (statement.typeName == "foreachStatement") {
    if (optional(field(statement, "filter")))
      throw std::runtime_error("execution foreach filter is not supported");
    const auto item = token(field(statement, "item"));
    checkRustIdentifier(item);
    auto inner = names;
    if (!inner.insert(item).second)
      throw std::runtime_error("duplicate execution binding: " + item);
    coge::ExecutionStatement result{Kind::Foreach};
    result.binding = function && !referencesName(field(statement, "body"), item)
                         ? "_" + item
                         : item;
    result.value = parseExecutionExpression(field(statement, "collection"),
                                            names, function);
    result.thenBranch =
        checkedExecutionStatements(field(statement, "body"), inner, function);
    return result;
  }
  if (statement.typeName == "whileStatement" && !function) {
    coge::ExecutionStatement result{Kind::While};
    result.value =
        parseExecutionExpression(field(statement, "condition"), names, true);
    result.thenBranch =
        checkedExecutionStatements(field(statement, "body"), names, false);
    return result;
  }
  if (statement.typeName == "matchStatement") {
    coge::ExecutionStatement result{Kind::Match};
    result.value =
        parseExecutionExpression(field(statement, "value"), names, false);
    for (const auto &arm : field(statement, "arms").elements) {
      auto armNames = names;
      auto pattern = executionFunctionPattern(field(arm, "pattern"),
                                              field(arm, "body"), armNames);
      coge::ExecutionMatchArm checkedArm;
      checkedArm.pattern = emitExecutionPatternRust(pattern);
      checkedArm.structuredPattern = std::move(pattern);
      if (function) {
        checkedArm.body.push_back(
            checkedExecutionStatement(field(arm, "body"), armNames, true));
      } else {
        checkedArm.body.push_back(
            checkedExecutionStatement(field(arm, "body"), armNames, false));
      }
      result.arms.push_back(std::move(checkedArm));
    }
    return result;
  }
  throw std::runtime_error(
      std::string{function ? "unsupported execution function statement: "
                           : "unsupported execution statement: "} +
      statement.typeName);
}

auto checkedExecutionBody(const Value &body, coge::ExecutionBodyKind kind,
                          std::set<std::string> names) -> coge::ExecutionBody {
  const auto &statements = field(body, "statements").elements;
  if (kind != coge::ExecutionBodyKind::Function) {
    for (std::size_t index = 0; index < statements.size(); ++index)
      if (statements[index].typeName == "returnStatement" &&
          index + 1 != statements.size())
        throw std::runtime_error("unreachable statement in execution contract");
    if (kind == coge::ExecutionBodyKind::HandlerEvaluate &&
        (statements.empty() || statements.back().typeName != "returnStatement"))
      throw std::runtime_error("evaluate handler must return a value");
  }
  return {kind, checkedExecutionStatements(
                    body, std::move(names),
                    kind == coge::ExecutionBodyKind::Function)};
}

auto executionArms(const Value &contract, std::string_view kind)
    -> std::vector<coge::ExecutionArm> {
  std::vector<coge::ExecutionArm> result;
  std::set<std::string> names;
  for (const auto &entry : field(contract, "entries").elements) {
    if (token(field(entry, "kind")) != kind)
      continue;
    const auto name = token(field(entry, "name"));
    if (name.empty() || name.front() < 'A' || name.front() > 'Z' ||
        !std::ranges::all_of(name, [](char character) {
          return (character >= 'A' && character <= 'Z') ||
                 (character >= 'a' && character <= 'z') ||
                 (character >= '0' && character <= '9') || character == '_';
        }))
      throw std::runtime_error("invalid execution variant name: " + name);
    if (!names.insert(name).second)
      throw std::runtime_error(
          "coge.duplicate_execution_member: duplicate execution handler: " +
          name);
    std::vector<std::pair<std::string, std::optional<std::string>>> parameters;
    if (const auto *list = optional(field(entry, "parameters"))) {
      const auto collect = [&](const Value &parameter) {
        const auto local = parameter.kind == agas::runtime::AstValueKind::Token
                               ? token(parameter)
                               : token(field(parameter, "name"));
        checkRustIdentifier(local);
        const auto *binding =
            parameter.kind == agas::runtime::AstValueKind::Token
                ? nullptr
                : optional(field(parameter, "binding"));
        std::optional<std::string> fieldName;
        if (binding) {
          fieldName = simpleToken(*binding);
          checkRustIdentifier(*fieldName);
        }
        parameters.emplace_back(local, fieldName);
      };
      if (list->typeName == "contractParameters") {
        collect(field(*list, "first"));
        for (const auto &tail : field(*list, "rest").elements)
          collect(tail);
      } else {
        collect(*list);
      }
    }
    const bool named = std::ranges::any_of(
        parameters, [](const auto &item) { return item.second.has_value(); });
    if (named && std::ranges::any_of(parameters, [](const auto &item) {
          return !item.second.has_value();
        }))
      throw std::runtime_error("mixed execution variant fields: " + name);
    std::string pattern =
        std::string{kind == "execute" ? "Operation::" : "ExpressionKind::"} +
        name;
    const auto bindingName = [&](std::string_view local) {
      return referencesName(field(entry, "body"), local)
                 ? std::string{local}
                 : "_" + std::string{local};
    };
    if (named) {
      pattern += " { ";
      for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (index)
          pattern += ", ";
        const auto local = bindingName(parameters[index].first);
        pattern += *parameters[index].second == local
                       ? local
                       : *parameters[index].second + ": " + local;
      }
      pattern += " }";
    } else if (!parameters.empty()) {
      pattern += '(';
      for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (index)
          pattern += ", ";
        pattern += bindingName(parameters[index].first);
      }
      pattern += ')';
    }
    std::set<std::string> names{"source"};
    if (kind == "evaluate") {
      names.insert("expression");
      names.insert("checks");
    }
    for (const auto &[local, fieldName] : parameters) {
      static_cast<void>(fieldName);
      if (!names.insert(local).second)
        throw std::runtime_error("duplicate execution parameter: " + local);
    }
    ExecutionPattern structured{
        kind == "execute" ? "Operation" : "ExpressionKind",
        name,
        named ? ExecutionPatternForm::Fields
              : (parameters.empty() ? ExecutionPatternForm::Unit
                                    : ExecutionPatternForm::Tuple),
        {}};
    for (const auto &[local, fieldName] : parameters)
      structured.bindings.push_back(
          {fieldName.value_or(""), local,
           referencesName(field(entry, "body"), local)});
    result.push_back(
        {std::move(pattern),
         checkedExecutionBody(field(entry, "body"),
                              kind == "execute"
                                  ? ExecutionBodyKind::HandlerExecute
                                  : ExecutionBodyKind::HandlerEvaluate,
                              std::move(names)),
         std::move(structured)});
  }
  return result;
}

auto prepareExecutionContract(const Value &root)
    -> std::optional<coge::InterpreterGenerationPlan> {
  const auto *settings = optional(field(root, "settings"));
  if (!settings)
    return std::nullopt;
  bool requested = false;
  for (const auto &entry : field(*settings, "entries").elements)
    if (token(field(entry, "name")) == "generate_interpreter") {
      if (simpleToken(field(entry, "value")) != "true")
        throw std::runtime_error("generate_interpreter must be true");
      requested = true;
    }
  if (!requested)
    return std::nullopt;
  const auto *contract = optional(field(root, "contract"));
  if (!contract)
    throw std::runtime_error("generate_interpreter needs execution_contract");
  coge::InterpreterGenerationPlan checked;
  checked.operations = executionArms(*contract, "execute");
  checked.expressions = executionArms(*contract, "evaluate");
  if (checked.operations.empty() || checked.expressions.empty())
    throw std::runtime_error(
        "execution contract needs execute and evaluate handlers");
  return checked;
}

using coge::PropertyExpression;
using coge::PropertyExprKind;
using coge::PropertyValueKind;

auto propertyArguments(const Value &call) -> std::vector<const Value *> {
  std::vector<const Value *> result;
  if (const auto *arguments = optional(field(call, "arguments"))) {
    if (arguments->typeName == "argumentList") {
      result.push_back(&field(*arguments, "first"));
      for (const auto &item : field(*arguments, "rest").elements)
        result.push_back(&item);
    } else {
      result.push_back(arguments);
    }
  }
  return result;
}

auto propertyExpression(const Value &value, const std::set<std::string> &names)
    -> PropertyExpression {
  if (value.kind == agas::runtime::AstValueKind::Token) {
    const auto name = token(value);
    if (names.contains(name))
      return {PropertyExprKind::Binding, PropertyValueKind::Scalar, name, {}};
    if (name == "true" || name == "false")
      return {
          PropertyExprKind::BoolLiteral, PropertyValueKind::Boolean, name, {}};
    if (!name.empty() && std::ranges::all_of(name, [](char c) {
          return c >= '0' && c <= '9';
        })) {
      const auto number = std::stoll(name);
      if (number > INT32_MAX)
        throw std::runtime_error("property literal must fit I32");
      return {PropertyExprKind::IntegerLiteral,
              PropertyValueKind::Scalar,
              std::to_string(number),
              {}};
    }
    throw std::runtime_error("unknown property value: " + name);
  }
  if (value.typeName == "actionExpression") {
    if (optional(field(value, "choice")) ||
        field(value, "rest").elements.size() > 1)
      throw std::runtime_error("unsupported property expression");
    auto first = propertyExpression(field(value, "first"), names);
    for (const auto &part : field(value, "rest").elements) {
      const auto op = token(field(part, "operator"));
      if (op != "==" && op != "!=" && op != "<" && op != "<=" && op != ">" &&
          op != ">=")
        throw std::runtime_error("property condition needs a comparison");
      const auto right = propertyExpression(field(part, "value"), names);
      if (first.valueKind == PropertyValueKind::Boolean ||
          right.valueKind == PropertyValueKind::Boolean)
        throw std::runtime_error("property comparison needs numbers");
      first = {PropertyExprKind::Comparison,
               PropertyValueKind::Boolean,
               op,
               {std::move(first), std::move(right)}};
    }
    return first;
  }
  if (value.typeName != "actionUnary")
    throw std::runtime_error("unsupported property expression AST");
  const auto &suffixes = field(value, "suffixes").elements;
  const auto &prefixes = field(value, "prefixes").elements;
  if (!prefixes.empty()) {
    if (prefixes.size() != 1 || token(prefixes.front()) != "-" ||
        !suffixes.empty())
      throw std::runtime_error("unsupported property prefix");
    auto inner = propertyExpression(field(value, "atom"), names);
    if (inner.valueKind == PropertyValueKind::Boolean)
      throw std::runtime_error("property negation needs a number");
    return {PropertyExprKind::Negation,
            PropertyValueKind::Mathematical,
            {},
            {std::move(inner)}};
  }
  if (suffixes.empty())
    return propertyExpression(field(value, "atom"), names);
  const auto name = token(field(value, "atom"));
  if (name == "I32" && suffixes.size() == 1 &&
      suffixes.front().variantName == "Field") {
    const auto member = token(field(suffixes.front(), "name"));
    if (member == "min" || member == "max")
      return {member == "min" ? PropertyExprKind::I32Min
                              : PropertyExprKind::I32Max,
              PropertyValueKind::Scalar,
              {},
              {}};
  }
  if (suffixes.size() != 1 || suffixes.front().variantName != "Call")
    throw std::runtime_error("unsupported property call");
  const auto args = propertyArguments(suffixes.front());
  std::string op;
  if (name == "mathematical_sum")
    op = "+";
  else if (name == "mathematical_difference")
    op = "-";
  else if (name == "mathematical_product")
    op = "*";
  else if (name != "mathematical_negation")
    throw std::runtime_error("unknown property oracle: " + name);
  if (args.size() != (name == "mathematical_negation" ? 1 : 2))
    throw std::runtime_error("wrong property oracle arity: " + name);
  std::vector<PropertyExpression> operands;
  for (const auto *arg : args) {
    const auto expression = propertyExpression(*arg, names);
    if (expression.valueKind != PropertyValueKind::Scalar)
      throw std::runtime_error("property oracle operands must be I32 scalars");
    operands.push_back(expression);
  }
  return {PropertyExprKind::Oracle, PropertyValueKind::Mathematical, op,
          std::move(operands)};
}

auto propertyType(const Value &value) -> std::string {
  if (value.kind == agas::runtime::AstValueKind::Token)
    return token(value);
  auto name = token(field(value, "name"));
  if (const auto *arguments = optional(field(value, "arguments"))) {
    name += "<" + propertyType(field(*arguments, "first"));
    for (const auto &item : field(*arguments, "rest").elements)
      name += "," + propertyType(item);
    name += ">";
  }
  return name;
}

auto propertyParameters(const Value &list) -> std::vector<const Value *> {
  if (list.typeName != "functionParameterList")
    return {&list};
  std::vector<const Value *> result{&field(list, "first")};
  for (const auto &item : field(list, "rest").elements)
    result.push_back(&item);
  return result;
}

auto parseExecutionType(const Value &) -> ExecutionType;

auto prepareProperties(const Value &root,
                       const ExpandedExecutionModel *expanded = nullptr)
    -> std::optional<coge::PropertyGenerationPlan> {
  const auto *model = optional(field(root, "execution"));
  if (!model)
    return std::nullopt;
  const Value *properties = nullptr;
  std::map<std::string, ExecutionFunctionSignature> functions;
  if (expanded)
    for (const auto &function : expanded->functions)
      functions.emplace(function.signature.name, function.signature);
  for (const auto &declaration : field(*model, "declarations").elements) {
    if (declaration.typeName == "propertyBlock") {
      if (properties)
        throw std::runtime_error("duplicate properties block");
      properties = &declaration;
    } else if (declaration.typeName == "functionDeclaration") {
      if (!expanded) {
        ExecutionFunctionSignature signature;
        signature.name = token(field(declaration, "name"));
        signature.mutates = optional(field(declaration, "effect"));
        if (const auto *result = optional(field(declaration, "result")))
          signature.result = parseExecutionType(field(*result, "type"));
        if (const auto *parameters = optional(field(declaration, "parameters")))
          for (const auto *parameter : propertyParameters(*parameters))
            signature.parameters.push_back(
                {token(field(*parameter, "name")),
                 parseExecutionType(field(
                     *optional(field(*parameter, "annotation")), "value")),
                 true});
        functions.emplace(signature.name, std::move(signature));
      }
    }
  }
  if (!properties)
    return std::nullopt;
  std::set<std::int64_t> cases{INT32_MIN,
                               INT32_MIN + 1LL,
                               -65536,
                               -46341,
                               -46340,
                               -256,
                               -2,
                               -1,
                               0,
                               1,
                               2,
                               255,
                               256,
                               46340,
                               46341,
                               65536,
                               INT32_MAX - 1LL,
                               INT32_MAX};
  const auto collect = [&](const auto &self, const Value &value) -> void {
    if (value.kind == agas::runtime::AstValueKind::Token &&
        !value.tokenText.empty() &&
        std::ranges::all_of(value.tokenText,
                            [](char c) { return c >= '0' && c <= '9'; })) {
      const auto number = std::stoll(value.tokenText);
      if (number > INT32_MAX)
        throw std::runtime_error("property literal must fit I32");
      for (const auto sign : {-1, 1})
        for (const auto delta : {-1, 0, 1}) {
          const auto candidate = sign * number + delta;
          if (candidate >= INT32_MIN && candidate <= INT32_MAX)
            cases.insert(candidate);
        }
    }
    for (const auto &item : value.elements)
      self(self, item);
  };
  collect(collect, *properties);
  coge::PropertyGenerationPlan plan;
  plan.boundaryCases.assign(cases.begin(), cases.end());
  for (const auto &property : field(*properties, "entries").elements) {
    coge::PropertyCheck checked;
    const auto parameters = propertyParameters(field(property, "parameters"));
    if (parameters.empty() || parameters.size() > 2)
      throw std::runtime_error("forall supports one or two I32 bindings");
    std::set<std::string> names;
    for (const auto *parameter : parameters) {
      const auto name = token(field(*parameter, "name"));
      checkRustIdentifier(name);
      if (!names.insert(name).second)
        throw std::runtime_error("duplicate forall binding: " + name);
      const auto *annotation = optional(field(*parameter, "annotation"));
      if (!annotation || propertyType(field(*annotation, "value")) != "I32")
        throw std::runtime_error("forall domain must be I32");
      checked.bindings.push_back(name);
    }
    const auto &actual = field(property, "actual");
    if (!isDirectExecutionCall(actual))
      throw std::runtime_error("expect needs an action function call");
    checked.function = token(field(actual, "atom"));
    const auto found = functions.find(checked.function);
    if (found == functions.end())
      throw std::runtime_error("unknown property function: " +
                               checked.function);
    const auto &function = found->second;
    if (function.mutates)
      throw std::runtime_error("properties require a pure function: " +
                               checked.function);
    if (function.result.kind != ExecutionTypeKind::Result ||
        function.result.arguments.size() != 2 ||
        function.result.arguments[0].name != "I32" ||
        function.result.arguments[1].name != "RuntimeError")
      throw std::runtime_error(
          "property function must return Result<I32,RuntimeError>");
    const auto arguments =
        propertyArguments(field(actual, "suffixes").elements.front());
    auto expectedParameters = function.parameters;
    if (!expectedParameters.empty() &&
        expectedParameters.back().type.name == "SourceRange") {
      checked.source = true;
      expectedParameters.pop_back();
    }
    if (arguments.size() != expectedParameters.size())
      throw std::runtime_error("wrong property call arity: " +
                               checked.function);
    for (std::size_t arg = 0; arg < arguments.size(); ++arg) {
      if (expectedParameters[arg].type.name != "I32")
        throw std::runtime_error("property function arguments must be I32");
      const auto binding = simpleToken(*arguments[arg]);
      if (!names.contains(binding))
        throw std::runtime_error("property call needs a quantified binding: " +
                                 binding);
      checked.arguments.push_back(binding);
    }
    if (const auto *constraint = optional(field(property, "constraint"))) {
      checked.constraint =
          propertyExpression(field(*constraint, "value"), names);
      if (const auto *range = optional(field(*constraint, "range"))) {
        if (token(field(*range, "type")) != "I32" ||
            checked.constraint->valueKind == PropertyValueKind::Boolean)
          throw std::runtime_error(
              "fits requires a numeric expression and I32");
        checked.fitsI32 = true;
        checked.negatedFits = optional(field(*range, "negated")) != nullptr;
      } else if (checked.constraint->valueKind != PropertyValueKind::Boolean) {
        throw std::runtime_error(
            "property where condition must be boolean or use fits");
      }
    }
    const auto &assertion = field(property, "assertion");
    if (assertion.variantName == "Equal") {
      checked.expected =
          propertyExpression(field(assertion, "expected"), names);
      if (checked.expected->valueKind == PropertyValueKind::Boolean)
        throw std::runtime_error("property expectation must be numeric");
    } else {
      checked.errorMessageLiteral = quotedText(field(assertion, "message"));
    }
    plan.checks.push_back(std::move(checked));
  }
  return plan;
}

auto parseExecutionType(const Value &value) -> coge::ExecutionType {
  if (value.kind != agas::runtime::AstValueKind::Token &&
      value.typeName != "typeExpression")
    throw std::runtime_error(
        "unexpected execution type AST: " + value.typeName + "/" +
        std::to_string(static_cast<int>(value.kind)));
  const auto name = value.kind == agas::runtime::AstValueKind::Token
                        ? token(value)
                        : token(field(value, "name"));
  const auto *arguments = value.kind == agas::runtime::AstValueKind::Token
                              ? nullptr
                              : optional(field(value, "arguments"));
  std::vector<coge::ExecutionType> types;
  if (arguments) {
    types.push_back(parseExecutionType(field(*arguments, "first")));
    for (const auto &item : field(*arguments, "rest").elements)
      types.push_back(parseExecutionType(item));
  }
  return coge::prepareExecutionType(name, std::move(types));
}

auto prepareExecutionDeclarations(const Value &root,
                                  std::string_view contextType)
    -> std::optional<coge::ExecutionDeclarations> {
  const auto *model = optional(field(root, "execution"));
  if (!model)
    return std::nullopt;
  coge::ExecutionDeclarations checked;
  std::set<std::string> declaredTypes;
  std::set<std::string> declaredRecords;
  std::vector<const Value *> functions;
  for (const auto &declaration : field(*model, "declarations").elements) {
    if (declaration.typeName == "propertyBlock")
      continue;
    if (declaration.typeName == "functionDeclaration") {
      functions.push_back(&declaration);
      continue;
    }
    if (declaration.typeName == "executionStateDeclaration") {
      if (checked.runtimeState)
        throw std::runtime_error("execution_model has multiple runtime states");
      const auto stateName = token(field(declaration, "name"));
      checkRustIdentifier(stateName);
      checked.runtimeState = coge::ExecutionState{
          stateName, parseExecutionType(field(declaration, "type"))};
      continue;
    }
    if (declaration.typeName == "executionProfileDeclaration")
      continue;
    if (declaration.typeName == "intrinsicDeclaration") {
      if (field(declaration, "tail").variantName != "External")
        throw std::runtime_error(
            "execution intrinsic needs an external implementation");
      continue;
    }
    if (declaration.typeName == "recordDeclaration") {
      const auto name = token(field(declaration, "name"));
      checkRustTypeIdentifier(name);
      if (!declaredTypes.insert(name).second)
        throw std::runtime_error("duplicate execution type: " + name);
      declaredRecords.insert(name);
      coge::ExecutionDeclaration record{
          coge::ExecutionDeclarationKind::Record, name, {}, {}};
      if (const auto *fields = optional(field(declaration, "fields"))) {
        std::set<std::string> declaredFields;
        const auto appendField = [&](const Value &entry) {
          const auto fieldName = token(field(entry, "name"));
          checkRustIdentifier(fieldName);
          if (!declaredFields.insert(fieldName).second)
            throw std::runtime_error("duplicate execution field: " + name +
                                     "." + fieldName);
          const auto *annotation = optional(field(entry, "type"));
          if (!annotation || optional(field(entry, "defaultValue")))
            throw std::runtime_error(
                "execution record fields need types and no default: " + name +
                "." + fieldName);
          record.fields.push_back(
              {fieldName, parseExecutionType(field(*annotation, "value"))});
        };
        if (fields->typeName == "recordFieldList") {
          appendField(field(*fields, "first"));
          for (const auto &item : field(*fields, "rest").elements)
            appendField(item);
        } else {
          appendField(*fields);
        }
      }
      checked.declarations.push_back(std::move(record));
      continue;
    }
    if (declaration.typeName != "executionEnumDeclaration")
      throw std::runtime_error(
          "execution_model declaration is not yet supported by Rust "
          "generation: " +
          declaration.typeName);
    const auto name = token(field(declaration, "name"));
    checkRustTypeIdentifier(name);
    if (!declaredTypes.insert(name).second)
      throw std::runtime_error("duplicate execution type: " + name);
    coge::ExecutionDeclaration enumeration{
        coge::ExecutionDeclarationKind::Enumeration, name, {}, {}};
    std::set<std::string> declaredVariants;
    const auto appendVariant = [&](const Value &value) {
      const auto variant = value.kind == agas::runtime::AstValueKind::Token
                               ? token(value)
                               : token(field(value, "name"));
      checkRustTypeIdentifier(variant);
      if (!declaredVariants.insert(variant).second)
        throw std::runtime_error("duplicate execution variant: " + name + "." +
                                 variant);
      coge::ExecutionVariant item{variant, {}};
      const auto *payload = value.kind == agas::runtime::AstValueKind::Token
                                ? nullptr
                                : optional(field(value, "payload"));
      if (payload) {
        const auto &fields = payload->typeName == "executionEnumPayload"
                                 ? field(*payload, "fields")
                                 : *payload;
        if (fields.typeName == "typeExpressionList") {
          item.payload.push_back(parseExecutionType(field(fields, "first")));
          for (const auto &entry : field(fields, "rest").elements)
            item.payload.push_back(parseExecutionType(entry));
        } else {
          item.payload.push_back(parseExecutionType(fields));
        }
      }
      enumeration.variants.push_back(std::move(item));
    };
    const auto &values = field(declaration, "values");
    appendVariant(field(values, "first"));
    for (const auto &item : field(values, "rest").elements)
      appendVariant(item);
    checked.declarations.push_back(std::move(enumeration));
  }
  if (checked.runtimeState) {
    const auto &stateType = checked.runtimeState->type;
    if (stateType.kind != coge::ExecutionTypeKind::Named ||
        !declaredRecords.contains(stateType.name))
      throw std::runtime_error(
          "runtime_state must refer to an execution record: " +
          coge::emitExecutionTypeRust(stateType));
    checkRustTypeIdentifier(std::string{contextType});
  }
  std::set<std::string> declaredMethods{"execute_generated",
                                        "evaluate_generated"};
  for (const auto *function : functions) {
    const auto name = token(field(*function, "name"));
    checkRustIdentifier(name);
    if (!declaredMethods.insert(name).second)
      throw std::runtime_error(
          "coge.duplicate_execution_member: duplicate execution function: " +
          name);
    const auto *result = optional(field(*function, "result"));
    if (!result)
      throw std::runtime_error("execution function needs a result: " + name);
    auto resultType = parseExecutionType(field(*result, "type"));
    if (resultType.kind != coge::ExecutionTypeKind::Result)
      throw std::runtime_error("execution function must return Result: " +
                               name);
    const bool mutates = optional(field(*function, "effect")) != nullptr;
    coge::ExecutionFunctionSignature signature{
        name, mutates, std::move(resultType), {}};
    std::set<std::string> names;
    if (mutates)
      names.insert("@mutates");
    if (checked.runtimeState)
      names.insert("@state:" + checked.runtimeState->name);
    if (const auto *list = optional(field(*function, "parameters"))) {
      const auto appendParameter = [&](const Value &parameter) {
        const auto parameterName = token(field(parameter, "name"));
        checkRustIdentifier(parameterName);
        if (!names.insert(parameterName).second)
          throw std::runtime_error("duplicate execution parameter: " +
                                   parameterName);
        const auto *annotation = optional(field(parameter, "annotation"));
        if (!annotation)
          throw std::runtime_error("execution parameter needs a type: " + name +
                                   "." + parameterName);
        signature.parameters.push_back(
            {parameterName, parseExecutionType(field(*annotation, "value")),
             referencesName(field(*function, "body"), parameterName)});
      };
      if (list->typeName == "functionParameterList") {
        appendParameter(field(*list, "first"));
        for (const auto &parameter : field(*list, "rest").elements)
          appendParameter(parameter);
      } else {
        appendParameter(*list);
      }
    }
    checked.functions.push_back(std::move(signature));
  }
  return checked;
}

auto prepareExecutionBodies(
    const Value &root,
    const std::optional<coge::ExecutionDeclarations> &declarations)
    -> std::vector<coge::ExecutionBody> {
  if (!declarations)
    return {};
  const auto *model = optional(field(root, "execution"));
  if (!model)
    throw std::runtime_error("missing execution model for prepared bodies");
  std::vector<const Value *> functions;
  for (const auto &declaration : field(*model, "declarations").elements)
    if (declaration.typeName == "functionDeclaration")
      functions.push_back(&declaration);
  if (functions.size() != declarations->functions.size())
    throw std::runtime_error("execution function plan mismatch");
  std::vector<coge::ExecutionBody> bodies;
  for (std::size_t index = 0; index < functions.size(); ++index) {
    const auto &signature = declarations->functions[index];
    std::set<std::string> names;
    if (signature.mutates)
      names.insert("@mutates");
    if (declarations->runtimeState)
      names.insert("@state:" + declarations->runtimeState->name);
    for (const auto &parameter : signature.parameters)
      names.insert(parameter.name);
    bodies.push_back(checkedExecutionBody(field(*functions[index], "body"),
                                          coge::ExecutionBodyKind::Function,
                                          std::move(names)));
  }
  return bodies;
}

auto statementEntries(const Value &declaration,
                      const std::set<std::string> &allowed,
                      std::string_view kind, bool complete = true)
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
    if (complete && !result.contains(key))
      throw std::runtime_error(std::string{kind} + " missing field " + key);
  return result;
}

auto loweringInput(const Value &root,
                   const std::vector<agsem::ContractSymbol> &contracts = {},
                   std::string_view contextType = "Context")
    -> coge::LoweringGenerationInput {
  const auto *declaration = optional(field(root, "lowering"));
  if (!declaration)
    return {};
  if (contextType != "Context")
    throw std::runtime_error(
        "structured Core lowering requires rust_context Context");
  auto fields = coge::loweringFields();
  fields.insert("profile");
  coge::LoweringGenerationInput input{
      statementEntries(*declaration, fields, "lowering_model", false),
      contracts};
  for (const auto &entry : field(*declaration, "entries").elements)
    input.locations.emplace(token(field(entry, "name")),
                            agsem::ast::location(entry));
  return input;
}

auto backendInput(const Value &root, const coge::CheckedLowering &lowering)
    -> coge::BackendGenerationInput {
  coge::BackendGenerationInput input;
  input.lowering = lowering.loweringName();
  static const std::set<std::string> fields{"emit", "lower", "profile"};
  const auto backend = [&](const Value &declaration, std::string_view kind) {
    auto bindings = statementEntries(declaration, fields, kind, false);
    if (!bindings.contains("profile"))
      for (const auto &key : {"emit", "lower"})
        if (!bindings.contains(key))
          throw std::runtime_error(std::string{kind} + " missing field " + key);
    coge::BackendBinding binding{bindings["emit"], bindings["lower"],
                                 bindings["profile"]};
    for (const auto &entry : field(declaration, "entries").elements)
      binding.locations.emplace(token(field(entry, "name")),
                                agsem::ast::location(entry));
    return binding;
  };
  if (const auto *declaration = optional(field(root, "backendC"))) {
    input.c = backend(*declaration, "backend_c");
  }
  if (const auto *declaration = optional(field(root, "backendLlvm"))) {
    input.llvm = backend(*declaration, "backend_llvm");
  }
  return input;
}

} // namespace

auto expandExecutionModel(const Value &root, std::string_view context,
                          std::string identity, std::string contractsIdentity)
    -> ExpandedExecutionModel {
  ExpandedExecutionModel result;
  result.sourceIdentity = std::move(identity);
  result.contractsIdentity = std::move(contractsIdentity);
  result.contextType = context;
  result.source = agsem::ast::location(root);
  if (const auto *settings = optional(field(root, "settings")))
    for (const auto &entry : field(*settings, "entries").elements)
      if (token(field(entry, "name")) == "generate_interpreter") {
        if (simpleToken(field(entry, "value")) != "true")
          throw std::runtime_error("generate_interpreter must be true");
        result.interpreterRequested = true;
      }
  result.declarations = prepareExecutionDeclarations(root, context);
  auto bodies = prepareExecutionBodies(root, result.declarations);
  if (const auto *model = optional(field(root, "execution"))) {
    std::size_t functionIndex = 0;
    std::set<std::string> intrinsicNames;
    for (const auto &d : field(*model, "declarations").elements) {
      const ExecutionOrigin origin{"explicit", agsem::ast::location(d), {}};
      if (d.typeName == "recordDeclaration" ||
          d.typeName == "executionEnumDeclaration")
        result.typeOrigins.emplace(token(field(d, "name")), origin.source);
      if (d.typeName == "executionProfileDeclaration") {
        if (result.profile)
          throw std::runtime_error("coge.duplicate_execution_profile: only one "
                                   "execution_profile is allowed");
        result.profile = ExecutionProfileIdentity{
            token(field(d, "name")), {}, {}, origin.source};
      } else if (d.typeName == "functionDeclaration") {
        result.functions.push_back(
            {result.declarations->functions.at(functionIndex),
             std::move(bodies.at(functionIndex)), origin});
        ++functionIndex;
      } else if (d.typeName == "intrinsicDeclaration") {
        ExecutionFunctionSignature f;
        f.name = token(field(d, "name"));
        if (!intrinsicNames.insert(f.name).second)
          throw std::runtime_error(
              "coge.duplicate_execution_member: intrinsic " + f.name);
        f.mutates = optional(field(d, "effect"));
        const auto *r = optional(field(d, "result"));
        if (!r)
          throw std::runtime_error(
              "execution intrinsic requires a result type");
        f.result = parseExecutionType(field(*r, "type"));
        if (const auto *parameters = optional(field(d, "parameters")))
          for (const auto *parameter : propertyParameters(*parameters)) {
            const auto *annotation = optional(field(*parameter, "annotation"));
            if (!annotation)
              throw std::runtime_error(
                  "execution intrinsic requires typed parameters");
            f.parameters.push_back(
                {token(field(*parameter, "name")),
                 parseExecutionType(field(*annotation, "value")), true});
          }
        result.intrinsics.push_back({std::move(f), origin});
      }
    }
  }
  if (result.declarations)
    result.declarations->functions.clear();
  if (const auto *contract = optional(field(root, "contract"))) {
    auto operations = executionArms(*contract, "execute");
    auto expressions = executionArms(*contract, "evaluate");
    std::size_t operation = 0, expression = 0;
    for (const auto &entry : field(*contract, "entries").elements) {
      ExecutionOrigin origin{"explicit", agsem::ast::location(entry), {}};
      if (token(field(entry, "kind")) == "execute")
        result.operations.push_back(
            {std::move(operations.at(operation++)), origin});
      else
        result.expressions.push_back(
            {std::move(expressions.at(expression++)), origin});
    }
  }
  applyExecutionProfile(result);
  result.properties = prepareProperties(root, &result);
  return result;
}

auto prepareGeneration(const ExecutionInput &input,
                       std::string_view contextType,
                       const std::vector<agsem::ContractSymbol> &contracts)
    -> CheckedGeneration {
  const auto &root = DocumentAccess::root(input);
  if (!input.identity().empty())
    throw std::runtime_error(
        "document.typed_preparation_required: format 1 generation requires "
        "bound model contracts and completeness validation");
  const auto pending = agsem::executionTemplateDiagnostics(root, true);
  if (!pending.empty())
    throw agsem::SemanticPreparationError(pending);
  if (const auto *model = optional(field(root, "execution")))
    for (const auto &d : field(*model, "declarations").elements)
      if (d.typeName == "executionProfileDeclaration")
        throw std::runtime_error(
            "coge.execution_profile_requires_contracts: execution_profile "
            "requires bound model contracts");
  auto interpreter = prepareExecutionContract(root);
  if (interpreter) {
    interpreter->contextType = contextType;
    interpreter->declarations = prepareExecutionDeclarations(root, contextType);
    interpreter->functionBodies =
        prepareExecutionBodies(root, interpreter->declarations);
  }
  auto properties = prepareProperties(root);
  auto lowering = prepareLowering(loweringInput(root, contracts, contextType));
  auto backends = prepareBackends(backendInput(root, lowering));
  return prepareCheckedGeneration({std::move(interpreter),
                                   std::move(properties), std::move(lowering),
                                   std::move(backends)});
}

auto tryPrepareGeneration(const ExecutionInput &input,
                          std::string_view contextType,
                          const std::vector<agsem::ContractSymbol> &contracts)
    -> agsem::Outcome<CheckedGeneration> {
  agsem::Outcome<CheckedGeneration> result;
  try {
    result.value = prepareGeneration(input, contextType, contracts);
  } catch (const std::runtime_error &error) {
    const auto span = DocumentAccess::root(input).sourceSpan;
    result.diagnostics.push_back(
        {agsem::Severity::Error, "coge.invalid_contract", error.what(),
         agsem::SourceLocation{0, span.beginByte, span.endByte},
         "execution_model"});
  }
  return result;
}

auto reviewConfiguration(const CogeDocument &document,
                         const agsem::BoundSemantics &semantics)
    -> ConfigurationReview {
  ConfigurationReview result;
  const auto &root = DocumentAccess::root(document.execution());
  std::vector<agsem::ContractSymbol> symbols;
  for (const auto &s : semantics.symbols())
    symbols.push_back(s.contract);
  std::string context;
  const auto &semanticRoot =
      agsem::SemanticInputAccess::root(document.semantics().semanticInput());
  if (const auto *model = optional(field(semanticRoot, "model")))
    for (const auto &d : field(*model, "declarations").elements)
      if (d.typeName == "rustContextDeclaration")
        context = token(field(d, "name"));
  try {
    result.lowering = prepareLowering(loweringInput(root, symbols, context));
  } catch (const std::runtime_error &error) {
    result.diagnostics.push_back(
        {agsem::Severity::Error, "coge.invalid_lowering", error.what(),
         optional(field(root, "lowering"))
             ? agsem::ast::location(*optional(field(root, "lowering")))
             : agsem::ast::location(root),
         "lowering"});
  }
  if (result.lowering) {
    try {
      result.backends = prepareBackends(backendInput(root, *result.lowering));
    } catch (const std::runtime_error &error) {
      result.diagnostics.push_back({agsem::Severity::Error,
                                    "coge.invalid_backend", error.what(),
                                    agsem::ast::location(root), "backend"});
    }
  }
  return result;
}

auto generationSelection(const CogeDocument &document) -> GenerationSelection {
  const auto &root = DocumentAccess::root(document.execution());
  bool interpreter = false;
  if (const auto *settings = optional(field(root, "settings")))
    for (const auto &entry : field(*settings, "entries").elements)
      if (token(field(entry, "name")) == "generate_interpreter")
        interpreter = true;
  GenerationSelection result{interpreter,
                             optional(field(root, "lowering")) != nullptr,
                             optional(field(root, "backendC")) != nullptr,
                             optional(field(root, "backendLlvm")) != nullptr};
  if (const auto *obligations = optional(field(root, "obligations")))
    for (const auto &entry : field(*obligations, "entries").elements)
      if (token(field(entry, "name")) == "targets")
        for (const auto &value : field(entry, "values").elements) {
          const auto target = nlohmann::json::parse(agsem::ast::token(value))
                                  .get<std::string>();
          result.interpreter |= target == "interpreter";
          result.c |= target == "c";
          result.llvm |= target == "llvm";
        }
  result.lowering |= result.c || result.llvm;
  return result;
}
auto checkCoge(const CogeDocument &document,
               const agsem::BoundSemantics &semantics,
               const agsem::ContractEnvironment &contracts)
    -> agsem::Outcome<CogeValidation> {
  agsem::Outcome<CogeValidation> result;
  if (document.identity() != semantics.document().identity() ||
      contracts.fingerprint() != semantics.contracts().fingerprint()) {
    result.diagnostics.push_back(
        {agsem::Severity::Error,
         "document.identity_mismatch",
         "execution and semantics belong to different inputs or contracts",
         {},
         {}});
    return result;
  }
  auto assessment =
      coge::assessCompleteness(DocumentAccess::syntax(document), contracts);
  result.diagnostics = std::move(assessment.diagnostics);
  if (!assessment.value)
    return result;
  result.diagnostics = agsem::generationDiagnostics(*assessment.value, false);
  if (!result.diagnostics.empty())
    return result;
  const auto selected = generationSelection(document);
  const auto &executionRoot = DocumentAccess::root(document.execution());
  for (const auto &[required, section] :
       std::vector<std::pair<bool, std::string>>{
           {selected.interpreter, "execution"},
           {selected.lowering, "lowering"},
           {selected.c, "backendC"},
           {selected.llvm, "backendLlvm"}})
    if (required && !optional(field(executionRoot, section)))
      result.diagnostics.push_back(
          {agsem::Severity::Error, "completeness.pending",
           "selected execution target requires " + section,
           agsem::ast::location(executionRoot), section});
  auto semanticCheck = agsem::checkSemantics(semantics);
  result.diagnostics.insert(result.diagnostics.end(),
                            semanticCheck.diagnostics.begin(),
                            semanticCheck.diagnostics.end());
  auto pending = agsem::executionTemplateDiagnostics(
      DocumentAccess::root(document.execution()), true);
  result.diagnostics.insert(result.diagnostics.end(), pending.begin(),
                            pending.end());
  for (const auto &element : DocumentAccess::syntax(document).elements())
    if (element.kind == agsem::DocumentElementKind::ExecutionResult)
      result.diagnostics.push_back(
          {agsem::Severity::Error,
           "coge.unsupported_execution_result",
           "execution result has no supported execution contract",
           element.location,
           {}});
  if (!result.diagnostics.empty())
    return result;
  try {
    const auto &root = DocumentAccess::root(document.execution());
    const auto *model = optional(field(
        agsem::SemanticInputAccess::root(document.semantics().semanticInput()),
        "model"));
    std::string context;
    if (model)
      for (const auto &declaration : field(*model, "declarations").elements)
        if (declaration.typeName == "rustContextDeclaration")
          context = token(field(declaration, "name"));
    std::vector<agsem::ContractSymbol> modelContracts;
    for (const auto &symbol : semantics.symbols())
      modelContracts.push_back(symbol.contract);
    const auto lowering =
        prepareLowering(loweringInput(root, modelContracts, context));
    const auto backends = prepareBackends(backendInput(root, lowering));
    static_cast<void>(prepareCheckedGeneration(
        {std::nullopt, std::nullopt, lowering, backends}));
    auto expanded = expandExecutionModel(root, context, document.identity(),
                                         contracts.fingerprint());
    auto execution = prepareCheckedExecution(std::move(expanded), semantics);
    result.diagnostics = std::move(execution.diagnostics);
    if (!execution.value)
      return result;
    if ((*execution.value)->model().interpreterRequested)
      static_cast<void>(prepareCheckedGeneration(
          {(*execution.value)->interpreter(),
           (*execution.value)->model().properties, lowering, backends}));
    if (!result.diagnostics.empty())
      return result;
    result.value = CogeValidation{*execution.value, lowering, backends,
                                  std::move(*assessment.value)};
  } catch (const std::runtime_error &error) {
    result.diagnostics.push_back(
        {agsem::Severity::Error,
         std::string(error.what()).starts_with("coge.")
             ? std::string(error.what())
                   .substr(0, std::string(error.what()).find(':'))
             : "coge.invalid_contract",
         error.what(),
         agsem::ast::location(DocumentAccess::root(document.execution())),
         {}});
  }
  return result;
}
auto prepareGeneration(const CogeDocument &document,
                       const agsem::CheckedSemantics &semantics,
                       const GenerationSelection &selection,
                       const agsem::ContractEnvironment &contracts)
    -> agsem::Outcome<CheckedGeneration> {
  agsem::Outcome<CheckedGeneration> result;
  if (agsem::SemanticInputAccess::identity(agsem::semanticInput(semantics)) !=
          document.identity() ||
      agsem::SemanticInputAccess::contractsIdentity(
          agsem::semanticInput(semantics)) != contracts.fingerprint()) {
    result.diagnostics.push_back(
        {agsem::Severity::Error,
         "document.identity_mismatch",
         "execution and checked semantics belong to different inputs",
         {},
         {}});
    return result;
  }
  const auto expected = generationSelection(document);
  if (selection.interpreter != expected.interpreter ||
      selection.lowering != expected.lowering || selection.c != expected.c ||
      selection.llvm != expected.llvm) {
    result.diagnostics.push_back(
        {agsem::Severity::Error,
         "document.identity_mismatch",
         "generation selection differs from the document",
         {},
         {}});
    return result;
  }
  auto bound = agsem::bindSemantics(document.semantics(), contracts);
  if (!bound.value) {
    result.diagnostics = std::move(bound.diagnostics);
    return result;
  }
  auto checked = checkCoge(document, *bound.value, contracts);
  result.diagnostics = std::move(checked.diagnostics);
  if (checked.value) {
    return prepareGeneration(document, semantics, selection, contracts,
                             *checked.value);
  }
  return result;
}

auto prepareGeneration(const CogeDocument &document,
                       const agsem::CheckedSemantics &semantics,
                       const GenerationSelection &selection,
                       const agsem::ContractEnvironment &contracts,
                       const CogeValidation &validation)
    -> agsem::Outcome<CheckedGeneration> {
  agsem::Outcome<CheckedGeneration> result;
  if (!validation.execution() ||
      validation.execution()->model().sourceIdentity != document.identity() ||
      validation.execution()->model().contractsIdentity !=
          contracts.fingerprint() ||
      agsem::SemanticInputAccess::identity(agsem::semanticInput(semantics)) !=
          document.identity() ||
      agsem::SemanticInputAccess::contractsIdentity(
          agsem::semanticInput(semantics)) != contracts.fingerprint()) {
    result.diagnostics.push_back(
        {agsem::Severity::Error,
         "document.identity_mismatch",
         "checked execution belongs to different input or contracts",
         {},
         {}});
    return result;
  }
  const auto expected = generationSelection(document);
  if (selection.interpreter != expected.interpreter ||
      selection.lowering != expected.lowering || selection.c != expected.c ||
      selection.llvm != expected.llvm) {
    result.diagnostics.push_back(
        {agsem::Severity::Error,
         "document.identity_mismatch",
         "generation selection differs from the document",
         {},
         {}});
    return result;
  }
  if (validation.completeness().sourceIdentity != document.identity() ||
      validation.completeness().contracts != contracts.identities()) {
    result.diagnostics.push_back(
        {agsem::Severity::Error,
         "document.identity_mismatch",
         "completeness proof belongs to different input or contracts",
         {},
         {}});
    return result;
  }
  result.diagnostics = agsem::generationDiagnostics(validation.completeness());
  if (!result.diagnostics.empty())
    return result;
  try {
    GenerationInput input;
    if (selection.interpreter)
      input.interpreter = validation.execution()->interpreter();
    input.properties = selection.interpreter
                           ? validation.execution()->model().properties
                           : std::nullopt;
    input.lowering = validation.lowering();
    input.backends = validation.backends();
    input.execution = validation.execution();
    result.value = GenerationAccess::prepare(std::move(input), validation);
  } catch (const std::runtime_error &error) {
    result.diagnostics.push_back({agsem::Severity::Error,
                                  "coge.invalid_contract",
                                  error.what(),
                                  {},
                                  {}});
  }
  return result;
}

} // namespace coge
