#include "agas/artifact/AstSchemaSection.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <set>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

namespace agas::artifact {
namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

constexpr std::array rootFields{"version", "rules"};
constexpr std::array ruleFields{"name", "treeModifier", "resultTypes",
                                "publicFields", "alternatives"};
constexpr std::array typeFields{"kind", "name", "arguments"};
constexpr std::array fieldFields{"name", "acceptedTypes",
                                 "absentInSomeAlternatives"};
constexpr std::array alternativeFields{"sourceAlternativeIndex", "variantName",
                                       "resultType", "fields"};

[[noreturn]] void invalid(std::string message) {
  throw GrammarSectionError{GrammarSectionErrorCode::InvalidJson,
                            std::move(message)};
}

void requireFields(const Json &object, const auto &allowed,
                   std::string_view context) {
  if (!object.is_object())
    invalid(std::string{context} + " must be an object");
  const std::unordered_set<std::string_view> names(allowed.begin(),
                                                   allowed.end());
  for (const auto &[key, value] : object.items()) {
    static_cast<void>(value);
    if (!names.contains(key))
      throw GrammarSectionError{GrammarSectionErrorCode::UnknownField,
                                "unknown " + std::string{context} +
                                    " field: " + key};
  }
  for (std::string_view name : allowed)
    if (!object.contains(name))
      invalid("missing " + std::string{context} +
              " field: " + std::string{name});
}

auto u32(const Json &value, std::string_view field) -> std::uint32_t {
  if (!value.is_number_unsigned() ||
      value.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
    invalid(std::string{field} + " must be an unsigned 32-bit integer");
  return static_cast<std::uint32_t>(value.get<std::uint64_t>());
}

auto text(const Json &value, std::string_view field) -> std::string {
  if (!value.is_string())
    invalid(std::string{field} + " must be a string");
  return value.get<std::string>();
}

auto optionalText(const Json &value, std::string_view field)
    -> std::optional<std::string> {
  if (value.is_null())
    return std::nullopt;
  return text(value, field);
}

auto typeKindName(generator::AstTypeKind kind) -> std::string_view {
  using enum generator::AstTypeKind;
  switch (kind) {
  case Unit:
    return "unit";
  case Token:
    return "token";
  case Rule:
    return "rule";
  case Node:
    return "node";
  case Record:
    return "record";
  case Optional:
    return "optional";
  case List:
    return "list";
  case NonEmptyList:
    return "non-empty-list";
  case Choice:
    return "choice";
  }
  invalid("unknown AST type kind");
}

auto parseTypeKind(std::string_view name) -> generator::AstTypeKind {
  using enum generator::AstTypeKind;
  if (name == "unit")
    return Unit;
  if (name == "token")
    return Token;
  if (name == "rule")
    return Rule;
  if (name == "node")
    return Node;
  if (name == "record")
    return Record;
  if (name == "optional")
    return Optional;
  if (name == "list")
    return List;
  if (name == "non-empty-list")
    return NonEmptyList;
  if (name == "choice")
    return Choice;
  invalid("unknown AST type kind: " + std::string{name});
}

auto modifierName(model::TreeModifier modifier) -> std::string_view {
  return modifier == model::TreeModifier::Node ? "node" : "inline";
}

auto parseModifier(std::string_view name) -> model::TreeModifier {
  if (name == "node")
    return model::TreeModifier::Node;
  if (name == "inline")
    return model::TreeModifier::Inline;
  invalid("unknown tree modifier: " + std::string{name});
}

auto typeJson(const generator::AstType &type) -> OrderedJson {
  OrderedJson arguments = OrderedJson::array();
  for (const generator::AstType &argument : type.arguments)
    arguments.push_back(typeJson(argument));
  return {{"kind", typeKindName(type.kind)},
          {"name", type.name},
          {"arguments", std::move(arguments)}};
}

auto fieldJson(const generator::AstFieldSchema &field) -> OrderedJson {
  OrderedJson types = OrderedJson::array();
  for (const generator::AstType &type : field.acceptedTypes)
    types.push_back(typeJson(type));
  return {{"name", field.name},
          {"acceptedTypes", std::move(types)},
          {"absentInSomeAlternatives", field.absentInSomeAlternatives}};
}

auto parseType(const Json &value, std::size_t depth) -> generator::AstType {
  if (depth > 128)
    throw GrammarSectionError{GrammarSectionErrorCode::ResourceLimit,
                              "AST type nesting is too deep"};
  requireFields(value, typeFields, "AST type");
  if (!value["arguments"].is_array())
    invalid("type arguments must be an array");
  generator::AstType result{parseTypeKind(text(value["kind"], "type kind")),
                            text(value["name"], "type name"),
                            {}};
  for (const Json &argument : value["arguments"])
    result.arguments.push_back(parseType(argument, depth + 1));
  return result;
}

auto parseField(const Json &value) -> generator::AstFieldSchema {
  requireFields(value, fieldFields, "AST field");
  if (!value["acceptedTypes"].is_array() ||
      !value["absentInSomeAlternatives"].is_boolean())
    invalid("AST field types or absence flag have invalid types");
  generator::AstFieldSchema result{
      text(value["name"], "field name"),
      {},
      value["absentInSomeAlternatives"].get<bool>()};
  for (const Json &type : value["acceptedTypes"])
    result.acceptedTypes.push_back(parseType(type, 0));
  return result;
}

auto strictJson(std::string_view source, std::uint64_t maximumBytes) -> Json {
  if (source.size() > maximumBytes)
    throw GrammarSectionError{GrammarSectionErrorCode::ResourceLimit,
                              "AST schema exceeds configured limits"};
  std::vector<std::set<std::string>> keys;
  const auto callback = [&keys](int depth, Json::parse_event_t event,
                                Json &parsed) {
    static_cast<void>(depth);
    if (event == Json::parse_event_t::object_start)
      keys.emplace_back();
    else if (event == Json::parse_event_t::key) {
      const std::string key = parsed.get<std::string>();
      if (keys.empty() || !keys.back().insert(key).second)
        invalid("duplicate JSON object field: " + key);
    } else if (event == Json::parse_event_t::object_end)
      keys.pop_back();
    return true;
  };
  try {
    return Json::parse(source.begin(), source.end(), callback, true, false);
  } catch (const GrammarSectionError &) {
    throw;
  } catch (const Json::exception &error) {
    invalid("invalid AST schema JSON: " + std::string{error.what()});
  }
}

void validateType(const generator::AstType &type,
                  const std::unordered_set<std::string> &rules,
                  std::size_t depth) {
  if (depth > 128)
    throw GrammarSectionError{GrammarSectionErrorCode::ResourceLimit,
                              "AST type nesting is too deep"};
  using enum generator::AstTypeKind;
  const bool container =
      type.kind == Optional || type.kind == List || type.kind == NonEmptyList;
  const bool choice = type.kind == Choice;
  if ((container && type.arguments.size() != 1) ||
      (choice && type.arguments.size() < 2) ||
      ((container || choice) && !type.name.empty()) ||
      (!container && !choice && !type.arguments.empty()))
    throw GrammarSectionError{GrammarSectionErrorCode::InvalidReference,
                              "AST type has an invalid argument shape"};
  const bool expectsName = type.kind != Unit && !container && !choice;
  if (expectsName == type.name.empty())
    throw GrammarSectionError{GrammarSectionErrorCode::InvalidReference,
                              "AST type " +
                                  std::string{typeKindName(type.kind)} +
                                  " has an invalid name `" + type.name + "`"};
  if (type.kind == Rule && !rules.contains(type.name))
    throw GrammarSectionError{GrammarSectionErrorCode::InvalidReference,
                              "AST type refers to an unknown rule"};
  for (const auto &argument : type.arguments)
    validateType(argument, rules, depth + 1);
}

void validateFields(const std::vector<generator::AstFieldSchema> &fields,
                    const std::unordered_set<std::string> &rules) {
  std::unordered_set<std::string> names;
  for (const auto &field : fields) {
    if (field.name.empty() || !names.insert(field.name).second ||
        field.acceptedTypes.empty())
      throw GrammarSectionError{GrammarSectionErrorCode::InvalidReference,
                                "AST field is empty or duplicated"};
    for (const auto &type : field.acceptedTypes)
      validateType(type, rules, 0);
  }
}

} // namespace

void validateAstSchemaSection(const ArtifactAstSchema &section) {
  if (section.version != 1)
    throw GrammarSectionError{GrammarSectionErrorCode::UnsupportedVersion,
                              "AST schema version must be 1"};
  std::unordered_set<std::string> rules;
  for (const auto &rule : section.schema.rules())
    rules.insert(rule.name);
  for (const auto &rule : section.schema.rules()) {
    for (const auto &type : rule.resultTypes)
      validateType(type, rules, 0);
    validateFields(rule.publicFields, rules);
    for (std::size_t index = 0; index < rule.alternatives.size(); ++index) {
      const auto &alternative = rule.alternatives[index];
      if (alternative.sourceAlternativeIndex != index)
        throw GrammarSectionError{GrammarSectionErrorCode::InvalidId,
                                  "AST alternative IDs must be dense"};
      validateType(alternative.resultType, rules, 0);
      validateFields(alternative.fields, rules);
    }
  }
}

auto dumpAstSchemaJson(const generator::AstSchema &schema) -> std::string {
  validateAstSchemaSection({1, schema});
  OrderedJson rules = OrderedJson::array();
  for (const auto &rule : schema.rules()) {
    OrderedJson resultTypes = OrderedJson::array();
    for (const auto &type : rule.resultTypes)
      resultTypes.push_back(typeJson(type));
    OrderedJson publicFields = OrderedJson::array();
    for (const auto &field : rule.publicFields)
      publicFields.push_back(fieldJson(field));
    OrderedJson alternatives = OrderedJson::array();
    for (const auto &alternative : rule.alternatives) {
      OrderedJson fields = OrderedJson::array();
      for (const auto &field : alternative.fields)
        fields.push_back(fieldJson(field));
      alternatives.push_back(
          {{"sourceAlternativeIndex", alternative.sourceAlternativeIndex},
           {"variantName", alternative.variantName},
           {"resultType", typeJson(alternative.resultType)},
           {"fields", std::move(fields)}});
    }
    rules.push_back({{"name", rule.name},
                     {"treeModifier", modifierName(rule.treeModifier)},
                     {"resultTypes", std::move(resultTypes)},
                     {"publicFields", std::move(publicFields)},
                     {"alternatives", std::move(alternatives)}});
  }
  return OrderedJson{{"version", 1}, {"rules", std::move(rules)}}.dump(2) +
         '\n';
}

auto parseAstSchemaJson(std::string_view source,
                        const ArtifactLoadLimits &limits) -> ArtifactAstSchema {
  const Json root = strictJson(source, limits.maximumSectionBytes);
  requireFields(root, rootFields, "AST schema");
  const std::uint32_t version = u32(root["version"], "AST schema version");
  if (version != 1)
    throw GrammarSectionError{GrammarSectionErrorCode::UnsupportedVersion,
                              "AST schema version must be 1"};
  if (!root["rules"].is_array())
    invalid("AST schema rules must be an array");
  std::vector<generator::AstRuleSchema> rules;
  for (const Json &value : root["rules"]) {
    requireFields(value, ruleFields, "AST rule");
    if (!value["resultTypes"].is_array() || !value["publicFields"].is_array() ||
        !value["alternatives"].is_array())
      invalid("AST rule lists must be arrays");
    generator::AstRuleSchema rule;
    rule.name = text(value["name"], "AST rule name");
    rule.treeModifier =
        parseModifier(text(value["treeModifier"], "tree modifier"));
    for (const Json &type : value["resultTypes"])
      rule.resultTypes.push_back(parseType(type, 0));
    for (const Json &field : value["publicFields"])
      rule.publicFields.push_back(parseField(field));
    for (const Json &item : value["alternatives"]) {
      requireFields(item, alternativeFields, "AST alternative");
      if (!item["fields"].is_array())
        invalid("alternative fields must be an array");
      generator::AstAlternativeSchema alternative{
          u32(item["sourceAlternativeIndex"], "source alternative index"),
          optionalText(item["variantName"], "variant name"),
          parseType(item["resultType"], 0),
          {}};
      for (const Json &field : item["fields"])
        alternative.fields.push_back(parseField(field));
      rule.alternatives.push_back(std::move(alternative));
    }
    rules.push_back(std::move(rule));
  }
  ArtifactAstSchema result{version, generator::AstSchema{std::move(rules)}};
  validateAstSchemaSection(result);
  return result;
}

} // namespace agas::artifact
