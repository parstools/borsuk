use agas_runtime::ast::{AstParseOutcome, AstParser, AstValue};
use agas_runtime::generated::compressed_table::{LEXER_TABLES, PARSER_TABLES, REDUCTION_PROGRAM};
use agas_runtime::lexer::Lexer;
use agas_runtime::lr::{Action, LookaheadSymbol, Parser, RuntimeLimits};
use agas_runtime::table_adapter::{
    OwnedParserTable, ParserAlgorithm, ParserTableLimits, ParserTableSymbols, TableAdapterError,
};

const SMALL_DSL: &str = r#"compressed-table "LR(2)" {
  start-state 0;
  action-row 0 {
    ["id", "EOF"] => shift 1;
    ["EOF", EOF] => reduce 0;
    any => error;
  }
  action-row 1 {
    [EOF] => accept;
    any => reduce 0;
  }
  action-state-rows [0, 1];
  goto-row 0 { "S" => 1; }
  goto-row 1 {}
  goto-state-rows [0, 1];
}"#;

fn parse_ast(source: &str) -> AstValue {
    let lexer = Lexer::new(&LEXER_TABLES).expect("generated lexer");
    let token_stream = lexer.tokenize(source.as_bytes()).expect("valid DSL tokens");
    let parser = Parser::new(&PARSER_TABLES, RuntimeLimits::default()).expect("generated table");
    let frontend = AstParser::new(parser, &REDUCTION_PROGRAM).expect("generated reductions");
    match frontend
        .parse(source.as_bytes(), &token_stream)
        .expect("AST runtime")
    {
        AstParseOutcome::Accepted(root) => root,
        AstParseOutcome::SyntaxError(error) => panic!("DSL syntax: {error:?}"),
    }
}

fn small_table(source: &str) -> Result<OwnedParserTable, TableAdapterError> {
    let symbols = ParserTableSymbols {
        terminals: &["id", "EOF"],
        nonterminals: &["S"],
    };
    OwnedParserTable::from_ast(
        &parse_ast(source),
        &symbols,
        1,
        source.len(),
        ParserTableLimits::default(),
    )
}

#[test]
fn small_table_matches_cpp_loader_fixture() {
    let table = small_table(SMALL_DSL).expect("valid table");
    let view = table.view();
    assert_eq!(view.algorithm(), ParserAlgorithm::Lr);
    assert_eq!(view.lookahead(), 2);
    assert_eq!(view.start_state(), 0);
    assert_eq!(view.state_count(), 2);
    assert_eq!(view.action_row_count(), 2);
    assert_eq!(view.goto_row_count(), 2);
    assert_eq!(
        view.action(
            0,
            &[LookaheadSymbol::Terminal(0), LookaheadSymbol::Terminal(1)]
        ),
        Some(Action::Shift(1))
    );
    assert_eq!(view.action(0, &[LookaheadSymbol::EndOfInput]), None);
    assert_eq!(
        view.action(1, &[LookaheadSymbol::Terminal(0)]),
        Some(Action::Reduce(0))
    );
    assert_eq!(
        view.action(
            0,
            &[LookaheadSymbol::Terminal(1), LookaheadSymbol::EndOfInput]
        ),
        Some(Action::Reduce(0))
    );
    assert_eq!(view.goto(0, 0), Some(1));
    assert_eq!(view.action_row(2), None);
    let symbols = ParserTableSymbols {
        terminals: &["id", "EOF"],
        nonterminals: &["S"],
    };
    let canonical = table.dump_dsl(&symbols).expect("canonical DSL");
    assert_eq!(small_table(&canonical), Ok(table));
}

#[test]
fn slr_name_and_resource_limits_are_checked() {
    let source = SMALL_DSL
        .replace("\"LR(2)\"", "\"SLR\"")
        .replace("[\"id\", \"EOF\"]", "[\"id\"]")
        .replace("[\"EOF\", EOF]", "[\"EOF\"]");
    let table = small_table(&source).expect("SLR table is valid");
    assert_eq!(table.view().algorithm(), ParserAlgorithm::Slr);
    assert_eq!(table.view().lookahead(), 1);
    let symbols = ParserTableSymbols {
        terminals: &["id", "EOF"],
        nonterminals: &["S"],
    };
    let limits = ParserTableLimits {
        maximum_states: 1,
        ..ParserTableLimits::default()
    };
    assert_eq!(
        OwnedParserTable::from_ast(&parse_ast(&source), &symbols, 1, source.len(), limits),
        Err(TableAdapterError::ResourceLimit("too many states"))
    );
    let byte_limits = ParserTableLimits {
        maximum_section_bytes: source.len() as u64 - 1,
        ..ParserTableLimits::default()
    };
    assert_eq!(
        OwnedParserTable::from_ast(&parse_ast(&source), &symbols, 1, source.len(), byte_limits),
        Err(TableAdapterError::ResourceLimit(
            "table section is too large"
        ))
    );
}

#[test]
fn malformed_tables_are_rejected_after_ast_parsing() {
    let cases = [
        (
            "start-state 0",
            "start-state 2",
            TableAdapterError::InvalidReference("invalid start state"),
        ),
        (
            "shift 1",
            "shift 2",
            TableAdapterError::InvalidReference("invalid shift"),
        ),
        (
            "action-row 1",
            "action-row 0",
            TableAdapterError::InvalidId("action row IDs are not dense"),
        ),
        (
            "[\"id\", \"EOF\"]",
            "[\"id\"]",
            TableAdapterError::InvalidReference("short lookahead lacks EOF"),
        ),
        (
            "[\"id\", \"EOF\"]",
            "[EOF, \"id\"]",
            TableAdapterError::InvalidReference("EOF is not last"),
        ),
        (
            "\"LR(2)\"",
            "\"LR(0)\"",
            TableAdapterError::InvalidAlgorithm,
        ),
    ];
    for (old, replacement, expected) in cases {
        let input = SMALL_DSL.replacen(old, replacement, 1);
        assert_eq!(small_table(&input), Err(expected), "mutation: {old}");
    }
    let input = SMALL_DSL.replace(
        "[\"id\", \"EOF\"] => shift 1;",
        "[\"id\", \"EOF\"] => shift 1; [\"id\", \"EOF\"] => reduce 0;",
    );
    assert_eq!(
        small_table(&input),
        Err(TableAdapterError::DuplicateEntry("duplicate lookahead"))
    );
}

#[test]
fn checked_in_ag_table_loads_through_static_compressed_table_parser() {
    let source = include_str!("fixtures/ag/v1/parser.dsl");
    let catalog: agas_runtime::artifact::Symbols =
        serde_json::from_str(include_str!("fixtures/ag/v1/symbols.json"))
            .expect("pinned symbols");
    let productions: agas_runtime::artifact::Productions = serde_json::from_str(include_str!(
        "fixtures/ag/v1/productions.json"
    ))
    .expect("pinned productions");
    let terminals: Vec<_> = catalog
        .terminals
        .iter()
        .map(|item| item.name.as_str())
        .collect();
    let nonterminals: Vec<_> = catalog
        .nonterminals
        .iter()
        .map(|item| item.name.as_str())
        .collect();
    let symbols = ParserTableSymbols {
        terminals: &terminals,
        nonterminals: &nonterminals,
    };
    let table = OwnedParserTable::from_ast(
        &parse_ast(source),
        &symbols,
        productions.productions.len(),
        source.len(),
        ParserTableLimits::default(),
    )
    .expect("pinned Ag.ag parser table is valid");
    let view = table.view();
    assert_eq!(view.algorithm(), ParserAlgorithm::Lalr);
    assert_eq!(view.lookahead(), 2);
    assert_eq!(view.state_count(), 161);
    assert_eq!(view.action_row_count(), 159);
    assert_eq!(view.goto_row_count(), 41);
    assert_eq!(
        view.action(2, &[LookaheadSymbol::EndOfInput]),
        Some(Action::Accept)
    );
    assert_eq!(view.action_state_rows()[100], 100);
    assert_eq!(view.goto_state_rows()[2], 1);
    assert_eq!(table.dump_dsl(&symbols).expect("canonical DSL"), source);
}
