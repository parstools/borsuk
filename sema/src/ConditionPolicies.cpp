#include "ConditionPolicies.h"

#include <cctype>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace agsem {
namespace {

using Value = agas::runtime::AstValue;

auto field(const Value &value, std::string_view name) -> const Value & {
  for (std::size_t index = 0; index < value.fieldNames.size(); ++index)
    if (value.fieldNames[index] == name)
      return value.elements[index];
  throw std::runtime_error("condition policy AST is missing field " +
                           std::string{name});
}

auto optional(const Value &value) -> const Value * {
  if (value.kind != agas::runtime::AstValueKind::Optional)
    throw std::runtime_error("condition policy needs an optional field");
  return value.elements.empty() ? nullptr : &value.elements.front();
}

auto spelling(const Value &value) -> std::string {
  if (value.kind != agas::runtime::AstValueKind::Token)
    throw std::runtime_error("condition policy needs a token");
  return value.tokenText;
}

auto failure(const Value &value, std::string_view message)
    -> std::runtime_error {
  return std::runtime_error("condition_policy at bytes " +
                            std::to_string(value.sourceSpan.beginByte) + ".." +
                            std::to_string(value.sourceSpan.endByte) + ": " +
                            std::string{message});
}

void checkIdentifier(const Value &value, const std::string &name) {
  if (name.empty() ||
      !(std::isalpha(static_cast<unsigned char>(name.front())) ||
        name.front() == '_'))
    throw failure(value, "invalid Rust identifier " + name);
  for (const char letter : name)
    if (!(std::isalnum(static_cast<unsigned char>(letter)) || letter == '_'))
      throw failure(value, "invalid Rust identifier " + name);
  static const std::set<std::string> keywords{
      "as",     "async",  "await", "break",  "const",  "continue", "crate",
      "dyn",    "else",   "enum",  "extern", "false",  "fn",       "for",
      "if",     "impl",   "in",    "let",    "loop",   "match",    "mod",
      "move",   "mut",    "pub",   "ref",    "return", "self",     "Self",
      "static", "struct", "super", "trait",  "true",   "type",     "unsafe",
      "use",    "where",  "while"};
  if (keywords.contains(name))
    throw failure(value, "Rust keyword used as identifier: " + name);
}

auto entries(const Value &declaration, const std::set<std::string> &allowed)
    -> std::map<std::string, std::string> {
  std::map<std::string, std::string> result;
  for (const auto &entry : field(declaration, "entries").elements) {
    const auto name = spelling(field(entry, "name"));
    if (!allowed.contains(name))
      throw failure(entry, "unknown field " + name);
    if (!result.emplace(name, spelling(field(entry, "value"))).second)
      throw failure(entry, "duplicate field " + name);
  }
  return result;
}

auto required(const Value &value,
              const std::map<std::string, std::string> &items,
              std::string_view name) -> const std::string & {
  const auto found = items.find(std::string{name});
  if (found == items.end())
    throw failure(value, "missing field " + std::string{name});
  return found->second;
}

void expect(const Value &value, const std::map<std::string, std::string> &items,
            std::string_view name, std::string_view expected) {
  const auto &actual = required(value, items, name);
  if (actual != expected)
    throw failure(value,
                  "unknown value for " + std::string{name} + ": " + actual);
}

void replaceAll(std::string &body, std::string_view key,
                std::string_view replacement) {
  std::size_t offset = 0;
  while ((offset = body.find(key, offset)) != std::string::npos) {
    body.replace(offset, key.size(), replacement);
    offset += replacement.size();
  }
}

} // namespace

auto parseConditionPolicies(const Value &root) -> std::vector<ConditionPolicy> {
  std::vector<ConditionPolicy> result;
  const auto *model = optional(field(root, "model"));
  if (!model)
    return result;
  static const std::set<std::string> allowed{"target", "already", "accepted",
                                             "conversion"};
  std::set<std::string> names;
  for (const auto &declaration : field(*model, "declarations").elements) {
    if (declaration.typeName != "conditionPolicy")
      continue;
    const auto name = spelling(field(declaration, "name"));
    checkIdentifier(declaration, name);
    if (!names.insert(name).second)
      throw failure(declaration, "duplicate policy " + name);
    const auto fields = entries(declaration, allowed);
    expect(declaration, fields, "target", "bool");
    expect(declaration, fields, "already", "identity");
    expect(declaration, fields, "accepted", "numeric_or_pointer");
    expect(declaration, fields, "conversion", "explicit_bool");
    const auto *block = optional(field(declaration, "errors"));
    if (!block || spelling(field(*block, "name")) != "errors")
      throw failure(declaration, "missing errors block");
    static const std::set<std::string> errorNames{"dependent", "incompatible"};
    std::map<std::string, std::string> messages;
    for (const auto &entry : field(*block, "entries").elements) {
      const auto key = spelling(field(entry, "name"));
      if (!errorNames.contains(key))
        throw failure(entry, "unknown error field " + key);
      const auto quoted = spelling(field(entry, "message"));
      if (quoted.size() < 2 || quoted.front() != '"' || quoted.back() != '"' ||
          quoted.find('\\') != std::string::npos)
        throw failure(entry,
                      "error message needs a plain double-quoted string");
      if (!messages.emplace(key, quoted.substr(1, quoted.size() - 2)).second)
        throw failure(entry, "duplicate error field " + key);
    }
    result.push_back({name, required(declaration, messages, "dependent"),
                      required(declaration, messages, "incompatible")});
  }
  return result;
}

auto parseConditionBindings(const Value &root)
    -> std::vector<ConditionBindings> {
  std::vector<ConditionBindings> result;
  const auto *model = optional(field(root, "model"));
  if (!model)
    return result;
  static const std::set<std::string> allowed{"expressions", "poison", "emit"};
  std::set<std::string> names;
  for (const auto &declaration : field(*model, "declarations").elements) {
    if (declaration.typeName != "conditionBindings")
      continue;
    const auto name = spelling(field(declaration, "name"));
    if (!names.insert(name).second)
      throw failure(declaration, "duplicate condition_bindings " + name);
    auto fields = entries(declaration, allowed);
    for (const auto &key : allowed)
      checkIdentifier(declaration, required(declaration, fields, key));
    result.push_back({name, std::move(fields)});
  }
  return result;
}

auto emitConditionPolicyBody(const ConditionPolicy &policy) -> std::string {
  std::ostringstream out;
  out << "    agsem_runtime::apply_condition_policy(ctx, value, "
         "agsem_runtime::ConditionPolicyErrors {\n"
      << "        dependent: \"" << policy.dependent << "\",\n"
      << "        incompatible: \"" << policy.incompatible << "\",\n"
      << "    })\n";
  return out.str();
}

auto emitConditionModelRust(const ConditionBindings &bindings,
                            std::string_view contextType) -> std::string {
  std::string body =
      R"(impl agsem_runtime::ConditionPolicyContext for @CONTEXT@ {
    type Expression = ExprId;
    type Type = crate::Type;
    type Source = crate::SourceRange;

    fn condition_is_poisoned(&self, value: ExprId) -> bool {
        self.@POISON@(value)
    }
    fn condition_type(&self, value: ExprId) -> crate::Type {
        self.@EXPRESSIONS@[value.0].ty.clone()
    }
    fn condition_is_bool(&self, ty: &crate::Type) -> bool {
        *ty == crate::Type::Bool
    }
    fn condition_convertible(&self, ty: &crate::Type) -> bool {
        matches!(ty, crate::Type::Int | crate::Type::Float | crate::Type::Char
            | crate::Type::CharPointer | crate::Type::NullPointer)
    }
    fn condition_source(&self, value: ExprId) -> crate::SourceRange {
        self.@EXPRESSIONS@[value.0].source
    }
    fn condition_convert_to_bool(&mut self, value: ExprId, source: crate::SourceRange) -> ExprId {
        self.@EMIT@(
            crate::model::ExpressionKind::Convert { value, target: crate::Type::Bool },
            crate::Type::Bool, source)
    }
}

)";
  replaceAll(body, "@CONTEXT@", contextType);
  for (const auto &[key, value] : bindings.fields) {
    std::string placeholder = "@";
    for (const char letter : key)
      placeholder +=
          static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
    placeholder += "@";
    replaceAll(body, placeholder, value);
  }
  return body;
}

} // namespace agsem
