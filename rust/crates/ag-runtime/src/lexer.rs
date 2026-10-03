//! Runtime for validated, statically embedded Unicode lexer automata.

use std::collections::HashSet;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CodePointRange {
    pub first: u32,
    pub last: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Transition<'a> {
    pub ranges: &'a [CodePointRange],
    pub target: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DfaState<'a> {
    pub accepting_rule: Option<u32>,
    pub transitions: &'a [Transition<'a>],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct NfaState<'a> {
    pub epsilon_transitions: &'a [u32],
    pub transitions: &'a [Transition<'a>],
    pub ordered_decision: bool,
    pub activates_priority: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OrderedNfa<'a> {
    pub start_state: u32,
    pub accepting_state: u32,
    pub states: &'a [NfaState<'a>],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct LexerRule<'a> {
    pub name: &'a str,
    pub terminal: Option<u32>,
    pub channel: Option<u32>,
    pub skipped: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct LexerTables<'a> {
    pub terminal_count: u32,
    pub channel_count: u32,
    pub rules: &'a [LexerRule<'a>],
    pub dfa_states: &'a [DfaState<'a>],
    pub ordered_nfas: &'a [OrderedNfa<'a>],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Token {
    pub terminal: u32,
    pub channel: Option<u32>,
    pub byte_start: usize,
    pub byte_end: usize,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LexResult {
    pub tokens: Vec<Token>,
    pub parser_terminal_ids: Vec<u32>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum LexerError {
    InvalidTable(&'static str),
    InvalidEncoding {
        token_start: usize,
        error_offset: usize,
    },
    NoMatchingRule {
        token_start: usize,
        error_offset: usize,
    },
}

pub struct Lexer<'a> {
    tables: &'a LexerTables<'a>,
}

impl<'a> Lexer<'a> {
    /// Validates every static automaton reference before accepting the tables.
    ///
    /// # Errors
    ///
    /// Returns [`LexerError::InvalidTable`] when rule metadata, Unicode ranges,
    /// or an automaton reference violates the runtime contract.
    pub fn new(tables: &'a LexerTables<'a>) -> Result<Self, LexerError> {
        validate_tables(tables, false)?;
        Ok(Self { tables })
    }

    pub fn new_contextual(tables: &'a LexerTables<'a>) -> Result<Self, LexerError> {
        validate_tables(tables, true)?;
        Ok(Self { tables })
    }

    /// Tokenizes arbitrary bytes as strict UTF-8 and preserves byte ranges.
    ///
    /// # Errors
    ///
    /// Returns an encoding error for malformed UTF-8 or a matching error when
    /// no lexer rule accepts the next input prefix.
    pub fn tokenize(&self, input: &[u8]) -> Result<LexResult, LexerError> {
        if self.tables.ordered_nfas.is_empty() {
            self.tokenize_dfa(input)
        } else {
            self.tokenize_ordered_nfas(input)
        }
    }

    fn tokenize_dfa(&self, input: &[u8]) -> Result<LexResult, LexerError> {
        let mut result = LexResult {
            tokens: Vec::new(),
            parser_terminal_ids: Vec::new(),
        };
        let mut offset = 0;
        while offset < input.len() {
            let mut state = 0_usize;
            let mut cursor = offset;
            let mut matched_rule = None;
            let mut matched_end = offset;
            while cursor < input.len() {
                let (code_point, next_offset) = decode(input, cursor, offset)?;
                let Some(transition) = self.tables.dfa_states[state]
                    .transitions
                    .iter()
                    .find(|transition| contains(transition.ranges, code_point))
                else {
                    break;
                };
                state = transition.target as usize;
                cursor = next_offset;
                if let Some(rule) = self.tables.dfa_states[state].accepting_rule {
                    matched_rule = Some(rule as usize);
                    matched_end = cursor;
                }
            }
            let Some(rule) = matched_rule else {
                return Err(LexerError::NoMatchingRule {
                    token_start: offset,
                    error_offset: cursor,
                });
            };
            emit(&mut result, &self.tables.rules[rule], offset, matched_end);
            offset = matched_end;
        }
        Ok(result)
    }

    fn tokenize_ordered_nfas(&self, input: &[u8]) -> Result<LexResult, LexerError> {
        let mut code_points = Vec::new();
        let mut byte_offsets = vec![0];
        let mut offset = 0;
        while offset < input.len() {
            let (code_point, next_offset) = decode(input, offset, offset)?;
            code_points.push(code_point);
            offset = next_offset;
            byte_offsets.push(offset);
        }

        let mut result = LexResult {
            tokens: Vec::new(),
            parser_terminal_ids: Vec::new(),
        };
        let mut point = 0;
        while point < code_points.len() {
            let mut matched_rule = None;
            let mut matched_length = 0;
            for (rule, nfa) in self.tables.ordered_nfas.iter().enumerate() {
                if let Some(length) = preferred_prefix(nfa, &code_points[point..])
                    && length > matched_length
                {
                    matched_rule = Some(rule);
                    matched_length = length;
                }
            }
            let Some(rule) = matched_rule else {
                return Err(LexerError::NoMatchingRule {
                    token_start: byte_offsets[point],
                    error_offset: byte_offsets[point],
                });
            };
            emit(
                &mut result,
                &self.tables.rules[rule],
                byte_offsets[point],
                byte_offsets[point + matched_length],
            );
            point += matched_length;
        }
        Ok(result)
    }
}

fn emit(result: &mut LexResult, rule: &LexerRule, begin: usize, end: usize) {
    if rule.skipped {
        return;
    }
    let terminal = rule
        .terminal
        .expect("validated non-skipped lexer rule has a terminal");
    result.tokens.push(Token {
        terminal,
        channel: rule.channel,
        byte_start: begin,
        byte_end: end,
    });
    if rule.channel.is_none() {
        result.parser_terminal_ids.push(terminal);
    }
}

fn decode(input: &[u8], start: usize, token_start: usize) -> Result<(u32, usize), LexerError> {
    let first = input[start];
    if first < 0x80 {
        return Ok((u32::from(first), start + 1));
    }
    let (length, mut value, minimum) = if first & 0xe0 == 0xc0 {
        (2, u32::from(first & 0x1f), 0x80)
    } else if first & 0xf0 == 0xe0 {
        (3, u32::from(first & 0x0f), 0x800)
    } else if first & 0xf8 == 0xf0 {
        (4, u32::from(first & 0x07), 0x10000)
    } else {
        return Err(invalid_encoding(token_start, start));
    };
    if start + length > input.len() {
        return Err(invalid_encoding(token_start, input.len()));
    }
    for index in 1..length {
        let continuation = input[start + index];
        if continuation & 0xc0 != 0x80 {
            return Err(invalid_encoding(token_start, start + index));
        }
        value = (value << 6) | u32::from(continuation & 0x3f);
    }
    if value < minimum || !is_unicode_scalar(value) {
        return Err(invalid_encoding(token_start, start));
    }
    Ok((value, start + length))
}

const fn invalid_encoding(token_start: usize, error_offset: usize) -> LexerError {
    LexerError::InvalidEncoding {
        token_start,
        error_offset,
    }
}

const fn is_unicode_scalar(value: u32) -> bool {
    value <= 0x10_ffff && !(value >= 0xd800 && value <= 0xdfff)
}

fn contains(ranges: &[CodePointRange], value: u32) -> bool {
    ranges
        .iter()
        .any(|range| range.first <= value && value <= range.last)
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
struct Config {
    state: u32,
    offset: usize,
}

fn preferred_after_priority(nfa: &OrderedNfa, input: &[u32], start: Config) -> Option<usize> {
    let mut visited = HashSet::new();
    let mut pending = vec![start];
    while let Some(current) = pending.pop() {
        if !visited.insert(current) {
            continue;
        }
        if current.state == nfa.accepting_state {
            return Some(current.offset);
        }
        let state = &nfa.states[current.state as usize];
        for transition in state.transitions.iter().rev() {
            if current.offset < input.len() && contains(transition.ranges, input[current.offset]) {
                pending.push(Config {
                    state: transition.target,
                    offset: current.offset + 1,
                });
            }
        }
        for &target in state.epsilon_transitions.iter().rev() {
            pending.push(Config {
                state: target,
                offset: current.offset,
            });
        }
    }
    None
}

fn preferred_prefix(nfa: &OrderedNfa, input: &[u32]) -> Option<usize> {
    let mut visited = HashSet::new();
    let mut pending = vec![Config {
        state: nfa.start_state,
        offset: 0,
    }];
    let mut result = None;
    while let Some(current) = pending.pop() {
        if !visited.insert(current) {
            continue;
        }
        let state = &nfa.states[current.state as usize];
        if state.activates_priority {
            if let Some(preferred) = preferred_after_priority(nfa, input, current)
                && result.is_none_or(|length| preferred > length)
            {
                result = Some(preferred);
            }
            continue;
        }
        if current.state == nfa.accepting_state
            && result.is_none_or(|length| current.offset > length)
        {
            result = Some(current.offset);
        }
        for &target in state.epsilon_transitions {
            pending.push(Config {
                state: target,
                offset: current.offset,
            });
        }
        if current.offset < input.len() {
            for transition in state.transitions {
                if contains(transition.ranges, input[current.offset]) {
                    pending.push(Config {
                        state: transition.target,
                        offset: current.offset + 1,
                    });
                }
            }
        }
    }
    result
}

fn validate_tables(tables: &LexerTables, contextual: bool) -> Result<(), LexerError> {
    if tables.rules.is_empty() || tables.dfa_states.is_empty() {
        return Err(LexerError::InvalidTable(
            "lexer rules and DFA states must not be empty",
        ));
    }
    for rule in tables.rules {
        if rule.name.is_empty()
            || rule.skipped == rule.terminal.is_some()
            || rule.terminal.is_some_and(|id| id >= tables.terminal_count)
            || rule.channel.is_some_and(|id| id >= tables.channel_count)
        {
            return Err(LexerError::InvalidTable(
                "lexer rule metadata is inconsistent",
            ));
        }
    }
    for state in tables.dfa_states {
        if state
            .accepting_rule
            .is_some_and(|rule| rule as usize >= tables.rules.len())
        {
            return Err(LexerError::InvalidTable(
                "DFA accepts an unknown lexer rule",
            ));
        }
        validate_transitions(state.transitions, tables.dfa_states.len(), true)?;
    }
    if !tables.ordered_nfas.is_empty() && tables.ordered_nfas.len() != tables.rules.len() {
        return Err(LexerError::InvalidTable(
            "ordered NFA count must equal the lexer rule count",
        ));
    }
    let mut has_priority = false;
    for nfa in tables.ordered_nfas {
        if nfa.states.is_empty()
            || nfa.start_state as usize >= nfa.states.len()
            || nfa.accepting_state as usize >= nfa.states.len()
        {
            return Err(LexerError::InvalidTable(
                "ordered NFA endpoints are invalid",
            ));
        }
        for state in nfa.states {
            has_priority |= state.activates_priority;
            if state.activates_priority && !state.ordered_decision {
                return Err(LexerError::InvalidTable(
                    "an NFA priority activation must be an ordered decision",
                ));
            }
            if state
                .epsilon_transitions
                .iter()
                .any(|&target| target as usize >= nfa.states.len())
            {
                return Err(LexerError::InvalidTable("NFA epsilon target is invalid"));
            }
            validate_transitions(state.transitions, nfa.states.len(), false)?;
        }
    }
    if !tables.ordered_nfas.is_empty() && !has_priority && !contextual {
        return Err(LexerError::InvalidTable(
            "ordered NFAs require a priority-activating decision",
        ));
    }
    Ok(())
}

fn validate_transitions(
    transitions: &[Transition],
    state_count: usize,
    require_disjoint: bool,
) -> Result<(), LexerError> {
    let mut all_ranges = Vec::new();
    for transition in transitions {
        if transition.target as usize >= state_count {
            return Err(LexerError::InvalidTable(
                "lexer transition target is outside states",
            ));
        }
        let mut previous_last = None;
        for &range in transition.ranges {
            if range.first > range.last
                || !is_unicode_scalar(range.first)
                || !is_unicode_scalar(range.last)
                || (range.first <= 0xdfff && range.last >= 0xd800)
                || previous_last.is_some_and(|last| range.first <= last)
            {
                return Err(LexerError::InvalidTable(
                    "lexer transition has invalid Unicode ranges",
                ));
            }
            previous_last = Some(range.last);
            all_ranges.push(range);
        }
        if transition.ranges.is_empty() {
            return Err(LexerError::InvalidTable(
                "lexer transition must contain a range",
            ));
        }
    }
    if require_disjoint {
        all_ranges.sort_unstable_by_key(|range| range.first);
        if all_ranges
            .windows(2)
            .any(|pair| pair[1].first <= pair[0].last)
        {
            return Err(LexerError::InvalidTable("DFA transitions overlap"));
        }
    }
    Ok(())
}

/// Decoded source reused by parser-directed token requests.
pub struct ContextualInput {
    points: Vec<u32>,
    offsets: Vec<usize>,
}

impl ContextualInput {
    pub fn new(input: &[u8]) -> Result<Self, LexerError> {
        let mut points = Vec::new();
        let mut offsets = vec![0];
        let mut offset = 0;
        while offset < input.len() {
            let (point, next) = decode(input, offset, offset)?;
            points.push(point);
            offset = next;
            offsets.push(offset);
        }
        Ok(Self { points, offsets })
    }
}

impl Lexer<'_> {
    /// Returns hidden tokens and the next parser token under a class mask.
    /// The cursor is an index into the decoded source, not a byte offset.
    pub fn next_contextual(
        &self,
        input: &ContextualInput,
        cursor: &mut usize,
        active: u64,
        required: &[u64],
    ) -> Result<(Vec<Token>, Option<Token>), LexerError> {
        if required.len() != self.tables.rules.len()
            || self.tables.ordered_nfas.len() != required.len()
        {
            return Err(LexerError::InvalidTable(
                "contextual lexer requires all rule NFAs and masks",
            ));
        }
        let mut hidden = Vec::new();
        while *cursor < input.points.len() {
            let mut best = None;
            let mut length = 0;
            for (id, nfa) in self.tables.ordered_nfas.iter().enumerate() {
                if required[id] & active != required[id] {
                    continue;
                }
                if let Some(size) = preferred_prefix(nfa, &input.points[*cursor..])
                    && size > length
                {
                    best = Some(id);
                    length = size;
                }
            }
            let Some(id) = best else {
                return Err(LexerError::NoMatchingRule {
                    token_start: input.offsets[*cursor],
                    error_offset: input.offsets[*cursor],
                });
            };
            let begin = *cursor;
            *cursor += length;
            let rule = &self.tables.rules[id];
            if rule.skipped {
                continue;
            }
            let token = Token {
                terminal: rule.terminal.expect("validated lexer rule"),
                channel: rule.channel,
                byte_start: input.offsets[begin],
                byte_end: input.offsets[*cursor],
            };
            if token.channel.is_none() {
                return Ok((hidden, Some(token)));
            }
            hidden.push(token);
        }
        Ok((hidden, None))
    }
}
