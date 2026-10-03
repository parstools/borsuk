use agas_runtime::ast::{
    AstParseOutcome, AstParser, AstValue, AstValueKind, InputSpan, ReductionError, ReductionField,
    ReductionInstruction, ReductionOpcode, ReductionSpanPolicy, ReductionStackValue,
    execute_reduction, measure_ast,
};
use agas_runtime::generated::compressed_table::{LEXER_TABLES, PARSER_TABLES, REDUCTION_PROGRAM};
use agas_runtime::lexer::Lexer;
use agas_runtime::lr::{Parser, RuntimeLimits};
use agas_runtime::recovery::{RecoveryEditKind, RecoveryPolicy, parse_with_recovery};

fn token(kind: u32, text: &str, begin: u64, end: u64) -> ReductionStackValue {
    let span = InputSpan {
        begin_byte: begin,
        end_byte: end,
    };
    ReductionStackValue {
        payload: AstValue {
            kind: AstValueKind::Token,
            source_span: span,
            recognized_span: span,
            type_name: String::new(),
            variant_name: String::new(),
            token_kind: kind,
            token_text: text.to_owned(),
            field_names: Vec::new(),
            elements: Vec::new(),
        },
        recognized_span: span,
    }
}

#[test]
fn generated_frontend_builds_a_neutral_ast() {
    let source = br#"compressed-table "LR(1)" {
        start-state 0;
        action-row 0 { any => error; }
        action-state-rows [0];
        goto-row 0 {}
        goto-state-rows [0];
    }"#;
    let lexer = Lexer::new(&LEXER_TABLES).expect("generated lexer is valid");
    let tokens = lexer.tokenize(source).expect("minimal table tokenizes");
    let parser =
        Parser::new(&PARSER_TABLES, RuntimeLimits::default()).expect("generated parser is valid");
    let frontend =
        AstParser::new(parser, &REDUCTION_PROGRAM).expect("generated reductions are valid");
    let AstParseOutcome::Accepted(root) = frontend.parse(source, &tokens).expect("AST parse runs")
    else {
        panic!("minimal table must be accepted");
    };

    assert_eq!(root.kind, AstValueKind::Node);
    assert_eq!(root.type_name, "document");
    assert_eq!(
        root.field_names,
        [
            "parserName",
            "startState",
            "actionRows",
            "actionStateRows",
            "gotoRows",
            "gotoStateRows",
        ]
    );
    assert_eq!(root.source_span.begin_byte, 0);
    assert_eq!(root.source_span.end_byte, source.len() as u64);
}

#[test]
fn generic_recovery_can_skip_an_extra_terminal() {
    let source = br#"compressed-table "LR(1)" {
        start-state 0;
        action-row 0 { any => error; }
        action-state-rows [0];
        goto-row 0 {}
        goto-state-rows [0];
    };"#;
    let lexer = Lexer::new(&LEXER_TABLES).unwrap();
    let tokenized = lexer.tokenize(source).unwrap();
    let parser = Parser::new(&PARSER_TABLES, RuntimeLimits::default()).unwrap();
    let frontend = AstParser::new(parser, &REDUCTION_PROGRAM).unwrap();
    assert!(matches!(
        frontend.parse(source, &tokenized).unwrap(),
        AstParseOutcome::SyntaxError(_)
    ));
    let recovered = parse_with_recovery(
        &frontend,
        source,
        &tokenized,
        &RecoveryPolicy {
            insertable_terminals: &[],
            max_repairs: 4,
        },
    )
    .unwrap();
    assert!(recovered.root.is_some());
    assert_eq!(recovered.edits.len(), 1);
    assert!(matches!(
        recovered.edits[0].kind,
        RecoveryEditKind::Skipped(_)
    ));
}

#[test]
fn forward_preserves_payload_but_recognizes_the_complete_rhs() {
    static FORWARD: ReductionInstruction = ReductionInstruction {
        rhs_len: 3,
        opcode: ReductionOpcode::Forward,
        span_policy: ReductionSpanPolicy::MatchedRhs,
        type_name: None,
        variant_name: None,
        operands: &[1],
        fields: &[],
    };
    let value = execute_reduction(
        &FORWARD,
        vec![
            token(0, "(", 0, 1),
            token(1, "name", 1, 5),
            token(2, ")", 5, 6),
        ],
        6,
    )
    .expect("forward reduction succeeds");
    assert_eq!(value.payload.token_text, "name");
    assert_eq!(
        value.payload.source_span,
        InputSpan {
            begin_byte: 1,
            end_byte: 5
        }
    );
    assert_eq!(
        value.recognized_span,
        InputSpan {
            begin_byte: 0,
            end_byte: 6
        }
    );
    assert_eq!(value.payload.recognized_span, value.recognized_span);
}

#[test]
fn chain_reduction_promotes_only_when_every_tail_is_absent() {
    static CHAIN: ReductionInstruction = ReductionInstruction {
        rhs_len: 3,
        opcode: ReductionOpcode::ConstructNodeOrForward,
        span_policy: ReductionSpanPolicy::MatchedRhs,
        type_name: Some("expression"),
        variant_name: None,
        operands: &[0],
        fields: &[
            ReductionField {
                name: "first",
                rhs_index: 0,
            },
            ReductionField {
                name: "optional",
                rhs_index: 1,
            },
            ReductionField {
                name: "rest",
                rhs_index: 2,
            },
        ],
    };
    let guard = |kind, present, begin| {
        let mut value = token(0, "", begin, begin + u64::from(present));
        value.payload.kind = kind;
        if present {
            value
                .payload
                .elements
                .push(token(1, "+", begin, begin + 1).payload);
        }
        value
    };
    for optional in [false, true] {
        for repeated in [false, true] {
            let result = execute_reduction(
                &CHAIN,
                vec![
                    token(0, "x", 0, 1),
                    guard(AstValueKind::Optional, optional, 1),
                    guard(AstValueKind::List, repeated, 2),
                ],
                3,
            )
            .unwrap();
            if optional || repeated {
                assert_eq!(result.payload.kind, AstValueKind::Node);
                assert_eq!(result.payload.type_name, "expression");
                assert_eq!(result.payload.elements.len(), 3);
            } else {
                assert_eq!(result.payload.kind, AstValueKind::Token);
                assert_eq!(result.payload.token_text, "x");
                assert_eq!(measure_ast(&result.payload).maximum_depth, 0);
            }
        }
    }
    assert_eq!(
        execute_reduction(
            &CHAIN,
            vec![
                token(0, "x", 0, 1),
                token(1, "+", 1, 2),
                guard(AstValueKind::List, false, 2),
            ],
            3
        ),
        Err(ReductionError::ValueKindMismatch)
    );
}

#[test]
fn empty_and_list_reductions_preserve_order_and_spans() {
    static EMPTY: ReductionInstruction = ReductionInstruction {
        rhs_len: 0,
        opcode: ReductionOpcode::ListEmpty,
        span_policy: ReductionSpanPolicy::EmptyAtLookahead,
        type_name: None,
        variant_name: None,
        operands: &[],
        fields: &[],
    };
    static APPEND: ReductionInstruction = ReductionInstruction {
        rhs_len: 2,
        opcode: ReductionOpcode::ListAppend,
        span_policy: ReductionSpanPolicy::MatchedRhs,
        type_name: None,
        variant_name: None,
        operands: &[0, 1],
        fields: &[],
    };
    let mut list = execute_reduction(&EMPTY, Vec::new(), 0).expect("empty list");
    for index in 0..1_000_u64 {
        list = execute_reduction(
            &APPEND,
            vec![list, token(0, &index.to_string(), index, index + 1)],
            index + 1,
        )
        .expect("list append");
    }
    assert_eq!(list.payload.elements.len(), 1_000);
    assert_eq!(list.payload.elements[0].token_text, "0");
    assert_eq!(list.payload.elements[999].token_text, "999");
    assert_eq!(
        list.recognized_span,
        InputSpan {
            begin_byte: 0,
            end_byte: 1_000
        }
    );

    let error = execute_reduction(
        &APPEND,
        vec![token(0, "not-a-list", 0, 1), token(0, "x", 1, 2)],
        2,
    );
    assert_eq!(error, Err(ReductionError::ValueKindMismatch));
}
