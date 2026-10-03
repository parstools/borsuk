//! Emit a versioned neutral AST through stdout for an independent consumer.

use agas_runtime::artifact::{PackageLimits, load_package_directory};
use agas_runtime::ast::AstParseOutcome;
use agas_runtime::ast_wire::emit_ast_wire;
use agas_runtime::lr::RuntimeLimits;
use std::path::Path;

fn main() {
    if let Err(message) = run() {
        eprintln!("{message}");
        std::process::exit(1);
    }
}

fn run() -> Result<(), String> {
    let mut arguments = std::env::args_os().skip(1);
    let first = arguments
        .next()
        .ok_or("usage: agas_ast_wire [--stats] PACKAGE SOURCE [SOURCE_NAME]")?;
    let statistics = first == "--stats";
    let directory = if statistics {
        arguments.next().ok_or("missing package directory")?
    } else {
        first
    };
    let source_path = arguments
        .next()
        .ok_or("usage: agas_ast_wire PACKAGE SOURCE [SOURCE_NAME]")?;
    let source_name = arguments.next().map_or_else(
        || {
            Path::new(&source_path)
                .file_name()
                .map_or_else(String::new, |name| name.to_string_lossy().into_owned())
        },
        |name| name.to_string_lossy().into_owned(),
    );
    if arguments.next().is_some() {
        return Err("too many arguments".to_owned());
    }
    let package = load_package_directory(Path::new(&directory), PackageLimits::default())
        .map_err(|error| format!("cannot load package: {error:?}"))?;
    let source =
        std::fs::read(&source_path).map_err(|error| format!("cannot read source: {error}"))?;
    let parsed = package
        .parse(&source, RuntimeLimits::default())
        .map_err(|error| format!("cannot parse source: {error:?}"))?;
    let AstParseOutcome::Accepted(root) = parsed.outcome else {
        return Err("source has a syntax error".to_owned());
    };
    if statistics {
        println!(
            "{}",
            serde_json::to_string(&agas_runtime::ast::measure_ast(&root))
                .map_err(|error| format!("cannot emit AST statistics: {error}"))?
        );
        return Ok(());
    }
    let context = package
        .wire_context(&source_name, &source)
        .map_err(|error| format!("cannot bind AST wire: {error:?}"))?;
    let wire = emit_ast_wire(&root, context)
        .map_err(|error| format!("cannot emit AST wire: {error:?}"))?;
    print!("{wire}");
    Ok(())
}
