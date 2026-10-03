#include "ExecutionModelInternal.h"
#include "agas/artifact/Sha256.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
namespace coge {
namespace {
using J = nlohmann::ordered_json;
auto type(const ExecutionType &t) -> J {
  J result{{"name", t.name}, {"arguments", J::array()}};
  for (const auto &a : t.arguments)
    result["arguments"].push_back(type(a));
  return result;
}
auto ref(const agsem::TypeRef &t) -> J {
  J result{{"name", t.name}, {"arguments", J::array()}};
  for (const auto &a : t.arguments)
    result["arguments"].push_back(ref(a));
  return result;
}
auto location(const agsem::SourceLocation &s) -> J {
  return {{"file", s.source}, {"begin", s.beginByte}, {"end", s.endByte}};
}
auto origin(const ExecutionOrigin &s) -> J {
  return {
      {"kind", s.kind}, {"source", location(s.source)}, {"member", s.member}};
}
auto signature(const ExecutionFunctionSignature &f) -> J {
  J result{{"name", f.name},
           {"mutates", f.mutates},
           {"result", type(f.result)},
           {"parameters", J::array()}};
  for (const auto &p : f.parameters)
    result["parameters"].push_back(
        {{"name", p.name}, {"type", type(p.type)}, {"used", p.used}});
  return result;
}
auto pattern(const std::optional<ExecutionPattern> &p,
             const std::string &legacy) -> J {
  if (!p)
    return {{"legacy", legacy}};
  J result{{"owner", p->owner},
           {"variant", p->variant},
           {"form", static_cast<int>(p->form)},
           {"bindings", J::array()}};
  for (const auto &b : p->bindings)
    result["bindings"].push_back(
        {{"field", b.field}, {"name", b.name}, {"used", b.used}});
  return result;
}
auto expression(const ExecutionExpression &e) -> J {
  static const char *names[] = {
      "state",   "none",         "unit",        "boolean",
      "text",    "number",       "binding",     "binary",
      "negate",  "member_field", "member_call", "runtime_call",
      "variant", "constructor",  "capture",     "direct_call"};
  J result{{"kind", names[static_cast<int>(e.kind)]},
           {"value", e.value},
           {"detail", e.detail},
           {"propagate", e.propagate},
           {"option_constructor", e.optionConstructor},
           {"children", J::array()}};
  for (const auto &c : e.children)
    result["children"].push_back(expression(c));
  return result;
}
auto statements(const std::vector<ExecutionStatement> &body) -> J {
  static const char *names[] = {"let",     "expression", "return",
                                "error",   "assignment", "if",
                                "foreach", "while",      "match"};
  J result = J::array();
  for (const auto &s : body) {
    J value{{"kind", names[static_cast<int>(s.kind)]},
            {"binding", s.binding},
            {"mutable", s.mutableBinding},
            {"wrap_return", s.wrapReturn},
            {"has_else", s.hasElse},
            {"value", s.value ? expression(*s.value) : J(nullptr)},
            {"extra", s.extra ? expression(*s.extra) : J(nullptr)},
            {"then", statements(s.thenBranch)},
            {"else", statements(s.elseBranch)},
            {"arms", J::array()}};
    for (const auto &a : s.arms)
      value["arms"].push_back(
          {{"pattern", pattern(a.structuredPattern, a.pattern)},
           {"body", statements(a.body)}});
    result.push_back(std::move(value));
  }
  return result;
}
auto body(const ExecutionBody &b) -> J {
  return {{"kind", static_cast<int>(b.kind)},
          {"statements", statements(b.statements)}};
}
auto propertyExpression(const PropertyExpression &p) -> J {
  J result{{"kind", static_cast<int>(p.expressionKind)},
           {"value_kind", static_cast<int>(p.valueKind)},
           {"value", p.value},
           {"children", J::array()}};
  for (const auto &c : p.children)
    result["children"].push_back(propertyExpression(c));
  return result;
}
auto serialize(const ExpandedExecutionModel &m, bool origins) -> J {
  J result{{"context", m.contextType},   {"state", nullptr},
           {"declarations", J::array()}, {"functions", J::array()},
           {"intrinsics", J::array()},   {"execute", J::array()},
           {"evaluate", J::array()}};
  if (m.declarations) {
    if (const auto &s = m.declarations->runtimeState)
      result["state"] = {{"name", s->name}, {"type", type(s->type)}};
    for (const auto &d : m.declarations->declarations) {
      J value{{"name", d.name},
              {"kind", static_cast<int>(d.kind)},
              {"fields", J::array()},
              {"variants", J::array()}};
      for (const auto &f : d.fields)
        value["fields"].push_back({{"name", f.name}, {"type", type(f.type)}});
      for (const auto &v : d.variants) {
        J payload = J::array();
        for (const auto &t : v.payload)
          payload.push_back(type(t));
        value["variants"].push_back({{"name", v.name}, {"payload", payload}});
      }
      result["declarations"].push_back(value);
    }
  }
  for (const auto &f : m.functions) {
    J value{{"signature", signature(f.signature)}, {"body", body(f.body)}};
    if (origins)
      value["origin"] = origin(f.origin);
    result["functions"].push_back(value);
  }
  for (const auto &f : m.intrinsics) {
    J value{{"signature", signature(f.signature)}};
    if (origins)
      value["origin"] = origin(f.origin);
    result["intrinsics"].push_back(value);
  }
  const auto handlers = [&](const auto &items, const char *key) {
    for (const auto &a : items) {
      J value{{"pattern", pattern(a.arm.structuredPattern, a.arm.pattern)},
              {"body", body(a.arm.body)}};
      if (origins)
        value["origin"] = origin(a.origin);
      result[key].push_back(value);
    }
  };
  handlers(m.operations, "execute");
  handlers(m.expressions, "evaluate");
  result["properties"] = nullptr;
  if (m.properties) {
    J properties{{"boundary_cases", m.properties->boundaryCases},
                 {"checks", J::array()}};
    for (const auto &p : m.properties->checks)
      properties["checks"].push_back(
          {{"bindings", p.bindings},
           {"function", p.function},
           {"arguments", p.arguments},
           {"source", p.source},
           {"constraint",
            p.constraint ? propertyExpression(*p.constraint) : J(nullptr)},
           {"fits_i32", p.fitsI32},
           {"negated_fits", p.negatedFits},
           {"expected",
            p.expected ? propertyExpression(*p.expected) : J(nullptr)},
           {"error",
            p.errorMessageLiteral ? J(*p.errorMessageLiteral) : J(nullptr)}});
    result["properties"] = std::move(properties);
  }
  return result;
}
auto sameSignature(const ExecutionFunctionSignature &a,
                   const ExecutionFunctionSignature &b) -> bool {
  if (a.mutates != b.mutates || type(a.result) != type(b.result) ||
      a.parameters.size() != b.parameters.size())
    return false;
  for (std::size_t i = 0; i < a.parameters.size(); ++i)
    if (type(a.parameters[i].type) != type(b.parameters[i].type))
      return false;
  return true;
}
} // namespace
auto serializeExecutionModel(const ExpandedExecutionModel &model, bool origins)
    -> std::string {
  return serialize(model, origins).dump();
}
auto serializeProfileRequirements(const ExecutionProfileSpec &spec)
    -> std::string {
  J result = J::array();
  result.push_back({{"state_name", "runtime"}, {"state_type", "Runtime"}});
  for (const auto &r : spec.requirements) {
    J value{{"kind", r.contract.kind},
            {"owner", r.contract.owner},
            {"name", r.contract.name},
            {"either_effect", r.eitherEffect},
            {"local_body", r.localBody}};
    if (r.contract.type)
      value["type"] = ref(*r.contract.type);
    J payload = J::array();
    for (const auto &t : r.contract.payload)
      payload.push_back(ref(t));
    value["payload"] = payload;
    if (r.contract.function) {
      J parameters = J::array();
      for (const auto &t : r.contract.function->parameters)
        parameters.push_back(ref(t));
      value["function"] = {{"parameters", parameters},
                           {"result", ref(r.contract.function->result)},
                           {"mutates", r.contract.function->mutates}};
    }
    result.push_back(value);
  }
  return result.dump();
}

auto executionInterpreter(const ExpandedExecutionModel &m)
    -> InterpreterGenerationPlan {
  InterpreterGenerationPlan result;
  result.contextType = m.contextType;
  result.declarations = m.declarations;
  for (const auto &f : m.functions) {
    if (!result.declarations)
      result.declarations = ExecutionDeclarations{};
    result.declarations->functions.push_back(f.signature);
    result.functionBodies.push_back(f.body);
  }
  for (const auto &h : m.operations)
    result.operations.push_back(h.arm);
  for (const auto &h : m.expressions)
    result.expressions.push_back(h.arm);
  return result;
}
void applyExecutionProfile(ExpandedExecutionModel &m) {
  if (!m.profile)
    return;
  if (m.profile->id != "shared_value_v1")
    throw std::runtime_error("coge.unknown_execution_profile: " +
                             m.profile->id);
  const auto &spec = sharedValueProfile();
  auto local = std::move(m.functions);
  m.functions.clear();
  for (const auto &f : spec.functions) {
    const auto found = std::ranges::find_if(local, [&](const auto &v) {
      return v.signature.name == f.signature.name;
    });
    if (found != local.end()) {
      if (!sameSignature(f.signature, found->signature))
        throw std::runtime_error(
            "coge.execution_profile_override_mismatch: " + f.signature.name +
            " expected " + signature(f.signature).dump() + " actual " +
            signature(found->signature).dump());
      found->origin.kind = "profile_override";
      found->origin.member = "function/" + f.signature.name;
      m.functions.push_back(std::move(*found));
      local.erase(found);
    } else {
      m.functions.push_back(f);
      m.functions.back().origin = {"profile_default", m.profile->source,
                                   "function/" + f.signature.name};
    }
  }
  for (auto &f : local)
    m.functions.push_back(std::move(f));
  const auto merge = [&](auto &items, const auto &defaults, const char *kind) {
    auto localItems = std::move(items);
    items.clear();
    for (const auto &h : defaults) {
      const auto name = h.arm.structuredPattern->variant;
      const auto found = std::ranges::find_if(localItems, [&](const auto &v) {
        return v.arm.structuredPattern->variant == name;
      });
      if (found != localItems.end()) {
        found->origin.kind = "profile_override";
        found->origin.member = std::string(kind) + "/" + name;
        items.push_back(std::move(*found));
        localItems.erase(found);
      } else {
        items.push_back(h);
        items.back().origin = {"profile_default", m.profile->source,
                               std::string(kind) + "/" + name};
      }
    }
    for (auto &h : localItems)
      items.push_back(std::move(h));
  };
  merge(m.operations, spec.operations, "execute");
  merge(m.expressions, spec.expressions, "evaluate");
  m.profile->version = "1";
  m.profile->sha256 = executionProfileHash();
}
struct ExecutionModelAccess {
  static auto make(ExpandedExecutionModel m,
                   std::vector<ExecutionDependency> deps)
      -> std::shared_ptr<const CheckedExecutionModel> {
    auto value =
        std::shared_ptr<CheckedExecutionModel>{new CheckedExecutionModel};
    value->interpreter_ = executionInterpreter(m);
    value->effectiveHash_ = agas::artifact::sha256Hex(
        m.contractsIdentity + serializeExecutionModel(m, false));
    value->model_ = std::move(m);
    value->dependencies_ = std::move(deps);
    return value;
  }
};
auto prepareCheckedExecution(ExpandedExecutionModel model,
                             const agsem::BoundSemantics &semantics)
    -> agsem::Outcome<std::shared_ptr<const CheckedExecutionModel>> {
  agsem::Outcome<std::shared_ptr<const CheckedExecutionModel>> result;
  std::vector<ExecutionDependency> dependencies;
  result.diagnostics = checkExpandedExecution(model, semantics, dependencies);
  if (result.diagnostics.empty())
    result.value =
        ExecutionModelAccess::make(std::move(model), std::move(dependencies));
  return result;
}
auto inspectExecutionModel(const CheckedExecutionModel &checked)
    -> std::string {
  const auto &m = checked.model();
  auto result = serialize(m, true);
  result["format"] = "coge-checked-execution-v1";
  result["source_sha256"] = m.sourceIdentity;
  result["contracts_sha256"] = m.contractsIdentity;
  result["effective_sha256"] = checked.effectiveHash();
  result["interpreter_requested"] = m.interpreterRequested;
  result["profile"] = nullptr;
  if (m.profile)
    result["profile"] = {{"id", m.profile->id},
                         {"version", m.profile->version},
                         {"sha256", m.profile->sha256},
                         {"source", location(m.profile->source)}};
  result["dependencies"] = J::array();
  for (const auto &d : checked.dependencies()) {
    J value{{"member", d.member},
            {"kind", d.kind},
            {"owner", d.owner},
            {"name", d.name},
            {"path", d.path}};
    if (d.type)
      value["type"] = ref(*d.type);
    if (d.function) {
      J parameters = J::array();
      for (const auto &t : d.function->parameters)
        parameters.push_back(ref(t));
      value["function"] = {{"parameters", parameters},
                           {"result", ref(d.function->result)},
                           {"mutates", d.function->mutates},
                           {"context", d.function->context}};
    }
    J payload = J::array();
    for (const auto &t : d.payload)
      payload.push_back(ref(t));
    value["payload"] = payload;
    if (d.contract)
      value["contract"] = {{"id", d.contract->identity.id},
                           {"version", d.contract->identity.version},
                           {"sha256", d.contract->identity.sha256},
                           {"path", d.contract->path}};
    if (d.declaration)
      value["declaration"] = location(*d.declaration);
    result["dependencies"].push_back(value);
  }
  result["representation_obligations"] = {
      "Rust variant representation and field names",
      "external intrinsic implementation", "Box and Clone/Copy ABI"};
  result["required_implementations"] =
      m.profile ? J{"initialize_global", "value_matches_type", "load"}
                : J::array();
  result["coverage"] = {{"execute", J::array()}, {"evaluate", J::array()}};
  for (const auto &h : m.operations)
    if (h.arm.structuredPattern)
      result["coverage"]["execute"].push_back(h.arm.structuredPattern->variant);
  for (const auto &h : m.expressions)
    if (h.arm.structuredPattern)
      result["coverage"]["evaluate"].push_back(
          h.arm.structuredPattern->variant);
  return result.dump(2) + '\n';
}
auto executionCalls(const CheckedExecutionModel &checked)
    -> std::vector<std::string> {
  std::set<std::string> calls;
  const auto visit = [&](auto &&self, const ExecutionExpression &e) -> void {
    if (e.kind == ExecutionExprKind::DirectCall)
      calls.insert(e.optionConstructor ? "Some" : e.value);
    if (e.kind == ExecutionExprKind::RuntimeCall)
      calls.insert(e.value);
    if (e.kind == ExecutionExprKind::Constructor)
      calls.insert(e.detail);
    if (e.kind == ExecutionExprKind::Capture)
      calls.insert("capture");
    if (e.kind == ExecutionExprKind::MemberCall && !e.children.empty())
      calls.insert(e.children.front().value);
    for (const auto &c : e.children)
      self(self, c);
  };
  const auto body = [&](auto &&self,
                        const std::vector<ExecutionStatement> &ss) -> void {
    for (const auto &s : ss) {
      if (s.value)
        visit(visit, *s.value);
      if (s.extra)
        visit(visit, *s.extra);
      self(self, s.thenBranch);
      self(self, s.elseBranch);
      for (const auto &a : s.arms) {
        // Preserve constructor names reported by the document call collector.
        if (a.structuredPattern &&
            (a.structuredPattern->form == ExecutionPatternForm::Tuple ||
             a.structuredPattern->form == ExecutionPatternForm::Fields))
          calls.insert(a.structuredPattern->variant);
        self(self, a.body);
      }
    }
  };
  for (const auto &f : checked.model().functions)
    body(body, f.body.statements);
  for (const auto &h : checked.model().operations)
    body(body, h.arm.body.statements);
  for (const auto &h : checked.model().expressions)
    body(body, h.arm.body.statements);
  return {calls.begin(), calls.end()};
}
} // namespace coge
