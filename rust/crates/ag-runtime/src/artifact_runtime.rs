//! Execute a validated, loaded package using the same Rust runtime as static parsers.

use crate::artifact::LoadedPackage;
use crate::ast::{self, AstParseOutcome, AstParser, AstParserError, ReductionTraceEntry};
use crate::lexer::{self, LexResult, Lexer, LexerError};
use crate::lr::{self, Parser, RuntimeError, RuntimeLimits};

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PackageRunError {
    Lexer(LexerError),
    Parser(RuntimeError),
    Ast(AstParserError),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PackageParseResult {
    pub lexed: LexResult,
    pub outcome: AstParseOutcome,
}

impl LoadedPackage {
    /// Lexes source with the automata from this validated package.
    ///
    /// # Errors
    ///
    /// Returns the lexical error with its source byte offset.
    #[allow(clippy::too_many_lines)]
    pub fn tokenize(&self, input: &[u8]) -> Result<LexResult, LexerError> {
        let mut result = self
            .tokenize_for_parser(input, RuntimeLimits::default())
            .map_err(|error| match error {
                PackageRunError::Lexer(error) => error,
                _ => LexerError::InvalidTable("contextual parser failed during tokenization"),
            })?;
        self.restore_token_ids(&mut result);
        Ok(result)
    }

    fn restore_token_ids(&self, result: &mut LexResult) {
        if let Some(context) = &self.lexer().context {
            for token in &mut result.tokens {
                token.terminal = context.original_terminals[token.terminal as usize];
            }
            for terminal in &mut result.parser_terminal_ids {
                *terminal = context.original_terminals[*terminal as usize];
            }
        }
    }

    fn tokenize_for_parser(
        &self,
        input: &[u8],
        limits: RuntimeLimits,
    ) -> Result<LexResult, PackageRunError> {
        let artifact = self.lexer();
        let dfa_ranges: Vec<Vec<Vec<lexer::CodePointRange>>> = artifact
            .dfa_states
            .iter()
            .map(|state| {
                state
                    .transitions
                    .iter()
                    .map(|transition| {
                        transition
                            .ranges
                            .iter()
                            .map(|range| lexer::CodePointRange {
                                first: range.first,
                                last: range.last,
                            })
                            .collect()
                    })
                    .collect()
            })
            .collect();
        let dfa_transitions: Vec<Vec<lexer::Transition<'_>>> = artifact
            .dfa_states
            .iter()
            .enumerate()
            .map(|(state_id, state)| {
                state
                    .transitions
                    .iter()
                    .enumerate()
                    .map(|(transition_id, transition)| lexer::Transition {
                        ranges: &dfa_ranges[state_id][transition_id],
                        target: transition.target,
                    })
                    .collect()
            })
            .collect();
        let dfa_states: Vec<lexer::DfaState<'_>> = artifact
            .dfa_states
            .iter()
            .enumerate()
            .map(|(id, state)| lexer::DfaState {
                accepting_rule: state.accepting_rule,
                transitions: &dfa_transitions[id],
            })
            .collect();

        let nfa_ranges: Vec<Vec<Vec<Vec<lexer::CodePointRange>>>> = artifact
            .ordered_nfas
            .iter()
            .map(|nfa| {
                nfa.states
                    .iter()
                    .map(|state| {
                        state
                            .transitions
                            .iter()
                            .map(|transition| {
                                transition
                                    .ranges
                                    .iter()
                                    .map(|range| lexer::CodePointRange {
                                        first: range.first,
                                        last: range.last,
                                    })
                                    .collect()
                            })
                            .collect()
                    })
                    .collect()
            })
            .collect();
        let nfa_transitions: Vec<Vec<Vec<lexer::Transition<'_>>>> = artifact
            .ordered_nfas
            .iter()
            .enumerate()
            .map(|(nfa_id, nfa)| {
                nfa.states
                    .iter()
                    .enumerate()
                    .map(|(state_id, state)| {
                        state
                            .transitions
                            .iter()
                            .enumerate()
                            .map(|(transition_id, transition)| lexer::Transition {
                                ranges: &nfa_ranges[nfa_id][state_id][transition_id],
                                target: transition.target,
                            })
                            .collect()
                    })
                    .collect()
            })
            .collect();
        let nfa_states: Vec<Vec<lexer::NfaState<'_>>> = artifact
            .ordered_nfas
            .iter()
            .enumerate()
            .map(|(nfa_id, nfa)| {
                nfa.states
                    .iter()
                    .enumerate()
                    .map(|(state_id, state)| lexer::NfaState {
                        epsilon_transitions: &state.epsilon_transitions,
                        transitions: &nfa_transitions[nfa_id][state_id],
                        ordered_decision: state.ordered_decision,
                        activates_priority: state.activates_priority,
                    })
                    .collect()
            })
            .collect();
        let ordered_nfas: Vec<lexer::OrderedNfa<'_>> = artifact
            .ordered_nfas
            .iter()
            .enumerate()
            .map(|(id, nfa)| lexer::OrderedNfa {
                start_state: nfa.start_state,
                accepting_state: nfa.accepting_state,
                states: &nfa_states[id],
            })
            .collect();
        let rules: Vec<lexer::LexerRule<'_>> = artifact
            .rules
            .iter()
            .map(|rule| lexer::LexerRule {
                name: &rule.name,
                terminal: rule.terminal,
                channel: rule.channel,
                skipped: rule.skipped,
            })
            .collect();
        let tables = lexer::LexerTables {
            terminal_count: u32::try_from(self.symbols().terminals.len()).map_err(|_| {
                PackageRunError::Lexer(LexerError::InvalidTable("too many terminals"))
            })?,
            channel_count: u32::try_from(self.symbols().channels.len()).map_err(|_| {
                PackageRunError::Lexer(LexerError::InvalidTable("too many channels"))
            })?,
            rules: &rules,
            dfa_states: &dfa_states,
            ordered_nfas: &ordered_nfas,
        };
        if artifact.context.is_some() {
            let lexer = Lexer::new_contextual(&tables).map_err(PackageRunError::Lexer)?;
            crate::artifact_context::tokenize(self, &lexer, input, limits)
        } else {
            Lexer::new(&tables)
                .and_then(|lexer| lexer.tokenize(input))
                .map_err(PackageRunError::Lexer)
        }
    }

    /// Executes the package's lexer, LR table, and neutral AST reductions.
    ///
    /// # Errors
    ///
    /// Returns lexical, parser-runtime, or AST reduction failures separately.
    #[allow(clippy::too_many_lines)]
    pub fn parse(
        &self,
        input: &[u8],
        limits: RuntimeLimits,
    ) -> Result<PackageParseResult, PackageRunError> {
        self.parse_inner(input, limits, None)
    }

    /// Executes the package and records reductions for a coverage report.
    ///
    /// # Errors
    ///
    /// Returns the same errors as [`Self::parse`].
    pub fn parse_with_trace(
        &self,
        input: &[u8],
        limits: RuntimeLimits,
        trace: &mut Vec<ReductionTraceEntry>,
    ) -> Result<PackageParseResult, PackageRunError> {
        self.parse_inner(input, limits, Some(trace))
    }

    #[allow(clippy::too_many_lines)]
    fn parse_inner(
        &self,
        input: &[u8],
        limits: RuntimeLimits,
        trace: Option<&mut Vec<ReductionTraceEntry>>,
    ) -> Result<PackageParseResult, PackageRunError> {
        let mut lexed = self.tokenize_for_parser(input, limits)?;
        let view = self.parser_table().view();
        let action_entries: Vec<Vec<lr::ActionEntry<'_>>> = view
            .action_rows()
            .iter()
            .map(|row| {
                row.entries
                    .iter()
                    .map(|entry| lr::ActionEntry {
                        lookahead: &entry.lookahead,
                        action: entry.action,
                    })
                    .collect()
            })
            .collect();
        let action_rows: Vec<lr::ActionRow<'_>> = view
            .action_rows()
            .iter()
            .enumerate()
            .map(|(id, row)| lr::ActionRow {
                entries: &action_entries[id],
                fallback: row.fallback.map(lr::Action::Reduce),
            })
            .collect();
        let goto_entries: Vec<Vec<lr::GotoEntry>> = view
            .goto_rows()
            .iter()
            .map(|row| {
                row.iter()
                    .map(|entry| lr::GotoEntry {
                        nonterminal: entry.nonterminal,
                        state: entry.state,
                    })
                    .collect()
            })
            .collect();
        let goto_rows: Vec<lr::GotoRow<'_>> = goto_entries
            .iter()
            .map(|entries| lr::GotoRow { entries })
            .collect();
        let productions: Vec<lr::Production> = self
            .productions()
            .productions
            .iter()
            .map(|production| {
                Ok(lr::Production {
                    lhs: production.lhs,
                    rhs_len: u32::try_from(production.rhs.len()).map_err(|_| {
                        PackageRunError::Parser(RuntimeError::InvalidTableExecution(
                            "RHS length overflow",
                        ))
                    })?,
                })
            })
            .collect::<Result<_, PackageRunError>>()?;
        let tables = lr::ParserTables {
            lookahead: view.lookahead(),
            terminal_count: u32::try_from(self.symbols().terminals.len()).map_err(|_| {
                PackageRunError::Parser(RuntimeError::InvalidTableExecution("too many terminals"))
            })?,
            nonterminal_count: u32::try_from(self.symbols().nonterminals.len()).map_err(|_| {
                PackageRunError::Parser(RuntimeError::InvalidTableExecution(
                    "too many nonterminals",
                ))
            })?,
            start_state: view.start_state(),
            action_rows: &action_rows,
            action_state_rows: view.action_state_rows(),
            goto_rows: &goto_rows,
            goto_state_rows: view.goto_state_rows(),
            productions: &productions,
        };
        let parser = Parser::new(&tables, limits).map_err(PackageRunError::Parser)?;

        let fields: Vec<Vec<ast::ReductionField<'_>>> = self
            .reductions()
            .instructions
            .iter()
            .map(|instruction| {
                instruction
                    .fields
                    .iter()
                    .map(|field| ast::ReductionField {
                        name: &field.name,
                        rhs_index: field.rhs_index,
                    })
                    .collect()
            })
            .collect();
        let instructions: Vec<ast::ReductionInstruction<'_>> = self
            .reductions()
            .instructions
            .iter()
            .enumerate()
            .map(|(id, instruction)| ast::ReductionInstruction {
                rhs_len: instruction.rhs_length,
                opcode: opcode(&instruction.opcode),
                span_policy: if instruction.span_policy == "matched-rhs" {
                    ast::ReductionSpanPolicy::MatchedRhs
                } else {
                    ast::ReductionSpanPolicy::EmptyAtLookahead
                },
                type_name: instruction.type_name.as_deref(),
                variant_name: instruction.variant_name.as_deref(),
                operands: &instruction.operands,
                fields: &fields[id],
            })
            .collect();
        let program = ast::ReductionProgram {
            instructions: &instructions,
        };
        let frontend = AstParser::new(parser, &program).map_err(PackageRunError::Ast)?;
        let mut outcome = match trace {
            Some(entries) => frontend.parse_with_trace(input, &lexed, entries),
            None => frontend.parse(input, &lexed),
        }
        .map_err(PackageRunError::Ast)?;
        if let (Some(context), AstParseOutcome::Accepted(root)) =
            (&self.lexer().context, &mut outcome)
        {
            fn restore(value: &mut ast::AstValue, originals: &[u32]) {
                if value.kind == ast::AstValueKind::Token {
                    value.token_kind = originals[value.token_kind as usize];
                }
                for child in &mut value.elements {
                    restore(child, originals);
                }
            }
            restore(root, &context.original_terminals);
        }
        self.restore_token_ids(&mut lexed);
        Ok(PackageParseResult { lexed, outcome })
    }
}

fn opcode(name: &str) -> ast::ReductionOpcode {
    match name {
        "unit" => ast::ReductionOpcode::Unit,
        "forward" => ast::ReductionOpcode::Forward,
        "construct-node" => ast::ReductionOpcode::ConstructNode,
        "construct-node-or-forward" => ast::ReductionOpcode::ConstructNodeOrForward,
        "construct-record" => ast::ReductionOpcode::ConstructRecord,
        "optional-some" => ast::ReductionOpcode::OptionalSome,
        "optional-none" => ast::ReductionOpcode::OptionalNone,
        "list-empty" => ast::ReductionOpcode::ListEmpty,
        "list-singleton" => ast::ReductionOpcode::ListSingleton,
        "list-append" => ast::ReductionOpcode::ListAppend,
        _ => unreachable!("the package loader validated the opcode"),
    }
}
