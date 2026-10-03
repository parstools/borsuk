#include "SemanticValidation.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace agsem {
namespace {

auto field(const agas::runtime::AstValue &value, std::string_view name)
    -> const agas::runtime::AstValue * {
  for (std::size_t index = 0; index < value.fieldNames.size(); ++index)
    if (value.fieldNames[index] == name)
      return &value.elements[index];
  return nullptr;
}

void collectDefinitions(
    const agas::runtime::AstValue &value,
    std::map<std::string, const agas::runtime::AstValue *> &functions,
    std::set<std::string> &leaves) {
  if (value.typeName == "functionDeclaration" ||
      value.typeName == "queryDeclaration") {
    const auto *name = field(value, "name");
    const auto *body =
        field(value, value.typeName == "queryDeclaration" ? "value" : "body");
    if (!name || !body || !functions.emplace(name->tokenText, body).second)
      throw std::runtime_error("duplicate or invalid function declaration: " +
                               (name ? name->tokenText : std::string{}));
  } else if (value.typeName == "intrinsicDeclaration" ||
             value.typeName == "recordDeclaration" ||
             value.typeName == "entityDeclaration") {
    const auto *name = field(value, "name");
    if (name)
      leaves.insert(name->tokenText);
  }
  for (const auto &element : value.elements)
    collectDefinitions(element, functions, leaves);
}

void collectDirectCalls(const agas::runtime::AstValue &value,
                        std::set<std::string> &names) {
  if (value.typeName == "actionUnary" && value.elements.size() == 3) {
    const auto &atom = value.elements[1];
    const auto &suffixes = value.elements[2];
    if (atom.kind == agas::runtime::AstValueKind::Token &&
        !suffixes.elements.empty() &&
        suffixes.elements.front().typeName == "actionPostfix" &&
        suffixes.elements.front().variantName == "Call")
      names.insert(atom.tokenText);
  }
  for (const auto &element : value.elements)
    collectDirectCalls(element, names);
}

} // namespace

void checkFunctionCycles(const agas::runtime::AstValue &root) {
  std::map<std::string, const agas::runtime::AstValue *> functions;
  std::set<std::string> leaves;
  collectDefinitions(root, functions, leaves);
  for (const auto &[name, body] : functions) {
    static_cast<void>(body);
    if (leaves.contains(name))
      throw std::runtime_error(
          "function name conflicts with intrinsic or constructor: " + name);
  }
  std::map<std::string, std::set<std::string>> edges;
  for (const auto &[name, body] : functions) {
    auto &outgoing = edges[name];
    collectDirectCalls(*body, outgoing);
    for (auto it = outgoing.begin(); it != outgoing.end();) {
      if (leaves.contains(*it)) {
        it = outgoing.erase(it);
      } else if (functions.contains(*it)) {
        ++it;
      } else {
        throw std::runtime_error("unresolved call in function " + name + ": " +
                                 *it);
      }
    }
  }

  std::map<std::string, int> state;
  std::vector<std::string> stack;
  std::function<void(const std::string &)> visit =
      [&](const std::string &name) {
        state[name] = 1;
        stack.push_back(name);
        for (const auto &callee : edges.at(name)) {
          if (state[callee] == 1) {
            std::string cycle;
            const auto first = std::find(stack.begin(), stack.end(), callee);
            for (auto it = first; it != stack.end(); ++it)
              cycle += (cycle.empty() ? "" : " -> ") + *it;
            throw std::runtime_error("function call cycle: " + cycle + " -> " +
                                     callee);
          }
          if (state[callee] == 0)
            visit(callee);
        }
        stack.pop_back();
        state[name] = 2;
      };
  for (const auto &[name, body] : functions) {
    static_cast<void>(body);
    if (state[name] == 0)
      visit(name);
  }
}

} // namespace agsem
