#include "agas/artifact/ArtifactManifestJson.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace agas::artifact {
namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

constexpr std::array rootFields{
    "formatVersion",        "parserAlgorithm", "lookahead",
    "startSymbol",          "rootType",        "generatorVersion",
    "zbikRevision",         "unicodeVersion",  "exactSourceSha256",
    "expandedSourceSha256", "settingsSha256",  "sections",
};
constexpr std::array sectionFields{"kind",     "version",    "file",
                                   "required", "byteLength", "sha256"};

void requireObjectFields(const Json &object, const auto &allowedFields,
                         std::string_view context) {
  if (!object.is_object()) {
    throw ArtifactManifestError{ArtifactManifestErrorCode::InvalidJson,
                                std::string{context} + " must be an object"};
  }
  const std::unordered_set<std::string_view> allowed(allowedFields.begin(),
                                                     allowedFields.end());
  for (const auto &[key, value] : object.items()) {
    static_cast<void>(value);
    if (!allowed.contains(key)) {
      throw ArtifactManifestError{ArtifactManifestErrorCode::UnknownField,
                                  "unknown " + std::string{context} +
                                      " field: " + key};
    }
  }
  for (std::string_view field : allowedFields) {
    if (!object.contains(field)) {
      throw ArtifactManifestError{ArtifactManifestErrorCode::InvalidJson,
                                  "missing " + std::string{context} +
                                      " field: " + std::string{field}};
    }
  }
}

auto unsigned32(const Json &value, std::string_view field) -> std::uint32_t {
  if (!value.is_number_unsigned() ||
      value.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
    throw ArtifactManifestError{ArtifactManifestErrorCode::InvalidJson,
                                std::string{field} +
                                    " must be an unsigned 32-bit integer"};
  }
  return static_cast<std::uint32_t>(value.get<std::uint64_t>());
}

auto unsigned64(const Json &value, std::string_view field) -> std::uint64_t {
  if (!value.is_number_unsigned()) {
    throw ArtifactManifestError{ArtifactManifestErrorCode::InvalidJson,
                                std::string{field} +
                                    " must be an unsigned 64-bit integer"};
  }
  return value.get<std::uint64_t>();
}

auto stringValue(const Json &value, std::string_view field) -> std::string {
  if (!value.is_string()) {
    throw ArtifactManifestError{ArtifactManifestErrorCode::InvalidJson,
                                std::string{field} + " must be a string"};
  }
  return value.get<std::string>();
}

auto parseSectionKind(std::string_view value) -> ArtifactSectionKind {
  for (std::uint32_t raw = 0;
       raw <= static_cast<std::uint32_t>(ArtifactSectionKind::Diagnostics);
       ++raw) {
    const auto kind = static_cast<ArtifactSectionKind>(raw);
    if (sectionKindName(kind) == value)
      return kind;
  }
  throw ArtifactManifestError{ArtifactManifestErrorCode::InvalidSection,
                              "unknown artifact section kind: " +
                                  std::string{value}};
}

auto sectionJson(const ArtifactSectionDescriptor &section) -> OrderedJson {
  return OrderedJson{{"kind", sectionKindName(section.kind)},
                     {"version", section.version},
                     {"file", section.file},
                     {"required", section.required},
                     {"byteLength", section.byteLength},
                     {"sha256", section.sha256}};
}

} // namespace

auto dumpArtifactManifestJson(const ArtifactManifest &manifest) -> std::string {
  validateArtifactManifest(manifest);
  std::vector<ArtifactSectionDescriptor> sections = manifest.sections;
  std::ranges::sort(sections, {}, &ArtifactSectionDescriptor::kind);
  OrderedJson sectionArray = OrderedJson::array();
  for (const ArtifactSectionDescriptor &section : sections)
    sectionArray.push_back(sectionJson(section));

  const OrderedJson root{
      {"formatVersion", manifest.formatVersion},
      {"parserAlgorithm", manifest.parserAlgorithm},
      {"lookahead", manifest.lookahead},
      {"startSymbol", manifest.startSymbol},
      {"rootType", manifest.rootType},
      {"generatorVersion", manifest.generatorVersion},
      {"zbikRevision", manifest.zbikRevision},
      {"unicodeVersion", manifest.unicodeVersion},
      {"exactSourceSha256", manifest.exactSourceSha256},
      {"expandedSourceSha256", manifest.expandedSourceSha256},
      {"settingsSha256", manifest.settingsSha256},
      {"sections", std::move(sectionArray)}};
  return root.dump(2) + '\n';
}

auto parseArtifactManifestJson(std::string_view text,
                               const ArtifactLoadLimits &limits)
    -> ArtifactManifest {
  if (text.size() > limits.maximumManifestBytes) {
    throw ArtifactManifestError{ArtifactManifestErrorCode::ResourceLimit,
                                "artifact manifest exceeds configured limits"};
  }

  std::vector<std::set<std::string>> objectKeys;
  const auto duplicateRejector =
      [&objectKeys](int depth, Json::parse_event_t event, Json &parsed) {
        static_cast<void>(depth);
        if (event == Json::parse_event_t::object_start) {
          objectKeys.emplace_back();
        } else if (event == Json::parse_event_t::key) {
          const std::string key = parsed.get<std::string>();
          if (objectKeys.empty() || !objectKeys.back().insert(key).second) {
            throw ArtifactManifestError{ArtifactManifestErrorCode::InvalidJson,
                                        "duplicate JSON object field: " + key};
          }
        } else if (event == Json::parse_event_t::object_end) {
          objectKeys.pop_back();
        }
        return true;
      };

  Json root;
  try {
    root =
        Json::parse(text.begin(), text.end(), duplicateRejector, true, false);
  } catch (const ArtifactManifestError &) {
    throw;
  } catch (const Json::exception &error) {
    throw ArtifactManifestError{ArtifactManifestErrorCode::InvalidJson,
                                "invalid artifact manifest JSON: " +
                                    std::string{error.what()}};
  }
  requireObjectFields(root, rootFields, "manifest");
  if (!root["sections"].is_array()) {
    throw ArtifactManifestError{ArtifactManifestErrorCode::InvalidJson,
                                "manifest sections must be an array"};
  }

  ArtifactManifest result;
  result.formatVersion = unsigned32(root["formatVersion"], "formatVersion");
  result.parserAlgorithm =
      stringValue(root["parserAlgorithm"], "parserAlgorithm");
  result.lookahead = unsigned32(root["lookahead"], "lookahead");
  result.startSymbol = stringValue(root["startSymbol"], "startSymbol");
  result.rootType = stringValue(root["rootType"], "rootType");
  result.generatorVersion =
      stringValue(root["generatorVersion"], "generatorVersion");
  result.zbikRevision = stringValue(root["zbikRevision"], "zbikRevision");
  result.unicodeVersion = stringValue(root["unicodeVersion"], "unicodeVersion");
  result.exactSourceSha256 =
      stringValue(root["exactSourceSha256"], "exactSourceSha256");
  result.expandedSourceSha256 =
      stringValue(root["expandedSourceSha256"], "expandedSourceSha256");
  result.settingsSha256 = stringValue(root["settingsSha256"], "settingsSha256");

  for (const Json &jsonSection : root["sections"]) {
    requireObjectFields(jsonSection, sectionFields, "section");
    if (!jsonSection["required"].is_boolean()) {
      throw ArtifactManifestError{ArtifactManifestErrorCode::InvalidJson,
                                  "section required must be a boolean"};
    }
    result.sections.push_back(
        {parseSectionKind(stringValue(jsonSection["kind"], "section kind")),
         unsigned32(jsonSection["version"], "section version"),
         stringValue(jsonSection["file"], "section file"),
         jsonSection["required"].get<bool>(),
         unsigned64(jsonSection["byteLength"], "section byteLength"),
         stringValue(jsonSection["sha256"], "section sha256")});
  }
  validateArtifactManifest(result, limits);
  return result;
}

} // namespace agas::artifact
