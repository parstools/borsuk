use agas_runtime::lr::{
    Action, ActionEntry, ActionRow, GotoEntry, GotoRow, LookaheadSymbol, ParseOutcome, Parser,
    ParserTables, Production, RuntimeError, RuntimeLimits,
};

use LookaheadSymbol::{EndOfInput, Terminal};

static TABLES: ParserTables = ParserTables {
    lookahead: 2,
    terminal_count: 2,
    nonterminal_count: 1,
    start_state: 0,
    action_rows: &[
        ActionRow {
            entries: &[ActionEntry {
                lookahead: &[Terminal(0), Terminal(1)],
                action: Action::Shift(1),
            }],
            fallback: None,
        },
        ActionRow {
            entries: &[ActionEntry {
                lookahead: &[Terminal(1), EndOfInput],
                action: Action::Shift(2),
            }],
            fallback: None,
        },
        ActionRow {
            entries: &[],
            fallback: Some(Action::Reduce(0)),
        },
        ActionRow {
            entries: &[ActionEntry {
                lookahead: &[EndOfInput],
                action: Action::Accept,
            }],
            fallback: None,
        },
    ],
    action_state_rows: &[0, 1, 2, 3],
    goto_rows: &[
        GotoRow {
            entries: &[GotoEntry {
                nonterminal: 0,
                state: 3,
            }],
        },
        GotoRow { entries: &[] },
    ],
    goto_state_rows: &[0, 1, 1, 1],
    productions: &[Production { lhs: 0, rhs_len: 2 }],
};

#[test]
fn executes_lr_two_shift_reduce_goto_and_accept() {
    let parser = Parser::new(&TABLES, RuntimeLimits::default()).expect("valid static table");
    assert_eq!(parser.parse(&[0, 1]), Ok(ParseOutcome::Accepted));
}

#[test]
fn reports_the_full_lr_two_lookahead() {
    let parser = Parser::new(&TABLES, RuntimeLimits::default()).expect("valid static table");
    let ParseOutcome::SyntaxError(error) = parser.parse(&[0, 0]).expect("syntax result") else {
        panic!("invalid input must produce a syntax error");
    };
    assert_eq!(error.state, 0);
    assert_eq!(error.token_index, 0);
    assert_eq!(error.lookahead, vec![Terminal(0), Terminal(0)]);
    assert_eq!(error.expected, vec![&[Terminal(0), Terminal(1)][..]]);
}

#[test]
fn rejects_invalid_tokens_before_execution() {
    let parser = Parser::new(&TABLES, RuntimeLimits::default()).expect("valid static table");
    assert_eq!(
        parser.parse(&[2]),
        Err(RuntimeError::InvalidInput {
            token_index: 0,
            terminal: 2,
        })
    );
}

#[test]
fn enforces_the_step_limit() {
    let parser = Parser::new(
        &TABLES,
        RuntimeLimits {
            maximum_steps: 2,
            maximum_stack_depth: 16,
        },
    )
    .expect("valid static table");
    assert_eq!(parser.parse(&[0, 1]), Err(RuntimeError::StepLimitExceeded));
}

#[test]
fn generated_compressed_table_parser_accepts_a_minimal_table() {
    use agas_runtime::generated::compressed_table::PARSER_TABLES;

    // compressed-table "LR(1)" { start-state 0;
    // action-row 0 { any => error; } action-state-rows [0];
    // goto-row 0 {} goto-state-rows [0]; }
    let tokens = [
        0, 20, 12, 1, 19, 17, 2, 19, 12, 6, 18, 10, 17, 13, 3, 14, 19, 15, 17, 4, 19, 12, 13, 5,
        14, 19, 15, 17, 13,
    ];
    let parser =
        Parser::new(&PARSER_TABLES, RuntimeLimits::default()).expect("generated table is valid");
    assert_eq!(parser.parse(&tokens), Ok(ParseOutcome::Accepted));
}
