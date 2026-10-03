//! Parser-directed tokenization of a validated package before AST reduction.
use crate::artifact::LoadedPackage;
use crate::artifact_runtime::PackageRunError;
use crate::lexer::{ContextualInput, LexResult, Lexer, Token};
use crate::lr::{Action, LookaheadSymbol, RuntimeError, RuntimeLimits};

struct Preview {
    source: Token,
    scoped: Token,
    hidden: Vec<Token>,
    cursor: usize,
}

fn append(result: &mut LexResult, preview: Preview) {
    result.tokens.extend(preview.hidden);
    result.parser_terminal_ids.push(preview.scoped.terminal);
    result.tokens.push(preview.scoped);
}

fn invalid(message: &'static str) -> PackageRunError {
    PackageRunError::Parser(RuntimeError::InvalidTableExecution(message))
}

pub(crate) fn tokenize(
    package: &LoadedPackage,
    lexer: &Lexer,
    input: &[u8],
    limits: RuntimeLimits,
) -> Result<LexResult, PackageRunError> {
    let context = package
        .lexer()
        .context
        .as_ref()
        .expect("contextual package");
    let decoded = ContextualInput::new(input).map_err(PackageRunError::Lexer)?;
    let table = package.parser_table().view();
    let mut result = LexResult {
        tokens: Vec::new(),
        parser_terminal_ids: Vec::new(),
    };
    let mut states = vec![table.start_state()];
    let mut committed = 0;
    let mut buffered: Vec<Token> = Vec::new();
    for _ in 0..limits.maximum_steps {
        let state = *states
            .last()
            .ok_or_else(|| invalid("empty contextual stack"))?;
        let nodes = &context.rows[state as usize];
        let mut node = 0;
        let mut cursor = committed;
        let mut prefix = Vec::new();
        let mut preview: Vec<Preview> = Vec::new();
        let mut trailing = Vec::new();
        while prefix.len() < table.lookahead() as usize && !nodes[node].edges.is_empty() {
            let (hidden, token) = lexer
                .next_contextual(
                    &decoded,
                    &mut cursor,
                    nodes[node].active,
                    &context.required_classes,
                )
                .map_err(PackageRunError::Lexer)?;
            let Some(source) = token else {
                trailing = hidden;
                prefix.push(LookaheadSymbol::EndOfInput);
                break;
            };
            let mut edges = nodes[node].edges.iter().filter(|edge| {
                edge.terminal
                    .is_some_and(|id| context.original_terminals[id as usize] == source.terminal)
            });
            let Some(edge) = edges.next() else {
                // Original token IDs are outside the scoped ACTION alphabet.
                // Preserve the unexpected token so the AST pass reports a syntax error.
                for item in preview {
                    append(&mut result, item);
                }
                result.tokens.extend(hidden);
                result.tokens.push(source);
                result.parser_terminal_ids.push(source.terminal);
                return Ok(result);
            };
            if edges.next().is_some() {
                return Err(invalid("ambiguous contextual terminal mapping"));
            }
            let scoped = Token {
                terminal: edge.terminal.expect("matched terminal"),
                ..source
            };
            prefix.push(LookaheadSymbol::Terminal(scoped.terminal));
            preview.push(Preview {
                source,
                scoped,
                hidden,
                cursor,
            });
            node = edge.target as usize;
        }
        for (old, new) in buffered.iter().zip(&preview) {
            if old != &new.source {
                return Err(invalid("lexer context changes buffered token"));
            }
        }
        let row = &table.action_rows()[table.action_state_rows()[state as usize] as usize];
        let action = row
            .entries
            .iter()
            .find(|entry| entry.lookahead == prefix)
            .map(|entry| entry.action)
            .or(row.fallback.map(Action::Reduce));
        match action {
            Some(Action::Shift(target)) => {
                if preview.is_empty() {
                    return Err(invalid("contextual shift of EOF"));
                }
                let first = preview.remove(0);
                committed = first.cursor;
                append(&mut result, first);
                buffered = preview.iter().map(|item| item.source).collect();
                states.push(target);
            }
            Some(Action::Reduce(id)) => {
                let production = &package.productions().productions[id as usize];
                if production.rhs.len() >= states.len() {
                    return Err(invalid("contextual stack underflow"));
                }
                states.truncate(states.len() - production.rhs.len());
                let source = *states
                    .last()
                    .ok_or_else(|| invalid("empty contextual stack"))?;
                let row = &table.goto_rows()[table.goto_state_rows()[source as usize] as usize];
                let target = row
                    .iter()
                    .find(|entry| entry.nonterminal == production.lhs)
                    .ok_or_else(|| invalid("missing contextual GOTO"))?
                    .state;
                states.push(target);
                buffered = preview.iter().map(|item| item.source).collect();
            }
            Some(Action::Accept) => {
                if prefix.first() != Some(&LookaheadSymbol::EndOfInput) {
                    return Err(invalid("contextual accept before EOF"));
                }
                result.tokens.extend(trailing);
                return Ok(result);
            }
            None => {
                for item in preview {
                    append(&mut result, item);
                }
                result.tokens.extend(trailing);
                return Ok(result);
            }
        }
        if states.len() > limits.maximum_stack_depth {
            return Err(PackageRunError::Parser(RuntimeError::StackLimitExceeded));
        }
    }
    Err(PackageRunError::Parser(RuntimeError::StepLimitExceeded))
}
