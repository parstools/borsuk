#include "agas/artifact/LexerSection.h"

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
constexpr std::array contextRootFields{"version", "rules", "dfaStates", "orderedNfas", "context"};
constexpr std::array contextFields{"originalTerminals", "requiredClasses", "rows"};
constexpr std::array contextNodeFields{"active", "edges"};
constexpr std::array contextEdgeFields{"terminal", "target"};
constexpr std::array rootFields{"version", "rules", "dfaStates", "orderedNfas"};
constexpr std::array ruleFields{"name", "terminal", "channel", "skipped"};
constexpr std::array dfaFields{"acceptingRule", "transitions"};
constexpr std::array transitionFields{"ranges", "target"};
constexpr std::array rangeFields{"first", "last"};
constexpr std::array nfaFields{"startState", "acceptingState", "states"};
constexpr std::array nfaStateFields{"epsilonTransitions", "transitions",
                                    "orderedDecision", "activatesPriority"};

[[noreturn]] void invalid(std::string message) {
  throw std::invalid_argument(std::move(message));
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
      invalid("unknown " + std::string{context} + " field: " + key);
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
auto optionalU32(const Json &value, std::string_view field)
    -> std::optional<std::uint32_t> {
  if (value.is_null())
    return std::nullopt;
  return u32(value, field);
}
auto stringValue(const Json &value, std::string_view field) -> std::string {
  if (!value.is_string())
    invalid(std::string{field} + " must be a string");
  return value.get<std::string>();
}
auto boolean(const Json &value, std::string_view field) -> bool {
  if (!value.is_boolean())
    invalid(std::string{field} + " must be a boolean");
  return value.get<bool>();
}
auto strictJson(std::string_view source, std::uint64_t maximumBytes) -> Json {
  if (source.size() > maximumBytes)
    throw std::length_error("lexer JSON exceeds configured limits");
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
  } catch (const std::invalid_argument &) {
    throw;
  } catch (const Json::exception &error) {
    invalid("invalid lexer JSON: " + std::string{error.what()});
  }
}
auto rangesJson(const std::vector<ArtifactCodePointRange> &ranges)
    -> OrderedJson {
  OrderedJson result = OrderedJson::array();
  for (const auto range : ranges)
    result.push_back({{"first", range.first}, {"last", range.last}});
  return result;
}
auto transitionsJson(const std::vector<ArtifactLexerTransition> &transitions)
    -> OrderedJson {
  OrderedJson result = OrderedJson::array();
  for (const auto &transition : transitions)
    result.push_back({{"ranges", rangesJson(transition.ranges)},
                      {"target", transition.target}});
  return result;
}
auto parseTransitions(const Json &values)
    -> std::vector<ArtifactLexerTransition> {
  if (!values.is_array())
    invalid("lexer transitions must be an array");
  std::vector<ArtifactLexerTransition> result;
  result.reserve(values.size());
  for (const Json &value : values) {
    requireFields(value, transitionFields, "lexer transition");
    if (!value["ranges"].is_array())
      invalid("lexer ranges must be an array");
    ArtifactLexerTransition transition{
        {}, u32(value["target"], "transition target")};
    for (const Json &range : value["ranges"]) {
      requireFields(range, rangeFields, "code point range");
      transition.ranges.push_back({u32(range["first"], "range first"),
                                   u32(range["last"], "range last")});
    }
    result.push_back(std::move(transition));
  }
  return result;
}
} // namespace

auto dumpLexerJson(const ArtifactLexer &lexer, std::size_t terminalCount,
                   std::size_t channelCount) -> std::string {
  validateLexerSection(lexer, terminalCount, channelCount);
  OrderedJson rules = OrderedJson::array();
  for (const auto &rule : lexer.rules)
    rules.push_back({{"name", rule.name},
                     {"terminal", rule.terminal},
                     {"channel", rule.channel},
                     {"skipped", rule.skipped}});
  OrderedJson dfaStates = OrderedJson::array();
  for (const auto &state : lexer.dfaStates)
    dfaStates.push_back({{"acceptingRule", state.acceptingRule},
                         {"transitions", transitionsJson(state.transitions)}});
  OrderedJson nfas = OrderedJson::array();
  for (const auto &nfa : lexer.orderedNfas) {
    OrderedJson states = OrderedJson::array();
    for (const auto &state : nfa.states)
      states.push_back({{"epsilonTransitions", state.epsilonTransitions},
                        {"transitions", transitionsJson(state.transitions)},
                        {"orderedDecision", state.orderedDecision},
                        {"activatesPriority", state.activatesPriority}});
    nfas.push_back({{"startState", nfa.startState},
                    {"acceptingState", nfa.acceptingState},
                    {"states", std::move(states)}});
  }
  OrderedJson result{{"version", lexer.version},
                     {"rules", std::move(rules)},
                     {"dfaStates", std::move(dfaStates)},
                     {"orderedNfas", std::move(nfas)}};
  if (lexer.context) {
    OrderedJson rows = OrderedJson::array();
    for (const auto &row : lexer.context->rows) {
      OrderedJson nodes = OrderedJson::array();
      for (const auto &node : row) {
        OrderedJson edges = OrderedJson::array();
        for (const auto &edge : node.edges)
          edges.push_back({{"terminal", edge.terminal}, {"target", edge.target}});
        nodes.push_back({{"active", node.active}, {"edges", std::move(edges)}});
      }
      rows.push_back(std::move(nodes));
    }
    result["context"] = {{"originalTerminals", lexer.context->originalTerminals},
                          {"requiredClasses", lexer.context->requiredClasses},
                          {"rows", std::move(rows)}};
  }
  return result.dump(2) + '\n';
}

auto parseLexerJson(std::string_view source, std::size_t terminalCount,
                    std::size_t channelCount,
                    const ArtifactLoadLimits &loadLimits,
                    const LexerSectionLimits &sectionLimits) -> ArtifactLexer {
  const Json root = strictJson(source, loadLimits.maximumSectionBytes);
  if (root.value("version", 0) == 2)
    requireFields(root, contextRootFields, "lexer section");
  else requireFields(root, rootFields, "lexer section");
  if (!root["rules"].is_array() || !root["dfaStates"].is_array() ||
      !root["orderedNfas"].is_array())
    invalid("lexer section lists must be arrays");
  ArtifactLexer result{u32(root["version"], "lexer version"), {}, {}, {}};
  for (const Json &value : root["rules"]) {
    requireFields(value, ruleFields, "lexer rule");
    result.rules.push_back({stringValue(value["name"], "lexer rule name"),
                            optionalU32(value["terminal"], "lexer terminal"),
                            optionalU32(value["channel"], "lexer channel"),
                            boolean(value["skipped"], "lexer skipped")});
  }
  for (const Json &value : root["dfaStates"]) {
    requireFields(value, dfaFields, "DFA state");
    result.dfaStates.push_back(
        {optionalU32(value["acceptingRule"], "accepting rule"),
         parseTransitions(value["transitions"])});
  }
  for (const Json &value : root["orderedNfas"]) {
    requireFields(value, nfaFields, "ordered NFA");
    if (!value["states"].is_array())
      invalid("NFA states must be an array");
    ArtifactOrderedNfa nfa{u32(value["startState"], "NFA start state"),
                           u32(value["acceptingState"], "NFA accepting state"),
                           {}};
    for (const Json &state : value["states"]) {
      requireFields(state, nfaStateFields, "NFA state");
      if (!state["epsilonTransitions"].is_array())
        invalid("epsilon transitions must be an array");
      ArtifactNfaState parsedState{
          {},
          parseTransitions(state["transitions"]),
          boolean(state["orderedDecision"], "ordered decision"),
          boolean(state["activatesPriority"], "priority activation")};
      for (const Json &target : state["epsilonTransitions"])
        parsedState.epsilonTransitions.push_back(u32(target, "epsilon target"));
      nfa.states.push_back(std::move(parsedState));
    }
    result.orderedNfas.push_back(std::move(nfa));
  }
  if (result.version == 2) {
    const auto &context = root["context"];
    requireFields(context, contextFields, "lexer context");
    if (!context["originalTerminals"].is_array() || !context["requiredClasses"].is_array() ||
        !context["rows"].is_array()) invalid("lexer context fields must be arrays");
    result.context.emplace();
    for (const auto &id : context["originalTerminals"])
      result.context->originalTerminals.push_back(u32(id, "original terminal"));
    for (const auto &mask : context["requiredClasses"]) {
      if (!mask.is_number_unsigned()) invalid("class mask must be an unsigned 64-bit integer");
      result.context->requiredClasses.push_back(mask.get<std::uint64_t>());
    }
    for (const auto &row : context["rows"]) {
      if (!row.is_array()) invalid("lexer context row must be an array");
      std::vector<ArtifactContextNode> nodes;
      for (const auto &node : row) {
        requireFields(node, contextNodeFields, "lexer context node");
        if (!node["active"].is_number_unsigned() || !node["edges"].is_array())
          invalid("invalid lexer context node");
        ArtifactContextNode parsed{node["active"].get<std::uint64_t>(), {}};
        for (const auto &edge : node["edges"]) {
          requireFields(edge, contextEdgeFields, "lexer context edge");
          parsed.edges.push_back({optionalU32(edge["terminal"], "context terminal"),
                                  u32(edge["target"], "context target")});
        }
        nodes.push_back(std::move(parsed));
      }
      result.context->rows.push_back(std::move(nodes));
    }
  }
  validateLexerSection(result, terminalCount, channelCount, sectionLimits);
  return result;
}
} // namespace agas::artifact
