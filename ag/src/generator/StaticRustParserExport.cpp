#include "agas/generator/StaticRustParserExport.h"

#include <sstream>
#include <string_view>
#include <type_traits>
#include <variant>

#include "agas/artifact/ReductionSection.h"

namespace agas::generator {
namespace {

auto algorithmName(artifact::ArtifactParserAlgorithm algorithm) -> const
    char * {
  switch (algorithm) {
  case artifact::ArtifactParserAlgorithm::Lr:
    return "LR";
  case artifact::ArtifactParserAlgorithm::Lalr:
    return "LALR";
  case artifact::ArtifactParserAlgorithm::Slr:
    return "SLR";
  }
  return "unknown";
}

void writeAction(std::ostream &output,
                 const artifact::ArtifactParserAction &action) {
  std::visit(
      [&output](const auto &value) {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, artifact::ArtifactShift>)
          output << "Action::Shift(" << value.state << ')';
        else if constexpr (std::is_same_v<Value, artifact::ArtifactReduce>)
          output << "Action::Reduce(" << value.production << ')';
        else
          output << "Action::Accept";
      },
      action);
}

void writeLookahead(std::ostream &output,
                    const artifact::ArtifactLookaheadWord &lookahead) {
  output << "&[";
  for (std::size_t index = 0; index < lookahead.size(); ++index) {
    if (index != 0)
      output << ", ";
    if (lookahead[index].terminal)
      output << "LookaheadSymbol::Terminal(" << *lookahead[index].terminal
             << ')';
    else
      output << "LookaheadSymbol::EndOfInput";
  }
  output << ']';
}

void writeRustString(std::ostream &output, std::string_view value) {
  output << '"';
  for (const unsigned char byte : value) {
    switch (byte) {
    case '\\':
      output << "\\\\";
      break;
    case '"':
      output << "\\\"";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      if (byte < 0x20U || byte == 0x7FU) {
        constexpr char digits[] = "0123456789abcdef";
        output << "\\u{00" << digits[byte >> 4U] << digits[byte & 0x0FU] << '}';
      } else {
        output << static_cast<char>(byte);
      }
    }
  }
  output << '"';
}

void writeRanges(std::ostream &output,
                 const std::vector<artifact::ArtifactCodePointRange> &ranges) {
  output << "&[";
  for (std::size_t index = 0; index < ranges.size(); ++index) {
    if (index != 0)
      output << ", ";
    output << "CodePointRange { first: " << ranges[index].first
           << ", last: " << ranges[index].last << " }";
  }
  output << ']';
}

void writeTransitions(
    std::ostream &output,
    const std::vector<artifact::ArtifactLexerTransition> &transitions,
    std::string_view indentation) {
  output << "&[\n";
  for (const auto &transition : transitions) {
    output << indentation << "Transition { ranges: ";
    writeRanges(output, transition.ranges);
    output << ", target: " << transition.target << " },\n";
  }
  output << indentation.substr(0, indentation.size() - 4) << ']';
}

auto rustOpcode(ReductionOpcode opcode) -> const char * {
  switch (opcode) {
  case ReductionOpcode::Unit:
    return "ReductionOpcode::Unit";
  case ReductionOpcode::Forward:
    return "ReductionOpcode::Forward";
  case ReductionOpcode::ConstructNode:
    return "ReductionOpcode::ConstructNode";
  case ReductionOpcode::ConstructNodeOrForward:
    return "ReductionOpcode::ConstructNodeOrForward";
  case ReductionOpcode::ConstructRecord:
    return "ReductionOpcode::ConstructRecord";
  case ReductionOpcode::OptionalSome:
    return "ReductionOpcode::OptionalSome";
  case ReductionOpcode::OptionalNone:
    return "ReductionOpcode::OptionalNone";
  case ReductionOpcode::ListEmpty:
    return "ReductionOpcode::ListEmpty";
  case ReductionOpcode::ListSingleton:
    return "ReductionOpcode::ListSingleton";
  case ReductionOpcode::ListAppend:
    return "ReductionOpcode::ListAppend";
  }
  return "ReductionOpcode::Unit";
}

auto rustSpanPolicy(ReductionSpanPolicy policy) -> const char * {
  return policy == ReductionSpanPolicy::MatchedRhs
             ? "ReductionSpanPolicy::MatchedRhs"
             : "ReductionSpanPolicy::EmptyAtLookahead";
}

void writeOptionalRustString(std::ostream &output,
                             const std::optional<std::string> &value) {
  if (!value) {
    output << "None";
    return;
  }
  output << "Some(";
  writeRustString(output, *value);
  output << ')';
}

template <class Range>
void writeU32Slice(std::ostream &output, const Range &values) {
  output << "&[";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0)
      output << ", ";
    output << values[index];
  }
  output << ']';
}

} // namespace

auto exportStaticRustParser(const artifact::ArtifactParserTable &table,
                            const artifact::ArtifactSymbols &symbols,
                            const artifact::ArtifactProductions &productions)
    -> std::string {
  artifact::validateGrammarSections(symbols, productions);
  artifact::validateParserTableSection(table, symbols, productions);

  std::ostringstream output;
  output << "// Generated by Agas. Do not edit.\n"
            "use crate::lr::{\n"
            "    Action, ActionEntry, ActionRow, GotoEntry, GotoRow, "
            "LookaheadSymbol, ParserTables, Production,\n"
            "};\n\n"
         << "pub const PARSER_ALGORITHM: &str = \""
         << algorithmName(table.algorithm) << "\";\n\n";

  output << "#[rustfmt::skip]\nstatic ACTION_ROWS: &[ActionRow] = &[\n";
  for (const auto &row : table.actionRows) {
    output << "    ActionRow {\n        entries: &[\n";
    for (const auto &entry : row.entries) {
      output << "            ActionEntry { lookahead: ";
      writeLookahead(output, entry.lookahead);
      output << ", action: ";
      writeAction(output, entry.action);
      output << " },\n";
    }
    output << "        ],\n        fallback: ";
    if (row.fallback) {
      output << "Some(Action::Reduce(" << row.fallback->production << "))";
    } else {
      output << "None";
    }
    output << ",\n    },\n";
  }
  output << "];\n\n#[rustfmt::skip]\nstatic ACTION_STATE_ROWS: &[u32] = ";
  writeU32Slice(output, table.actionStateRows);

  output << ";\n\n#[rustfmt::skip]\nstatic GOTO_ROWS: &[GotoRow] = &[\n";
  for (const auto &row : table.gotoRows) {
    output << "    GotoRow { entries: &[\n";
    for (const auto &entry : row.entries)
      output << "        GotoEntry { nonterminal: " << entry.nonterminal
             << ", state: " << entry.state << " },\n";
    output << "    ] },\n";
  }
  output << "];\n\n#[rustfmt::skip]\nstatic GOTO_STATE_ROWS: &[u32] = ";
  writeU32Slice(output, table.gotoStateRows);

  output << ";\n\n#[rustfmt::skip]\nstatic PRODUCTIONS: &[Production] = &[\n";
  for (const auto &production : productions.productions)
    output << "    Production { lhs: " << production.lhs
           << ", rhs_len: " << production.rhs.size() << " },\n";
  output << "];\n\n"
         << "pub static PARSER_TABLES: ParserTables = ParserTables {\n"
         << "    lookahead: " << table.lookahead << ",\n"
         << "    terminal_count: " << symbols.terminals.size() << ",\n"
         << "    nonterminal_count: " << symbols.nonterminals.size() << ",\n"
         << "    start_state: " << table.startState << ",\n"
         << "    action_rows: ACTION_ROWS,\n"
         << "    action_state_rows: ACTION_STATE_ROWS,\n"
         << "    goto_rows: GOTO_ROWS,\n"
         << "    goto_state_rows: GOTO_STATE_ROWS,\n"
         << "    productions: PRODUCTIONS,\n"
         << "};\n";
  return output.str();
}

auto exportStaticRustLexer(const artifact::ArtifactLexer &lexer,
                           const artifact::ArtifactSymbols &symbols)
    -> std::string {
  if (lexer.context) throw std::invalid_argument("static Rust lexer export does not support parser-directed classes; use a package");
  artifact::validateLexerSection(lexer, symbols.terminals.size(),
                                 symbols.channels.size());
  std::ostringstream output;
  output << "#![allow(clippy::unreadable_literal, unused_imports)]\n"
            "// Generated by Agas. Do not edit.\n"
            "use crate::lexer::{\n"
            "    CodePointRange, DfaState, LexerRule, LexerTables, NfaState, "
            "OrderedNfa, Transition,\n"
            "};\n\n";

  output << "#[rustfmt::skip]\nstatic LEXER_RULES: &[LexerRule] = &[\n";
  for (const auto &rule : lexer.rules) {
    output << "    LexerRule { name: ";
    writeRustString(output, rule.name);
    output << ", terminal: ";
    if (rule.terminal)
      output << "Some(" << *rule.terminal << ')';
    else
      output << "None";
    output << ", channel: ";
    if (rule.channel)
      output << "Some(" << *rule.channel << ')';
    else
      output << "None";
    output << ", skipped: " << (rule.skipped ? "true" : "false") << " },\n";
  }
  output << "];\n\n#[rustfmt::skip]\nstatic DFA_STATES: &[DfaState] = &[\n";
  for (const auto &state : lexer.dfaStates) {
    output << "    DfaState { accepting_rule: ";
    if (state.acceptingRule)
      output << "Some(" << *state.acceptingRule << ')';
    else
      output << "None";
    output << ", transitions: ";
    writeTransitions(output, state.transitions, "        ");
    output << " },\n";
  }
  output << "];\n\n#[rustfmt::skip]\nstatic ORDERED_NFAS: &[OrderedNfa] = &[\n";
  for (const auto &nfa : lexer.orderedNfas) {
    output << "    OrderedNfa { start_state: " << nfa.startState
           << ", accepting_state: " << nfa.acceptingState << ", states: &[\n";
    for (const auto &state : nfa.states) {
      output << "        NfaState { epsilon_transitions: ";
      writeU32Slice(output, state.epsilonTransitions);
      output << ", transitions: ";
      writeTransitions(output, state.transitions, "            ");
      output << ", ordered_decision: "
             << (state.orderedDecision ? "true" : "false")
             << ", activates_priority: "
             << (state.activatesPriority ? "true" : "false") << " },\n";
    }
    output << "    ] },\n";
  }
  output << "];\n\n"
         << "pub static LEXER_TABLES: LexerTables = LexerTables {\n"
         << "    terminal_count: " << symbols.terminals.size() << ",\n"
         << "    channel_count: " << symbols.channels.size() << ",\n"
         << "    rules: LEXER_RULES,\n"
         << "    dfa_states: DFA_STATES,\n"
         << "    ordered_nfas: ORDERED_NFAS,\n"
         << "};\n";
  return output.str();
}

auto exportStaticRustReductions(
    const AstReductionProgram &reductions,
    const artifact::ArtifactProductions &productions) -> std::string {
  artifact::validateReductionSection(productions, {1, reductions});
  std::ostringstream output;
  output << "// Generated by Agas. Do not edit.\n"
            "use crate::ast::{\n"
            "    ReductionField, ReductionInstruction, ReductionOpcode, "
            "ReductionProgram, ReductionSpanPolicy,\n"
            "};\n\n"
            "#[rustfmt::skip]\n"
            "static REDUCTION_INSTRUCTIONS: &[ReductionInstruction] = &[\n";
  for (const auto &instruction : reductions.instructions()) {
    output << "    ReductionInstruction { rhs_len: " << instruction.rhsLength
           << ", opcode: " << rustOpcode(instruction.opcode)
           << ", span_policy: " << rustSpanPolicy(instruction.spanPolicy)
           << ", type_name: ";
    writeOptionalRustString(output, instruction.typeName);
    output << ", variant_name: ";
    writeOptionalRustString(output, instruction.variantName);
    output << ", operands: ";
    writeU32Slice(output, instruction.operands);
    output << ", fields: &[";
    for (std::size_t index = 0; index < instruction.fields.size(); ++index) {
      if (index != 0)
        output << ", ";
      output << "ReductionField { name: ";
      writeRustString(output, instruction.fields[index].name);
      output << ", rhs_index: " << instruction.fields[index].rhsIndex << " }";
    }
    output << "] },\n";
  }
  output << "];\n\n"
            "pub static REDUCTION_PROGRAM: ReductionProgram = "
            "ReductionProgram {\n"
            "    instructions: REDUCTION_INSTRUCTIONS,\n"
            "};\n";
  return output.str();
}

} // namespace agas::generator
