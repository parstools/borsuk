#include "agas/artifact/ReductionSection.h"

#include <array>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

namespace agas::artifact {
namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

constexpr std::array rootFields{"version", "instructions"};
constexpr std::array instructionFields{"rule",       "rhsLength", "opcode",
                                       "spanPolicy", "typeName",  "variantName",
                                       "operands",   "fields"};
constexpr std::array fieldFields{"name", "rhsIndex"};

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

auto opcodeName(generator::ReductionOpcode opcode) -> std::string_view {
  using enum generator::ReductionOpcode;
  switch (opcode) {
  case Unit:
    return "unit";
  case Forward:
    return "forward";
  case ConstructNode:
    return "construct-node";
  case ConstructNodeOrForward:
    return "construct-node-or-forward";
  case ConstructRecord:
    return "construct-record";
  case OptionalSome:
    return "optional-some";
  case OptionalNone:
    return "optional-none";
  case ListEmpty:
    return "list-empty";
  case ListSingleton:
    return "list-singleton";
  case ListAppend:
    return "list-append";
  }
  invalid("unknown reduction opcode");
}

auto parseOpcode(std::string_view name) -> generator::ReductionOpcode {
  using enum generator::ReductionOpcode;
  if (name == "unit")
    return Unit;
  if (name == "forward")
    return Forward;
  if (name == "construct-node")
    return ConstructNode;
  if (name == "construct-node-or-forward")
    return ConstructNodeOrForward;
  if (name == "construct-record")
    return ConstructRecord;
  if (name == "optional-some")
    return OptionalSome;
  if (name == "optional-none")
    return OptionalNone;
  if (name == "list-empty")
    return ListEmpty;
  if (name == "list-singleton")
    return ListSingleton;
  if (name == "list-append")
    return ListAppend;
  invalid("unknown reduction opcode: " + std::string{name});
}

auto spanName(generator::ReductionSpanPolicy policy) -> std::string_view {
  return policy == generator::ReductionSpanPolicy::MatchedRhs
             ? "matched-rhs"
             : "empty-at-lookahead";
}

auto parseSpan(std::string_view name) -> generator::ReductionSpanPolicy {
  if (name == "matched-rhs")
    return generator::ReductionSpanPolicy::MatchedRhs;
  if (name == "empty-at-lookahead")
    return generator::ReductionSpanPolicy::EmptyAtLookahead;
  invalid("unknown reduction span policy: " + std::string{name});
}

auto strictJson(std::string_view source, std::uint64_t maximumBytes) -> Json {
  if (source.size() > maximumBytes)
    throw GrammarSectionError{GrammarSectionErrorCode::ResourceLimit,
                              "reductions section exceeds configured limits"};
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
    invalid("invalid reductions JSON: " + std::string{error.what()});
  }
}

auto instructionJson(const generator::ReductionInstruction &instruction)
    -> OrderedJson {
  OrderedJson operands = instruction.operands;
  OrderedJson fields = OrderedJson::array();
  for (const generator::ReductionField &field : instruction.fields)
    fields.push_back({{"name", field.name}, {"rhsIndex", field.rhsIndex}});
  return {{"rule", instruction.rule.value},
          {"rhsLength", instruction.rhsLength},
          {"opcode", opcodeName(instruction.opcode)},
          {"spanPolicy", spanName(instruction.spanPolicy)},
          {"typeName", instruction.typeName},
          {"variantName", instruction.variantName},
          {"operands", std::move(operands)},
          {"fields", std::move(fields)}};
}

} // namespace

void validateReductionSection(const ArtifactProductions &productions,
                              const ArtifactReductions &reductions) {
  if (reductions.version != 1)
    throw GrammarSectionError{GrammarSectionErrorCode::UnsupportedVersion,
                              "reductions section version must be 1"};
  const auto &instructions = reductions.program.instructions();
  if (instructions.size() != productions.productions.size())
    throw GrammarSectionError{GrammarSectionErrorCode::InvalidReference,
                              "every production requires one reduction"};
  for (std::size_t index = 0; index < instructions.size(); ++index) {
    if (instructions[index].rule.value != productions.productions[index].id ||
        instructions[index].rhsLength !=
            productions.productions[index].rhs.size())
      throw GrammarSectionError{
          GrammarSectionErrorCode::InvalidReference,
          "reduction rule or RHS length does not match its production"};
  }
}

auto dumpReductionsJson(const ArtifactProductions &productions,
                        const generator::AstReductionProgram &program)
    -> std::string {
  validateReductionSection(productions, {1, program});
  OrderedJson instructions = OrderedJson::array();
  for (const auto &instruction : program.instructions())
    instructions.push_back(instructionJson(instruction));
  return OrderedJson{{"version", 1}, {"instructions", std::move(instructions)}}
             .dump(2) +
         '\n';
}

auto parseReductionsJson(std::string_view source,
                         const ArtifactProductions &productions,
                         const ArtifactLoadLimits &limits)
    -> ArtifactReductions {
  const Json root = strictJson(source, limits.maximumSectionBytes);
  requireFields(root, rootFields, "reductions section");
  const std::uint32_t version = u32(root["version"], "reductions version");
  if (version != 1)
    throw GrammarSectionError{GrammarSectionErrorCode::UnsupportedVersion,
                              "reductions section version must be 1"};
  if (!root["instructions"].is_array())
    invalid("instructions must be an array");
  if (root["instructions"].size() != productions.productions.size())
    throw GrammarSectionError{GrammarSectionErrorCode::InvalidReference,
                              "every production requires one reduction"};
  std::vector<generator::ReductionInstruction> instructions;
  for (const Json &value : root["instructions"]) {
    requireFields(value, instructionFields, "reduction instruction");
    if (!value["operands"].is_array() || !value["fields"].is_array())
      invalid("reduction operands and fields must be arrays");
    generator::ReductionInstruction instruction{
        zbik::RuleId{u32(value["rule"], "reduction rule")},
        u32(value["rhsLength"], "reduction RHS length"),
        parseOpcode(text(value["opcode"], "reduction opcode")),
        parseSpan(text(value["spanPolicy"], "reduction span policy")),
        optionalText(value["typeName"], "reduction type name"),
        optionalText(value["variantName"], "reduction variant name"),
        {},
        {}};
    for (const Json &operand : value["operands"])
      instruction.operands.push_back(u32(operand, "reduction operand"));
    for (const Json &field : value["fields"]) {
      requireFields(field, fieldFields, "reduction field");
      instruction.fields.push_back({text(field["name"], "field name"),
                                    u32(field["rhsIndex"], "field RHS index")});
    }
    instructions.push_back(std::move(instruction));
  }
  ArtifactReductions result{
      version, generator::AstReductionProgram{std::move(instructions)}};
  validateReductionSection(productions, result);
  return result;
}

} // namespace agas::artifact
