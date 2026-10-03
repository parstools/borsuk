//! Neutral AST reduction program and LR integration.

use crate::lexer::{LexResult, Token};
use crate::lr::{Action, Parser, RuntimeError, SyntaxError, action_row, goto_row, make_lookahead};
use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct InputSpan {
    pub begin_byte: u64,
    pub end_byte: u64,
}

#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum AstValueKind {
    Unit,
    Token,
    Node,
    Record,
    Optional,
    List,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct AstValue {
    pub kind: AstValueKind,
    pub source_span: InputSpan,
    pub recognized_span: InputSpan,
    pub type_name: String,
    pub variant_name: String,
    pub token_kind: u32,
    pub token_text: String,
    pub field_names: Vec<String>,
    pub elements: Vec<AstValue>,
}

impl AstValue {
    fn wrapper(kind: AstValueKind, source_span: InputSpan) -> Self {
        Self {
            kind,
            source_span,
            recognized_span: source_span,
            type_name: String::new(),
            variant_name: String::new(),
            token_kind: 0,
            token_text: String::new(),
            field_names: Vec::new(),
            elements: Vec::new(),
        }
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AstStatistics {
    pub values: usize,
    pub nodes: usize,
    /// Edges from the root, including optional and list wrappers.
    pub maximum_depth: usize,
    /// Named nodes on the longest path, counting a named root as one.
    pub maximum_node_depth: usize,
}

#[must_use]
pub fn measure_ast(root: &AstValue) -> AstStatistics {
    let mut result = AstStatistics::default();
    let mut pending = vec![(root, 0, 0)];
    while let Some((value, depth, parent_nodes)) = pending.pop() {
        result.values += 1;
        result.nodes += usize::from(value.kind == AstValueKind::Node);
        result.maximum_depth = result.maximum_depth.max(depth);
        let node_depth = parent_nodes + usize::from(value.kind == AstValueKind::Node);
        result.maximum_node_depth = result.maximum_node_depth.max(node_depth);
        pending.extend(
            value
                .elements
                .iter()
                .map(|child| (child, depth + 1, node_depth)),
        );
    }
    result
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ReductionStackValue {
    pub payload: AstValue,
    pub recognized_span: InputSpan,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ReductionOpcode {
    Unit,
    Forward,
    ConstructNode,
    ConstructNodeOrForward,
    ConstructRecord,
    OptionalSome,
    OptionalNone,
    ListEmpty,
    ListSingleton,
    ListAppend,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ReductionSpanPolicy {
    MatchedRhs,
    EmptyAtLookahead,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ReductionField<'a> {
    pub name: &'a str,
    pub rhs_index: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ReductionInstruction<'a> {
    pub rhs_len: u32,
    pub opcode: ReductionOpcode,
    pub span_policy: ReductionSpanPolicy,
    pub type_name: Option<&'a str>,
    pub variant_name: Option<&'a str>,
    pub operands: &'a [u32],
    pub fields: &'a [ReductionField<'a>],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ReductionProgram<'a> {
    pub instructions: &'a [ReductionInstruction<'a>],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ReductionError {
    InvalidProgram(&'static str),
    RhsSizeMismatch,
    InvalidSpan,
    ValueKindMismatch,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum AstParseOutcome {
    Accepted(AstValue),
    SyntaxError(SyntaxError),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ReductionTraceEntry {
    pub production_id: u32,
    pub rhs_cardinalities: Vec<usize>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum AstParserError {
    InvalidInput(&'static str),
    Parser(RuntimeError),
    Reduction(ReductionError),
}

pub struct AstParser<'a> {
    parser: Parser<'a>,
    reductions: &'a ReductionProgram<'a>,
}

impl<'a> AstParser<'a> {
    #[must_use]
    pub fn lookahead_depth(&self) -> usize {
        self.parser.tables.lookahead as usize
    }

    /// Validates the LR tables and the complete reduction program.
    ///
    /// # Errors
    ///
    /// Returns an error when either static layer is inconsistent.
    pub fn new(
        parser: Parser<'a>,
        reductions: &'a ReductionProgram<'a>,
    ) -> Result<Self, AstParserError> {
        validate_program(parser.tables, reductions).map_err(AstParserError::Reduction)?;
        Ok(Self { parser, reductions })
    }

    /// Executes LR(k) and the neutral reduction program over lexer output.
    ///
    /// # Errors
    ///
    /// Returns an error for mismatched input/token data, invalid table
    /// execution, a resource limit, or a violated reduction contract.
    #[allow(clippy::too_many_lines)]
    pub fn parse(
        &self,
        input: &[u8],
        lexed: &LexResult,
    ) -> Result<AstParseOutcome, AstParserError> {
        self.parse_inner(input, lexed, None)
    }

    /// Parses while recording production IDs and RHS cardinalities for coverage.
    /// Trace entries from a failed parse must be discarded by the caller.
    ///
    /// # Errors
    ///
    /// Returns the same errors as [`Self::parse`].
    pub fn parse_with_trace(
        &self,
        input: &[u8],
        lexed: &LexResult,
        trace: &mut Vec<ReductionTraceEntry>,
    ) -> Result<AstParseOutcome, AstParserError> {
        self.parse_inner(input, lexed, Some(trace))
    }

    #[allow(clippy::too_many_lines)]
    fn parse_inner(
        &self,
        input: &[u8],
        lexed: &LexResult,
        mut trace: Option<&mut Vec<ReductionTraceEntry>>,
    ) -> Result<AstParseOutcome, AstParserError> {
        let tokens = parser_tokens(lexed).map_err(AstParserError::InvalidInput)?;
        validate_token_spans(&tokens, input, self.parser.tables.terminal_count)
            .map_err(AstParserError::InvalidInput)?;

        let mut states = vec![self.parser.tables.start_state];
        let mut values = Vec::new();
        let mut token_index = 0_usize;
        let mut steps = 0_u64;
        loop {
            steps = steps
                .checked_add(1)
                .ok_or(AstParserError::Parser(RuntimeError::StepLimitExceeded))?;
            if steps > self.parser.limits.maximum_steps {
                return Err(AstParserError::Parser(RuntimeError::StepLimitExceeded));
            }

            let state = *states.last().ok_or(AstParserError::Parser(
                RuntimeError::InvalidTableExecution("empty state stack"),
            ))?;
            let lookahead = make_lookahead(
                self.parser.tables.lookahead,
                &lexed.parser_terminal_ids,
                token_index,
            );
            let row = action_row(self.parser.tables, state);
            let action = row
                .entries
                .iter()
                .find(|entry| entry.lookahead == lookahead)
                .map(|entry| entry.action)
                .or(row.fallback);
            let Some(action) = action else {
                return Ok(AstParseOutcome::SyntaxError(SyntaxError {
                    state,
                    token_index,
                    lookahead,
                    expected: row
                        .entries
                        .iter()
                        .map(|entry| entry.lookahead.to_vec())
                        .collect(),
                }));
            };
            let lookahead_byte = tokens
                .get(token_index)
                .map_or(input.len() as u64, |token| token.byte_start as u64);

            match action {
                Action::Shift(target) => {
                    let token = tokens.get(token_index).ok_or(AstParserError::Parser(
                        RuntimeError::InvalidTableExecution(
                            "parser table attempts to shift end of input",
                        ),
                    ))?;
                    values.push(make_token_value(input, token)?);
                    states.push(target);
                    token_index += 1;
                }
                Action::Reduce(production_id) => {
                    let production = &self.parser.tables.productions[production_id as usize];
                    let rhs_len = production.rhs_len as usize;
                    if states.len() <= rhs_len || values.len() < rhs_len {
                        return Err(AstParserError::Parser(RuntimeError::InvalidTableExecution(
                            "parser stack underflow during reduction",
                        )));
                    }
                    let rhs = values.split_off(values.len() - rhs_len);
                    states.truncate(states.len() - rhs_len);
                    let source = *states.last().ok_or(AstParserError::Parser(
                        RuntimeError::InvalidTableExecution("empty state stack after reduction"),
                    ))?;
                    let target = goto_row(self.parser.tables, source)
                        .entries
                        .iter()
                        .find(|entry| entry.nonterminal == production.lhs)
                        .map(|entry| entry.state)
                        .ok_or(AstParserError::Parser(RuntimeError::InvalidTableExecution(
                            "missing GOTO after reduction",
                        )))?;
                    if let Some(entries) = trace.as_mut() {
                        entries.push(ReductionTraceEntry {
                            production_id,
                            rhs_cardinalities: rhs
                                .iter()
                                .map(|value| match value.payload.kind {
                                    AstValueKind::Optional | AstValueKind::List => {
                                        value.payload.elements.len()
                                    }
                                    _ => 1,
                                })
                                .collect(),
                        });
                    }
                    let value = execute_reduction(
                        &self.reductions.instructions[production_id as usize],
                        rhs,
                        lookahead_byte,
                    )
                    .map_err(AstParserError::Reduction)?;
                    values.push(value);
                    states.push(target);
                }
                Action::Accept => {
                    if token_index != tokens.len() || values.len() != 1 {
                        return Err(AstParserError::Parser(RuntimeError::InvalidTableExecution(
                            "parser table accepted an incomplete AST",
                        )));
                    }
                    let value = values.pop().ok_or(AstParserError::Parser(
                        RuntimeError::InvalidTableExecution("missing accepted AST value"),
                    ))?;
                    return Ok(AstParseOutcome::Accepted(value.payload));
                }
            }

            if states.len() > self.parser.limits.maximum_stack_depth
                || values.len() > self.parser.limits.maximum_stack_depth
            {
                return Err(AstParserError::Parser(RuntimeError::StackLimitExceeded));
            }
            if states.len() != values.len() + 1 {
                return Err(AstParserError::Parser(RuntimeError::InvalidTableExecution(
                    "parser state and semantic stacks are inconsistent",
                )));
            }
        }
    }
}

/// Executes one validated neutral reduction instruction.
///
/// # Errors
///
/// Returns an error when the RHS length, spans, or operand value kinds do not
/// satisfy the instruction contract.
pub fn execute_reduction(
    instruction: &ReductionInstruction,
    mut rhs: Vec<ReductionStackValue>,
    lookahead_byte: u64,
) -> Result<ReductionStackValue, ReductionError> {
    if rhs.len() != instruction.rhs_len as usize {
        return Err(ReductionError::RhsSizeMismatch);
    }
    let span = reduction_span(instruction.span_policy, &rhs, lookahead_byte)?;
    let mut result = match instruction.opcode {
        ReductionOpcode::Unit => AstValue::wrapper(AstValueKind::Unit, span),
        ReductionOpcode::Forward => {
            let index = instruction.operands[0] as usize;
            std::mem::replace(
                &mut rhs[index].payload,
                AstValue::wrapper(AstValueKind::Unit, span),
            )
        }
        ReductionOpcode::ConstructNode => {
            aggregate_value(AstValueKind::Node, instruction, &mut rhs, span)
        }
        ReductionOpcode::ConstructNodeOrForward => {
            let operand = instruction.operands[0];
            let mut empty = true;
            for field in instruction.fields {
                if field.rhs_index == operand {
                    continue;
                }
                let guard = &rhs[field.rhs_index as usize].payload;
                if !matches!(guard.kind, AstValueKind::List | AstValueKind::Optional) {
                    return Err(ReductionError::ValueKindMismatch);
                }
                empty &= guard.elements.is_empty();
            }
            if empty {
                take_operand(&mut rhs, operand, span)
            } else {
                aggregate_value(AstValueKind::Node, instruction, &mut rhs, span)
            }
        }
        ReductionOpcode::ConstructRecord => {
            aggregate_value(AstValueKind::Record, instruction, &mut rhs, span)
        }
        ReductionOpcode::OptionalSome => {
            let mut value = AstValue::wrapper(AstValueKind::Optional, span);
            value
                .elements
                .push(take_operand(&mut rhs, instruction.operands[0], span));
            value
        }
        ReductionOpcode::OptionalNone => AstValue::wrapper(AstValueKind::Optional, span),
        ReductionOpcode::ListEmpty => AstValue::wrapper(AstValueKind::List, span),
        ReductionOpcode::ListSingleton => {
            let mut value = AstValue::wrapper(AstValueKind::List, span);
            value
                .elements
                .push(take_operand(&mut rhs, instruction.operands[0], span));
            value
        }
        ReductionOpcode::ListAppend => {
            let prefix_index = instruction.operands[0];
            let element_index = instruction.operands[1];
            let element = take_operand(&mut rhs, element_index, span);
            let mut prefix = take_operand(&mut rhs, prefix_index, span);
            if prefix.kind != AstValueKind::List {
                return Err(ReductionError::ValueKindMismatch);
            }
            prefix.source_span = span;
            prefix.elements.push(element);
            prefix
        }
    };
    result.recognized_span = span;
    Ok(ReductionStackValue {
        payload: result,
        recognized_span: span,
    })
}

fn take_operand(rhs: &mut [ReductionStackValue], index: u32, span: InputSpan) -> AstValue {
    std::mem::replace(
        &mut rhs[index as usize].payload,
        AstValue::wrapper(AstValueKind::Unit, span),
    )
}

fn aggregate_value(
    kind: AstValueKind,
    instruction: &ReductionInstruction,
    rhs: &mut [ReductionStackValue],
    span: InputSpan,
) -> AstValue {
    let mut result = AstValue::wrapper(kind, span);
    instruction
        .type_name
        .expect("validated type name")
        .clone_into(&mut result.type_name);
    instruction
        .variant_name
        .unwrap_or_default()
        .clone_into(&mut result.variant_name);
    result.field_names.reserve(instruction.fields.len());
    result.elements.reserve(instruction.fields.len());
    for field in instruction.fields {
        result.field_names.push(field.name.to_owned());
        result
            .elements
            .push(take_operand(rhs, field.rhs_index, span));
    }
    result
}

fn reduction_span(
    policy: ReductionSpanPolicy,
    rhs: &[ReductionStackValue],
    lookahead_byte: u64,
) -> Result<InputSpan, ReductionError> {
    if policy == ReductionSpanPolicy::EmptyAtLookahead {
        return Ok(InputSpan {
            begin_byte: lookahead_byte,
            end_byte: lookahead_byte,
        });
    }
    if rhs.is_empty()
        || rhs
            .iter()
            .any(|value| value.recognized_span.begin_byte > value.recognized_span.end_byte)
        || rhs
            .windows(2)
            .any(|pair| pair[0].recognized_span.end_byte > pair[1].recognized_span.begin_byte)
    {
        return Err(ReductionError::InvalidSpan);
    }
    let mut nonempty = rhs
        .iter()
        .filter(|value| value.recognized_span.begin_byte != value.recognized_span.end_byte);
    let Some(first) = nonempty.next() else {
        return Ok(rhs[0].recognized_span);
    };
    let last = nonempty.next_back().unwrap_or(first);
    Ok(InputSpan {
        begin_byte: first.recognized_span.begin_byte,
        end_byte: last.recognized_span.end_byte,
    })
}

fn make_token_value(input: &[u8], token: &Token) -> Result<ReductionStackValue, AstParserError> {
    let text = std::str::from_utf8(&input[token.byte_start..token.byte_end])
        .map_err(|_| AstParserError::InvalidInput("token text is not valid UTF-8"))?
        .to_owned();
    let span = InputSpan {
        begin_byte: token.byte_start as u64,
        end_byte: token.byte_end as u64,
    };
    let mut payload = AstValue::wrapper(AstValueKind::Token, span);
    payload.token_kind = token.terminal;
    payload.token_text = text;
    Ok(ReductionStackValue {
        payload,
        recognized_span: span,
    })
}

fn parser_tokens(lexed: &LexResult) -> Result<Vec<&Token>, &'static str> {
    let result: Vec<_> = lexed
        .tokens
        .iter()
        .filter(|token| token.channel.is_none())
        .collect();
    if result.len() != lexed.parser_terminal_ids.len()
        || result
            .iter()
            .zip(&lexed.parser_terminal_ids)
            .any(|(token, terminal)| token.terminal != *terminal)
    {
        return Err("lexer parser-terminal projection is inconsistent");
    }
    Ok(result)
}

fn validate_token_spans(
    tokens: &[&Token],
    input: &[u8],
    terminal_count: u32,
) -> Result<(), &'static str> {
    let mut previous_end = 0;
    for token in tokens {
        if token.terminal >= terminal_count
            || token.byte_start < previous_end
            || token.byte_start > token.byte_end
            || token.byte_end > input.len()
        {
            return Err("lexer token has an invalid terminal or byte span");
        }
        previous_end = token.byte_end;
    }
    Ok(())
}

fn validate_program(
    tables: &crate::lr::ParserTables,
    program: &ReductionProgram,
) -> Result<(), ReductionError> {
    if program.instructions.len() != tables.productions.len() {
        return Err(ReductionError::InvalidProgram(
            "every production must have one reduction instruction",
        ));
    }
    for (production, instruction) in tables.productions.iter().zip(program.instructions) {
        if instruction.rhs_len != production.rhs_len {
            return Err(ReductionError::InvalidProgram(
                "reduction RHS length differs from its production",
            ));
        }
        let rhs_len = instruction.rhs_len;
        if instruction.operands.iter().any(|&index| index >= rhs_len)
            || instruction
                .fields
                .iter()
                .any(|field| field.name.is_empty() || field.rhs_index >= rhs_len)
        {
            return Err(ReductionError::InvalidProgram(
                "reduction operand is outside its RHS",
            ));
        }
        let expected_operands = match instruction.opcode {
            ReductionOpcode::Forward
            | ReductionOpcode::ConstructNodeOrForward
            | ReductionOpcode::OptionalSome
            | ReductionOpcode::ListSingleton => 1,
            ReductionOpcode::ListAppend => 2,
            _ => 0,
        };
        let constructs_fields = matches!(
            instruction.opcode,
            ReductionOpcode::ConstructNode
                | ReductionOpcode::ConstructNodeOrForward
                | ReductionOpcode::ConstructRecord
        );
        let valid_shape = match instruction.opcode {
            ReductionOpcode::Unit
            | ReductionOpcode::Forward
            | ReductionOpcode::OptionalSome
            | ReductionOpcode::ListSingleton
            | ReductionOpcode::ListAppend => instruction.fields.is_empty(),
            ReductionOpcode::ConstructNode | ReductionOpcode::ConstructRecord => true,
            ReductionOpcode::ConstructNodeOrForward => {
                instruction.fields.len() >= 2
                    && instruction.operands.first().is_some_and(|operand| {
                        instruction
                            .fields
                            .iter()
                            .filter(|field| field.rhs_index == *operand)
                            .count()
                            == 1
                    })
            }
            ReductionOpcode::OptionalNone | ReductionOpcode::ListEmpty => {
                instruction.fields.is_empty() && rhs_len == 0
            }
        };
        if !valid_shape
            || instruction.operands.len() != expected_operands
            || constructs_fields != instruction.type_name.is_some()
            || (instruction.variant_name.is_some()
                && !matches!(
                    instruction.opcode,
                    ReductionOpcode::ConstructNode | ReductionOpcode::ConstructNodeOrForward
                ))
            || (instruction.span_policy == ReductionSpanPolicy::EmptyAtLookahead && rhs_len != 0)
            || (instruction.span_policy == ReductionSpanPolicy::MatchedRhs && rhs_len == 0)
        {
            return Err(ReductionError::InvalidProgram(
                "reduction instruction has an invalid shape",
            ));
        }
    }
    Ok(())
}
