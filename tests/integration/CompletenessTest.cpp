#include "coge/Completeness.h"
#include "agsem/SemanticCompleteness.h"
#include "agsem/Template.h"
#include "coge/GenerationPreparation.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <type_traits>
static_assert(!std::is_aggregate_v<coge::CogeValidation>);
static_assert(!std::is_default_constructible_v<coge::CogeValidation>);
namespace {
using namespace agsem;
void require(bool value, std::string_view message) {
  if (!value)
    throw std::runtime_error(std::string(message));
}
auto read(const std::filesystem::path &path) -> std::string {
  std::ifstream stream(path);
  return {std::istreambuf_iterator<char>(stream), {}};
}
auto action(const CompletenessReport &r, std::string_view rule)
    -> const Obligation & {
  const auto found = std::ranges::find_if(r.obligations, [&](const auto &o) {
    return o.key.kind == ObligationKind::AnalysisAction && o.rule == rule;
  });
  require(found != r.obligations.end(), "missing authored alternative");
  return *found;
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 4)
      return 2;
    DocumentFrontend frontend{argv[1], argv[2]};
    const std::filesystem::path repo{argv[3]};
    ContractEnvironment contracts;
    require(
        contracts
            .addManifest(
                R"({"format":1,"id":"complete-test","version":"1","semantic":[{"name":"Context","kind":"opaque"}]})")
            .empty(),
        "test manifest");
    const auto parse = [&](const std::string &text) {
      auto parsed = frontend.parse(text);
      if (!parsed.value) {
        for (const auto &d : parsed.diagnostics)
          std::cerr << d.message << '\n';
        throw std::runtime_error("test document parse");
      }
      return *parsed.value;
    };
    const auto review = [&](const std::string &text) {
      auto r = assessCompleteness(parse(text), contracts);
      require(r.value.has_value(), "report must survive validation errors");
      return *r.value;
    };
    const std::string prefix =
        "sema Pilot; grammar Pilot; semantic_model { rust_context Context; "
        "analyzer start() -> Int; } ";
    const std::string suffix = "; ID:'a';";
    auto complete = review(
        prefix + "node start : ID #Start analysis { result = 1; }" + suffix);
    require(complete.complete() && complete.results.front().canEmit,
            "checked result completeness");
    require(action(complete, "start").state == ObligationState::Implemented &&
                !action(complete, "start").identityPersisted,
            "legacy inference");
    auto missing =
        review(prefix + "node start : ID #Start analysis {}" + suffix);
    require(!missing.complete() && !missing.hasErrors() &&
                action(missing, "start").state == ObligationState::Pending,
            "missing result is a warning");
    auto unmarked = review(prefix + "node start : ID #Start" + suffix);
    require(!unmarked.complete() && !unmarked.hasErrors(),
            "removing metadata cannot hide an obligation");
    auto wrong = review(prefix +
                        "node start : ID #Start analysis { require 1 else "
                        "error \"bad\"; result = true; }" +
                        suffix);
    require(wrong.hasErrors() &&
                std::ranges::count_if(wrong.diagnostics,
                                      [](const auto &d) {
                                        return d.code ==
                                               "action.invalid_alternative";
                                      }) == 2,
            "independent statement errors");
    const auto draft = templateFromAg(
        "grammar Pilot; node start : ID #Start; ID:'a';", frontend);
    require(draft.value.has_value(), "template");
    const auto status = parse(draft.value->text).analysisStatuses().front();
    const auto metadata = [&](std::string state) {
      return "analysis_status { id \"" + status.id.value +
             "\"; syntax_sha256 \"" + status.syntaxSha256 + "\"; state " +
             state + "; } ";
    };
    auto pending =
        review(prefix + "node start : ID #Start " + metadata("pending") +
               "analysis { result = 1; }" + suffix);
    require(!pending.complete() && action(pending, "start").validation ==
                                       ObligationValidation::Valid,
            "explicit pending survives checked body");
    const auto rejectedSemantics = [&](const std::string &text) {
      auto doc = makeSemaDocument(parse(text));
      require(doc.value.has_value(), "semantic document");
      auto binding = bindSemantics(*doc.value, contracts);
      require(binding.value.has_value(), "semantic binding");
      require(!prepareSemanticModel(*binding.value).value,
              "incomplete semantics admitted to generation");
    };
    rejectedSemantics(prefix + "node start : ID #Start " + metadata("pending") +
                      "analysis { result = 1; }" + suffix);
    rejectedSemantics(prefix + "node start : ID #Start analysis {}" + suffix);
    rejectedSemantics(prefix +
                      "node start : part=child #Start analysis { "
                      "analyze part -> value; result = value; }; "
                      "node child : ID #Child analysis {}" +
                      suffix);
    auto claim = review(prefix + "node start : ID #Start " +
                        metadata("implemented") + "analysis {}" + suffix);
    require(claim.hasErrors(), "false implemented claim");
    auto pendingWrong =
        review(prefix + "node start : ID #Start " + metadata("pending") +
               "analysis { result = true; }" + suffix);
    require(pendingWrong.hasErrors() &&
                action(pendingWrong, "start").state == ObligationState::Pending,
            "pending must not suppress type checking");
    const std::string unitPrefix =
        "sema Pilot; grammar Pilot; semantic_model { rust_context Context; "
        "analyzer start() -> Unit; } ";
    auto noActionText = unitPrefix + "node start : ID #Start " +
                        metadata("no_action; reason \"intentional\"") + suffix;
    auto noAction = review(noActionText);
    require(noAction.complete() &&
                action(noAction, "start").state == ObligationState::NoAction,
            "checked NoActionUnit");
    auto document = makeSemaDocument(parse(noActionText));
    auto bound = bindSemantics(*document.value, contracts);
    auto prepared = prepareSemanticModel(*bound.value);
    if (!prepared.value)
      for (const auto &d : prepared.diagnostics)
        std::cerr << d.code << ": " << d.message << '\n';
    require(prepared.value.has_value(), "NoActionUnit preparation proof");
    const auto noActionRust = emitCheckedSemanticsRust(**prepared.value);
    if (noActionRust.sema.find("Ok(())") == std::string::npos)
      std::cerr << noActionRust.sema << '\n';
    require(noActionRust.sema.find("Ok(())") != std::string::npos,
            "NoActionUnit emission proof");
    require(review(prefix + "node start : ID #Start " +
                   metadata("no_action; reason \"intentional\"") + suffix)
                .hasErrors(),
            "no_action cannot return Int");
    require(review(unitPrefix + "node start : ID #Start " +
                   metadata("no_action; reason \"intentional\"") +
                   "analysis { result = unit; }" + suffix)
                .hasErrors(),
            "no_action cannot contain instructions");
    auto stale =
        review(prefix + "node start : ID ID #Start " + metadata("implemented") +
               "analysis { result = 1; }" + suffix);
    require(stale.hasErrors() &&
                action(stale, "start").state == ObligationState::Pending,
            "stale signature");
    auto hole = review(
        "sema Pilot; grammar Pilot; semantic_model { rust_context Context; "
        "analyzer start() -> Int; } node start : part=child #Start analysis { "
        "analyze part -> value; require 1 else error \"bad\"; result = value; "
        "}; node child : ID #Child analysis {}; ID:'a';");
    require(hole.hasErrors() &&
                std::ranges::any_of(
                    action(hole, "start").dependsOn,
                    [](const auto &key) {
                      return key.kind == ObligationKind::AnalyzerInterface &&
                             std::get<AnalyzerSubject>(key.subject).name ==
                                 "child";
                    }),
            "dependency holes retain independent errors");
    auto unresolvedIf =
        review("sema Pilot; grammar Pilot; semantic_model { rust_context "
               "Context; } node start : ID #Start analysis { if true { result "
               "= 1; } else { result = 2; } }; ID:'a';");
    require(!unresolvedIf.hasErrors() && !unresolvedIf.complete(),
            "unresolved expected branch result is blocked, not a type error");
    auto badFunction = review(
        "sema Pilot; grammar Pilot; semantic_model { rust_context Context; "
        "analyzer start() -> Int; function helper() -> Int { return true; } } "
        "node start : ID #Start analysis { result = helper(); }; ID:'a';");
    require(badFunction.hasErrors() &&
                action(badFunction, "start").state ==
                    ObligationState::Pending &&
                action(badFunction, "start").validation ==
                    ObligationValidation::Blocked,
            "an action depends on a checked function implementation");
    const auto serialized = nlohmann::json::parse(
        serializeCompleteness(pendingWrong, noActionText, "pilot.sema"));
    require(serialized.at("format") == "agsem-completeness-v1" &&
                serialized.at("obligations")
                    .front()
                    .at("location")
                    .contains("line"),
            "versioned located report");
    // Unsupported emitter shapes can be semantically complete.
    const auto emptyDraft = templateFromAg(
        "grammar Pilot; node start : empty #Start; ID:'a';", frontend);
    const auto emptyStatus =
        parse(emptyDraft.value->text).analysisStatuses().front();
    auto emptyText =
        unitPrefix + "node start : empty #Start analysis_status { id \"" +
        emptyStatus.id.value + "\"; syntax_sha256 \"" +
        emptyStatus.syntaxSha256 +
        "\"; state no_action; reason \"empty language\"; }; ID:'a';";
    auto empty = review(emptyText);
    require(empty.complete() && !empty.results.front().canEmit,
            "completeness is separate from emitter support");
    const auto cogeText =
        std::string("coge") +
        (prefix + "node start : ID #Start analysis { result = 1; }" + suffix)
            .substr(4);
    const auto cogeCheck = [&](std::string text,
                               std::vector<CompletenessTarget> targets) {
      auto assessed = coge::assessCompleteness(parse(text), contracts, targets);
      require(assessed.value.has_value(), "coge report");
      return *assessed.value;
    };
    require(cogeCheck(cogeText, {}).complete(),
            "analysis-only coge has no implicit backend");
    const auto admit = [&](const std::string &text) {
      auto doc = coge::makeCogeDocument(parse(text));
      require(doc.value.has_value(), "execution document");
      auto binding = bindSemantics(doc.value->semantics(), contracts);
      require(binding.value.has_value(), "execution semantic binding");
      auto semantics = prepareSemanticModel(*binding.value);
      require(semantics.value.has_value(), "execution checked semantics");
      return coge::prepareGeneration(*doc.value, **semantics.value,
                                     coge::generationSelection(*doc.value),
                                     contracts);
    };
    auto analysisOnly = admit(cogeText);
    require(analysisOnly.value.has_value(),
            "complete analysis-only generation");
    require(
        coge::emitCheckedGeneration(*analysisOnly.value).interpreter.empty(),
        "analysis-only model unexpectedly emitted an interpreter");
    for (const std::string target : {"c", "interpreter"}) {
      auto rejected = admit(cogeText + " execution_obligations { targets \"" +
                            target + "\"; }");
      require(!rejected.value &&
                  std::ranges::any_of(rejected.diagnostics,
                                      [](const auto &d) {
                                        return d.code ==
                                                   "completeness.pending" &&
                                               d.severity == Severity::Error;
                                      }),
              "deleting pending entries bypassed generation admission");
    }
    auto c = cogeCheck(cogeText, {CompletenessTarget::C});
    require(!c.complete() && !c.hasErrors() &&
                std::ranges::any_of(
                    c.obligations,
                    [](const auto &o) {
                      return o.key.kind == ObligationKind::BackendBinding &&
                             o.state == ObligationState::Pending;
                    }),
            "selected C needs lowering and backend");
    require(
        !cogeCheck(cogeText + " execution_obligations { targets \"c\"; }", {})
             .complete(),
        "targets without pending still enforced");
    auto interpreter = cogeCheck(cogeText, {CompletenessTarget::Interpreter});
    require(!interpreter.complete() &&
                std::ranges::none_of(interpreter.obligations,
                                     [](const auto &o) {
                                       return o.key.kind ==
                                              ObligationKind::ExecuteHandler;
                                     }),
            "unknown IR does not fabricate handlers");
    for (const std::string language : {"toyc", "toycp"}) {
      ContractEnvironment runtime;
      const auto manifest =
          read(repo / ("contracts/" + language + "-runtime-v1.json"));
      require(runtime.addManifest(manifest).empty(), "real manifest");
      const auto parsed =
          parse(read(repo / ("agsem/" + language + "_typed.coge")));
      const auto real = coge::assessCompleteness(parsed, runtime);
      require(real.value && real.value->complete(),
              "real execution profile is complete");
      require(std::ranges::any_of(real.value->obligations,
                                  [](const auto &o) {
                                    return o.evidence ==
                                           ObligationEvidence::CheckedProfile;
                                  }),
              "profile evidence");
      auto overrideText = read(repo / ("agsem/" + language + "_typed.coge"));
      const auto contractAt = overrideText.find("execution_contract {");
      require(contractAt != std::string::npos, "real execution contract");
      overrideText.insert(
          contractAt + std::string("execution_contract {").size(),
          "\n    evaluate Load(place) { return load(place, source); }\n");
      const auto overridden =
          coge::assessCompleteness(parse(overrideText), runtime);
      require(overridden.value && overridden.value->complete() &&
                  std::ranges::any_of(
                      overridden.value->obligations,
                      [](const auto &o) {
                        return o.key.kind == ObligationKind::EvaluateHandler &&
                               o.key.slot == "Load" &&
                               o.evidence == ObligationEvidence::CheckedBody;
                      }),
              "effective local handler lost completeness evidence");
      const auto forbiddenNoAction = frontend.parse(
          overrideText +
          " execution_obligations { targets \"interpreter\"; "
          "no_action \"evaluate_handler\", \"ExpressionKind\", \"Load\", "
          "\"none\"; }");
      require(!forbiddenNoAction.value &&
                  !forbiddenNoAction.diagnostics.empty(),
              "no_action must not approve an execution handler");
      auto extended = nlohmann::json::parse(manifest);
      for (auto &s : extended.at("semantic"))
        if (s.at("name") == "Operation")
          s.at("variants")["NewOperation"] = nlohmann::json::array();
      ContractEnvironment newRuntime;
      require(newRuntime.addManifest(extended.dump()).empty(),
              "extended manifest");
      auto changed = coge::assessCompleteness(parsed, newRuntime);
      require(changed.value && !changed.value->complete() &&
                  std::ranges::any_of(
                      changed.value->obligations,
                      [](const auto &o) {
                        return o.key.kind == ObligationKind::ExecuteHandler &&
                               o.key.slot == "NewOperation" &&
                               o.state == ObligationState::Pending;
                      }),
              "new IR variant requires a handler");
      auto doc = coge::makeCogeDocument(parsed);
      auto binding = bindSemantics(doc.value->semantics(), newRuntime);
      require(binding.value.has_value(), "extended semantic binding");
      require(!coge::checkCoge(*doc.value, *binding.value, newRuntime).value,
              "new IR variant admitted to checked generation");
    }
    std::cout << "Located completeness, fragment checking, NoActionUnit and "
                 "execution target coverage OK\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
