//! Runtime for validated, statically embedded LR(k) tables.

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum LookaheadSymbol {
    Terminal(u32),
    EndOfInput,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Action {
    Shift(u32),
    Reduce(u32),
    Accept,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ActionEntry<'a> {
    pub lookahead: &'a [LookaheadSymbol],
    pub action: Action,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ActionRow<'a> {
    pub entries: &'a [ActionEntry<'a>],
    pub fallback: Option<Action>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GotoEntry {
    pub nonterminal: u32,
    pub state: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GotoRow<'a> {
    pub entries: &'a [GotoEntry],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Production {
    pub lhs: u32,
    pub rhs_len: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ParserTables<'a> {
    pub lookahead: u32,
    pub terminal_count: u32,
    pub nonterminal_count: u32,
    pub start_state: u32,
    pub action_rows: &'a [ActionRow<'a>],
    pub action_state_rows: &'a [u32],
    pub goto_rows: &'a [GotoRow<'a>],
    pub goto_state_rows: &'a [u32],
    pub productions: &'a [Production],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RuntimeLimits {
    pub maximum_steps: u64,
    pub maximum_stack_depth: usize,
}

impl Default for RuntimeLimits {
    fn default() -> Self {
        Self {
            maximum_steps: 100_000_000,
            maximum_stack_depth: 10_000_000,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SyntaxError {
    pub state: u32,
    pub token_index: usize,
    pub lookahead: Vec<LookaheadSymbol>,
    pub expected: Vec<Vec<LookaheadSymbol>>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ParseOutcome {
    Accepted,
    SyntaxError(SyntaxError),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum RuntimeError {
    InvalidTable(&'static str),
    InvalidInput { token_index: usize, terminal: u32 },
    InvalidTableExecution(&'static str),
    StepLimitExceeded,
    StackLimitExceeded,
}

pub struct Parser<'a> {
    pub(crate) tables: &'a ParserTables<'a>,
    pub(crate) limits: RuntimeLimits,
}

impl<'a> Parser<'a> {
    /// Validates every table reference before accepting the static tables.
    ///
    /// # Errors
    ///
    /// Returns [`RuntimeError::InvalidTable`] when a table reference or a
    /// runtime limit violates the static runtime contract.
    pub fn new(tables: &'a ParserTables<'a>, limits: RuntimeLimits) -> Result<Self, RuntimeError> {
        validate_tables(tables)?;
        if limits.maximum_steps == 0 || limits.maximum_stack_depth == 0 {
            return Err(RuntimeError::InvalidTable(
                "parser runtime limits must be nonzero",
            ));
        }
        Ok(Self { tables, limits })
    }

    /// Runs LR(k) over terminal identifiers. Lexing and AST reductions are
    /// deliberately separate runtime layers.
    ///
    /// # Errors
    ///
    /// Returns an error for an invalid terminal, inconsistent table
    /// execution, or an exceeded resource limit. A grammar mismatch is
    /// returned as [`ParseOutcome::SyntaxError`], not as a runtime error.
    pub fn parse(&self, tokens: &[u32]) -> Result<ParseOutcome, RuntimeError> {
        for (token_index, &terminal) in tokens.iter().enumerate() {
            if terminal >= self.tables.terminal_count {
                return Err(RuntimeError::InvalidInput {
                    token_index,
                    terminal,
                });
            }
        }

        let mut states = vec![self.tables.start_state];
        let mut token_index = 0_usize;
        let mut steps = 0_u64;
        loop {
            steps = steps
                .checked_add(1)
                .ok_or(RuntimeError::StepLimitExceeded)?;
            if steps > self.limits.maximum_steps {
                return Err(RuntimeError::StepLimitExceeded);
            }

            let state = *states
                .last()
                .ok_or(RuntimeError::InvalidTableExecution("empty state stack"))?;
            let lookahead = make_lookahead(self.tables.lookahead, tokens, token_index);
            let row = action_row(self.tables, state);
            let action = row
                .entries
                .iter()
                .find(|entry| entry.lookahead == lookahead)
                .map(|entry| entry.action)
                .or(row.fallback);

            let Some(action) = action else {
                return Ok(ParseOutcome::SyntaxError(SyntaxError {
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

            match action {
                Action::Shift(target) => {
                    if token_index >= tokens.len() {
                        return Err(RuntimeError::InvalidTableExecution(
                            "parser table attempts to shift end of input",
                        ));
                    }
                    states.push(target);
                    token_index += 1;
                }
                Action::Reduce(production_id) => {
                    let production = &self.tables.productions[production_id as usize];
                    let rhs_len = production.rhs_len as usize;
                    if states.len() <= rhs_len {
                        return Err(RuntimeError::InvalidTableExecution(
                            "parser stack underflow during reduction",
                        ));
                    }
                    states.truncate(states.len() - rhs_len);
                    let source = *states.last().ok_or(RuntimeError::InvalidTableExecution(
                        "empty state stack after reduction",
                    ))?;
                    let target = goto_row(self.tables, source)
                        .entries
                        .iter()
                        .find(|entry| entry.nonterminal == production.lhs)
                        .map(|entry| entry.state)
                        .ok_or(RuntimeError::InvalidTableExecution(
                            "missing GOTO after reduction",
                        ))?;
                    states.push(target);
                }
                Action::Accept => {
                    if token_index != tokens.len() {
                        return Err(RuntimeError::InvalidTableExecution(
                            "parser table accepts before end of input",
                        ));
                    }
                    return Ok(ParseOutcome::Accepted);
                }
            }

            if states.len() > self.limits.maximum_stack_depth {
                return Err(RuntimeError::StackLimitExceeded);
            }
        }
    }
}

pub(crate) fn make_lookahead(k: u32, tokens: &[u32], token_index: usize) -> Vec<LookaheadSymbol> {
    let mut result = Vec::with_capacity(k as usize);
    let mut position = token_index;
    while position < tokens.len() && result.len() < k as usize {
        result.push(LookaheadSymbol::Terminal(tokens[position]));
        position += 1;
    }
    if position == tokens.len() && result.len() < k as usize {
        result.push(LookaheadSymbol::EndOfInput);
    }
    result
}

pub(crate) fn action_row<'a>(tables: &'a ParserTables<'a>, state: u32) -> &'a ActionRow<'a> {
    &tables.action_rows[tables.action_state_rows[state as usize] as usize]
}

pub(crate) fn goto_row<'a>(tables: &'a ParserTables<'a>, state: u32) -> &'a GotoRow<'a> {
    &tables.goto_rows[tables.goto_state_rows[state as usize] as usize]
}

fn validate_tables(tables: &ParserTables) -> Result<(), RuntimeError> {
    if tables.lookahead == 0 {
        return Err(RuntimeError::InvalidTable("lookahead must be nonzero"));
    }
    let state_count = tables.action_state_rows.len();
    if state_count == 0 || tables.goto_state_rows.len() != state_count {
        return Err(RuntimeError::InvalidTable(
            "ACTION and GOTO state maps must have the same nonzero size",
        ));
    }
    if tables.start_state as usize >= state_count {
        return Err(RuntimeError::InvalidTable("start state is out of range"));
    }
    if tables.action_rows.is_empty() || tables.goto_rows.is_empty() {
        return Err(RuntimeError::InvalidTable("row set must not be empty"));
    }
    for &row in tables.action_state_rows {
        if row as usize >= tables.action_rows.len() {
            return Err(RuntimeError::InvalidTable(
                "ACTION state map refers to an unknown row",
            ));
        }
    }
    for &row in tables.goto_state_rows {
        if row as usize >= tables.goto_rows.len() {
            return Err(RuntimeError::InvalidTable(
                "GOTO state map refers to an unknown row",
            ));
        }
    }
    for row in tables.action_rows {
        for entry in row.entries {
            if entry.lookahead.is_empty() || entry.lookahead.len() > tables.lookahead as usize {
                return Err(RuntimeError::InvalidTable(
                    "ACTION entry has an invalid lookahead length",
                ));
            }
            for (index, symbol) in entry.lookahead.iter().enumerate() {
                match symbol {
                    LookaheadSymbol::Terminal(terminal) if *terminal >= tables.terminal_count => {
                        return Err(RuntimeError::InvalidTable(
                            "ACTION entry refers to an unknown terminal",
                        ));
                    }
                    LookaheadSymbol::EndOfInput if index + 1 != entry.lookahead.len() => {
                        return Err(RuntimeError::InvalidTable(
                            "end of input must terminate a lookahead word",
                        ));
                    }
                    _ => {}
                }
            }
            validate_action(entry.action, tables, state_count)?;
        }
        if let Some(action) = row.fallback {
            if !matches!(action, Action::Reduce(_)) {
                return Err(RuntimeError::InvalidTable(
                    "only reduction can be a fallback action",
                ));
            }
            validate_action(action, tables, state_count)?;
        }
    }
    for row in tables.goto_rows {
        for entry in row.entries {
            if entry.nonterminal >= tables.nonterminal_count {
                return Err(RuntimeError::InvalidTable(
                    "GOTO entry refers to an unknown nonterminal",
                ));
            }
            if entry.state as usize >= state_count {
                return Err(RuntimeError::InvalidTable(
                    "GOTO entry refers to an unknown state",
                ));
            }
        }
    }
    for production in tables.productions {
        if production.lhs >= tables.nonterminal_count {
            return Err(RuntimeError::InvalidTable(
                "production refers to an unknown nonterminal",
            ));
        }
    }
    Ok(())
}

fn validate_action(
    action: Action,
    tables: &ParserTables,
    state_count: usize,
) -> Result<(), RuntimeError> {
    match action {
        Action::Shift(state) if state as usize >= state_count => Err(RuntimeError::InvalidTable(
            "shift refers to an unknown state",
        )),
        Action::Reduce(production) if production as usize >= tables.productions.len() => Err(
            RuntimeError::InvalidTable("reduction refers to an unknown production"),
        ),
        _ => Ok(()),
    }
}
