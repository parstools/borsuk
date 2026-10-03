#include "agas/generator/ContextualGeneration.h"

#include <map>
#include <set>
#include "agas/generator/LexerGeneration.h"
#include "agas/model/Validation.h"
#include "lr/DirectLALRkDfa.h"
#include "grammar/GrammarBuilder.h"

namespace agas::generator {

auto generateContextualParser(const model::SyntaxDocument &document)
    -> GeneratedContextualParser {
  const auto validation = model::validateSyntaxModel(document);
  if (!validation.valid()) {
    for (const auto &issue : validation.issues)
      if (issue.severity == model::DiagnosticSeverity::Error)
        throw ParserConfigurationError(issue.span, issue.message);
  }
  std::map<std::string, zbik::LexerClassMask> bits;
  zbik::LexerClassMask initial = 0;
  for (std::size_t i = 0; i < document.lexerClasses.size(); ++i) {
    const auto &entry = document.lexerClasses[i];
    const auto bit = zbik::LexerClassMask{1} << i;
    bits.emplace(entry.name, bit);
    if (entry.value.spelling == "true") initial |= bit;
  }
  auto source = model::lowerToBnf(document);
  const auto compiled = compileLexerAutomaton(document, source, true);
  std::map<std::string, zbik::LexerClassMask> required;
  for (const auto &rule : document.lexerRules)
    for (const auto &command : rule.commands)
      if (command.name == "require")
        required[rule.name] |= bits.at(*command.argument);
  std::vector<zbik::LexerRule> rules;
  std::vector<zbik::RegexAst> expressions;
  for (const auto &rule : compiled.rules()) {
    const auto mask = required[rule.name];
    if (mask && (rule.skipped || rule.channel))
      throw ParserConfigurationError(rule.span,
          "class-gated skip/channel rules are not supported by scoped parsing yet");
    rules.push_back({rule.skipped || rule.channel ? std::nullopt : rule.terminal,
                     {}, mask});
    expressions.push_back(rule.expression);
  }
  zbik::Utf8Lexer lexer{std::move(rules), std::move(expressions)};
  std::vector<zbik::ParserLexerClass> scopes;
  for (const auto &rule : document.parserRules) {
    if (rule.lexerContext.empty()) continue;
    zbik::ParserLexerClass scope{*source.grammar().findNonterminal(rule.name)};
    for (const auto &command : rule.lexerContext) {
      auto &mask = command.name == "enable" ? scope.enabled : scope.disabled;
      mask |= bits.at(*command.argument);
    }
    scopes.push_back(scope);
  }
  auto scoped = zbik::scopeLexerClasses(source.grammar(), scopes, lexer, initial);
  // Keep source token names in the artifact alphabet for lexing and AST tokens.
  // They are deliberately unused in parser productions; scoped tokens remain
  // the ACTION alphabet. This also preserves lexer rules unused by the parser.
  std::vector<std::string> terminals;
  std::set<std::string> sourceNames;
  for (std::size_t i = 0; i < source.grammar().terminalCount(); ++i)
    sourceNames.insert(source.grammar().terminalName(zbik::TerminalId{static_cast<std::uint32_t>(i)}));
  for (std::size_t i = 0; i < scoped.grammar.terminalCount(); ++i) {
    auto name = scoped.grammar.terminalName(zbik::TerminalId{static_cast<std::uint32_t>(i)});
    while (sourceNames.contains(name)) name = "scoped_" + name;
    terminals.push_back(std::move(name));
  }
  std::vector<std::string> productions;
  for (const auto &rule : scoped.grammar.rules()) {
    std::string line = scoped.grammar.nonterminalName(rule.lhs()) + " ->";
    for (const auto &symbol : rule.rhs()) {
      if (const auto terminal = std::get_if<zbik::TerminalId>(&symbol))
        line += " " + terminals.at(zbik::toIndex(*terminal));
      else line += " " + scoped.grammar.nonterminalName(std::get<zbik::NonterminalId>(symbol));
    }
    productions.push_back(std::move(line));
  }
  for (std::size_t i = 0; i < source.grammar().terminalCount(); ++i) {
    const zbik::TerminalId id{static_cast<std::uint32_t>(i)};
    terminals.push_back(source.grammar().terminalName(id));
    scoped.sourceTerminals.push_back(id);
  }
  scoped.grammar = zbik::GrammarBuilder{}.build(productions, terminals);
  const auto configuration = parserConfiguration(document);
  zbik::LRkDfaStats statistics;
  auto table = [&] {
    if (configuration.algorithm == ParserAlgorithm::Lalr) {
      const zbik::DirectLALRkDfa dfa{scoped.grammar, configuration.lookahead};
      statistics = dfa.statistics();
      return zbik::ParseTable{dfa};
    }
    const zbik::LRkDfa dfa{scoped.grammar, configuration.lookahead};
    statistics = dfa.statistics();
    return zbik::ParseTable{dfa};
  }();
  // Policies still identify original alternatives, including all scoped copies.
  const std::vector<zbik::Conflict> conflicts(table.conflicts().begin(), table.conflicts().end());
  std::set<std::pair<zbik::StateId, zbik::LookaheadWord>> selected;
  std::vector<ResolvedConflict> resolved;
  for (std::size_t preference = 0; preference < document.conflictPreferences.size(); ++preference) {
    const auto &policy = document.conflictPreferences[preference];
    std::size_t matches = 0;
    const auto terminal = source.grammar().findTerminal(policy.shiftTerminal);
    for (const auto &conflict : conflicts) {
      const auto word = conflict.lookahead().symbols();
      if (word.empty() || conflict.actions().size() != 2) continue;
      const auto scopedTerminal = std::get_if<zbik::TerminalId>(&word.front());
      if (!scopedTerminal || scoped.sourceTerminals.at(zbik::toIndex(*scopedTerminal)) != *terminal) continue;
      std::optional<zbik::Action> shift, reduce;
      for (const auto &action : conflict.actions()) {
        if (std::holds_alternative<zbik::Shift>(action)) shift = action;
        if (const auto *r = std::get_if<zbik::Reduce>(&action)) {
          const auto &origin = source.origin(scoped.sourceRules.at(zbik::toIndex(r->rule)));
          const auto &rule = document.parserRules.at(origin.sourceRuleIndex);
          if (origin.role == zbik::EbnfGeneratedRuleRole::SourceAlternative &&
              rule.name == policy.reduceRule &&
              rule.alternatives.at(origin.alternativeIndex).label == policy.reduceAlternative)
            reduce = action;
        }
      }
      if (!shift || !reduce) continue;
      if (!selected.emplace(conflict.state(), conflict.lookahead()).second)
        throw ParserConfigurationError(policy.span, "conflict policies overlap");
      zbik::ActionCell expected;
      for (const auto &action : conflict.actions()) static_cast<void>(expected.add(action));
      table.resolveConflict(conflict.state(), conflict.lookahead(), expected,
          policy.choice == model::ConflictPreference::Choice::Shift ? *shift : *reduce);
      resolved.push_back({preference, conflict,
          policy.choice == model::ConflictPreference::Choice::Shift ? *shift : *reduce});
      ++matches;
    }
    if (!matches) throw ParserConfigurationError(policy.span, "conflict policy did not match an exact shift/reduce cell");
  }
  return {std::move(source), std::move(lexer), std::move(scoped), statistics,
          std::move(table), selected.size(), std::move(resolved)};
}

auto contextualAstTable(const model::SyntaxDocument &document,
                        const GeneratedContextualParser &generated)
    -> GeneratedParserTable {
  const auto sourceMetadata = buildProductionMetadata(document, generated.source);
  const auto sourceReductions = buildAstReductionProgram(document, generated.source);
  std::vector<model::BnfProductionOrigin> origins;
  std::vector<RuntimeProductionMetadata> runtime;
  std::vector<DiagnosticProductionMetadata> diagnostic;
  std::vector<ReductionInstruction> instructions;
  for (const auto &rule : generated.scoped.grammar.rules()) {
    const auto original = generated.scoped.sourceRules.at(zbik::toIndex(rule.id()));
    auto origin = generated.source.origin(original);
    origin.generatedRule = rule.id();
    origins.push_back(std::move(origin));
    runtime.push_back({rule.id(), rule.lhs(), rule.size()});
    auto metadata = sourceMetadata.diagnostic(original);
    metadata.rule = rule.id();
    metadata.stableIdentity += "/scope:" + std::to_string(rule.lhs().value);
    diagnostic.push_back(std::move(metadata));
    auto instruction = sourceReductions.instruction(original);
    instruction.rule = rule.id();
    instructions.push_back(std::move(instruction));
  }
  model::BnfModel bnf{generated.scoped.grammar, std::move(origins), {}};
  return {parserConfiguration(document), std::move(bnf), buildAstSchema(document),
          ProductionMetadata{std::move(runtime), std::move(diagnostic)},
          AstReductionProgram{std::move(instructions)}, generated.statistics,
          generated.table, generated.resolvedConflicts};
}

} // namespace agas::generator
