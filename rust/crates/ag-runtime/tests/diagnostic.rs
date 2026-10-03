use std::path::Path;

use agas_runtime::ast::InputSpan;
use agas_runtime::diagnostic::{Diagnostic, Severity, render, source_location};

#[test]
fn semantic_error_underlines_the_source_range() {
    let source = "fn main() {\n    elo\n}\n";
    let begin = source.find("elo").unwrap() as u64;
    let result = render(
        Path::new("src/main.rs"),
        source,
        &Diagnostic {
            severity: Severity::Error,
            message: "cannot find value `elo` in this scope",
            source: InputSpan {
                begin_byte: begin,
                end_byte: begin + 3,
            },
        },
    );
    assert_eq!(
        result,
        "error: cannot find value `elo` in this scope\n \
         --> src/main.rs:2:5\n  |\n2 |     elo\n  |     ^^^"
    );
}

#[test]
fn warning_at_insertion_point_has_one_caret() {
    let source = "x = 1\r\ny = 2\r\n";
    let result = render(
        Path::new("main.toyc"),
        source,
        &Diagnostic {
            severity: Severity::Warning,
            message: "semicolon inserted",
            source: InputSpan {
                begin_byte: 5,
                end_byte: 5,
            },
        },
    );
    assert_eq!(
        result,
        "warning: semicolon inserted\n --> main.toyc:1:6\n  |\n1 | x = 1\n  |      ^"
    );
}

#[test]
fn skipped_token_underlines_its_full_spelling() {
    let source = "int x; extra int y;";
    let begin = source.find("extra").unwrap() as u64;
    let result = render(
        Path::new("main.toyc"),
        source,
        &Diagnostic {
            severity: Severity::Error,
            message: "unexpected token",
            source: InputSpan {
                begin_byte: begin,
                end_byte: begin + 5,
            },
        },
    );
    assert!(result.ends_with("1 | int x; extra int y;\n  |        ^^^^^"));
}

#[test]
fn unicode_columns_count_characters_not_bytes() {
    let source = "ą = z;\n";
    assert_eq!(source_location(source, 5).column, 5);
    assert_eq!(source_location(source, u64::MAX).line, 2);
}
