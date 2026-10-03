#include "ModelBindingsInternal.h"
#include "agas/artifact/Sha256.h"
#include <nlohmann/json.hpp>
namespace agsem {
namespace {
auto type(std::string text) -> TypeRef {
  const auto open = text.find('<');
  if (open == std::string::npos)
    return {std::move(text), {}};
  return {text.substr(0, open),
          {type(text.substr(open + 1, text.size() - open - 2))}};
}
} // namespace
auto standardBindingRequirements(std::string_view family,
                                 std::string_view context, bool cleanup)
    -> std::map<std::string, BindingRequirement> {
  std::map<std::string, BindingRequirement> result;
  const auto field = [&](std::string port, std::string target,
                         std::string owner, std::string spelling,
                         std::string access = "read") {
    result.emplace(
        std::move(port),
        BindingRequirement{BindingTargetKind::Field,
                           std::move(target),
                           {{std::move(owner), type(std::move(spelling))}},
                           {},
                           "field",
                           {},
                           std::move(access)});
  };
  const auto method = [&](std::string port, std::string target,
                          std::vector<std::string> parameters,
                          std::string returned, bool mutates,
                          std::vector<std::string> passing,
                          std::string invocation = "receiver") {
    FunctionContract signature{
        {}, type(std::move(returned)), mutates, std::string{context}};
    for (const auto &parameter : parameters)
      signature.parameters.push_back(type(parameter));
    result.emplace(std::move(port),
                   BindingRequirement{BindingTargetKind::Method,
                                      std::move(target),
                                      {},
                                      signature,
                                      std::move(invocation),
                                      std::move(passing),
                                      mutates ? "mutates" : "pure"});
  };
  const auto operation = [&](std::string port) {
    method(std::move(port), "add_operation", {"Operation", "SourceRange"},
           "OpId", true, {"owned", "value"});
  };
  const auto expression = [&](std::string port) {
    method(std::move(port), "add_expression",
           {"ExpressionKind", "Type", "SourceRange"}, "ExprId", true,
           {"owned", "owned", "value"});
  };
  const auto poison = [&] {
    method("poison", "poisoned_expression", {"ExprId"}, "Bool", false,
           {"value"});
  };
  const auto convert = [&] {
    method("convert", "convert", {"ExprId", "Type"}, "Result<ExprId>", true,
           {"value", "owned"});
  };
  const auto mode = [&](std::string port) {
    result.emplace(std::move(port),
                   BindingRequirement{BindingTargetKind::Mode});
  };
  const auto c = std::string{context};
  if (family == "return") {
    field("active_function", "active_function", c, "Option<FunctionId>");
    field("functions", "functions", c, "List<Function>");
    field("flow", "current_flow", c, "Flow", "write");
    poison();
    convert();
    operation("emit");
    mode("return_ir");
    mode("cleanup_scopes");
    if (cleanup) {
      field("scopes", "scopes", c, "List<Scope>");
      field("variables", "variables", c, "List<Variable>");
      method("destructible", "type_needs_destruction", {"Type"}, "Bool", false,
             {"borrow"});
    }
  } else if (family == "assignment") {
    field("places", "places", c, "List<Place>");
    field("expressions", "expressions", c, "List<Expression>");
    field("flow", "current_flow", c, "Flow", "read_write");
    poison();
    convert();
    operation("emit");
    expression("expression_builder");
    method("read", "read", {"Flow", "PlaceId"}, "Result<Unit>", false,
           {"borrow", "value"});
    method("binary", "binary", {"Operator", "ExprId", "ExprId", "SourceRange"},
           "Result<ExprId>", true, {"value", "value", "value", "value"});
    method("integer_literal", "integer_literal", {"Text", "SourceRange"},
           "Result<ExprId>", true, {"borrow", "value"});
    method("mark", "mark_initialized", {"Flow", "PlaceId"}, "Unit", false,
           {"borrow_mut", "value"});
  } else if (family == "condition") {
    field("expressions", "expressions", c, "List<Expression>");
    poison();
    expression("emit");
  } else if (family == "statement") {
    field("scopes", "scopes", c, "List<Scope>", "read_write");
    operation("emit");
  } else if (family == "flow") {
    field("current", "current_flow", c, "Flow", "read_write");
    field("snapshots", "flow_snapshots", c, "List<Flow>", "read_write");
    method("merge", "merge_flow", {"Flow", "Flow"}, "Flow", false,
           {"borrow", "borrow"});
    method("record_error", "record_error", {"Text", "SourceRange"}, "Unit",
           true, {"borrow", "value"});
    operation("emit");
    result.emplace("error_ir", BindingRequirement{BindingTargetKind::Variant});
  } else if (family == "selection") {
    field("structs", "structs", c, "List<Struct>");
    field("constructors", "constructors", "Struct", "List<FunctionId>");
    field("functions", "functions", c, "List<Function>");
    field("parameters", "parameters", "Function", "List<Type>");
    field("expressions", "expressions", c, "List<Expression>");
    field("expression_type", "ty", "Expression", "Type");
    field("visibility", "visibility", "Function", "Visibility");
    field("variables", "variables", c, "List<Variable>");
    field("variable_type", "ty", "Variable", "Type");
    field("variable_place", "place", "Variable", "PlaceId");
    field("fields", "fields", c, "List<Field>");
    result.at("fields").fields.emplace_back("Struct", type("List<FieldId>"));
    field("field_type", "ty", "Field", "Type");
    field("scopes", "scopes", c, "List<Scope>", "read_write");
    field("scope_operations", "operations", "Scope", "List<OpId>",
          "read_write");
    field("current_flow", "current_flow", c, "Flow", "read_write");
    field("active_initializer", "active_initializer", c, "Option<SymbolId>",
          "write");
    method("accessible", "accessible", {"Visibility", "StructId"}, "Bool",
           false, {"value", "value"});
    method("convertible", "conversion_allowed", {"Type", "Type"}, "Bool", false,
           {"borrow", "borrow"}, "associated");
    method("default_constructible", "default_constructible", {"Type"}, "Bool",
           false, {"borrow"});
    convert();
    operation("add_operation");
    method("mark_initialized", "mark_initialized", {"Flow", "PlaceId"}, "Unit",
           false, {"borrow_mut", "value"});
    result.emplace("construction_ir",
                   BindingRequirement{BindingTargetKind::Variant});
  }
  return result;
}
auto standardBindingDependencies(
    std::string_view family, const std::map<std::string, std::string> &policy,
    const std::map<std::string, std::string> &bindings, bool cleanup)
    -> std::vector<BindingDependency> {
  std::vector<BindingDependency> result;
  const auto depField = [&](std::string owner, std::string name,
                            std::string expected) {
    result.push_back({BindingTargetKind::Field,
                      std::move(owner),
                      std::move(name),
                      {type(expected)}});
  };
  const auto depVariant = [&](std::string owner, std::string name,
                              std::vector<std::string> expected) {
    std::vector<TypeRef> types;
    for (const auto &spelling : expected)
      types.push_back(type(spelling));
    result.push_back({BindingTargetKind::Variant, std::move(owner),
                      std::move(name), std::move(types)});
  };
  if (family == "return") {
    depField("Function", "result", "Type");
    depField("Flow", "reachable", "Bool");
    depVariant("Type", "Void", {});
    std::vector<std::string> payload{"Option<ExprId>"};
    if (bindings.at("return_ir") == "record")
      payload.push_back("List<SymbolId>");
    depVariant("Operation", "Return", payload);
    if (cleanup) {
      depField("Scope", "symbols", "List<SymbolId>");
      depField("Variable", "ty", "Type");
      depField("Variable", "is_self", "Bool");
    }
  } else if (family == "assignment") {
    depField("Place", "ty", "Type");
    depField("Expression", "ty", "Type");
    depVariant("ExpressionKind", "Load", {"PlaceId"});
    depVariant("ExpressionKind", "Convert", {"ExprId", "Type"});
    depVariant("Operation", "Store", {"PlaceId", "ExprId"});
    if (policy.contains("ir") && policy.at("ir") == "StoreAndCompoundStore")
      depVariant("Operation", "CompoundStore", {"PlaceId", "ExprId"});
    for (const auto &name : {"Int", "Float", "Char"})
      depVariant("Type", name, {});
    depVariant("Type", "Array", {"Type", "Index"});
    for (const auto &name : {"Add", "Subtract", "Multiply", "Divide"}) {
      depVariant("Operator", name, {});
      depVariant("AssignmentOp", name, {});
    }
    depVariant("AssignmentOp", "Set", {});
  } else if (family == "condition") {
    depField("Expression", "ty", "Type");
    depField("Expression", "source", "SourceRange");
    depVariant("ExpressionKind", "Convert", {"ExprId", "Type"});
    for (const auto &name :
         {"Bool", "Int", "Float", "Char", "CharPointer", "NullPointer"})
      depVariant("Type", name, {});
  } else if (family == "statement") {
    depField("Scope", "operations", "List<OpId>");
    for (const auto &[port, payload] :
         std::map<std::string, std::vector<std::string>>{
             {"call_ir", {"ExprId"}},
             {"if_ir", {"ExprId", "OpId", "Option<OpId>"}},
             {"while_ir", {"ExprId", "OpId"}},
             {"block_ir", {"ScopeId", "List<OpId>"}},
             {"for_ir", {"ScopeId", "OpId", "ExprId", "OpId", "OpId"}}}) {
      if (!policy.contains(port))
        throw std::runtime_error("missing statement variant " + port);
      depVariant("Operation", policy.at(port), payload);
    }
  } else if (family == "selection") {
    depVariant("Type", "Struct", {"StructId"});
    if (!policy.contains("candidate"))
      throw std::runtime_error("missing selection candidate");
    const auto &candidate = policy.at("candidate");
    depField(candidate, "id", "FunctionId");
    depField(candidate, "conversions", "List<Type>");
    depField(candidate, "rank", "Index");
    depField(candidate, "accessible", "Bool");
  }
  return result;
}
auto standardBindingSchemaHash() -> std::string {
  nlohmann::ordered_json table = nlohmann::ordered_json::array();
  for (const auto family :
       {"assignment", "condition", "flow", "return", "selection", "statement"})
    for (const auto &[port, requirement] :
         standardBindingRequirements(family, "C", true)) {
      nlohmann::ordered_json row{{"family", family},
                                 {"port", port},
                                 {"kind", static_cast<int>(requirement.kind)},
                                 {"default", requirement.target},
                                 {"invocation", requirement.invocation},
                                 {"passing", requirement.argumentPassing},
                                 {"access", requirement.access}};
      row["fields"] = nlohmann::ordered_json::array();
      for (const auto &[owner, type] : requirement.fields)
        row["fields"].push_back({owner, typeSpelling(type)});
      if (requirement.function) {
        row["parameters"] = nlohmann::ordered_json::array();
        for (const auto &type : requirement.function->parameters)
          row["parameters"].push_back(typeSpelling(type));
        row["result"] = typeSpelling(requirement.function->result);
        row["mutates"] = requirement.function->mutates;
      }
      table.push_back(std::move(row));
    }
  auto dependencies = nlohmann::ordered_json::array();
  const std::map<std::string, std::string> policy{
      {"ir", "StoreAndCompoundStore"},
      {"candidate", "$Candidate"},
      {"call_ir", "$Call"},
      {"if_ir", "$If"},
      {"while_ir", "$While"},
      {"block_ir", "$Block"},
      {"for_ir", "$For"}};
  for (const auto family :
       {"assignment", "condition", "flow", "return", "selection", "statement"})
    for (const auto cleanup : {false, true})
      for (const auto shape : {"tuple", "record"}) {
        auto entries = nlohmann::ordered_json::array();
        for (const auto &dependency : standardBindingDependencies(
                 family, policy, {{"return_ir", shape}}, cleanup)) {
          auto types = nlohmann::ordered_json::array();
          for (const auto &type : dependency.types)
            types.push_back(typeSpelling(type));
          entries.push_back({{"kind", static_cast<int>(dependency.kind)},
                             {"owner", dependency.owner},
                             {"target", dependency.target},
                             {"types", types}});
        }
        dependencies.push_back({{"family", family},
                                {"cleanup", cleanup},
                                {"return_shape", shape},
                                {"requirements", entries}});
      }
  return agas::artifact::sha256Hex(nlohmann::ordered_json{
      {"id", "standard_semantic_v1"},
      {"version", "1"},
      {"dependencies", dependencies},
      {"ports", table}}.dump());
}
} // namespace agsem
