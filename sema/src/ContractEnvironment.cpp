#include "agsem/ContractEnvironment.h"
#include "agas/artifact/Sha256.h"
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <set>
namespace agsem {
namespace {
using Json = nlohmann::json;
auto identifier(std::string_view name) -> bool {
  return !name.empty() &&
         (std::isalpha(static_cast<unsigned char>(name.front())) ||
          name.front() == '_') &&
         std::ranges::all_of(
             name, [](unsigned char c) { return std::isalnum(c) || c == '_'; });
}
void keys(const Json &json, const std::set<std::string> &allowed) {
  if (!json.is_object())
    throw std::runtime_error("contract entry must be an object");
  for (const auto &[key, value] : json.items())
    if (!allowed.contains(key))
      throw std::runtime_error("unknown contract field: " + key);
}
auto type(const Json &json) -> TypeRef {
  if (!json.is_object() || !json.contains("name"))
    throw std::runtime_error("type must be an object with name and arguments");
  keys(json, {"name", "arguments"});
  if (json.contains("arguments") && !json.at("arguments").is_array())
    throw std::runtime_error("type arguments must be an array");
  TypeRef result{json.at("name").get<std::string>(), {}};
  if (!identifier(result.name))
    throw std::runtime_error("invalid contract type name");
  for (const auto &argument : json.value("arguments", Json::array()))
    result.arguments.push_back(type(argument));
  return result;
}
auto symbol(const Json &json, SymbolOwner owner) -> ContractSymbol {
  ContractSymbol result{json.at("name").get<std::string>(), SymbolKind::Type,
                        owner};
  if (!identifier(result.name))
    throw std::runtime_error("invalid contract symbol name");
  const auto kind = json.at("kind").get<std::string>();
  std::set<std::string> allowed{"name", "kind", "rust"};
  if (kind == "function" || kind == "method")
    allowed.insert({"parameters", "result", "effect", "context"});
  else if (kind == "role")
    allowed.insert("type");
  else {
    allowed.insert("arity");
    if (kind == "alias")
      allowed.insert("target");
    if (kind == "record")
      allowed.insert("fields");
    if (kind == "enum")
      allowed.insert("variants");
  }
  keys(json, allowed);
  if (kind == "function" || kind == "method") {
    result.kind = kind == "method" ? SymbolKind::Method : SymbolKind::Function;
    FunctionContract function;
    if (!json.at("parameters").is_array())
      throw std::runtime_error("function parameters must be an array");
    for (const auto &parameter : json.at("parameters"))
      function.parameters.push_back(type(parameter));
    function.result = type(json.at("result"));
    const auto effect = json.value("effect", "pure");
    if (effect != "pure" && effect != "mutates")
      throw std::runtime_error("unknown contract effect");
    function.mutates = effect == "mutates";
    function.context = json.value("context", "");
    if (kind == "method" && !identifier(function.context))
      throw std::runtime_error("method requires a context type");
    result.function = std::move(function);
  } else if (kind == "role") {
    result.kind = SymbolKind::Role;
    result.roleType = type(json.at("type"));
  } else {
    TypeContract contract;
    if (json.contains("arity") && (!json.at("arity").is_number_integer() ||
                                   json.at("arity").get<std::int64_t>() < 0))
      throw std::runtime_error("arity must be a nonnegative integer");
    contract.arity = json.value("arity", std::size_t{0});
    if (kind == "opaque")
      contract.kind = TypeContract::Kind::Opaque;
    else if (kind == "alias") {
      contract.kind = TypeContract::Kind::Alias;
      contract.alias = type(json.at("target"));
    } else if (kind == "record") {
      contract.kind = TypeContract::Kind::Record;
      if (!json.at("fields").is_object())
        throw std::runtime_error("record fields must be an object");
      for (const auto &[name, field] : json.at("fields").items()) {
        if (!identifier(name))
          throw std::runtime_error("invalid contract field name");
        contract.fields.emplace(name, type(field));
      }
    } else if (kind == "enum") {
      contract.kind = TypeContract::Kind::Enum;
      if (!json.at("variants").is_object())
        throw std::runtime_error("enum variants must be an object");
      for (const auto &[name, payload] : json.at("variants").items()) {
        if (!identifier(name))
          throw std::runtime_error("invalid contract variant name");
        if (!payload.is_array())
          throw std::runtime_error("variant payload must be an array");
        std::vector<TypeRef> fields;
        for (const auto &field : payload)
          fields.push_back(type(field));
        contract.variants.emplace(name, std::move(fields));
      }
    } else
      throw std::runtime_error("unknown contract export kind: " + kind);
    if (contract.arity && contract.kind != TypeContract::Kind::Opaque)
      throw std::runtime_error("generic structural contracts require explicit "
                               "type parameters and are not supported");
    result.type = std::move(contract);
  }
  if (json.contains("rust")) {
    result.rust = json.at("rust").get<std::vector<std::string>>();
    if (result.rust.empty() || !std::ranges::all_of(result.rust, identifier))
      throw std::runtime_error("invalid Rust symbol path");
  }
  return result;
}
} // namespace
auto typeSpelling(const TypeRef &type) -> std::string {
  auto result = type.name;
  if (!type.arguments.empty()) {
    result += '<';
    for (std::size_t i = 0; i < type.arguments.size(); ++i) {
      if (i)
        result += ',';
      result += typeSpelling(type.arguments[i]);
    }
    result += '>';
  }
  return result;
}
auto builtinTypeArity(std::string_view name, bool execution)
    -> std::optional<std::size_t> {
  if (name == "Option" || name == "List" || name == "Node")
    return 1;
  if (name == "Result")
    return execution ? 2 : 1;
  if (execution && (name == "Map" || name == "Store"))
    return 2;
  for (const auto builtin : {"Bool", "Int", "Text", "OwnedText", "Unit",
                             "Token", "SourceRange", "Index"})
    if (name == builtin)
      return 0;
  if (execution &&
      (name == "I32" || name == "F32" || name == "U8" || name == "U64"))
    return 0;
  return {};
}
auto ContractEnvironment::fingerprint() const -> std::string {
  auto identities = identities_;
  std::ranges::sort(identities, {}, &ContractIdentity::id);
  Json entries = Json::array();
  for (const auto &identity : identities)
    entries.push_back({identity.id, identity.version, identity.sha256});
  return agas::artifact::sha256Hex(entries.dump());
}
auto ContractEnvironment::addManifest(std::string_view json)
    -> std::vector<Diagnostic> {
  try {
    std::vector<std::set<std::string>> objectKeys;
    const auto root =
        Json::parse(json, [&](int, Json::parse_event_t event, Json &value) {
          if (event == Json::parse_event_t::object_start)
            objectKeys.emplace_back();
          else if (event == Json::parse_event_t::key &&
                   !objectKeys.back().insert(value.get<std::string>()).second)
            throw std::runtime_error("duplicate JSON contract key");
          else if (event == Json::parse_event_t::object_end)
            objectKeys.pop_back();
          return true;
        });
    keys(root, {"format", "id", "version", "semantic", "execution"});
    for (const auto *key : {"semantic", "execution"})
      if (root.contains(key) && !root.at(key).is_array())
        throw std::runtime_error("contract exports must be arrays");
    if (!root.at("format").is_number_integer() || root.at("format") != 1)
      throw std::runtime_error("unsupported contract manifest format");
    const ContractIdentity identity{root.at("id").get<std::string>(),
                                    root.at("version").get<std::string>(),
                                    agas::artifact::sha256Hex(json)};
    if (identity.id.empty() || identity.version.empty())
      throw std::runtime_error("contract identity must not be empty");
    const auto previous =
        std::ranges::find(identities_, identity.id, &ContractIdentity::id);
    if (previous != identities_.end()) {
      if (*previous == identity)
        return {};
      throw std::runtime_error("conflicting contract version or definition: " +
                               identity.id);
    }
    std::vector<ContractSymbol> additions;
    std::vector<ContractOrigin> origins;
    for (const auto &[key, owner] :
         {std::pair{"semantic", SymbolOwner::Semantic},
          std::pair{"execution", SymbolOwner::Execution}})
      for (std::size_t index = 0; index < root.value(key, Json::array()).size();
           ++index) {
        const auto &entry = root.at(key).at(index);
        auto value = symbol(entry, owner);
        const auto conflict = [&](const auto &existing) {
          return existing.name == value.name && existing.kind == value.kind;
        };
        if (std::ranges::any_of(symbols_, conflict) ||
            std::ranges::any_of(additions, conflict))
          throw std::runtime_error("duplicate contract export: " + value.name);
        additions.push_back(std::move(value));
        origins.push_back(
            {identity, "/" + std::string{key} + "/" + std::to_string(index)});
      }
    symbols_.insert(symbols_.end(), additions.begin(), additions.end());
    origins_.insert(origins_.end(), origins.begin(), origins.end());
    identities_.push_back(identity);
    return {};
  } catch (const std::exception &error) {
    return {{Severity::Error, "sema.contract_mismatch", error.what(), {}, {}}};
  }
}
auto ContractEnvironment::validate() const -> std::vector<Diagnostic> {
  std::vector<Diagnostic> errors;
  const auto check = [&](auto &&self, const TypeRef &type,
                         SymbolOwner owner) -> void {
    const auto builtin =
        builtinTypeArity(type.name, owner == SymbolOwner::Execution);
    const auto found = std::ranges::find_if(symbols_, [&](const auto &item) {
      return item.kind == SymbolKind::Type && item.name == type.name;
    });
    if (!builtin && found == symbols_.end())
      errors.push_back({Severity::Error,
                        "sema.missing_type_contract",
                        "unknown contract type: " + type.name,
                        {},
                        type.name});
    else if (found != symbols_.end() && owner == SymbolOwner::Semantic &&
             found->owner == SymbolOwner::Execution)
      errors.push_back(
          {Severity::Error,
           "sema.execution_dependency",
           "semantic contract depends on execution type: " + type.name,
           {},
           type.name});
    else if (type.arguments.size() != (builtin ? *builtin : found->type->arity))
      errors.push_back({Severity::Error,
                        "sema.contract_mismatch",
                        "wrong type arity: " + type.name,
                        {},
                        type.name});
    for (const auto &argument : type.arguments)
      self(self, argument, owner);
  };
  for (const auto &value : symbols_) {
    if (value.type) {
      if (value.type->alias)
        check(check, *value.type->alias, value.owner);
      for (const auto &[name, field] : value.type->fields)
        check(check, field, value.owner);
      for (const auto &[name, payload] : value.type->variants)
        for (const auto &field : payload)
          check(check, field, value.owner);
    }
    if (value.function) {
      for (const auto &parameter : value.function->parameters)
        check(check, parameter, value.owner);
      check(check, value.function->result, value.owner);
      if (!value.function->context.empty())
        check(check, {value.function->context, {}}, value.owner);
    }
    if (value.roleType)
      check(check, *value.roleType, value.owner);
  }
  // Alias cycles are invalid even if none of the declarations are used.
  std::map<std::string, int> states;
  const auto visit = [&](auto &&self, const std::string &name) -> void {
    if (states[name] == 2)
      return;
    if (states[name] == 1) {
      errors.push_back({Severity::Error,
                        "sema.contract_mismatch",
                        "contract alias cycle: " + name,
                        {},
                        name});
      return;
    }
    states[name] = 1;
    const auto found = std::ranges::find_if(symbols_, [&](const auto &item) {
      return item.kind == SymbolKind::Type && item.name == name;
    });
    if (found != symbols_.end() && found->type->alias) {
      const auto dependencies = [&](auto &&walk, const TypeRef &type) -> void {
        self(self, type.name);
        for (const auto &argument : type.arguments)
          walk(walk, argument);
      };
      dependencies(dependencies, *found->type->alias);
    }
    states[name] = 2;
  };
  for (const auto &symbol : symbols_)
    if (symbol.type && symbol.type->alias)
      visit(visit, symbol.name);
  return errors;
}
} // namespace agsem
