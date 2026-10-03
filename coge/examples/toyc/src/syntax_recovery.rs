use crate::model::{Diagnostic, SourceComment};
use crate::{ast, lexer, lr, parser_gen};
use agas_runtime::recovery::{
    RecoveredParse as RuntimeParse, RecoveryEditKind, RecoveryPolicy, RecoveryStop,
    parse_with_recovery,
};

pub struct RecoveredParse {
    pub root: Option<ast::AstValue>,
    pub diagnostics: Vec<Diagnostic>,
    pub comments: Vec<SourceComment>,
}

fn terminal(name: &str) -> u32 {
    parser_gen::LEXER_TABLES
        .rules
        .iter()
        .find(|rule| rule.name == name)
        .and_then(|rule| rule.terminal)
        .expect("ToyC recovery terminal must exist")
}

fn label(terminal_id: u32) -> &'static str {
    for (name, label) in [
        ("SEMI", ";"),
        ("RBRACE", "}"),
        ("RPAREN", ")"),
        ("RightBrack", "]"),
        ("COMMA", ","),
    ] {
        if terminal(name) == terminal_id {
            return label;
        }
    }
    "token"
}

pub fn parse_source_recovering(source: &str) -> Result<RecoveredParse, &'static str> {
    let lexer = lexer::Lexer::new(&parser_gen::LEXER_TABLES).map_err(|_| "lexer setup")?;
    let lexed = lexer
        .tokenize(source.as_bytes())
        .map_err(|_| "lexical error")?;
    let block_comment = terminal("BLOCK_COMMENT");
    let line_comment = terminal("LINE_COMMENT");
    let comments = lexed
        .tokens
        .iter()
        .filter(|token| token.terminal == block_comment || token.terminal == line_comment)
        .map(|token| SourceComment {
            text: source[token.byte_start..token.byte_end].to_owned(),
            source: ast::InputSpan {
                begin_byte: token.byte_start as u64,
                end_byte: token.byte_end as u64,
            },
        })
        .collect();
    let parser = lr::Parser::new(&parser_gen::PARSER_TABLES, lr::RuntimeLimits::default())
        .map_err(|_| "parser setup")?;
    let frontend =
        ast::AstParser::new(parser, &parser_gen::REDUCTION_PROGRAM).map_err(|_| "AST setup")?;
    let insertable = ["SEMI", "RBRACE", "RPAREN", "RightBrack", "COMMA"].map(terminal);
    let result = parse_with_recovery(
        &frontend,
        source.as_bytes(),
        &lexed,
        &RecoveryPolicy {
            insertable_terminals: &insertable,
            max_repairs: 32,
        },
    )
    .map_err(|_| "parser error")?;
    Ok(map_result(result, comments))
}

fn map_result(result: RuntimeParse, comments: Vec<SourceComment>) -> RecoveredParse {
    let mut diagnostics: Vec<_> = result
        .edits
        .iter()
        .map(|edit| Diagnostic {
            message: match edit.kind {
                RecoveryEditKind::Inserted(terminal) => {
                    format!("syntax error: inserted '{}'", label(terminal))
                }
                RecoveryEditKind::Skipped(_) => "syntax error: skipped token".to_owned(),
            },
            source: edit.source,
        })
        .collect();
    if let Some(stop) = result.stop {
        diagnostics.push(Diagnostic {
            message: match stop.reason {
                RecoveryStop::NoViableRepair => "syntax error: unable to recover",
                RecoveryStop::LimitReached => "syntax error: recovery limit exceeded",
            }
            .to_owned(),
            source: stop.source,
        });
    }
    RecoveredParse {
        root: result.root,
        diagnostics,
        comments,
    }
}
