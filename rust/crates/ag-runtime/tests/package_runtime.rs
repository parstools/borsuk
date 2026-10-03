use agas_runtime::artifact::{PackageLimits, load_package_directory};
use agas_runtime::artifact_runtime::PackageRunError;
use agas_runtime::ast::{AstParseOutcome, AstParserError, AstValue, AstValueKind};
use agas_runtime::ast_wire::{emit_ast_wire, parse_ast_wire};
use agas_runtime::lexer::LexerError;
use agas_runtime::lr::Action;
use agas_runtime::lr::RuntimeLimits;
use agas_runtime::lr::{LookaheadSymbol, RuntimeError};
use sha2::{Digest, Sha256};
use std::fs;
use std::path::Path;

const FIXTURES: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/fixtures");

#[test]
fn parses_ag_grammar_from_the_loaded_package() {
    let directory = Path::new(FIXTURES).join("ag/v1");
    let package = load_package_directory(&directory, PackageLimits::default()).expect("package");
    let source = fs::read(Path::new(FIXTURES).join("grammars/Ag.ag")).expect("Ag.ag");
    let parsed = package
        .parse(&source, RuntimeLimits::default())
        .expect("runtime");
    let AstParseOutcome::Accepted(root) = parsed.outcome else {
        panic!("Ag.ag must be accepted");
    };
    assert_eq!(root.kind, AstValueKind::Node);
    assert_eq!(root.type_name, "document");
    assert_eq!(
        root.source_span.end_byte,
        parsed.lexed.tokens.last().expect("tokens").byte_end as u64
    );
    assert_eq!(parsed.lexed.tokens.len(), 782);
    assert_eq!(parsed.lexed.parser_terminal_ids.len(), 782);
    assert!(
        parsed
            .lexed
            .tokens
            .iter()
            .all(|token| token.channel.is_none())
    );
    assert_eq!(
        token_digest(&parsed.lexed, &source),
        "e22559bb112d15330cd1ab7b48feacbe45afce0a92292dfd775e85744ff3f7de"
    );
    assert_eq!(
        ast_digest(&root),
        "c2db1469a4967e4219726fde73790c9702ef08efae162ad4ce7bbcf2eb1673ed"
    );
}

fn number(out: &mut Vec<u8>, value: u64) {
    out.extend_from_slice(value.to_string().as_bytes());
    out.push(b';');
}

fn bytes(out: &mut Vec<u8>, value: &[u8]) {
    out.extend_from_slice(value.len().to_string().as_bytes());
    out.push(b':');
    out.extend_from_slice(value);
}

fn ast_bytes(out: &mut Vec<u8>, value: &AstValue) {
    number(out, value.kind as u64);
    number(out, value.source_span.begin_byte);
    number(out, value.source_span.end_byte);
    bytes(out, value.type_name.as_bytes());
    bytes(out, value.variant_name.as_bytes());
    number(out, u64::from(value.token_kind));
    bytes(out, value.token_text.as_bytes());
    number(out, value.field_names.len() as u64);
    for field in &value.field_names {
        bytes(out, field.as_bytes());
    }
    number(out, value.elements.len() as u64);
    for element in &value.elements {
        ast_bytes(out, element);
    }
}

fn ast_digest(value: &AstValue) -> String {
    let mut out = Vec::new();
    ast_bytes(&mut out, value);
    format!("{:x}", Sha256::digest(out))
}

fn token_digest(lexed: &agas_runtime::lexer::LexResult, source: &[u8]) -> String {
    let mut out = Vec::new();
    number(&mut out, lexed.tokens.len() as u64);
    for token in &lexed.tokens {
        number(&mut out, u64::from(token.terminal));
        number(
            &mut out,
            token.channel.map_or(0, |channel| u64::from(channel) + 1),
        );
        number(&mut out, token.byte_start as u64);
        number(&mut out, token.byte_end as u64);
        bytes(&mut out, &source[token.byte_start..token.byte_end]);
    }
    number(&mut out, lexed.parser_terminal_ids.len() as u64);
    for id in &lexed.parser_terminal_ids {
        number(&mut out, u64::from(*id));
    }
    format!("{:x}", Sha256::digest(out))
}

#[test]
fn matches_cpp_error_locations_and_classes() {
    let directory = Path::new(FIXTURES).join("ag/v1");
    let package = load_package_directory(&directory, PackageLimits::default()).expect("package");
    let malformed = b"grammar Broken; start node: ;";
    let parsed = package
        .parse(malformed, RuntimeLimits::default())
        .expect("syntax outcome");
    let AstParseOutcome::SyntaxError(error) = parsed.outcome else {
        panic!("syntax error required");
    };
    assert_eq!(error.state, 4);
    assert_eq!(error.token_index, 2);
    assert_eq!(
        error.lookahead,
        vec![LookaheadSymbol::Terminal(27), LookaheadSymbol::Terminal(17)]
    );
    assert_eq!(error.expected.len(), 9);
    let token = parsed
        .lexed
        .tokens
        .iter()
        .filter(|token| token.channel.is_none())
        .nth(error.token_index)
        .expect("offending token");
    assert_eq!((token.byte_start, token.byte_end), (14, 15));

    let mut invalid_utf8 = b"grammar Broken;\n".to_vec();
    invalid_utf8.push(0xc0);
    assert_eq!(
        package.parse(&invalid_utf8, RuntimeLimits::default()),
        Err(PackageRunError::Lexer(LexerError::InvalidEncoding {
            token_start: 16,
            error_offset: 17,
        }))
    );
    assert_eq!(
        package.parse(b"grammar Broken;\n@", RuntimeLimits::default()),
        Err(PackageRunError::Lexer(LexerError::NoMatchingRule {
            token_start: 16,
            error_offset: 16,
        }))
    );
}

#[test]
fn loaded_package_enforces_step_and_stack_limits() {
    let directory = Path::new(FIXTURES).join("ag/v1");
    let package = load_package_directory(&directory, PackageLimits::default()).expect("package");
    let source = fs::read(Path::new(FIXTURES).join("grammars/Ag.ag")).expect("Ag.ag");
    let limits = RuntimeLimits {
        maximum_steps: 1,
        ..RuntimeLimits::default()
    };
    assert_eq!(
        package.parse(&source, limits),
        Err(PackageRunError::Ast(AstParserError::Parser(
            RuntimeError::StepLimitExceeded
        )))
    );
    let limits = RuntimeLimits {
        maximum_stack_depth: 1,
        ..RuntimeLimits::default()
    };
    assert_eq!(
        package.parse(&source, limits),
        Err(PackageRunError::Ast(AstParserError::Parser(
            RuntimeError::StackLimitExceeded
        )))
    );
}

#[test]
fn ag_source_uses_a_second_symbol_for_an_explicit_vs_default_action() {
    let directory = Path::new(FIXTURES).join("ag/v1");
    let package = load_package_directory(&directory, PackageLimits::default()).expect("package");
    let source = fs::read(Path::new(FIXTURES).join("grammars/Ag.ag")).expect("Ag.ag");
    let lexed = package.tokenize(&source).expect("tokens");
    let tokens = &lexed.parser_terminal_ids;
    let table = package.parser_table().view();
    assert_eq!(table.lookahead(), 2);
    let mut stack = vec![table.start_state()];
    let mut position = 0;
    let mut decisions_using_second = 0;
    let mut default_reductions = 0;
    for _ in 0..100_000 {
        let state = *stack.last().expect("state");
        let word: Vec<_> = (position..position + 2)
            .map_while(|index| tokens.get(index).copied().map(LookaheadSymbol::Terminal))
            .chain((position + 2 > tokens.len()).then_some(LookaheadSymbol::EndOfInput))
            .collect();
        let row = table.action_row(state).expect("ACTION row");
        let action = table.action(state, &word).expect("valid input action");
        if row.entries.iter().all(|entry| entry.lookahead != word)
            && row
                .fallback
                .is_some_and(|rule| action == Action::Reduce(rule))
        {
            default_reductions += 1;
        }
        if word.len() == 2
            && let Some(first) = word.first()
        {
            let alternatives: Vec<_> = row
                .entries
                .iter()
                .filter(|entry| entry.lookahead.first() == Some(first))
                .collect();
            if (alternatives.len() > 1 && alternatives.iter().any(|entry| entry.action != action))
                || row
                    .fallback
                    .is_some_and(|fallback| Action::Reduce(fallback) != action)
            {
                decisions_using_second += 1;
            }
        }
        match action {
            Action::Shift(target) => {
                stack.push(target);
                position += 1;
            }
            Action::Reduce(id) => {
                let production = &package.productions().productions[id as usize];
                stack.truncate(stack.len() - production.rhs.len());
                let source = *stack.last().expect("reduction source");
                stack.push(table.goto(source, production.lhs).expect("GOTO"));
            }
            Action::Accept => {
                assert_eq!(position, tokens.len());
                assert!(
                    decisions_using_second > 0,
                    "Ag.ag did not visit a second-lookahead decision"
                );
                assert!(
                    default_reductions > 0,
                    "Ag.ag did not exercise default reductions"
                );
                return;
            }
        }
    }
    panic!("parser trace exceeded its bound");
}

#[test]
fn full_ag_ast_has_a_canonical_versioned_wire_round_trip() {
    let directory = Path::new(FIXTURES).join("ag/v1");
    let package = load_package_directory(&directory, PackageLimits::default()).expect("package");
    let source = fs::read(Path::new(FIXTURES).join("grammars/Ag.ag")).expect("Ag.ag");
    let parsed = package
        .parse(&source, RuntimeLimits::default())
        .expect("runtime");
    let AstParseOutcome::Accepted(root) = parsed.outcome else {
        panic!("accepted AST required");
    };
    let context = package.wire_context("Ag.ag", &source).expect("context");
    let wire = emit_ast_wire(&root, context).expect("wire");
    assert_eq!(parse_ast_wire(&wire, context), Ok(root));
    assert!(
        parse_ast_wire(
            &wire,
            package.wire_context("Other.ag", &source).expect("context")
        )
        .is_err()
    );
    let wrong_schema = agas_runtime::ast_wire::AstWireContext {
        ast_schema_version: 2,
        ..context
    };
    assert!(parse_ast_wire(&wire, wrong_schema).is_err());
    assert!(
        parse_ast_wire(
            &wire,
            package.wire_context("Ag.ag", b"changed").expect("context")
        )
        .is_err()
    );
    let wrong_symbols = agas_runtime::ast_wire::AstWireContext {
        symbols_sha256: "0000000000000000000000000000000000000000000000000000000000000000",
        ..context
    };
    assert!(parse_ast_wire(&wire, wrong_symbols).is_err());
    assert!(
        parse_ast_wire(
            &wire.replace("\"wireVersion\": 1", "\"wireVersion\": 2"),
            context
        )
        .is_err()
    );
    assert!(
        parse_ast_wire(
            &wire.replace(
                "\"wireVersion\": 1",
                "\"wireVersion\": 1, \"wireVersion\": 1"
            ),
            context
        )
        .is_err()
    );
}
