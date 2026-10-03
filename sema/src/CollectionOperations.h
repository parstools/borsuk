#pragma once

#include "AstAccess.h"
#include <optional>
#include <vector>

namespace agsem {
// List is a builtin namespace; local bindings take precedence at each caller.
struct CollectionOperation {
  std::string name;
  std::vector<const ast::Value *> arguments;
};

inline auto collectionOperation(const ast::Value &value)
    -> std::optional<CollectionOperation> {
  using namespace ast;
  if (value.typeName != "actionUnary" ||
      !field(value, "prefixes").elements.empty() ||
      token(field(value, "atom")) != "List")
    return {};
  const auto &suffixes = field(value, "suffixes").elements;
  if (suffixes.size() != 2 || suffixes[0].variantName != "Field" ||
      suffixes[1].variantName != "Call")
    return {};
  CollectionOperation result{token(field(suffixes[0], "name")), {}};
  if (const auto *arguments = optional(field(suffixes[1], "arguments"))) {
    if (arguments->typeName == "argumentList") {
      result.arguments.push_back(&field(*arguments, "first"));
      for (const auto &argument : field(*arguments, "rest").elements)
        result.arguments.push_back(&argument);
    } else
      result.arguments.push_back(arguments);
  }
  for (auto *&argument : result.arguments)
    if (argument->typeName == "actionArgument") {
      if (optional(field(*argument, "named")))
        throw std::runtime_error("collection operations require positional operands");
      argument = &field(*argument, "value");
    }
  const auto count = result.name == "append" ? 2u : 1u;
  if (result.name != "empty" && result.name != "single" && result.name != "append")
    throw std::runtime_error("unknown collection operation: List." + result.name);
  if (result.arguments.size() != count)
    throw std::runtime_error("wrong argument count for List." + result.name);
  return result;
}

// An empty collection takes a type name, never a runtime value or expression.
inline auto collectionElementName(const ast::Value &value) -> std::string {
  using namespace ast;
  if (value.kind == Kind::Token)
    return value.tokenText;
  if (value.typeName == "actionExpression" &&
      field(value, "rest").elements.empty() && !optional(field(value, "choice")))
    return collectionElementName(field(value, "first"));
  if (value.typeName == "actionUnary" &&
      field(value, "prefixes").elements.empty() &&
      field(value, "suffixes").elements.empty())
    return collectionElementName(field(value, "atom"));
  throw std::runtime_error("List.empty requires an explicit element type name");
}
} // namespace agsem
