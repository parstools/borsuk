use std::env;
use std::fs;
use std::path::Path;
use std::process::ExitCode;

use agas_runtime::ast::InputSpan;
use agas_runtime::diagnostic::{Diagnostic, Severity, render};
use borsuk_toyc::{Interpreter, Value, analyze_source};

fn report(path: &Path, source: &str, message: &str, span: InputSpan) {
    let relative = env::current_dir()
        .ok()
        .and_then(|directory| path.strip_prefix(directory).ok());
    let path = relative.unwrap_or(path);
    let message = message.strip_prefix("syntax error: ").unwrap_or(message);
    eprintln!(
        "{}",
        render(
            path,
            source,
            &Diagnostic {
                severity: Severity::Error,
                message,
                source: span,
            }
        )
    );
}

fn main() -> ExitCode {
    let mut arguments = env::args_os();
    let program = arguments.next().unwrap_or_default();
    let Some(path) = arguments.next() else {
        eprintln!("usage: {} SOURCE.toyc", program.to_string_lossy());
        return ExitCode::from(2);
    };
    if arguments.next().is_some() {
        eprintln!("expected exactly one source path");
        return ExitCode::from(2);
    }
    let source = match fs::read_to_string(&path) {
        Ok(source) => source,
        Err(error) => {
            eprintln!("cannot read {}: {error}", path.to_string_lossy());
            return ExitCode::FAILURE;
        }
    };
    let context = match analyze_source(&source) {
        Ok(context) => context,
        Err(message) => {
            eprintln!("{message}");
            return ExitCode::FAILURE;
        }
    };
    if !context.diagnostics.is_empty() {
        for error in &context.diagnostics {
            report(Path::new(&path), &source, &error.message, error.source);
        }
        return ExitCode::FAILURE;
    }
    let mut interpreter = match Interpreter::new(&context) {
        Ok(interpreter) => interpreter,
        Err(error) => {
            report(Path::new(&path), &source, error.message, error.source);
            return ExitCode::FAILURE;
        }
    };
    match interpreter.call_named("main", Vec::new()) {
        Ok(Value::Int(value)) => println!("{value}"),
        Ok(Value::Void) => {}
        Ok(value) => println!("{value:?}"),
        Err(error) => {
            report(Path::new(&path), &source, error.message, error.source);
            return ExitCode::FAILURE;
        }
    }
    ExitCode::SUCCESS
}
