use std::collections::HashSet;

use crate::ast::{AstParseOutcome, AstParser, AstParserError, AstValue, InputSpan};
use crate::lexer::{LexResult, Token};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RecoveryEditKind {
    Inserted(u32),
    Skipped(u32),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RecoveryEdit {
    pub kind: RecoveryEditKind,
    pub source: InputSpan,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RecoveryStop {
    NoViableRepair,
    LimitReached,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RecoveryFailure {
    pub reason: RecoveryStop,
    pub source: InputSpan,
}

pub struct RecoveryPolicy<'a> {
    pub insertable_terminals: &'a [u32],
    pub max_repairs: usize,
}

pub struct RecoveredParse {
    pub root: Option<AstValue>,
    pub edits: Vec<RecoveryEdit>,
    pub stop: Option<RecoveryFailure>,
}

struct Candidate {
    tokens: Vec<Token>,
    edit: RecoveryEdit,
    insertion: Option<(usize, u32)>,
    score: (u8, usize, usize, usize),
}

fn parser_tokens(tokens: &[Token]) -> LexResult {
    LexResult {
        tokens: tokens.to_vec(),
        parser_terminal_ids: tokens.iter().map(|token| token.terminal).collect(),
    }
}

fn parse(
    parser: &AstParser<'_>,
    input: &[u8],
    tokens: &[Token],
) -> Result<AstParseOutcome, AstParserError> {
    parser.parse(input, &parser_tokens(tokens))
}

fn error_position(tokens: &[Token], index: usize, source_len: usize) -> usize {
    tokens
        .get(index)
        .map_or(source_len, |token| token.byte_start)
}

/// Tries bounded token insertions and deletions, reparsing every candidate.
/// The caller supplies grammar-specific insertable terminals and interprets edits.
///
/// # Errors
///
/// Returns an error if the parser rejects its tables, reductions, or input representation.
#[allow(clippy::too_many_lines)]
pub fn parse_with_recovery(
    parser: &AstParser<'_>,
    input: &[u8],
    lexed: &LexResult,
    policy: &RecoveryPolicy<'_>,
) -> Result<RecoveredParse, AstParserError> {
    let mut tokens: Vec<_> = lexed
        .tokens
        .iter()
        .filter(|token| token.channel.is_none())
        .copied()
        .collect();
    let mut edits = Vec::new();
    let mut seen = HashSet::new();
    let mut inserted_at = HashSet::new();
    let mut outcome = parser.parse(input, lexed)?;

    for _ in 0..policy.max_repairs {
        let AstParseOutcome::SyntaxError(error) = outcome else {
            let AstParseOutcome::Accepted(root) = outcome else {
                unreachable!()
            };
            return Ok(RecoveredParse {
                root: Some(root),
                edits,
                stop: None,
            });
        };
        let position = error_position(&tokens, error.token_index, input.len());
        let mut candidates = Vec::new();
        // LR(k) may report a missing delimiter at the token preceding its position.
        for &id in policy.insertable_terminals {
            for offset in 0..parser.lookahead_depth() {
                let insert_index = error.token_index + offset;
                if insert_index > tokens.len() {
                    continue;
                }
                let insert_byte = if offset == 0 {
                    position
                } else {
                    tokens[insert_index - 1].byte_end
                };
                if inserted_at.contains(&(insert_byte, id)) {
                    continue;
                }
                let mut inserted = tokens.clone();
                inserted.insert(
                    insert_index,
                    Token {
                        terminal: id,
                        channel: None,
                        byte_start: insert_byte,
                        byte_end: insert_byte,
                    },
                );
                let span = InputSpan {
                    begin_byte: insert_byte as u64,
                    end_byte: insert_byte as u64,
                };
                candidates.push(Candidate {
                    tokens: inserted,
                    edit: RecoveryEdit {
                        kind: RecoveryEditKind::Inserted(id),
                        source: span,
                    },
                    insertion: Some((insert_byte, id)),
                    score: (0, 0, 0, 0),
                });
            }
        }
        if let Some(skipped) = tokens.get(error.token_index) {
            let mut deleted = tokens.clone();
            deleted.remove(error.token_index);
            candidates.push(Candidate {
                tokens: deleted,
                edit: RecoveryEdit {
                    kind: RecoveryEditKind::Skipped(skipped.terminal),
                    source: InputSpan {
                        begin_byte: skipped.byte_start as u64,
                        end_byte: skipped.byte_end as u64,
                    },
                },
                insertion: None,
                score: (0, 0, 0, 0),
            });
        }
        let mut best_insert = None;
        let mut best_delete = None;
        // Every candidate is reparsed before choosing a bounded local repair.
        for mut candidate in candidates {
            let fingerprint: Vec<_> = candidate
                .tokens
                .iter()
                .map(|token| (token.terminal, token.byte_start, token.byte_end))
                .collect();
            if seen.contains(&fingerprint) {
                continue;
            }
            let trial = parse(parser, input, &candidate.tokens)?;
            let (accepted, next_position, next_index) = match trial {
                AstParseOutcome::Accepted(_) => (1, input.len(), candidate.tokens.len()),
                AstParseOutcome::SyntaxError(next) => (
                    0,
                    error_position(&candidate.tokens, next.token_index, input.len()),
                    next.token_index,
                ),
            };
            candidate.score = (
                accepted,
                next_position,
                next_index,
                usize::from(candidate.insertion.is_some()),
            );
            if candidate.insertion.is_some() {
                if accepted == 0 && next_position <= position && position != input.len() {
                    continue;
                }
                if best_insert
                    .as_ref()
                    .is_none_or(|previous: &Candidate| candidate.score > previous.score)
                {
                    best_insert = Some(candidate);
                }
            } else if best_delete
                .as_ref()
                .is_none_or(|previous: &Candidate| candidate.score > previous.score)
            {
                best_delete = Some(candidate);
            }
        }
        let best = match (best_insert, best_delete) {
            (Some(inserted), Some(deleted)) => Some(if inserted.score >= deleted.score {
                inserted
            } else {
                deleted
            }),
            (Some(inserted), None) => Some(inserted),
            (None, Some(deleted)) => Some(deleted),
            (None, None) => None,
        };
        let Some(best) = best else {
            return Ok(RecoveredParse {
                root: None,
                edits,
                stop: Some(RecoveryFailure {
                    reason: RecoveryStop::NoViableRepair,
                    source: InputSpan {
                        begin_byte: position as u64,
                        end_byte: position as u64,
                    },
                }),
            });
        };
        seen.insert(
            tokens
                .iter()
                .map(|token| (token.terminal, token.byte_start, token.byte_end))
                .collect::<Vec<_>>(),
        );
        if let Some(location) = best.insertion {
            inserted_at.insert(location);
        }
        tokens = best.tokens;
        edits.push(best.edit);
        outcome = parse(parser, input, &tokens)?;
    }
    match outcome {
        AstParseOutcome::Accepted(root) => Ok(RecoveredParse {
            root: Some(root),
            edits,
            stop: None,
        }),
        AstParseOutcome::SyntaxError(error) => {
            let position = error_position(&tokens, error.token_index, input.len()) as u64;
            Ok(RecoveredParse {
                root: None,
                edits,
                stop: Some(RecoveryFailure {
                    reason: RecoveryStop::LimitReached,
                    source: InputSpan {
                        begin_byte: position,
                        end_byte: position,
                    },
                }),
            })
        }
    }
}
