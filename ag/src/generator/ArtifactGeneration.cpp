#include "agas/generator/ArtifactGeneration.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include <nlohmann/json.hpp>

#include "agas/artifact/Sha256.h"
#include "agas/generator/TableExport.h"

namespace agas::generator {
namespace {

auto artifactId(std::size_t value, const char *kind) -> std::uint32_t {
  if (value > std::numeric_limits<std::uint32_t>::max())
    throw std::overflow_error(std::string{kind} + " ID exceeds uint32 range");
  return static_cast<std::uint32_t>(value);
}

auto repetitionName(zbik::Repetition repetition) -> std::string_view {
  switch (repetition) {
  case zbik::Repetition::One: return "one";
  case zbik::Repetition::Optional: return "optional";
  case zbik::Repetition::ZeroOrMore: return "zero-or-more";
  case zbik::Repetition::OneOrMore: return "one-or-more";
  }
  throw std::logic_error("unknown repetition");
}

auto roleName(zbik::EbnfGeneratedRuleRole role) -> std::string_view {
  switch (role) {
  case zbik::EbnfGeneratedRuleRole::SourceAlternative: return "source";
  case zbik::EbnfGeneratedRuleRole::OptionalPresent: return "optional-present";
  case zbik::EbnfGeneratedRuleRole::OptionalEmpty: return "optional-empty";
  case zbik::EbnfGeneratedRuleRole::RepetitionRecursive: return "repetition-recursive";
  case zbik::EbnfGeneratedRuleRole::RepetitionBase: return "repetition-base";
  }
  throw std::logic_error("unknown production role");
}

auto artifactRanges(const zbik::CodePointClass &codePoints)
    -> std::vector<artifact::ArtifactCodePointRange> {
  std::vector<artifact::ArtifactCodePointRange> result;
  for (const zbik::CodePointRange range : codePoints.ranges()) {
    if (range.first < zbik::firstHighSurrogate)
      result.push_back(
          {range.first, std::min(range.last, zbik::firstHighSurrogate - 1U)});
    if (range.last > zbik::lastLowSurrogate)
      result.push_back(
          {std::max(range.first, zbik::lastLowSurrogate + 1U), range.last});
  }
  return result;
}

auto artifactTransition(const zbik::LexerAutomatonTransition &transition)
    -> artifact::ArtifactLexerTransition {
  return {artifactRanges(transition.codePoints),
          artifactId(transition.target, "lexer state")};
}

auto artifactTransition(const zbik::NfaByteTransition &transition)
    -> artifact::ArtifactLexerTransition {
  return {artifactRanges(transition.bytes), transition.target.value};
}

auto packageDiagnostics(const model::SyntaxDocument &document,
                        const GeneratedParserTable &generated)
    -> std::string {
  nlohmann::ordered_json root;
  root["version"] = 1;
  root["productionCoverage"] = nlohmann::ordered_json::array();
  for (const DiagnosticProductionMetadata &metadata :
       generated.productions().diagnostic()) {
    root["productionCoverage"].push_back(
        {{"id", metadata.rule.value},
         {"stableIdentity", metadata.stableIdentity},
         {"rule", metadata.sourceRuleName},
         {"alternative", metadata.alternativeIndex},
         {"alternativeLabel", metadata.alternativeLabel},
         {"element", metadata.elementIndex},
         {"helperName", metadata.helperName},
         {"repetition", repetitionName(metadata.repetition)},
         {"role", roleName(metadata.role)},
         {"sourceLine", metadata.sourceSpan.begin.line},
         {"sourceColumn", metadata.sourceSpan.begin.column}});
  }
  root["resolvedConflicts"] = nlohmann::ordered_json::array();
  const zbik::Grammar &grammar = generated.bnf().grammar();
  for (const ResolvedConflict &resolution : generated.resolvedConflicts()) {
    const model::ConflictPreference &preference =
        document.conflictPreferences.at(resolution.preferenceIndex);
    nlohmann::ordered_json lookahead = nlohmann::ordered_json::array();
    for (const zbik::LookaheadSymbol &symbol :
         resolution.original.lookahead().symbols()) {
      if (const auto *terminal = std::get_if<zbik::TerminalId>(&symbol))
        lookahead.push_back(grammar.terminalName(*terminal));
      else
        lookahead.push_back("EOF");
    }
    std::optional<zbik::StateId> shiftTarget;
    std::optional<zbik::RuleId> reduceRule;
    for (const zbik::Action &action : resolution.original.actions()) {
      if (const auto *shift = std::get_if<zbik::Shift>(&action))
        shiftTarget = shift->target;
      if (const auto *reduce = std::get_if<zbik::Reduce>(&action))
        reduceRule = reduce->rule;
    }
    if (!shiftTarget || !reduceRule)
      throw std::logic_error("resolved conflict is not shift/reduce");
    root["resolvedConflicts"].push_back(
        {{"state", resolution.original.state().value},
         {"lookahead", std::move(lookahead)},
         {"shiftTarget", shiftTarget->value},
         {"reduceProduction", reduceRule->value},
         {"reduceRule", preference.reduceRule},
         {"reduceAlternative", preference.reduceAlternative},
         {"selected", preference.choice == model::ConflictPreference::Choice::Shift
                          ? "shift" : "reduce"},
         {"sourceBeginByte", preference.span.begin.offset},
         {"sourceEndByte", preference.span.end.offset}});
  }
  return root.dump(2) + '\n';
}

} // namespace

auto buildGrammarArtifactSections(const model::SyntaxDocument &document,
                                  const GeneratedParserTable &generated)
    -> std::pair<artifact::ArtifactSymbols, artifact::ArtifactProductions> {
  const zbik::Grammar &grammar = generated.bnf().grammar();
  artifact::ArtifactSymbols symbols;
  symbols.version = 1;
  symbols.terminals.reserve(grammar.terminalCount());
  for (std::size_t index = 0; index < grammar.terminalCount(); ++index) {
    const zbik::TerminalId id{artifactId(index, "terminal")};
    symbols.terminals.push_back({id.value, grammar.terminalName(id)});
  }
  symbols.nonterminals.reserve(grammar.nonterminalCount());
  for (std::size_t index = 0; index < grammar.nonterminalCount(); ++index) {
    const zbik::NonterminalId id{artifactId(index, "nonterminal")};
    symbols.nonterminals.push_back({id.value, grammar.nonterminalName(id)});
  }
  symbols.channels.reserve(document.channels.size());
  for (std::size_t index = 0; index < document.channels.size(); ++index) {
    symbols.channels.push_back(
        {artifactId(index, "channel"), document.channels[index].name});
  }

  artifact::ArtifactProductions productions;
  productions.version = 1;
  productions.productions.reserve(grammar.ruleCount());
  for (const zbik::Rule &rule : grammar.rules()) {
    artifact::ArtifactProduction production{
        rule.id().value, rule.lhs().value, {}};
    production.rhs.reserve(rule.size());
    for (const zbik::SymbolRef &reference : rule.rhs()) {
      std::visit(
          [&production](const auto id) {
            using Id = std::remove_cv_t<decltype(id)>;
            constexpr auto kind =
                std::is_same_v<Id, zbik::TerminalId>
                    ? artifact::ArtifactSymbolKind::Terminal
                    : artifact::ArtifactSymbolKind::Nonterminal;
            production.rhs.push_back({kind, id.value});
          },
          reference);
    }
    productions.productions.push_back(std::move(production));
  }
  artifact::validateGrammarSections(symbols, productions);
  return {std::move(symbols), std::move(productions)};
}

auto buildLexerArtifactSection(const GeneratedLexerAutomaton &lexer,
                               const artifact::ArtifactSymbols &symbols, bool allNfas)
    -> artifact::ArtifactLexer {
  artifact::ArtifactLexer result;
  result.version = 1;
  result.rules.reserve(lexer.rules().size());
  for (const CompiledLexerRule &rule : lexer.rules()) {
    std::optional<std::uint32_t> channel;
    if (rule.channel) {
      const auto found =
          std::ranges::find(symbols.channels, *rule.channel,
                            &artifact::ArtifactNamedSymbol::name);
      if (found == symbols.channels.end())
        throw std::invalid_argument("lexer rule refers to an unknown channel");
      channel = found->id;
    }
    result.rules.push_back(
        {rule.name,
         rule.terminal ? std::optional{rule.terminal->value} : std::nullopt,
         channel, rule.skipped});
  }

  const zbik::Utf8Lexer &runtime = lexer.runtimeLexer();
  const zbik::LexerAutomaton &dfa = runtime.automaton();
  result.dfaStates.reserve(dfa.stateCount());
  for (std::size_t state = 0; state < dfa.stateCount(); ++state) {
    artifact::ArtifactDfaState target;
    if (const auto accepting = dfa.acceptingRule(state))
      target.acceptingRule = artifactId(*accepting, "lexer rule");
    for (const auto &transition : dfa.transitions(state)) {
      auto artifact = artifactTransition(transition);
      if (!artifact.ranges.empty())
        target.transitions.push_back(std::move(artifact));
    }
    result.dfaStates.push_back(std::move(target));
  }

  std::vector<zbik::RegexNfa> completeNfas;
  if (allNfas)
    for (const auto &rule : lexer.rules()) completeNfas.push_back(zbik::RegexNfa::fromRegex(rule.expression));
  const auto nfas = allNfas ? std::span<const zbik::RegexNfa>{completeNfas} : runtime.prioritizedNfas();
  for (const zbik::RegexNfa &nfa : nfas) {
    artifact::ArtifactOrderedNfa target{
        nfa.startState().value, nfa.acceptingState().value, {}};
    target.states.reserve(nfa.states().size());
    for (const zbik::NfaState &state : nfa.states()) {
      artifact::ArtifactNfaState artifactState;
      artifactState.orderedDecision = state.orderedDecision;
      artifactState.activatesPriority = state.activatesPriority;
      for (const zbik::NfaStateId epsilon : state.epsilonTransitions)
        artifactState.epsilonTransitions.push_back(epsilon.value);
      for (const auto &transition : state.byteTransitions) {
        auto artifact = artifactTransition(transition);
        if (!artifact.ranges.empty())
          artifactState.transitions.push_back(std::move(artifact));
      }
      target.states.push_back(std::move(artifactState));
    }
    result.orderedNfas.push_back(std::move(target));
  }
  // The contextual caller installs its v2 metadata before validating the lexer.
  if (!allNfas) artifact::validateLexerSection(result, symbols.terminals.size(),
                                               symbols.channels.size());
  return result;
}

auto buildParserArtifactPackage(const model::SyntaxDocument &document,
                                const GeneratedParserTable &generated,
                                const GeneratedLexerAutomaton &lexer,
                                std::string_view exactSource,
                                std::string_view expandedSource,
                                const ArtifactBuildIdentity &identity,
                                std::optional<artifact::ArtifactLexer> contextualLexer)
    -> artifact::ArtifactPackage {
  if (document.parserRules.empty() || identity.generatorVersion.empty() ||
      identity.zbikRevision.empty() || identity.unicodeVersion.empty())
    throw std::invalid_argument(
        "artifact package requires a root rule and complete build identity");
  auto [symbols, productions] =
      buildGrammarArtifactSections(document, generated);
  const artifact::ArtifactLexer artifactLexer =
      contextualLexer ? std::move(*contextualLexer) : buildLexerArtifactSection(lexer, symbols);
  const std::string algorithm =
      generated.configuration().algorithm == ParserAlgorithm::Lalr
          ? "lalr"
          : "canonical-lr";
  std::string settings =
      "artifact-format=1\nparser=" + algorithm +
      "\nlookahead=" + std::to_string(generated.configuration().lookahead) +
      "\nlexer=ordered-nfa-v1\ncompression=default-reduction-v1\n"
      "reduction=v1\n";
  if (artifactLexer.context) settings += "lexer-context=v2\n";
  if (!generated.resolvedConflicts().empty())
    settings += "conflict-resolution=exact-shift-reduce-v1\n";
  artifact::ArtifactManifest manifest{
      1,
      algorithm,
      artifactId(generated.configuration().lookahead, "lookahead"),
      generated.bnf().grammar().nonterminalName(generated.bnf().grammar().start()),
      document.parserRules.front().name,
      identity.generatorVersion,
      identity.zbikRevision,
      identity.unicodeVersion,
      artifact::sha256Hex(exactSource),
      artifact::sha256Hex(expandedSource),
      artifact::sha256Hex(settings),
      {}};
  artifact::ArtifactPackageSections sections{
      artifact::dumpSymbolsJson(symbols),
      artifact::dumpLexerJson(artifactLexer, symbols.terminals.size(),
                              symbols.channels.size()),
      exportCompressedTableDsl(generated).text,
      artifact::dumpProductionsJson(symbols, productions),
      artifact::dumpReductionsJson(productions, generated.reductions()),
      artifact::dumpAstSchemaJson(generated.astSchema()),
      packageDiagnostics(document, generated)};
  return artifact::makeArtifactPackage(std::move(manifest),
                                       std::move(sections));
}

auto buildContextualArtifactPackage(const model::SyntaxDocument &document,
                                    const GeneratedContextualParser &generated,
                                    std::string_view source,
                                    const ArtifactBuildIdentity &identity)
    -> artifact::ArtifactPackage {
  const zbik::ContextualLRMachine machine{generated.table, generated.scoped.requirements,
                                          generated.scoped.sourceTerminals};
  machine.validateLexer(generated.lexer.rules());
  const zbik::LexerContextPlan plan{generated.table, generated.scoped.requirements};
  const auto astTable = contextualAstTable(document, generated);
  const auto [symbols, productions] = buildGrammarArtifactSections(document, astTable);
  const auto compiled = compileLexerAutomaton(document, generated.source, true);
  auto lexer = buildLexerArtifactSection(compiled, symbols, true);
  lexer.version = 2;
  lexer.context.emplace();
  auto &context = *lexer.context;
  const auto offset = symbols.terminals.size() - generated.source.grammar().terminalCount();
  for (const auto original : generated.scoped.sourceTerminals)
    context.originalTerminals.push_back(artifactId(offset + original.value, "original terminal"));
  for (std::size_t i = 0; i < lexer.rules.size(); ++i) {
    if (lexer.rules[i].terminal) *lexer.rules[i].terminal += artifactId(offset, "terminal offset");
    context.requiredClasses.push_back(generated.lexer.rules()[i].requiredClasses);
  }
  for (std::size_t state = 0; state < generated.table.stateCount(); ++state) {
    std::vector<artifact::ArtifactContextNode> nodes(1);
    std::vector<std::vector<zbik::LookaheadSymbol>> prefixes(1);
    for (const auto &[word, cell] : generated.table.actionRows()[state]) {
      if (cell.empty()) continue;
      std::size_t node = 0;
      for (const auto &symbol : word.symbols()) {
        std::optional<std::uint32_t> terminal;
        if (const auto id = std::get_if<zbik::TerminalId>(&symbol)) terminal = id->value;
        const auto edge = std::ranges::find(nodes[node].edges, terminal, &artifact::ArtifactContextEdge::terminal);
        if (edge != nodes[node].edges.end()) { node = edge->target; continue; }
        const auto next = nodes.size();
        auto prefix = prefixes[node];
        prefix.push_back(symbol);
        nodes[node].edges.push_back({terminal, artifactId(next, "context node")});
        nodes.emplace_back();
        prefixes.push_back(std::move(prefix));
        node = next;
      }
    }
    for (std::size_t i = 0; i < nodes.size(); ++i)
      nodes[i].active = plan.activeMask(zbik::StateId{artifactId(state, "state")}, prefixes[i]).value_or(0);
    context.rows.push_back(std::move(nodes));
  }
  return buildParserArtifactPackage(document, astTable, compiled, source, source, identity, std::move(lexer));
}

} // namespace agas::generator
