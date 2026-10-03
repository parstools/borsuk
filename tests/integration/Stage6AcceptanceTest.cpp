#include "agsem/SemanticCompleteness.h"
#include "agsem/Template.h"
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
using namespace agsem;
void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string(message));
}
auto replace(std::string text, std::string_view from, std::string_view to)
    -> std::string {
  const auto at = text.find(from);
  require(at != std::string::npos, "missing mutation anchor");
  return text.replace(at, from.size(), to);
}
auto action(const CompletenessReport &report, std::string_view rule,
            std::size_t ordinal = 0) -> const Obligation & {
  const auto found =
      std::ranges::find_if(report.obligations, [&](const auto &o) {
        return o.key.kind == ObligationKind::AnalysisAction && o.rule == rule &&
               o.ordinal == ordinal;
      });
  require(found != report.obligations.end(), "missing action obligation");
  return *found;
}
auto has(const CompletenessReport &report, std::string_view code) -> bool {
  return std::ranges::any_of(report.diagnostics,
                             [&](const auto &d) { return d.code == code; });
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      return 2;
    DocumentFrontend frontend{argv[1], argv[2]};
    ContractEnvironment contracts;
    require(
        contracts
            .addManifest(
                R"({"format":1,"id":"audit","version":"1","semantic":[{"name":"Context","kind":"opaque"},{"name":"ExprId","kind":"opaque"}]})")
            .empty(),
        "acceptance contracts");
    const auto parse = [&](const std::string &source) {
      auto parsed = frontend.parse(source);
      if (!parsed.value) {
        for (const auto &d : parsed.diagnostics)
          std::cerr << d.code << ": " << d.message << '\n';
        std::cerr << source << '\n';
        throw std::runtime_error("acceptance document parse");
      }
      return *parsed.value;
    };
    const auto review = [&](const std::string &source) {
      auto checked = assessCompleteness(parse(source), contracts);
      require(checked.value.has_value(), "acceptance report");
      return *checked.value;
    };
    const auto makeTemplate = [&](const std::string &source) {
      auto draft = templateFromAg(source, frontend);
      require(draft.value.has_value(), "acceptance template");
      return draft.value->text;
    };
    const std::string model =
        "semantic_model { rust_context Context; analyzer start() -> Int; }";
    const auto draft = makeTemplate(
        "grammar Audit; options { ast=explicit; } "
        "lexerClasses { NORMAL=true; OTHER=false; } "
        "node start -> enable(NORMAL) : value=ID #Start; ID:'a'; OTHER:'b';");
    auto complete = replace(draft, "grammar Audit;", "grammar Audit; " + model);
    complete = replace(complete, "state pending;", "state implemented;");
    complete =
        replace(complete, "analysis {\n    }", "analysis { result = 1; }");
    const auto original = parse(complete);
    const auto identity = original.analysisStatuses().front();
    require(review(complete).complete(), "complete fixture");

    const auto movedText = std::string("// Zażółć\n\n") + complete;
    const auto moved = parse(movedText);
    const auto movedReport = review(movedText);
    require(moved.analysisStatuses().front().id == identity.id &&
                moved.analysisStatuses().front().currentSyntaxSha256 ==
                    identity.currentSyntaxSha256 &&
                moved.identity() != original.identity() &&
                movedReport.complete(),
            "UTF-8 comments changed alternative identity or approval");
    const auto wire = nlohmann::json::parse(
        serializeCompleteness(movedReport, movedText, "moved.sema"));
    const auto emitted =
        std::ranges::find_if(wire.at("obligations"), [](const auto &o) {
          return o.at("key").at("kind") == "analysis_action";
        });
    const auto begin = action(movedReport, "start").location->beginByte;
    const auto line = 1 + std::ranges::count(movedText.substr(0, begin), '\n');
    require(emitted->at("location").at("line") == line &&
                emitted->at("location").at("column") ==
                    begin - movedText.rfind('\n', begin),
            "moved UTF-8 source location");
    auto unicodeText =
        replace(complete, "grammar Audit;", "grammar Audit; ID:'ą';");
    unicodeText = replace(unicodeText, "ID:'a';", "");
    const auto unicodeReport = review(unicodeText);
    require(unicodeReport.complete(), "UTF-8 lexer edit changed completeness");
    const auto unicodeWire = nlohmann::json::parse(
        serializeCompleteness(unicodeReport, unicodeText, "unicode.sema"));
    const auto unicodeAction =
        std::ranges::find_if(unicodeWire.at("obligations"), [](const auto &o) {
          return o.at("key").at("kind") == "analysis_action";
        });
    const auto unicodeBegin =
        action(unicodeReport, "start").location->beginByte;
    require(unicodeAction->at("location").at("begin_byte") == unicodeBegin &&
                unicodeAction->at("location").at("column") ==
                    unicodeBegin - unicodeText.rfind('\n', unicodeBegin) - 1,
            "UTF-8 columns counted bytes instead of characters");

    for (const auto &[from, to] :
         std::vector<std::pair<std::string, std::string>>{
             {"value=ID", "other=ID"},
             {"value=ID", "value=OTHER"},
             {"value=ID", "value=ID?"},
             {"#Start", "#Renamed"},
             {"node start", "node renamed"},
             {"enable(NORMAL)", "enable(OTHER)"}}) {
      const auto changed = parse(replace(complete, from, to));
      require(changed.analysisStatuses().front().id == identity.id &&
                  changed.analysisStatuses().front().currentSyntaxSha256 !=
                      identity.currentSyntaxSha256,
              "syntax mutation failed to invalidate signature");
      const auto report = review(replace(complete, from, to));
      require(
          !report.complete() && has(report, "completeness.stale_alternative") &&
              report.obligations.end() !=
                  std::ranges::find_if(
                      report.obligations,
                      [](const auto &o) {
                        return o.key.kind == ObligationKind::AnalysisAction &&
                               o.state == ObligationState::Pending;
                      }),
          "syntax mutation retained an implementation proof");
    }
    for (const auto &[from, to] :
         std::vector<std::pair<std::string, std::string>>{
             {"ID:'a'", "ID:'z'"},
             {"ast=explicit", "ast=implicit"},
             {"result = 1", "result = true"}}) {
      const auto changedText = replace(complete, from, to);
      const auto changed = parse(changedText);
      require(changed.identity() != original.identity() &&
                  changed.analysisStatuses().front().currentSyntaxSha256 ==
                      identity.currentSyntaxSha256,
              "nonstructural edit did not preserve signature and invalidate "
              "source proof");
      const auto report = review(changedText);
      require(!has(report, "completeness.stale_alternative"),
              "nonstructural edit incorrectly marked syntax stale");
      if (from == "result = 1")
        require(report.hasErrors() && !report.complete(),
                "edited action reused old validation");
    }
    const auto added = review(replace(
        complete, "ID:'a';", "node added : OTHER #Added analysis {}; ID:'a';"));
    require(action(added, "start").state == ObligationState::Implemented &&
                action(added, "added").state == ObligationState::Pending &&
                !action(added, "added").identityPersisted && !added.complete(),
            "new unmarked alternative inherited approval");
    ContractEnvironment revisedContracts;
    require(
        revisedContracts
            .addManifest(
                R"({"format":1,"id":"audit","version":"2","semantic":[{"name":"Context","kind":"opaque"},{"name":"ExprId","kind":"opaque"}]})")
            .empty(),
        "revised acceptance contracts");
    const auto revised = assessCompleteness(original, revisedContracts);
    require(revised.value && revised.value->complete() &&
                revised.value->sourceIdentity == original.identity() &&
                revised.value->contracts != review(complete).contracts,
            "contract edit reused the previous report identity");
    auto repeatedStatus = complete;
    repeatedStatus.insert(identity.location.endByte,
                          " " +
                              complete.substr(identity.location.beginByte,
                                              identity.location.endByte -
                                                  identity.location.beginByte));
    const auto repeatedMetadata = frontend.parse(repeatedStatus);
    require(!repeatedMetadata.value &&
                repeatedMetadata.diagnostics.front().code ==
                    "completeness.invalid_metadata" &&
                repeatedMetadata.diagnostics.front().related.size() == 1,
            "repeated status block lost its conflict location");
    const auto missingReason = frontend.parse(
        replace(complete, "state implemented;", "state no_action;"));
    require(!missingReason.value && missingReason.diagnostics.front().code ==
                                        "completeness.invalid_metadata",
            "no_action without a reason");
    const auto projection = projectAg(original);
    require(projection.value.has_value(), "acceptance Ag projection");
    const auto regenerated = parse(makeTemplate(projection.value->text));
    require(std::ranges::all_of(regenerated.analysisStatuses(),
                                [](const auto &s) {
                                  return s.declaredState ==
                                         ObligationState::Pending;
                                }),
            "regeneration recovered authored approvals");

    auto duplicate = makeTemplate("grammar Dup; node start : ID | ID; ID:'a';");
    auto records = parse(duplicate).analysisStatuses();
    auto copied = frontend.parse(
        replace(duplicate, records[1].id.value, records[0].id.value));
    require(!copied.value &&
                copied.diagnostics.front().code ==
                    "completeness.duplicate_alternative_id" &&
                copied.diagnostics.front().location &&
                copied.diagnostics.front().related.size() == 1 &&
                *copied.diagnostics.front().location !=
                    copied.diagnostics.front().related.front(),
            "duplicate identity lost conflict locations");
    duplicate = replace(duplicate, "state pending;",
                        "state no_action; reason \"first\";");
    records = parse(duplicate).analysisStatuses();
    const auto first = records[0].alternative;
    auto second = records[1].alternative;
    // The rest-alternative AST span also contains its leading separator.
    second.beginByte = duplicate.find("ID", second.beginByte);
    require(second.beginByte < second.endByte, "second duplicate RHS");
    auto swapped = duplicate;
    swapped.replace(
        second.beginByte, second.endByte - second.beginByte,
        duplicate.substr(first.beginByte, first.endByte - first.beginByte));
    swapped.replace(
        first.beginByte, first.endByte - first.beginByte,
        duplicate.substr(second.beginByte, second.endByte - second.beginByte));
    const auto reordered = parse(swapped).analysisStatuses();
    require(reordered[0].id == records[1].id &&
                reordered[1].id == records[0].id &&
                reordered[1].declaredState == ObligationState::NoAction &&
                reordered[0].declaredState == ObligationState::Pending,
            "identical RHS approvals followed positions instead of metadata");

    const std::string forwardingText =
        "sema Forward; grammar Forward; semantic_model { rust_context Context; "
        "analyzer start() -> Int; analyzer child() -> Int; } "
        "node start : part=child #Start analysis {}; node child : ID #Child "
        "analysis "
        "{ "
        "result = 1; }; ID:'a';";
    const auto forwarding = review(forwardingText);
    require(forwarding.complete() &&
                action(forwarding, "start").evidence ==
                    ObligationEvidence::CheckedForwarding &&
                action(forwarding, "start").dependsOn.size() >= 3,
            "checked empty forwarding was not complete");
    const auto prepare = [&](const std::string &source) {
      auto document = makeSemaDocument(parse(source));
      require(document.value.has_value(), "acceptance semantic document");
      auto bound = bindSemantics(*document.value, contracts);
      require(bound.value.has_value(), "acceptance semantic binding");
      return prepareSemanticModel(*bound.value);
    };
    const auto forwardPlan = prepare(forwardingText);
    if (!forwardPlan.value)
      for (const auto &d : forwardPlan.diagnostics)
        std::cerr << d.code << ": " << d.message << '\n';
    require(forwardPlan.value &&
                emitCheckedSemanticsRust(**forwardPlan.value)
                        .sema.find("analyze_child(ctx, node)") != std::string::npos,
            "checked forwarding cannot emit its child call");
    const auto recursive =
        review("sema Recursive; grammar Recursive; semantic_model { "
               "rust_context Context; "
               "analyzer start() -> Int; analyzer child() -> Int; } "
               "node start : '(' part=child ')' #Start analysis { analyze part "
               "-> value; result = value; } "
               "| ID #Leaf analysis { result = 1; }; node child : '[' "
               "part=start ']' #Child "
               "analysis { analyze part -> value; result = value; }; ID:'a';");
    require(recursive.complete(),
            "valid mutual recursion was treated as a hole");
    auto unitText = replace(complete, "-> Int", "-> Unit");
    unitText = replace(unitText, "state implemented;",
                       "state no_action; reason \"intentional\";");
    unitText = replace(unitText, "analysis { result = 1; }", "analysis {}");
    const auto unitPlan = prepare(unitText);
    require(
        review(unitText).complete() && unitPlan.value &&
            emitCheckedSemanticsRust(**unitPlan.value).sema.find("Ok(())") !=
                std::string::npos,
        "empty NoActionUnit body lost its generation proof");
    const auto letOnly =
        review(replace(complete, "result = 1;", "let value = 1;"));
    require(!letOnly.complete() &&
                has(letOnly, "completeness.invalid_implementation_claim"),
            "a let statement was accepted as the required result");
    auto exprNoAction = replace(complete, "-> Int", "-> ExprId");
    exprNoAction = replace(exprNoAction, "state implemented;",
                           "state no_action; reason \"none\";");
    exprNoAction =
        replace(exprNoAction, "analysis { result = 1; }", "analysis {}");
    require(has(review(exprNoAction), "completeness.invalid_no_action"),
            "no_action fabricated an ExprId result");
    auto repeat = letOnly;
    const auto once = serializeCompleteness(repeat, complete);
    finalizeCompleteness(repeat);
    require(serializeCompleteness(repeat, complete) == once,
            "report finalization duplicated warnings or changed order");
    std::cout
        << "Stage 6 acceptance: identity mutations, locations, regeneration, "
           "forwarding and recursive completeness OK\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
