#include "AssignmentPolicies.h"

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
  throw std::runtime_error("assignment policy AST is missing field " +
                           std::string{name});
}

auto optional(const Value &value) -> const Value * {
  if (value.kind != agas::runtime::AstValueKind::Optional)
    throw std::runtime_error("assignment policy needs an optional field");
  return value.elements.empty() ? nullptr : &value.elements.front();
}

auto spelling(const Value &value) -> std::string {
  if (value.kind != agas::runtime::AstValueKind::Token)
    throw std::runtime_error("assignment policy needs a token");
  return value.tokenText;
}

auto failure(const Value &value, std::string_view message)
    -> std::runtime_error {
  return std::runtime_error("assignment_policy at bytes " +
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

auto messages(const Value &declaration) -> std::map<std::string, std::string> {
  const auto *block = optional(field(declaration, "errors"));
  if (!block || spelling(field(*block, "name")) != "errors")
    throw failure(declaration, "missing errors block");
  static const std::set<std::string> allowed{
      "invalid_target", "invalid_increment", "dependent", "incompatible"};
  std::map<std::string, std::string> result;
  for (const auto &entry : field(*block, "entries").elements) {
    const auto name = spelling(field(entry, "name"));
    if (!allowed.contains(name))
      throw failure(entry, "unknown error field " + name);
    const auto quoted = spelling(field(entry, "message"));
    if (quoted.size() < 2 || quoted.front() != '"' || quoted.back() != '"' ||
        quoted.find('\\') != std::string::npos)
      throw failure(entry, "error message needs a plain double-quoted string");
    if (!result.emplace(name, quoted.substr(1, quoted.size() - 2)).second)
      throw failure(entry, "duplicate error field " + name);
  }
  for (const auto &name : allowed)
    required(declaration, result, name);
  return result;
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

auto parseAssignmentPolicies(const Value &root)
    -> std::vector<AssignmentPolicy> {
  std::vector<AssignmentPolicy> result;
  const auto *model = optional(field(root, "model"));
  if (!model)
    return result;
  static const std::set<std::string> allowed{"check",
                                             "increment",
                                             "increment_operand",
                                             "increment_step",
                                             "increment_operation",
                                             "target",
                                             "simple",
                                             "compound",
                                             "result",
                                             "flow",
                                             "evaluation",
                                             "ir"};
  std::set<std::string> names;
  for (const auto &declaration : field(*model, "declarations").elements) {
    if (declaration.typeName != "assignmentPolicy")
      continue;
    const auto name = spelling(field(declaration, "name"));
    checkIdentifier(declaration, name);
    if (!names.insert(name).second)
      throw failure(declaration, "duplicate policy " + name);
    const auto fields = entries(declaration, allowed);
    expect(declaration, fields, "target", "non_array");
    expect(declaration, fields, "simple", "implicit_conversion");
    expect(declaration, fields, "compound", "read_modify_write");
    expect(declaration, fields, "result", "exact_or_numeric_conversion");
    expect(declaration, fields, "flow", "initialized_after_success");
    expect(declaration, fields, "increment_operand", "numeric");
    expect(declaration, fields, "increment_step", "integer_one");
    expect(declaration, fields, "increment_operation", "add_or_subtract");
    const auto &evaluation = required(declaration, fields, "evaluation");
    if (evaluation != "static_place" && evaluation != "target_once_before_rhs")
      throw failure(declaration, "unknown value for evaluation: " + evaluation);
    const auto &ir = required(declaration, fields, "ir");
    if (ir != "Store" && ir != "StoreAndCompoundStore")
      throw failure(declaration, "unknown value for ir: " + ir);
    if ((ir == "Store") != (evaluation == "static_place"))
      throw failure(declaration, "ir and evaluation disagree");
    const auto &check = required(declaration, fields, "check");
    const auto &increment = required(declaration, fields, "increment");
    checkIdentifier(declaration, check);
    checkIdentifier(declaration, increment);
    if (check == name || increment == name || increment == check)
      throw failure(declaration,
                    "assignment policy function names must differ");
    const auto errors = messages(declaration);
    result.push_back({name, check, increment, ir == "StoreAndCompoundStore",
                      errors.at("invalid_target"),
                      errors.at("invalid_increment"), errors.at("dependent"),
                      errors.at("incompatible")});
  }
  return result;
}

auto parseAssignmentBindings(const Value &root)
    -> std::vector<AssignmentBindings> {
  std::vector<AssignmentBindings> result;
  const auto *model = optional(field(root, "model"));
  if (!model)
    return result;
  static const std::set<std::string> allowed{
      "places",         "expressions", "flow", "poison", "read",
      "convert",        "binary",      "emit", "mark",   "expression_builder",
      "integer_literal"};
  std::set<std::string> names;
  for (const auto &declaration : field(*model, "declarations").elements) {
    if (declaration.typeName != "assignmentBindings")
      continue;
    const auto name = spelling(field(declaration, "name"));
    if (!names.insert(name).second)
      throw failure(declaration, "duplicate assignment_bindings " + name);
    auto fields = entries(declaration, allowed);
    for (const auto &key : allowed)
      checkIdentifier(declaration, required(declaration, fields, key));
    result.push_back({name, std::move(fields)});
  }
  return result;
}

auto emitAssignmentPolicyBody(const AssignmentPolicy &policy,
                              std::string_view functionName) -> std::string {
  std::ostringstream out;
  if (functionName == policy.checkName)
    out << "    agsem_runtime::check_assignment_target(ctx, place, operator, ";
  else if (functionName == policy.incrementName)
    out << "    agsem_runtime::apply_increment_policy(ctx, place, increment, "
           "source, ";
  else
    out << "    agsem_runtime::apply_assignment_policy(ctx, place, operator, "
           "value, source, ";
  out << "agsem_runtime::AssignmentPolicyErrors {\n"
      << "        invalid_target: \"" << policy.invalidTarget << "\",\n"
      << "        invalid_increment: \"" << policy.invalidIncrement << "\",\n"
      << "        dependent: \"" << policy.dependent << "\",\n"
      << "        incompatible: \"" << policy.incompatible << "\",\n"
      << "    })\n";
  return out.str();
}

auto emitAssignmentModelRust(const AssignmentPolicy &policy,
                             const AssignmentBindings &bindings,
                             std::string_view contextType) -> std::string {
  std::string body =
      R"(impl agsem_runtime::AssignmentPolicyContext for @CONTEXT@ {
    type Place = PlaceId;
    type Assignment = AssignmentOp;
    type Expression = ExprId;
    type Type = crate::Type;
    type Source = SourceRange;
    type Operation = OpId;

    fn assignment_is_set(&self, operator: AssignmentOp) -> bool {
        operator == AssignmentOp::Set
    }
    fn assignment_target_type(&self, place: PlaceId) -> crate::Type {
        self.@PLACES@[place.0].ty.clone()
    }
    fn assignment_target_is_array(&self, ty: &crate::Type) -> bool {
        matches!(ty, crate::Type::Array(_, _))
    }
    fn assignment_incrementable(&self, ty: &crate::Type) -> bool {
        matches!(ty, crate::Type::Int | crate::Type::Float | crate::Type::Char)
    }
    fn assignment_increment_operator(&self, increment: bool) -> AssignmentOp {
        if increment { AssignmentOp::Add } else { AssignmentOp::Subtract }
    }
    fn assignment_one(&mut self, source: SourceRange) -> Result<ExprId, &'static str> {
        self.@INTEGER_LITERAL@("1", source)
    }
    fn assignment_read_target(&self, place: PlaceId) -> Result<(), &'static str> {
        self.@READ@(&self.@FLOW@, place)
    }
    fn assignment_value_is_poisoned(&self, value: ExprId) -> bool {
        self.@POISON@(value)
    }
    fn assignment_convert(&mut self, value: ExprId, target: crate::Type) -> Result<ExprId, ()> {
        self.@CONVERT@(value, target).map_err(|_| ())
    }
    fn assignment_load(&mut self, place: PlaceId, target: crate::Type, source: SourceRange) -> ExprId {
        self.@EXPRESSION_BUILDER@(crate::model::ExpressionKind::Load(place), target, source)
    }
    fn assignment_binary(&mut self, operation: AssignmentOp, left: ExprId, right: ExprId, source: SourceRange) -> Result<ExprId, ()> {
        let operator = match operation {
            AssignmentOp::Add => crate::model::Operator::Add,
            AssignmentOp::Subtract => crate::model::Operator::Subtract,
            AssignmentOp::Multiply => crate::model::Operator::Multiply,
            AssignmentOp::Divide => crate::model::Operator::Divide,
            AssignmentOp::Set => unreachable!(),
        };
        self.@BINARY@(operator, left, right, source).map_err(|_| ())
    }
    fn assignment_expression_type(&self, value: ExprId) -> crate::Type {
        self.@EXPRESSIONS@[value.0].ty.clone()
    }
    fn assignment_convert_result(&mut self, value: ExprId, target: crate::Type, source: SourceRange) -> Result<ExprId, ()> {
        let result_type = &self.@EXPRESSIONS@[value.0].ty;
        if !matches!(result_type, crate::Type::Int | crate::Type::Float | crate::Type::Char)
            || !matches!(target, crate::Type::Int | crate::Type::Float | crate::Type::Char) {
            return Err(());
        }
        Ok(self.@EXPRESSION_BUILDER@(
            crate::model::ExpressionKind::Convert { value, target: target.clone() },
            target, source))
    }
    fn assignment_commit(&mut self, place: PlaceId, value: ExprId, compound: bool, source: SourceRange) -> OpId {
        let mut flow = self.@FLOW@.clone();
        self.@MARK@(&mut flow, place);
        self.@FLOW@ = flow;
        let operation = @STORE@;
        self.@EMIT@(operation, source)
    }
}

)";
  const auto &f = bindings.fields;
  replaceAll(body, "@CONTEXT@", contextType);
  for (const auto &[key, value] : f) {
    std::string placeholder = "@";
    for (const char letter : key)
      placeholder +=
          static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
    placeholder += "@";
    replaceAll(body, placeholder, value);
  }
  replaceAll(
      body, "@STORE@",
      policy.compoundStore
          ? "if compound { crate::model::Operation::CompoundStore { place, "
            "value } } else { crate::model::Operation::Store { place, value } }"
          : "{ let _ = compound; crate::model::Operation::Store { place, value "
            "} }");
  return body;
}

} // namespace agsem
