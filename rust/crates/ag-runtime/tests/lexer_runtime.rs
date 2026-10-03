use agas_runtime::generated::compressed_table::{LEXER_TABLES, PARSER_TABLES};
use agas_runtime::lexer::{
    CodePointRange, DfaState, Lexer, LexerError, LexerRule, LexerTables, NfaState, OrderedNfa,
    Transition,
};
use agas_runtime::lr::{ParseOutcome, Parser, RuntimeLimits};

#[test]
fn generated_lexer_and_parser_accept_a_minimal_table() {
    let source = br#"compressed-table "LR(1)" {
        start-state 0;
        action-row 0 { any => error; }
        action-state-rows [0];
        goto-row 0 {}
        goto-state-rows [0];
    }"#;
    let lexer = Lexer::new(&LEXER_TABLES).expect("generated lexer is valid");
    let tokenization = lexer.tokenize(source).expect("minimal table tokenizes");
    assert!(
        tokenization
            .tokens
            .iter()
            .all(|token| token.channel.is_none())
    );
    assert_eq!(
        &source[tokenization.tokens[1].byte_start..tokenization.tokens[1].byte_end],
        br#""LR(1)""#
    );

    let parser =
        Parser::new(&PARSER_TABLES, RuntimeLimits::default()).expect("generated parser is valid");
    assert_eq!(
        parser.parse(&tokenization.parser_terminal_ids),
        Ok(ParseOutcome::Accepted)
    );
}

#[test]
fn generated_lexer_reports_encoding_and_matching_errors() {
    let lexer = Lexer::new(&LEXER_TABLES).expect("generated lexer is valid");
    assert_eq!(
        lexer.tokenize(&[0xc0]),
        Err(LexerError::InvalidEncoding {
            token_start: 0,
            error_offset: 1,
        })
    );
    assert_eq!(
        lexer.tokenize(b"@"),
        Err(LexerError::NoMatchingRule {
            token_start: 0,
            error_offset: 0,
        })
    );
}

static ORDERED_NFA_TABLES: LexerTables = LexerTables {
    terminal_count: 1,
    channel_count: 0,
    rules: &[LexerRule {
        name: "LazyAThenB",
        terminal: Some(0),
        channel: None,
        skipped: false,
    }],
    dfa_states: &[DfaState {
        accepting_rule: None,
        transitions: &[],
    }],
    ordered_nfas: &[OrderedNfa {
        start_state: 0,
        accepting_state: 4,
        states: &[
            NfaState {
                epsilon_transitions: &[],
                transitions: &[Transition {
                    ranges: &[CodePointRange {
                        first: b'a' as u32,
                        last: b'a' as u32,
                    }],
                    target: 1,
                }],
                ordered_decision: false,
                activates_priority: false,
            },
            NfaState {
                epsilon_transitions: &[2, 3],
                transitions: &[],
                ordered_decision: true,
                activates_priority: true,
            },
            NfaState {
                epsilon_transitions: &[],
                transitions: &[Transition {
                    ranges: &[CodePointRange {
                        first: b'b' as u32,
                        last: b'b' as u32,
                    }],
                    target: 4,
                }],
                ordered_decision: false,
                activates_priority: false,
            },
            NfaState {
                epsilon_transitions: &[],
                transitions: &[Transition {
                    ranges: &[CodePointRange {
                        first: b'a' as u32,
                        last: b'a' as u32,
                    }],
                    target: 1,
                }],
                ordered_decision: false,
                activates_priority: false,
            },
            NfaState {
                epsilon_transitions: &[],
                transitions: &[],
                ordered_decision: false,
                activates_priority: false,
            },
        ],
    }],
};

#[test]
fn executes_a_priority_activating_ordered_nfa() {
    let lexer = Lexer::new(&ORDERED_NFA_TABLES).expect("ordered NFA is valid");
    let result = lexer.tokenize(b"aab").expect("ordered NFA matches");
    assert_eq!(result.parser_terminal_ids, vec![0]);
    assert_eq!(result.tokens[0].byte_start, 0);
    assert_eq!(result.tokens[0].byte_end, 3);
}
