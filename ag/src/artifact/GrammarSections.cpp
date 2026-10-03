#include "agas/artifact/GrammarSections.h"

#include <array>
#include <limits>
#include <set>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

namespace agas::artifact {
namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

constexpr std::array symbolsFields{"version", "terminals", "nonterminals",
                                   "channels"};
constexpr std::array symbolFields{"id", "name"};
constexpr std::array productionsFields{"version", "productions"};
constexpr std::array productionFields{"id", "lhs", "rhs"};
constexpr std::array referenceFields{"kind", "id"};

[[noreturn]] void fail(GrammarSectionErrorCode code, std::string message) {
  throw GrammarSectionError{code, std::move(message)};
}

void requireFields(const Json &object, const auto &allowed,
                   std::string_view context) {
  if (!object.is_object())
    fail(GrammarSectionErrorCode::InvalidJson,
         std::string{context} + " must be an object");
  const std::unordered_set<std::string_view> names(allowed.begin(),
                                                   allowed.end());
  for (const auto &[key, value] : object.items()) {
    static_cast<void>(value);
    if (!names.contains(key))
      fail(GrammarSectionErrorCode::UnknownField,
           "unknown " + std::string{context} + " field: " + key);
  }
  for (std::string_view name : allowed) {
    if (!object.contains(name))
      fail(GrammarSectionErrorCode::InvalidJson,
           "missing " + std::string{context} + " field: " + std::string{name});
  }
}

auto unsigned32(const Json &value, std::string_view field) -> std::uint32_t {
  if (!value.is_number_unsigned() ||
      value.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
    fail(GrammarSectionErrorCode::InvalidJson,
         std::string{field} + " must be an unsigned 32-bit integer");
  return static_cast<std::uint32_t>(value.get<std::uint64_t>());
}

auto stringValue(const Json &value, std::string_view field) -> std::string {
  if (!value.is_string())
    fail(GrammarSectionErrorCode::InvalidJson,
         std::string{field} + " must be a string");
  return value.get<std::string>();
}

auto parseJson(std::string_view text, std::uint64_t maximumBytes) -> Json {
  if (text.size() > maximumBytes)
    fail(GrammarSectionErrorCode::ResourceLimit,
         "artifact section exceeds configured limits");
  std::vector<std::set<std::string>> objectKeys;
  const auto callback = [&objectKeys](int depth, Json::parse_event_t event,
                                      Json &parsed) {
    static_cast<void>(depth);
    if (event == Json::parse_event_t::object_start) {
      objectKeys.emplace_back();
    } else if (event == Json::parse_event_t::key) {
      const std::string key = parsed.get<std::string>();
      if (objectKeys.empty() || !objectKeys.back().insert(key).second)
        fail(GrammarSectionErrorCode::InvalidJson,
             "duplicate JSON object field: " + key);
    } else if (event == Json::parse_event_t::object_end) {
      objectKeys.pop_back();
    }
    return true;
  };
  try {
    return Json::parse(text.begin(), text.end(), callback, true, false);
  } catch (const GrammarSectionError &) {
    throw;
  } catch (const Json::exception &error) {
    fail(GrammarSectionErrorCode::InvalidJson,
         "invalid artifact section JSON: " + std::string{error.what()});
  }
}

void validateNames(const std::vector<ArtifactNamedSymbol> &symbols,
                   std::string_view kind, const GrammarSectionLimits &limits) {
  if (symbols.size() > limits.maximumSymbols)
    fail(GrammarSectionErrorCode::ResourceLimit,
         std::string{kind} + " count exceeds configured limits");
  std::unordered_set<std::string> names;
  for (std::size_t index = 0; index < symbols.size(); ++index) {
    const ArtifactNamedSymbol &symbol = symbols[index];
    if (symbol.id != index)
      fail(GrammarSectionErrorCode::InvalidId,
           std::string{kind} + " IDs must be dense and ordered");
    if (symbol.name.empty() || symbol.name.size() > limits.maximumNameBytes)
      fail(GrammarSectionErrorCode::ResourceLimit,
           std::string{kind} + " name has an invalid size");
    if (!names.insert(symbol.name).second)
      fail(GrammarSectionErrorCode::DuplicateName,
           std::string{kind} + " name is duplicated: " + symbol.name);
  }
}

auto namedSymbols(const Json &value, std::string_view field)
    -> std::vector<ArtifactNamedSymbol> {
  if (!value.is_array())
    fail(GrammarSectionErrorCode::InvalidJson,
         std::string{field} + " must be an array");
  std::vector<ArtifactNamedSymbol> result;
  result.reserve(value.size());
  for (const Json &symbol : value) {
    requireFields(symbol, symbolFields, "symbol");
    result.push_back({unsigned32(symbol["id"], "symbol id"),
                      stringValue(symbol["name"], "symbol name")});
  }
  return result;
}

auto namedSymbolsJson(const std::vector<ArtifactNamedSymbol> &symbols)
    -> OrderedJson {
  OrderedJson result = OrderedJson::array();
  for (const ArtifactNamedSymbol &symbol : symbols)
    result.push_back(OrderedJson{{"id", symbol.id}, {"name", symbol.name}});
  return result;
}

auto symbolKindName(ArtifactSymbolKind kind) -> std::string_view {
  return kind == ArtifactSymbolKind::Terminal ? "terminal" : "nonterminal";
}

auto parseSymbolKind(std::string_view name) -> ArtifactSymbolKind {
  if (name == "terminal")
    return ArtifactSymbolKind::Terminal;
  if (name == "nonterminal")
    return ArtifactSymbolKind::Nonterminal;
  fail(GrammarSectionErrorCode::InvalidJson,
       "unknown production symbol kind: " + std::string{name});
}

} // namespace

GrammarSectionError::GrammarSectionError(GrammarSectionErrorCode code,
                                         std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

auto GrammarSectionError::code() const noexcept -> GrammarSectionErrorCode {
  return code_;
}

void validateGrammarSections(const ArtifactSymbols &symbols,
                             const ArtifactProductions &productions,
                             const GrammarSectionLimits &limits) {
  if (symbols.version != 1 || productions.version != 1)
    fail(GrammarSectionErrorCode::UnsupportedVersion,
         "grammar artifact section version must be 1");
  validateNames(symbols.terminals, "terminal", limits);
  validateNames(symbols.nonterminals, "nonterminal", limits);
  validateNames(symbols.channels, "channel", limits);
  if (symbols.nonterminals.empty())
    fail(GrammarSectionErrorCode::InvalidReference,
         "grammar must contain at least one nonterminal");
  if (productions.productions.size() > limits.maximumProductions)
    fail(GrammarSectionErrorCode::ResourceLimit,
         "production count exceeds configured limits");
  std::uint64_t rhsSymbols = 0;
  for (std::size_t index = 0; index < productions.productions.size(); ++index) {
    const ArtifactProduction &production = productions.productions[index];
    if (production.id != index)
      fail(GrammarSectionErrorCode::InvalidId,
           "production IDs must be dense and ordered");
    if (production.lhs >= symbols.nonterminals.size())
      fail(GrammarSectionErrorCode::InvalidReference,
           "production LHS refers to an unknown nonterminal");
    if (production.rhs.size() > limits.maximumRhsSymbols - rhsSymbols)
      fail(GrammarSectionErrorCode::ResourceLimit,
           "production RHS symbols exceed configured limits");
    rhsSymbols += production.rhs.size();
    for (const ArtifactSymbolReference &reference : production.rhs) {
      const std::size_t count = reference.kind == ArtifactSymbolKind::Terminal
                                    ? symbols.terminals.size()
                                    : symbols.nonterminals.size();
      if (reference.id >= count)
        fail(GrammarSectionErrorCode::InvalidReference,
             "production " + std::to_string(production.id) + " RHS " +
                 (reference.kind == ArtifactSymbolKind::Terminal
                      ? "terminal "
                      : "nonterminal ") +
                 std::to_string(reference.id) +
                 " refers outside symbol count " + std::to_string(count));
    }
  }
}

auto dumpSymbolsJson(const ArtifactSymbols &symbols) -> std::string {
  validateGrammarSections(symbols, ArtifactProductions{1, {}});
  const OrderedJson root{
      {"version", symbols.version},
      {"terminals", namedSymbolsJson(symbols.terminals)},
      {"nonterminals", namedSymbolsJson(symbols.nonterminals)},
      {"channels", namedSymbolsJson(symbols.channels)}};
  return root.dump(2) + '\n';
}

auto parseSymbolsJson(std::string_view text,
                      const ArtifactLoadLimits &loadLimits,
                      const GrammarSectionLimits &sectionLimits)
    -> ArtifactSymbols {
  const Json root = parseJson(text, loadLimits.maximumSectionBytes);
  requireFields(root, symbolsFields, "symbols section");
  ArtifactSymbols result{unsigned32(root["version"], "symbols version"),
                         namedSymbols(root["terminals"], "terminals"),
                         namedSymbols(root["nonterminals"], "nonterminals"),
                         namedSymbols(root["channels"], "channels")};
  validateGrammarSections(result, ArtifactProductions{1, {}}, sectionLimits);
  return result;
}

auto dumpProductionsJson(const ArtifactSymbols &symbols,
                         const ArtifactProductions &productions)
    -> std::string {
  validateGrammarSections(symbols, productions);
  OrderedJson values = OrderedJson::array();
  for (const ArtifactProduction &production : productions.productions) {
    OrderedJson rhs = OrderedJson::array();
    for (const ArtifactSymbolReference &reference : production.rhs) {
      rhs.push_back(OrderedJson{{"kind", symbolKindName(reference.kind)},
                                {"id", reference.id}});
    }
    values.push_back(OrderedJson{{"id", production.id},
                                 {"lhs", production.lhs},
                                 {"rhs", std::move(rhs)}});
  }
  return OrderedJson{{"version", productions.version},
                     {"productions", std::move(values)}}
             .dump(2) +
         '\n';
}

auto parseProductionsJson(std::string_view text, const ArtifactSymbols &symbols,
                          const ArtifactLoadLimits &loadLimits,
                          const GrammarSectionLimits &sectionLimits)
    -> ArtifactProductions {
  const Json root = parseJson(text, loadLimits.maximumSectionBytes);
  requireFields(root, productionsFields, "productions section");
  if (!root["productions"].is_array())
    fail(GrammarSectionErrorCode::InvalidJson, "productions must be an array");
  ArtifactProductions result{unsigned32(root["version"], "productions version"),
                             {}};
  for (const Json &value : root["productions"]) {
    requireFields(value, productionFields, "production");
    if (!value["rhs"].is_array())
      fail(GrammarSectionErrorCode::InvalidJson,
           "production RHS must be an array");
    ArtifactProduction production{unsigned32(value["id"], "production id"),
                                  unsigned32(value["lhs"], "production lhs"),
                                  {}};
    for (const Json &reference : value["rhs"]) {
      requireFields(reference, referenceFields, "symbol reference");
      production.rhs.push_back(
          {parseSymbolKind(stringValue(reference["kind"], "symbol kind")),
           unsigned32(reference["id"], "symbol id")});
    }
    result.productions.push_back(std::move(production));
  }
  validateGrammarSections(symbols, result, sectionLimits);
  return result;
}

} // namespace agas::artifact
