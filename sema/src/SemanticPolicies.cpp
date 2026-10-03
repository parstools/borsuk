#include "SemanticPolicies.h"

#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace agsem {
namespace {

using Value = agas::runtime::AstValue;

auto field(const Value &value, std::string_view name) -> const Value & {
  for (std::size_t index = 0; index < value.fieldNames.size(); ++index)
    if (value.fieldNames[index] == name)
      return value.elements[index];
  throw std::runtime_error("return policy AST is missing field " +
                           std::string{name});
}

auto optional(const Value &value) -> const Value * {
  if (value.kind != agas::runtime::AstValueKind::Optional)
    throw std::runtime_error("return policy AST needs an optional field");
  return value.elements.empty() ? nullptr : &value.elements.front();
}

auto spelling(const Value &value) -> std::string {
  if (value.kind != agas::runtime::AstValueKind::Token)
    throw std::runtime_error("return policy AST needs an identifier");
  return value.tokenText;
}

auto failure(const Value &value, std::string_view message)
    -> std::runtime_error {
  return std::runtime_error("return_policy at bytes " +
                            std::to_string(value.sourceSpan.beginByte) + ".." +
                            std::to_string(value.sourceSpan.endByte) + ": " +
                            std::string{message});
}

auto valueOf(const std::map<std::string, std::string> &items,
             const Value &policy, std::string_view fieldName,
             std::string_view expected) -> std::string {
  const auto found = items.find(std::string{fieldName});
  if (found == items.end())
    throw failure(policy, "missing field " + std::string{fieldName});
  if (found->second != expected)
    throw failure(policy, "unknown value for " + std::string{fieldName} + ": " +
                              found->second);
  return found->second;
}

auto parsePolicy(const Value &policy) -> ReturnPolicy {
  const auto name = spelling(field(policy, "name"));
  static const std::set<std::string> fields{
      "target", "value", "conversion", "cleanup", "evaluation", "flow", "ir"};
  static const std::set<std::string> errors{"missing_target", "wrong_presence",
                                            "failed_conversion"};
  std::map<std::string, std::string> entries;
  for (const auto &entry : field(policy, "entries").elements) {
    const auto key = spelling(field(entry, "name"));
    if (!fields.contains(key))
      throw failure(entry, "unknown field " + key);
    if (!entries.emplace(key, spelling(field(entry, "value"))).second)
      throw failure(entry, "duplicate field " + key);
  }
  const auto *errorBlock = optional(field(policy, "errors"));
  if (!errorBlock)
    throw failure(policy, "missing errors block");
  if (spelling(field(*errorBlock, "name")) != "errors")
    throw failure(*errorBlock, "expected errors block");
  std::map<std::string, std::string> messages;
  for (const auto &entry : field(*errorBlock, "entries").elements) {
    const auto key = spelling(field(entry, "name"));
    if (!errors.contains(key))
      throw failure(entry, "unknown error field " + key);
    const auto quoted = spelling(field(entry, "message"));
    if (quoted.size() < 2 || quoted.front() != '"' || quoted.back() != '"' ||
        quoted.find('\\') != std::string::npos)
      throw failure(entry, "error message needs a plain double-quoted string");
    if (!messages.emplace(key, quoted.substr(1, quoted.size() - 2)).second)
      throw failure(entry, "duplicate error field " + key);
  }
  valueOf(entries, policy, "target", "enclosing_function");
  valueOf(entries, policy, "value", "absent_for_void_otherwise_required");
  valueOf(entries, policy, "conversion", "implicit_conversion");
  const auto cleanup = entries.find("cleanup");
  if (cleanup == entries.end())
    throw failure(policy, "missing field cleanup");
  if (cleanup->second != "exited_automatic_lifetimes" &&
      cleanup->second != "no_cleanup")
    throw failure(policy, "unknown value for cleanup: " + cleanup->second);
  valueOf(entries, policy, "evaluation", "capture_value_before_cleanup");
  valueOf(entries, policy, "flow", "unreachable_after_success");
  valueOf(entries, policy, "ir", "Return");
  for (const auto &key : errors)
    if (!messages.contains(key))
      throw failure(policy, "missing error field " + key);
  return {name,
          ReturnTarget::EnclosingFunction,
          ReturnValueRule::AbsentForVoidOtherwiseRequired,
          ReturnConversion::ImplicitConversion,
          cleanup->second == "no_cleanup"
              ? ReturnCleanup::NoCleanup
              : ReturnCleanup::ExitedAutomaticLifetimes,
          ReturnEvaluation::CaptureValueBeforeCleanup,
          ReturnFlow::UnreachableAfterSuccess,
          ReturnIr::Return,
          messages.at("missing_target"),
          messages.at("wrong_presence"),
          messages.at("failed_conversion")};
}

} // namespace

auto parseReturnPolicies(const Value &root) -> std::vector<ReturnPolicy> {
  std::vector<ReturnPolicy> policies;
  const auto *model = optional(field(root, "model"));
  if (!model)
    return policies;
  std::set<std::string> names;
  for (const auto &declaration : field(*model, "declarations").elements) {
    if (declaration.typeName != "returnPolicy")
      continue;
    auto policy = parsePolicy(declaration);
    if (!names.insert(policy.name).second)
      throw failure(declaration, "duplicate policy " + policy.name);
    policies.push_back(std::move(policy));
  }
  return policies;
}

auto parseReturnModelBindings(const Value &root)
    -> std::vector<ReturnModelBindings> {
  std::vector<ReturnModelBindings> result;
  const auto *model = optional(field(root, "model"));
  if (!model)
    return result;
  static const std::set<std::string> common{
      "active_function", "functions", "flow",      "poison",
      "convert",         "emit",      "return_ir", "cleanup_scopes"};
  static const std::set<std::string> cleanup{"scopes", "variables",
                                             "destructible"};
  std::set<std::string> names;
  for (const auto &declaration : field(*model, "declarations").elements) {
    if (declaration.typeName != "modelBindings")
      continue;
    ReturnModelBindings binding{spelling(field(declaration, "name")), {}};
    if (!names.insert(binding.policyName).second)
      throw failure(declaration,
                    "duplicate model_bindings " + binding.policyName);
    for (const auto &entry : field(declaration, "entries").elements) {
      const auto key = spelling(field(entry, "name"));
      const auto value = spelling(field(entry, "value"));
      if (!common.contains(key) && !cleanup.contains(key))
        throw failure(entry, "unknown model binding " + key);
      if (!binding.fields.emplace(key, value).second)
        throw failure(entry, "duplicate model binding " + key);
    }
    for (const auto &key : common)
      if (!binding.fields.contains(key))
        throw failure(declaration, "missing model binding " + key);
    const auto &shape = binding.fields.at("return_ir");
    if (shape != "tuple" && shape != "record")
      throw failure(declaration, "return_ir must be tuple or record");
    const auto &scopes = binding.fields.at("cleanup_scopes");
    if (scopes == "no_scopes") {
      for (const auto &key : cleanup)
        if (binding.fields.contains(key))
          throw failure(declaration, "unused model binding " + key);
    } else {
      for (const auto &key : cleanup)
        if (!binding.fields.contains(key))
          throw failure(declaration, "missing model binding " + key);
    }
    result.push_back(std::move(binding));
  }
  return result;
}

auto emitReturnPolicyBody(const ReturnPolicy &policy) -> std::string {
  std::ostringstream output;
  output << "    agsem_runtime::apply_return_policy(\n"
            "        ctx, value, source,\n"
            "        agsem_runtime::ReturnPolicyErrors {\n"
         << "            missing_target: \"" << policy.missingTarget << "\",\n"
         << "            wrong_presence: \"" << policy.wrongPresence << "\",\n"
         << "            failed_conversion: \"" << policy.failedConversion
         << "\",\n"
            "        },\n"
            "    )\n";
  return output.str();
}

auto emitReturnModelRust(const ReturnModelBindings &binding,
                         std::string_view contextType) -> std::string {
  const auto &f = binding.fields;
  const auto member = [&](std::string_view name) {
    return f.at(std::string{name});
  };
  std::ostringstream out;
  out << "impl agsem_runtime::ReturnPolicyContext for " << contextType
      << " {\n"
         "    type Expression = ExprId;\n"
         "    type Type = crate::Type;\n"
         "    type Operation = OpId;\n"
         "    type Source = SourceRange;\n"
         "    type Scope = crate::ScopeId;\n"
         "    type Symbol = crate::SymbolId;\n\n"
         "    fn return_value_is_poisoned(&self, value: ExprId) -> bool {\n"
      << "        self." << member("poison")
      << "(value)\n"
         "    }\n"
         "    fn return_target_type(&self) -> Option<crate::Type> {\n"
      << "        self." << member("active_function") << ".map(|id| self."
      << member("functions")
      << "[id.0].result.clone())\n"
         "    }\n"
         "    fn return_type_is_void(&self, ty: &crate::Type) -> bool {\n"
         "        *ty == crate::Type::Void\n"
         "    }\n"
         "    fn convert_return_value(&mut self, value: ExprId, ty: "
         "crate::Type) -> Result<ExprId, ()> {\n"
      << "        self." << member("convert")
      << "(value, ty).map_err(|_| ())\n"
         "    }\n";
  if (member("cleanup_scopes") == "no_scopes") {
    out << "    fn exit_scopes(&self) -> &[crate::ScopeId] { &[] }\n"
           "    fn scope_symbols(&self, _scope: crate::ScopeId) -> "
           "&[crate::SymbolId] { &[] }\n"
           "    fn destruct_on_return(&self, _symbol: crate::SymbolId) -> bool "
           "{ false }\n";
  } else {
    out << "    fn exit_scopes(&self) -> &[crate::ScopeId] { &self."
        << member("cleanup_scopes")
        << " }\n"
           "    fn scope_symbols(&self, scope: crate::ScopeId) -> "
           "&[crate::SymbolId] { &self."
        << member("scopes")
        << "[scope.0].symbols }\n"
           "    fn destruct_on_return(&self, symbol: crate::SymbolId) -> bool "
           "{\n"
           "        let variable = &self."
        << member("variables")
        << "[symbol.0];\n"
           "        !variable.is_self && self."
        << member("destructible")
        << "(&variable.ty)\n"
           "    }\n";
  }
  out << "    fn commit_return(&mut self, value: Option<ExprId>, cleanup: "
         "Vec<crate::SymbolId>, source: SourceRange) -> OpId {\n"
         "        self."
      << member("flow") << ".reachable = false;\n";
  if (member("return_ir") == "tuple")
    out << "        let _ = cleanup;\n"
           "        self."
        << member("emit")
        << "(crate::model::Operation::Return(value), source)\n";
  else
    out << "        self." << member("emit")
        << "(crate::model::Operation::Return { value, cleanup }, source)\n";
  out << "    }\n}\n\n";
  return out.str();
}

} // namespace agsem
