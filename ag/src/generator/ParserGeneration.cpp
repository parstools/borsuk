#include "agas/generator/ParserGeneration.h"

#include <charconv>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>

#include "lr/DirectLALRkDfa.h"

namespace agas::generator {
namespace {

auto parseLookahead(const model::Option &option) -> std::size_t {
  if (option.value.kind != model::OptionValueKind::Integer) {
    throw ParserConfigurationError(
        option.value.span, "option `lookahead` must be a positive integer");
  }
  std::size_t value = 0;
  const char *begin = option.value.spelling.data();
  const char *end = begin + option.value.spelling.size();
  const auto [position, error] = std::from_chars(begin, end, value);
  if (error != std::errc{} || position != end || value == 0) {
    throw ParserConfigurationError(
        option.value.span, "option `lookahead` must be a positive integer");
  }
  return value;
}

auto resolveDeclaredConflicts(const model::SyntaxDocument &document,
                              const ProductionMetadata &productions,
                              zbik::ParseTable &table)
    -> std::vector<ResolvedConflict> {
  std::vector<ResolvedConflict> resolved;
  std::set<std::pair<zbik::StateId, zbik::LookaheadWord>> selectedCells;
  const std::vector<zbik::Conflict> conflicts(table.conflicts().begin(),
                                               table.conflicts().end());
  for (std::size_t index = 0; index < document.conflictPreferences.size();
       ++index) {
    const model::ConflictPreference &preference =
        document.conflictPreferences[index];
    const auto terminal = table.grammar().findTerminal(preference.shiftTerminal);
    if (!terminal) {
      throw ParserConfigurationError(preference.span,
                                     "conflict policy shift terminal is unknown");
    }
    std::optional<zbik::RuleId> reduction;
    for (const DiagnosticProductionMetadata &metadata :
         productions.diagnostic()) {
      if (metadata.role == zbik::EbnfGeneratedRuleRole::SourceAlternative &&
          metadata.sourceRuleName == preference.reduceRule &&
          metadata.alternativeLabel == preference.reduceAlternative) {
        if (reduction) {
          throw ParserConfigurationError(
              preference.span, "conflict policy names multiple productions");
        }
        reduction = metadata.rule;
      }
    }
    if (!reduction) {
      throw ParserConfigurationError(preference.span,
                                   "conflict policy reduce alternative is unknown");
    }

    std::size_t matches = 0;
    for (const zbik::Conflict &conflict : conflicts) {
      const auto symbols = conflict.lookahead().symbols();
      if (symbols.empty() ||
          symbols.front() != zbik::LookaheadSymbol{*terminal} ||
          conflict.actions().size() != 2) {
        continue;
      }
      std::optional<zbik::Action> shiftAction;
      std::optional<zbik::Action> reduceAction;
      for (const zbik::Action &action : conflict.actions()) {
        if (std::holds_alternative<zbik::Shift>(action)) {
          shiftAction = action;
        } else if (const auto *reduce = std::get_if<zbik::Reduce>(&action)) {
          if (reduce->rule == *reduction)
            reduceAction = action;
        }
      }
      if (!shiftAction || !reduceAction)
        continue;
      if (!selectedCells.emplace(conflict.state(), conflict.lookahead()).second) {
        throw ParserConfigurationError(preference.span,
                                       "conflict policies overlap on one ACTION cell");
      }
      resolved.push_back({index, conflict,
                          preference.choice == model::ConflictPreference::Choice::Shift
                              ? *shiftAction : *reduceAction});
      ++matches;
    }
    if (matches == 0) {
      throw ParserConfigurationError(preference.span,
                                   "conflict policy did not match any exact shift/reduce cell");
    }
  }

  for (const ResolvedConflict &resolution : resolved) {
    zbik::ActionCell expected;
    for (const zbik::Action &action : resolution.original.actions())
      static_cast<void>(expected.add(action));
    table.resolveConflict(resolution.original.state(),
                          resolution.original.lookahead(), expected,
                          resolution.selected);
  }
  return resolved;
}

} // namespace

ParserConfigurationError::ParserConfigurationError(model::SourceSpan span,
                                                   std::string message)
    : std::runtime_error(std::move(message)), span_(span) {}

auto ParserConfigurationError::span() const noexcept
    -> const model::SourceSpan & {
  return span_;
}

GeneratedParserTable::GeneratedParserTable(
    ParserConfiguration configuration, model::BnfModel bnf, AstSchema astSchema,
    ProductionMetadata productions, AstReductionProgram reductions,
    zbik::LRkDfaStats dfaStats, zbik::ParseTable table,
    std::vector<ResolvedConflict> resolvedConflicts)
    : configuration_(configuration), bnf_(std::move(bnf)),
      astSchema_(std::move(astSchema)), productions_(std::move(productions)),
      reductions_(std::move(reductions)), dfaStats_(dfaStats),
      table_(std::move(table)),
      resolvedConflicts_(std::move(resolvedConflicts)) {
  if (productions_.runtime().size() != bnf_.grammar().ruleCount()) {
    throw std::invalid_argument("every BNF rule must have production metadata");
  }
  if (reductions_.instructions().size() != bnf_.grammar().ruleCount()) {
    throw std::invalid_argument("every BNF rule must have an AST reduction");
  }
}

auto GeneratedParserTable::configuration() const noexcept
    -> const ParserConfiguration & {
  return configuration_;
}

auto GeneratedParserTable::bnf() const noexcept -> const model::BnfModel & {
  return bnf_;
}

auto GeneratedParserTable::astSchema() const noexcept -> const AstSchema & {
  return astSchema_;
}

auto GeneratedParserTable::productions() const noexcept
    -> const ProductionMetadata & {
  return productions_;
}

auto GeneratedParserTable::reductions() const noexcept
    -> const AstReductionProgram & {
  return reductions_;
}

auto GeneratedParserTable::dfaStatistics() const noexcept
    -> const zbik::LRkDfaStats & {
  return dfaStats_;
}

auto GeneratedParserTable::table() const noexcept -> const zbik::ParseTable & {
  return table_;
}

auto GeneratedParserTable::resolvedConflicts() const noexcept
    -> const std::vector<ResolvedConflict> & {
  return resolvedConflicts_;
}

auto parserConfiguration(const model::SyntaxDocument &document)
    -> ParserConfiguration {
  ParserConfiguration result;
  for (const model::Option &option : document.options) {
    if (option.name == "parser") {
      if (option.value.kind != model::OptionValueKind::Identifier) {
        throw ParserConfigurationError(
            option.value.span, "option `parser` must be `LR` or `LALR`");
      }
      if (option.value.spelling == "LR") {
        result.algorithm = ParserAlgorithm::CanonicalLr;
      } else if (option.value.spelling == "LALR") {
        result.algorithm = ParserAlgorithm::Lalr;
      } else {
        throw ParserConfigurationError(
            option.value.span, "option `parser` must be `LR` or `LALR`");
      }
    } else if (option.name == "lookahead") {
      result.lookahead = parseLookahead(option);
    }
  }
  return result;
}

auto generateParserTable(const model::SyntaxDocument &document)
    -> GeneratedParserTable {
  if (!document.lexerClasses.empty())
    throw ParserConfigurationError(document.span,
        "lexer classes require contextual generation; AST/artifact/Rust export is not implemented yet");
  const ParserConfiguration configuration = parserConfiguration(document);
  AstSchema astSchema = buildAstSchema(document);
  model::BnfModel bnf = model::lowerToBnf(document);
  ProductionMetadata productions = buildProductionMetadata(document, bnf);
  AstReductionProgram reductions = buildAstReductionProgram(document, bnf);
  if (configuration.algorithm == ParserAlgorithm::Lalr) {
    const zbik::DirectLALRkDfa dfa{bnf.grammar(), configuration.lookahead};
    const zbik::LRkDfaStats statistics = dfa.statistics();
    zbik::ParseTable table{dfa};
    auto resolved = resolveDeclaredConflicts(document, productions, table);
    return {configuration,          std::move(bnf),        std::move(astSchema),
            std::move(productions), std::move(reductions), statistics,
            std::move(table),       std::move(resolved)};
  }
  const zbik::LRkDfa dfa{bnf.grammar(), configuration.lookahead};
  const zbik::LRkDfaStats statistics = dfa.statistics();
  zbik::ParseTable table{dfa};
  auto resolved = resolveDeclaredConflicts(document, productions, table);
  return {configuration,          std::move(bnf),        std::move(astSchema),
          std::move(productions), std::move(reductions), statistics,
          std::move(table),       std::move(resolved)};
}

} // namespace agas::generator
