#include "agas/runtime/AstWireJson.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "agas/artifact/Sha256.h"

namespace agas::runtime {
namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

constexpr std::size_t maximumWireBytes = 128U << 20U;
constexpr std::size_t maximumNodes = 1'000'000;
constexpr std::size_t maximumDepth = 200;
constexpr std::size_t maximumJsonDepth = 2 * maximumDepth + 8;
constexpr std::array envelopeFields{
    "wireVersion", "astSchemaVersion", "symbolsSha256", "astSchemaSha256",
    "sourceName", "sourceByteLength", "sourceSha256", "root"};
constexpr std::array spanFields{"beginByte", "endByte"};
constexpr std::array valueFields{
    "kind", "sourceSpan", "recognizedSpan", "typeName", "variantName",
    "tokenKind", "tokenText", "fieldNames", "elements"};

[[noreturn]] void invalid(std::string message) {
  throw std::invalid_argument(std::move(message));
}

void requireFields(const Json &value, const auto &fields) {
  if (!value.is_object())
    invalid("AST wire value must be an object");
  if (value.size() != fields.size())
    invalid("AST wire has a missing or unknown field");
  for (std::string_view field : fields)
    if (!value.contains(std::string{field}))
      invalid("AST wire is missing a required field");
}

auto u64(const Json &value) -> std::uint64_t {
  if (!value.is_number_unsigned())
    invalid("AST wire integer has an invalid type");
  return value.get<std::uint64_t>();
}

auto u32(const Json &value) -> std::uint32_t {
  const auto number = u64(value);
  if (number > std::numeric_limits<std::uint32_t>::max())
    invalid("AST wire integer exceeds 32 bits");
  return static_cast<std::uint32_t>(number);
}

auto text(const Json &value) -> std::string {
  if (!value.is_string())
    invalid("AST wire string has an invalid type");
  return value.get<std::string>();
}

auto kindName(AstValueKind kind) -> std::string_view {
  switch (kind) {
  case AstValueKind::Unit: return "unit";
  case AstValueKind::Token: return "token";
  case AstValueKind::Node: return "node";
  case AstValueKind::Record: return "record";
  case AstValueKind::Optional: return "optional";
  case AstValueKind::List: return "list";
  }
  invalid("unknown AST value kind");
}

auto parseKind(std::string_view kind) -> AstValueKind {
  for (unsigned index = 0; index <= static_cast<unsigned>(AstValueKind::List);
       ++index) {
    const auto candidate = static_cast<AstValueKind>(index);
    if (kindName(candidate) == kind)
      return candidate;
  }
  invalid("unknown AST value kind");
}

auto spanJson(InputSpan span) -> OrderedJson {
  return {{"beginByte", span.beginByte}, {"endByte", span.endByte}};
}

auto parseSpan(const Json &value) -> InputSpan {
  requireFields(value, spanFields);
  return {u64(value["beginByte"]), u64(value["endByte"])};
}

auto valueJson(const AstValue &value) -> OrderedJson {
  OrderedJson elements = OrderedJson::array();
  for (const auto &element : value.elements)
    elements.push_back(valueJson(element));
  return {{"kind", kindName(value.kind)},
          {"sourceSpan", spanJson(value.sourceSpan)},
          {"recognizedSpan", spanJson(value.recognizedSpan)},
          {"typeName", value.typeName},
          {"variantName", value.variantName},
          {"tokenKind", value.tokenKind},
          {"tokenText", value.tokenText},
          {"fieldNames", value.fieldNames},
          {"elements", std::move(elements)}};
}

auto parseValue(const Json &json, std::size_t depth, std::size_t &nodes)
    -> AstValue {
  if (depth > maximumDepth || ++nodes > maximumNodes)
    invalid("AST wire tree exceeds configured limits");
  requireFields(json, valueFields);
  if (!json["fieldNames"].is_array() || !json["elements"].is_array())
    invalid("AST wire children must be arrays");
  AstValue value;
  value.kind = parseKind(text(json["kind"]));
  value.sourceSpan = parseSpan(json["sourceSpan"]);
  value.recognizedSpan = parseSpan(json["recognizedSpan"]);
  value.typeName = text(json["typeName"]);
  value.variantName = text(json["variantName"]);
  value.tokenKind = u32(json["tokenKind"]);
  value.tokenText = text(json["tokenText"]);
  for (const auto &field : json["fieldNames"])
    value.fieldNames.push_back(text(field));
  for (const auto &element : json["elements"])
    value.elements.push_back(parseValue(element, depth + 1, nodes));
  return value;
}

auto utf8Boundaries(std::string_view source) -> std::vector<bool> {
  std::vector<bool> result(source.size() + 1);
  std::size_t offset = 0;
  while (offset < source.size()) {
    result[offset] = true;
    const auto first = static_cast<unsigned char>(source[offset]);
    std::size_t length = 1;
    char32_t value = first;
    char32_t minimum = 0;
    if (first >= 0x80) {
      if ((first & 0xe0U) == 0xc0U) {
        length = 2; value = first & 0x1fU; minimum = 0x80;
      } else if ((first & 0xf0U) == 0xe0U) {
        length = 3; value = first & 0x0fU; minimum = 0x800;
      } else if ((first & 0xf8U) == 0xf0U) {
        length = 4; value = first & 0x07U; minimum = 0x10000;
      } else {
        invalid("AST source has invalid UTF-8");
      }
      if (length > source.size() - offset)
        invalid("AST source has incomplete UTF-8");
      for (std::size_t index = 1; index < length; ++index) {
        const auto next = static_cast<unsigned char>(source[offset + index]);
        if ((next & 0xc0U) != 0x80U)
          invalid("AST source has invalid UTF-8 continuation");
        value = (value << 6U) | (next & 0x3fU);
      }
      if (value < minimum || value > 0x10ffff ||
          (value >= 0xd800 && value <= 0xdfff))
        invalid("AST source has an invalid Unicode scalar");
    }
    offset += length;
  }
  result[source.size()] = true;
  return result;
}

void validateValue(const AstValue &value, std::string_view source,
                   std::size_t terminalCount,
                   const std::vector<bool> &boundaries, std::size_t depth,
                   std::size_t &nodes) {
  if (depth > maximumDepth || ++nodes > maximumNodes)
    invalid("AST wire tree exceeds configured limits");
  const auto payload = value.sourceSpan;
  const auto recognized = value.recognizedSpan;
  if (payload.beginByte > payload.endByte ||
      recognized.beginByte > recognized.endByte ||
      payload.beginByte < recognized.beginByte ||
      payload.endByte > recognized.endByte ||
      recognized.endByte > source.size())
    invalid("AST wire has an invalid byte range");
  for (const auto offset : {payload.beginByte, payload.endByte,
                            recognized.beginByte, recognized.endByte})
    if (!boundaries[static_cast<std::size_t>(offset)])
      invalid("AST wire range splits UTF-8");
  const bool bare = value.typeName.empty() && value.variantName.empty() &&
                    value.tokenKind == 0 && value.tokenText.empty() &&
                    value.fieldNames.empty();
  switch (value.kind) {
  case AstValueKind::Unit:
    if (!bare || !value.elements.empty())
      invalid("AST unit has unexpected data");
    break;
  case AstValueKind::Token:
    if (!value.typeName.empty() || !value.variantName.empty() ||
        !value.fieldNames.empty() || !value.elements.empty() ||
        value.tokenKind >= terminalCount ||
        source.substr(static_cast<std::size_t>(payload.beginByte),
                      static_cast<std::size_t>(payload.endByte - payload.beginByte))
            != value.tokenText)
      invalid("AST token does not match source");
    break;
  case AstValueKind::Node:
  case AstValueKind::Record: {
    std::unordered_set<std::string_view> fields;
    if (value.typeName.empty() ||
        value.fieldNames.size() != value.elements.size() ||
        (value.kind == AstValueKind::Record && !value.variantName.empty()) ||
        value.tokenKind != 0 || !value.tokenText.empty())
      invalid("AST node or record has an invalid shape");
    for (const auto &field : value.fieldNames)
      if (field.empty() || !fields.insert(field).second)
        invalid("AST field is empty or repeated");
    break;
  }
  case AstValueKind::Optional:
  case AstValueKind::List:
    if (!bare || (value.kind == AstValueKind::Optional &&
                  value.elements.size() > 1))
      invalid("AST optional or list has an invalid shape");
    break;
  }
  for (const auto &child : value.elements)
    validateValue(child, source, terminalCount, boundaries, depth + 1, nodes);
}

auto validHash(std::string_view value) -> bool {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

void validateContext(AstWireContext context) {
  if (context.astSchemaVersion != 1 || context.sourceName.empty() ||
      context.terminalCount == 0 || !validHash(context.symbolsSha256) ||
      !validHash(context.astSchemaSha256))
    invalid("AST wire context is invalid");
}

auto strictJson(std::string_view wire) -> Json {
  std::size_t nesting = 0;
  bool inString = false;
  bool escaped = false;
  for (const char character : wire) {
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (character == '\\') {
        escaped = true;
      } else if (character == '"') {
        inString = false;
      }
    } else if (character == '"') {
      inString = true;
    } else if (character == '{' || character == '[') {
      if (++nesting > maximumJsonDepth)
        invalid("AST wire JSON nesting exceeds the limit");
    } else if (character == '}' || character == ']') {
      if (nesting == 0)
        invalid("AST wire JSON nesting is invalid");
      --nesting;
    }
  }
  std::vector<std::set<std::string>> keys;
  const auto callback = [&keys](int depth, Json::parse_event_t event,
                                Json &parsed) {
    static_cast<void>(depth);
    if (event == Json::parse_event_t::object_start) {
      keys.emplace_back();
    } else if (event == Json::parse_event_t::key) {
      const auto key = parsed.get<std::string>();
      if (keys.empty() || !keys.back().insert(key).second)
        invalid("AST wire has a duplicate JSON field");
    } else if (event == Json::parse_event_t::object_end) {
      keys.pop_back();
    }
    return true;
  };
  try {
    return Json::parse(wire.begin(), wire.end(), callback, true, false);
  } catch (const std::invalid_argument &) {
    throw;
  } catch (const Json::exception &) {
    invalid("AST wire is not valid JSON");
  }
}

} // namespace

auto dumpAstWireJson(const AstValue &root, AstWireContext context)
    -> std::string {
  validateContext(context);
  const auto boundaries = utf8Boundaries(context.source);
  std::size_t nodes = 0;
  validateValue(root, context.source, context.terminalCount, boundaries, 0,
                nodes);
  const OrderedJson envelope{
      {"wireVersion", 1},
      {"astSchemaVersion", context.astSchemaVersion},
      {"symbolsSha256", context.symbolsSha256},
      {"astSchemaSha256", context.astSchemaSha256},
      {"sourceName", context.sourceName},
      {"sourceByteLength", context.source.size()},
      {"sourceSha256", artifact::sha256Hex(context.source)},
      {"root", valueJson(root)}};
  const std::string result = envelope.dump(2) + '\n';
  if (result.size() > maximumWireBytes)
    invalid("AST wire exceeds configured byte limit");
  return result;
}

auto parseAstWireJson(std::string_view wire, AstWireContext context)
    -> AstValue {
  validateContext(context);
  if (wire.size() > maximumWireBytes)
    invalid("AST wire exceeds configured byte limit");
  const Json envelope = strictJson(wire);
  requireFields(envelope, envelopeFields);
  if (u32(envelope["wireVersion"]) != 1 ||
      u32(envelope["astSchemaVersion"]) != context.astSchemaVersion ||
      text(envelope["symbolsSha256"]) != context.symbolsSha256 ||
      text(envelope["astSchemaSha256"]) != context.astSchemaSha256 ||
      text(envelope["sourceName"]) != context.sourceName ||
      u64(envelope["sourceByteLength"]) != context.source.size() ||
      text(envelope["sourceSha256"]) != artifact::sha256Hex(context.source))
    invalid("AST wire identity does not match source");
  std::size_t nodes = 0;
  const AstValue root = parseValue(envelope["root"], 0, nodes);
  const auto boundaries = utf8Boundaries(context.source);
  nodes = 0;
  validateValue(root, context.source, context.terminalCount, boundaries, 0,
                nodes);
  if (dumpAstWireJson(root, context) != wire)
    invalid("AST wire is not canonical");
  return root;
}

} // namespace agas::runtime
