#include "CogeBinding.h"
#include "ExecutionModelInternal.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace coge {
namespace {
using namespace agsem;
using Type = TypeRef;
using Locals = std::map<std::string, Type>;
auto convert(const ExecutionType &input) -> Type {
  Type result{input.name, {}};
  for (const auto &argument : input.arguments)
    result.arguments.push_back(convert(argument));
  return result;
}
class Checker {
public:
  std::vector<Diagnostic> errors;
  Checker(const ExpandedExecutionModel &model,
          const std::optional<ExecutionDeclarations> &declarations,
          const BoundSemantics &semantics,
          std::vector<ExecutionDependency> &dependencies, bool partial = false)
      : partial_(partial), model_(model), semantics_(semantics),
        dependencies_(dependencies) {
    for (const auto &entry : semantics.symbols()) {
      const auto &symbol = entry.contract;
      if (symbol.type)
        types_.emplace(symbol.name, *symbol.type);
      if (symbol.function && symbol.kind == SymbolKind::Function) {
        functions_.emplace(symbol.name, *symbol.function);
        if (symbol.owner != SymbolOwner::Execution)
          semanticCalls_.insert(symbol.name);
      }
    }
    if (declarations) {
      for (const auto &entry : declarations->declarations) {
        TypeContract type;
        type.kind = entry.kind == ExecutionDeclarationKind::Record
                        ? TypeContract::Kind::Record
                        : TypeContract::Kind::Enum;
        for (const auto &field : entry.fields)
          type.fields.emplace(field.name, convert(field.type));
        for (const auto &variant : entry.variants) {
          auto &payload = type.variants[variant.name];
          for (const auto &item : variant.payload)
            payload.push_back(convert(item));
        }
        if (!types_.emplace(entry.name, std::move(type)).second)
          throw std::runtime_error(
              "execution type conflicts with semantic/contract type: " +
              entry.name);
      }
      if (declarations->runtimeState) {
        state_ = declarations->runtimeState->name;
        stateType_ = convert(declarations->runtimeState->type);
      }
      for (const auto &entry : declarations->functions) {
        FunctionContract signature;
        signature.result = convert(entry.result);
        signature.mutates = entry.mutates;
        for (const auto &parameter : entry.parameters)
          signature.parameters.push_back(convert(parameter.type));
        if (!functions_.emplace(entry.name, std::move(signature)).second)
          throw std::runtime_error(
              "execution function conflicts with semantic/contract function: " +
              entry.name);
      }
    }
    for (const auto &entry : model.intrinsics) {
      FunctionContract signature;
      signature.result = convert(entry.signature.result);
      signature.mutates = entry.signature.mutates;
      for (const auto &p : entry.signature.parameters)
        signature.parameters.push_back(convert(p.type));
      const auto &name = entry.signature.name;
      if (const auto old = functions_.find(name); old != functions_.end()) {
        const bool localFunction =
            std::ranges::any_of(model.functions, [&](const auto &f) {
              return f.signature.name == name;
            });
        if (localFunction || old->second != signature ||
            semanticCalls_.contains(name))
          throw std::runtime_error(
              "conflicting execution intrinsic contract: " + name);
      } else
        functions_.emplace(name, std::move(signature));
    }
    if (!model.operations.empty() || !model.expressions.empty() ||
        model.interpreterRequested) {
      generated("execute_generated", {{"Operation", {}}, {"SourceRange", {}}},
                {"Control", {}});
      generated("evaluate_generated",
                {{"ExpressionKind", {}},
                 {"List", {{"RuntimeCheck", {}}}},
                 {"ExprId", {}},
                 {"SourceRange", {}}},
                {"Value", {}});
    }
  }
  void run(const std::optional<ExecutionDeclarations> &declarations,
           const std::vector<ExecutionBody> &bodies) {
    for (const auto &[name, type] : types_) {
      for (const auto &[field, value] : type.fields)
        checkType(value);
      for (const auto &[variant, payload] : type.variants)
        for (const auto &value : payload)
          checkType(value);
    }
    for (const auto &[name, function] : functions_)
      if (!semanticCalls_.contains(name)) {
        for (const auto &parameter : function.parameters)
          checkType(parameter);
        checkType(function.result);
      }
    if (!state_.empty())
      checkType(stateType_);
    if (model_.profile) {
      try {
        checkProfileRequirements();
      } catch (const std::runtime_error &error) {
        if (!partial_)
          throw;
        errors.push_back(
            {Severity::Error, "coge.execution_profile_contract_mismatch",
             error.what(), model_.profile->source, "profile/interface"});
      }
    }
    if (declarations)
      for (std::size_t index = 0; index < bodies.size(); ++index) {
        const auto &function = declarations->functions.at(index);
        member_ = "function/" + function.name;
        path_.clear();
        mutates_ = function.mutates;
        Locals locals;
        if (!state_.empty())
          locals.emplace(state_, stateType_);
        for (const auto &parameter : function.parameters)
          locals.emplace(parameter.name, convert(parameter.type));
        try {
          statements(bodies[index].statements, locals,
                     convert(function.result));
        } catch (const std::runtime_error &error) {
          errors.push_back({Severity::Error,
                            std::string{error.what()}.starts_with("coge.")
                                ? std::string{error.what()}.substr(
                                      0, std::string{error.what()}.find(':'))
                                : "coge.invalid_execution_body",
                            member_ + path_ + ": " + error.what(),
                            functionLocation(function.name), function.name});
        }
      }
  }
  void handlers(const std::vector<ExecutionArm> &arms, bool evaluate) {
    for (const auto &arm : arms) {
      member_ = std::string(evaluate ? "evaluate/" : "execute/") +
                (arm.structuredPattern ? arm.structuredPattern->variant
                                       : arm.pattern);
      path_ = "/pattern";
      mutates_ = true;
      Locals locals{{"source", {"SourceRange", {}}}};
      if (evaluate) {
        locals.emplace("expression", Type{"ExprId", {}});
        locals.emplace("checks", Type{"List", {{"RuntimeCheck", {}}}});
      }
      try {
        if (arm.structuredPattern)
          pattern(*arm.structuredPattern,
                  {evaluate ? "ExpressionKind" : "Operation", {}}, locals);
        else
          pattern(arm.pattern, {evaluate ? "ExpressionKind" : "Operation", {}},
                  locals);
        statements(
            arm.body.statements, locals,
            {"Result",
             {{evaluate ? "Value" : "Control", {}}, {"RuntimeError", {}}}});
      } catch (const std::runtime_error &error) {
        errors.push_back({Severity::Error,
                          std::string{error.what()}.starts_with("coge.")
                              ? std::string{error.what()}.substr(
                                    0, std::string{error.what()}.find(':'))
                              : "coge.invalid_execution_body",
                          member_ + path_ + ": " + error.what(),
                          memberLocation(),
                          {}});
      }
    }
    if (model_.profile && !partial_)
      handlerCoverage(arms, evaluate);
  }

private:
  bool partial_;
  const ExpandedExecutionModel &model_;
  const BoundSemantics &semantics_;
  std::vector<ExecutionDependency> &dependencies_;
  std::string member_;
  std::string path_;
  std::map<std::string, TypeContract> types_;
  std::map<std::string, FunctionContract> functions_;
  std::set<std::string> semanticCalls_;
  std::map<std::string, Type> substitutions_;
  std::string state_;
  Type stateType_;
  bool mutates_{};
  std::size_t fresh_{};
  auto fresh() -> Type { return {"@" + std::to_string(fresh_++), {}}; }

public:
  void finish() {
    for (auto &d : dependencies_) {
      if (d.type)
        d.type = resolve(*d.type);
      for (auto &t : d.payload)
        t = resolve(t);
      if (d.function) {
        d.function->result = resolve(d.function->result);
        for (auto &t : d.function->parameters)
          t = resolve(t);
      }
    }
    std::ranges::sort(dependencies_, [](const auto &a, const auto &b) {
      return std::tie(a.member, a.path, a.kind, a.owner, a.name) <
             std::tie(b.member, b.path, b.kind, b.owner, b.name);
    });
  }

private:
  auto contractSource(const std::string &name, const std::string &suffix = "")
      -> std::string {
    const auto &environment = semantics_.contracts();
    for (std::size_t i = 0; i < environment.symbols().size(); ++i)
      if (environment.symbols()[i].name == name) {
        const auto &origin = environment.origins()[i];
        return " [manifest " + origin.identity.id + "@" +
               origin.identity.version + " sha256=" + origin.identity.sha256 +
               " " + origin.path + suffix + "]";
      }
    if (const auto found = model_.typeOrigins.find(name);
        found != model_.typeOrigins.end())
      return " [local declaration at byte " +
             std::to_string(found->second.beginByte) + "]";
    return {};
  }
  auto functionLocation(const std::string &name) -> SourceLocation {
    for (const auto &f : model_.functions)
      if (f.signature.name == name)
        return f.origin.source;
    return model_.source;
  }
  auto memberLocation() -> SourceLocation {
    for (const auto &f : model_.functions)
      if (member_ == "function/" + f.signature.name)
        return f.origin.source;
    for (const auto &h : model_.operations)
      if (h.arm.structuredPattern &&
          member_ == "execute/" + h.arm.structuredPattern->variant)
        return h.origin.source;
    for (const auto &h : model_.expressions)
      if (h.arm.structuredPattern &&
          member_ == "evaluate/" + h.arm.structuredPattern->variant)
        return h.origin.source;
    return model_.source;
  }
  void evidence(std::string kind, std::string owner, std::string name,
                std::optional<Type> type = {},
                std::optional<FunctionContract> function = {},
                std::vector<Type> payload = {}) {
    ExecutionDependency d{member_,           std::move(kind),
                          std::move(owner),  std::move(name),
                          std::move(type),   std::move(function),
                          std::move(payload)};
    d.path = path_;
    const auto key = d.owner.empty() ? d.name : d.owner;
    const auto &environment = semantics_.contracts();
    for (std::size_t i = 0; i < environment.symbols().size(); ++i)
      if (environment.symbols()[i].name == key) {
        d.contract = environment.origins()[i];
        if (d.kind == "field")
          d.contract->path += "/fields/" + d.name;
        if (d.kind == "variant")
          d.contract->path += "/variants/" + d.name;
        break;
      }
    if (!d.contract) {
      if (const auto found = model_.typeOrigins.find(key);
          found != model_.typeOrigins.end())
        d.declaration = found->second;
      for (const auto &f : model_.functions)
        if (f.signature.name == key)
          d.declaration = f.origin.source;
      for (const auto &f : model_.intrinsics)
        if (f.signature.name == key)
          d.declaration = f.origin.source;
      for (const auto &s : semantics_.symbols())
        if (s.contract.name == key)
          d.declaration = s.declaration;
    }
    const auto duplicate =
        std::ranges::any_of(dependencies_, [&](const auto &e) {
          return e.member == d.member && e.path == d.path && e.kind == d.kind &&
                 e.owner == d.owner && e.name == d.name;
        });
    if (!duplicate)
      dependencies_.push_back(std::move(d));
  }
  void handlerCoverage(const std::vector<ExecutionArm> &arms, bool evaluate) {
    const auto owner = evaluate ? "ExpressionKind" : "Operation";
    const auto found = types_.find(owner);
    if (found == types_.end() || found->second.kind != TypeContract::Kind::Enum)
      throw std::runtime_error(
          std::string(
              "coge.execution_profile_contract_mismatch: missing enum ") +
          owner);
    std::set<std::string> handled;
    for (const auto &a : arms)
      if (a.structuredPattern)
        handled.insert(a.structuredPattern->variant);
    for (const auto &[name, ignored] : found->second.variants)
      if (!handled.contains(name))
        throw std::runtime_error(
            std::string("coge.missing_execution_handler: ") + owner + "." +
            name);
  }
  void exhaustive(const std::vector<ExecutionMatchArm> &arms, Type owner) {
    if (!model_.profile)
      return;
    std::set<std::string> handled;
    for (const auto &a : arms) {
      if (!a.structuredPattern)
        throw std::runtime_error(
            "coge.nonexhaustive_execution_match: missing structural pattern");
      if (a.structuredPattern->form == ExecutionPatternForm::Wildcard)
        return;
      handled.insert(a.structuredPattern->variant);
    }
    owner = resolve(owner);
    std::set<std::string> variants;
    if (owner.name == "Option")
      variants = {"Some", "None"};
    else if (owner.name == "Result")
      variants = {"Ok", "Err"};
    else if (const auto found = types_.find(owner.name); found != types_.end())
      for (const auto &[name, ignored] : found->second.variants)
        variants.insert(name);
    for (const auto &name : variants)
      if (!handled.contains(name))
        throw std::runtime_error(
            "coge.nonexhaustive_execution_match: " + owner.name + "." + name);
  }
  void checkProfileRequirements();

  auto resolve(Type type) -> Type {
    std::set<std::string> visited;
    while (true) {
      if (!visited.insert(type.name).second)
        throw std::runtime_error("cyclic execution type alias");
      if (substitutions_.contains(type.name)) {
        type = substitutions_.at(type.name);
        continue;
      }
      const auto found = types_.find(type.name);
      if (found != types_.end() &&
          found->second.kind == TypeContract::Kind::Alias &&
          found->second.alias) {
        type = *found->second.alias;
        continue;
      }
      break;
    }
    for (auto &argument : type.arguments)
      argument = resolve(argument);
    return type;
  }
  void checkType(const Type &type) {
    const auto builtin = builtinTypeArity(type.name, true);
    const auto found = types_.find(type.name);
    if (!builtin && found == types_.end())
      throw std::runtime_error("missing execution type contract: " + type.name);
    if (type.arguments.size() != (builtin ? *builtin : found->second.arity))
      throw std::runtime_error("wrong execution type arity: " +
                               typeSpelling(type));
    for (const auto &argument : type.arguments)
      checkType(argument);
  }
  static auto numeric(const Type &type) -> bool {
    return type.name == "I32" || type.name == "F32" || type.name == "U8" ||
           type.name == "U64" || type.name == "Index" || type.name == "Int" ||
           type.name == "$integer" || type.name == "$float";
  }
  auto containsVariable(const Type &type, const std::string &name) -> bool {
    if (type.name == name)
      return true;
    for (const auto &argument : type.arguments)
      if (containsVariable(argument, name))
        return true;
    return false;
  }
  void unify(Type actual, Type expected) {
    actual = resolve(actual);
    expected = resolve(expected);
    if (actual.name.starts_with('@')) {
      if (actual.name != expected.name) {
        if (containsVariable(expected, actual.name))
          throw std::runtime_error("recursive execution type");
        substitutions_[actual.name] = expected;
      }
      return;
    }
    if (expected.name.starts_with('@')) {
      if (containsVariable(actual, expected.name))
        throw std::runtime_error("recursive execution type");
      substitutions_[expected.name] = actual;
      return;
    }
    if ((actual.name == "$integer" && numeric(expected)) ||
        (expected.name == "$integer" && numeric(actual)) ||
        (actual.name == "$float" && expected.name == "F32") ||
        (expected.name == "$float" && actual.name == "F32"))
      return;
    if ((actual.name == "Text" && expected.name == "OwnedText") ||
        (actual.name == "OwnedText" && expected.name == "Text"))
      return;
    if (actual.name != expected.name ||
        actual.arguments.size() != expected.arguments.size())
      throw std::runtime_error("type mismatch: " + typeSpelling(actual) +
                               " versus " + typeSpelling(expected));
    for (std::size_t i = 0; i < actual.arguments.size(); ++i)
      unify(actual.arguments[i], expected.arguments[i]);
  }
  void generated(std::string name, std::vector<Type> parameters, Type result) {
    functions_.emplace(
        std::move(name),
        FunctionContract{std::move(parameters),
                         {"Result", {std::move(result), {"RuntimeError", {}}}},
                         true,
                         {}});
  }
  auto field(Type owner, const std::string &name) -> Type {
    owner = resolve(owner);
    if (owner.name == "$Pair" && owner.arguments.size() == 2 &&
        (name == "first" || name == "second"))
      return owner.arguments[name == "first" ? 0 : 1];
    const auto found = types_.find(owner.name);
    if (found == types_.end() || !found->second.fields.contains(name))
      throw std::runtime_error(
          "field has no execution contract: " + typeSpelling(owner) + "." +
          name + contractSource(owner.name, "/fields/" + name));
    evidence("field", owner.name, name, found->second.fields.at(name));
    return found->second.fields.at(name);
  }
  auto payload(Type owner, const std::string &variant) -> std::vector<Type> {
    owner = resolve(owner);
    if (owner.name == "Option") {
      if (variant == "None")
        return {};
      if (variant == "Some")
        return {owner.arguments.at(0)};
    }
    if (owner.name == "Result") {
      if (variant == "Ok")
        return {owner.arguments.at(0)};
      if (variant == "Err")
        return {owner.arguments.at(1)};
    }
    const auto found = types_.find(owner.name);
    if (found == types_.end() || !found->second.variants.contains(variant))
      throw std::runtime_error(
          "variant has no execution contract: " + owner.name + "." + variant +
          contractSource(owner.name, "/variants/" + variant));
    evidence("variant", owner.name, variant, {}, {},
             found->second.variants.at(variant));
    return found->second.variants.at(variant);
  }
  auto call(const FunctionContract &function,
            const std::vector<Type> &arguments) -> Type {
    if (function.mutates && !mutates_)
      throw std::runtime_error("mutating call requires mutates");
    if (arguments.size() != function.parameters.size())
      throw std::runtime_error("wrong execution call arity");
    for (std::size_t i = 0; i < arguments.size(); ++i)
      unify(arguments[i], function.parameters[i]);
    return function.result;
  }
  auto expression(const ExecutionExpression &value, Locals &locals) -> Type {
    using K = ExecutionExprKind;
    std::vector<Type> arguments;
    if (value.kind != K::MemberCall)
      for (const auto &child : value.children)
        arguments.push_back(expression(child, locals));
    switch (value.kind) {
    case K::State:
    case K::Binding:
      if (!locals.contains(value.value))
        throw std::runtime_error("unresolved execution binding: " +
                                 value.value);
      return resolve(locals.at(value.value));
    case K::None:
      return {"Option", {fresh()}};
    case K::Unit:
      return {"Unit", {}};
    case K::Boolean:
      return {"Bool", {}};
    case K::Text:
      return {"Text", {}};
    case K::Number:
      return {value.value.find('.') == std::string::npos ? "$integer"
                                                         : "$float",
              {}};
    case K::Negate:
      if (!numeric(arguments.at(0)))
        throw std::runtime_error("negation requires numeric value");
      return arguments.at(0);
    case K::Binary:
      unify(arguments.at(0), arguments.at(1));
      if (value.value == "==" || value.value == "!=" || value.value == "<" ||
          value.value == "<=" || value.value == ">" || value.value == ">=")
        return {"Bool", {}};
      if (!numeric(resolve(arguments.at(0))))
        throw std::runtime_error("arithmetic requires numeric operands");
      return arguments.at(0);
    case K::MemberField:
      return field(arguments.at(0), value.value);
    case K::Variant:
    case K::Constructor: {
      const Type owner{value.value, {}};
      const auto parameters = payload(owner, value.detail);
      if (parameters.size() != arguments.size())
        throw std::runtime_error("wrong constructor arity: " + value.value +
                                 "." + value.detail);
      for (std::size_t i = 0; i < parameters.size(); ++i)
        unify(arguments[i], parameters[i]);
      return owner;
    }
    case K::Capture:
      return arguments.at(0);
    case K::DirectCall: {
      if (value.optionConstructor) {
        if (arguments.size() != 1)
          throw std::runtime_error("Some requires one value");
        return {"Option", {arguments.front()}};
      }
      if (semanticCalls_.contains(value.value))
        throw std::runtime_error("coge.unsupported_semantic_call: " +
                                 value.value);
      const auto found = functions_.find(value.value);
      if (found == functions_.end())
        throw std::runtime_error("unresolved execution function: " +
                                 value.value);
      evidence("function", {}, value.value, {}, found->second);
      Type result;
      try {
        result = call(found->second, arguments);
      } catch (const std::runtime_error &error) {
        throw std::runtime_error(std::string(error.what()) + " in call " +
                                 value.value + contractSource(value.value));
      }
      if (value.propagate) {
        if (result.name != "Result" || result.arguments.size() != 2)
          throw std::runtime_error(
              "execution call requires a fallible contract: " + value.value);
        return result.arguments.front();
      }
      return result;
    }
    case K::RuntimeCall: {
      const auto result = runtimeCall(value.value, arguments);
      evidence("runtime_function", "AgSemRuntime", value.value, {},
               FunctionContract{arguments, result, false, {}});
      return result;
    }
    case K::MemberCall: {
      const auto &member = value.children.at(0);
      if (member.kind != K::MemberField)
        throw std::runtime_error("invalid member call");
      const auto owner = expression(member.children.at(0), locals);
      for (std::size_t i = 1; i < value.children.size(); ++i)
        arguments.push_back(expression(value.children[i], locals));
      const auto resolvedOwner = resolve(owner);
      const auto result = memberCall(resolvedOwner, member.value, arguments);
      const bool mutates =
          resolvedOwner.name == "Store" &&
          (member.value == "push_bindings" || member.value == "pop" ||
           member.value == "insert_current" ||
           member.value == "insert_global" || member.value == "write_path");
      evidence(
          "method", resolvedOwner.name, member.value, {},
          FunctionContract{arguments, result, mutates, resolvedOwner.name});
      return result;
    }
    }
    throw std::runtime_error("unsupported execution expression");
  }
  auto runtimeCall(const std::string &name, const std::vector<Type> &arguments)
      -> Type {
    if (name == "list") {
      if (!arguments.empty())
        throw std::runtime_error("list requires no arguments");
      return {"List", {fresh()}};
    }
    if (name == "unbox") {
      if (arguments.size() != 1)
        throw std::runtime_error("unbox requires one argument");
      return arguments.front();
    }
    if (name == "zip") {
      if (arguments.size() != 2)
        throw std::runtime_error("zip requires two lists");
      auto a = fresh(), b = fresh();
      unify(arguments[0], {"List", {a}});
      unify(arguments[1], {"List", {b}});
      return {"List", {{"$Pair", {a, b}}}};
    }
    if (name == "indices") {
      call({{{"Index", {}}}, {"Unit", {}}, false, {}}, arguments);
      return {"List", {{"Index", {}}}};
    }
    const std::map<std::string, std::pair<std::string, std::string>>
        conversions{
            {"char_to_int", {"U8", "I32"}},   {"char_to_float", {"U8", "F32"}},
            {"int_to_float", {"I32", "F32"}}, {"int_to_char", {"I32", "U8"}},
            {"float_to_int", {"F32", "I32"}}, {"float_to_char", {"F32", "U8"}}};
    if (const auto found = conversions.find(name); found != conversions.end())
      return call(
          {{{found->second.first, {}}}, {found->second.second, {}}, false, {}},
          arguments);
    const std::set<std::string> checked{"checked_neg_i32", "checked_add_i32",
                                        "checked_sub_i32", "checked_mul_i32",
                                        "checked_div_i32"};
    if (checked.contains(name))
      return call(
          {std::vector<Type>(name == "checked_neg_i32" ? 1 : 2, {"I32", {}}),
           {"Option", {{"I32", {}}}},
           false,
           {}},
          arguments);
    throw std::runtime_error("unresolved AgSemRuntime contract: " + name);
  }
  auto memberCall(const Type &owner, const std::string &method,
                  const std::vector<Type> &arguments) -> Type {
    if (method == "clone") {
      if (!arguments.empty())
        throw std::runtime_error("clone requires no arguments");
      return owner;
    }
    if (owner.name == "List") {
      if (method == "len")
        return call({{}, {"Index", {}}, false, {}}, arguments);
      if (method == "push")
        return call({{owner.arguments.at(0)}, {"Unit", {}}, false, {}},
                    arguments);
    }
    if (owner.name == "Store" && owner.arguments.size() == 2) {
      const auto key = owner.arguments[0], value = owner.arguments[1];
      if (method == "depth")
        return call({{}, {"Index", {}}, false, {}}, arguments);
      if (method == "is_global")
        return call({{}, {"Bool", {}}, false, {}}, arguments);
      if (method == "push_bindings")
        return call(
            {{{"List", {key}}, {"List", {value}}}, {"Unit", {}}, true, {}},
            arguments);
      if (method == "pop")
        return call({{}, {"Option", {{"Map", {key, value}}}}, true, {}},
                    arguments);
      if (method == "insert_current" || method == "insert_global")
        return call({{key, value}, {"Option", {value}}, true, {}}, arguments);
      if (method == "load_path")
        return call({{key, {"List", {{"Index", {}}}}},
                     {"Result", {value, {"AccessError", {}}}},
                     false,
                     {}},
                    arguments);
      if (method == "write_path")
        return call({{key, {"List", {{"Index", {}}}}, value},
                     {"Result", {{"Unit", {}}, {"AccessError", {}}}},
                     true,
                     {}},
                    arguments);
    }
    throw std::runtime_error("method has no execution contract: " +
                             typeSpelling(owner) + "." + method);
  }
  void pattern(const ExecutionPattern &p, Type owner, Locals &locals) {
    if (p.form == ExecutionPatternForm::Wildcard)
      return;
    if (!p.owner.empty())
      unify(owner, {p.owner, {}});
    owner = resolve(owner);
    const auto parameters = payload(owner, p.variant);
    if (p.bindings.size() != parameters.size())
      throw std::runtime_error("wrong pattern arity: " + owner.name + "." +
                               p.variant);
    for (std::size_t i = 0; i < p.bindings.size(); ++i) {
      locals[p.bindings[i].name] = parameters[i];
      evidence("binding", owner.name + "." + p.variant, p.bindings[i].name,
               parameters[i]);
    }
  }
  void pattern(const std::string &spelling, Type owner, Locals &locals) {
    if (spelling == "_")
      return;
    const auto open = spelling.find_first_of("({");
    auto variant = spelling.substr(0, open);
    while (!variant.empty() && variant.back() == ' ')
      variant.pop_back();
    if (const auto separator = variant.find("::");
        separator != std::string::npos) {
      unify(owner, {variant.substr(0, separator), {}});
      variant = variant.substr(separator + 2);
    }
    const auto parameters = payload(owner, variant);
    std::vector<std::string> bindings;
    if (open != std::string::npos) {
      std::string inner = spelling.substr(open + 1, spelling.size() - open - 2);
      std::istringstream stream(inner);
      std::string part;
      while (std::getline(stream, part, ',')) {
        if (const auto colon = part.find(':'); colon != std::string::npos)
          part = part.substr(colon + 1);
        const auto first = part.find_first_not_of(' '),
                   last = part.find_last_not_of(' ');
        if (first != std::string::npos)
          bindings.push_back(part.substr(first, last - first + 1));
      }
    }
    if (bindings.size() != parameters.size())
      throw std::runtime_error("wrong pattern arity: " + spelling);
    for (std::size_t i = 0; i < bindings.size(); ++i)
      locals[bindings[i]] = parameters[i];
  }
  void statements(const std::vector<ExecutionStatement> &body, Locals locals,
                  const Type &result, const std::string &parent = "") {
    using K = ExecutionStatementKind;
    const auto previous = path_;
    for (std::size_t index = 0; index < body.size(); ++index) {
      const auto &statement = body[index];
      const auto current = parent + "/statements/" + std::to_string(index);
      path_ = current;
      const auto value = statement.value ? expression(*statement.value, locals)
                                         : Type{"Unit", {}};
      switch (statement.kind) {
      case K::Let:
        locals[statement.binding] = value;
        break;
      case K::Return:
        unify(value, statement.wrapReturn && result.name == "Result"
                         ? result.arguments.at(0)
                         : result);
        break;
      case K::Expression:
        break;
      case K::Error:
        unify(value, {"Text", {}});
        if (statement.extra)
          unify(expression(*statement.extra, locals), {"SourceRange", {}});
        break;
      case K::Assignment:
        if (!mutates_)
          throw std::runtime_error("state assignment requires mutates");
        unify(value, expression(*statement.extra, locals));
        break;
      case K::If:
      case K::While:
        unify(value, {"Bool", {}});
        statements(statement.thenBranch, locals, result, current + "/then");
        statements(statement.elseBranch, locals, result, current + "/else");
        break;
      case K::Foreach: {
        auto item = fresh();
        unify(value, {"List", {item}});
        auto loop = locals;
        loop[statement.binding] = item;
        statements(statement.thenBranch, loop, result, current + "/then");
        break;
      }
      case K::Match:
        for (std::size_t index = 0; index < statement.arms.size(); ++index) {
          const auto &arm = statement.arms[index];
          const auto armPath = current + "/arms/" + std::to_string(index);
          path_ = armPath + "/pattern";
          auto inner = locals;
          if (arm.structuredPattern)
            pattern(*arm.structuredPattern, value, inner);
          else
            pattern(arm.pattern, value, inner);
          statements(arm.body, inner, result, armPath);
        }
        path_ = current;
        exhaustive(statement.arms, value);
        break;
      }
    }
    path_ = previous;
  }
};
void Checker::checkProfileRequirements() {
  member_ = "profile/interface";
  path_ = "/requirements";
  if (state_ != "runtime" || stateType_ != Type{"Runtime", {}})
    throw std::runtime_error("coge.execution_profile_contract_mismatch: "
                             "runtime_state runtime: Runtime required");
  for (const auto &requirement : sharedValueProfile().requirements) {
    const auto &r = requirement.contract;
    if (r.kind == "field") {
      const auto found = types_.find(r.owner);
      if (found == types_.end() ||
          found->second.kind != TypeContract::Kind::Record ||
          !found->second.fields.contains(r.name))
        throw std::runtime_error(
            "coge.execution_profile_contract_mismatch: expected record " +
            r.owner + " field " + r.name +
            contractSource(r.owner, "/fields/" + r.name));
      const auto actual = field({r.owner, {}}, r.name);
      if (resolve(actual) != *r.type)
        throw std::runtime_error(
            "coge.execution_profile_contract_mismatch: " + r.owner + "." +
            r.name + " expected " + typeSpelling(*r.type) + ", actual " +
            typeSpelling(actual) +
            contractSource(r.owner, "/fields/" + r.name));
    } else if (r.kind == "variant") {
      const auto found = types_.find(r.owner);
      if (found == types_.end() ||
          found->second.kind != TypeContract::Kind::Enum ||
          !found->second.variants.contains(r.name) ||
          payload({r.owner, {}}, r.name) != r.payload)
        throw std::runtime_error(
            "coge.execution_profile_contract_mismatch: variant " + r.owner +
            "." + r.name + " has incompatible payload" +
            contractSource(r.owner, "/variants/" + r.name));
    } else {
      if (requirement.localBody &&
          !std::ranges::any_of(model_.functions, [&](const auto &v) {
            return v.signature.name == r.name;
          })) {
        if (partial_)
          continue;
        throw std::runtime_error(
            "coge.execution_profile_missing_implementation: " + r.name);
      }
      const auto f = functions_.find(r.name);
      if (f == functions_.end() || semanticCalls_.contains(r.name))
        throw std::runtime_error("coge.execution_profile_contract_mismatch: "
                                 "missing execution function " +
                                 r.name);
      if (f->second.parameters != r.function->parameters ||
          f->second.result != r.function->result ||
          (!requirement.eitherEffect &&
           f->second.mutates != r.function->mutates))
        throw std::runtime_error(
            "coge.execution_profile_contract_mismatch: function " + r.name +
            " has incompatible parameters/result/effect" +
            contractSource(r.name));
      evidence("function", {}, r.name, {}, f->second);
    }
  }
}
} // namespace
auto checkExpandedExecution(const ExpandedExecutionModel &model,
                            const BoundSemantics &semantics,
                            std::vector<ExecutionDependency> &dependencies,
                            bool partial) -> std::vector<Diagnostic> {
  try {
    const auto plan = executionInterpreter(model);
    Checker checker{model, plan.declarations, semantics, dependencies, partial};
    checker.run(plan.declarations, plan.functionBodies);
    checker.handlers(plan.operations, false);
    checker.handlers(plan.expressions, true);
    checker.finish();
    return std::move(checker.errors);
  } catch (const std::runtime_error &error) {
    const std::string message = error.what();
    return {{Severity::Error,
             message.starts_with("coge.") ? message.substr(0, message.find(':'))
                                          : "coge.invalid_contract",
             message,
             model.profile ? model.profile->source : model.source,
             {}}};
  }
}
} // namespace coge
