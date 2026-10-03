pub use agas_runtime::{ast, lexer, lr};

mod classes;
mod declarations;
mod expressions;
pub mod interpreter;
pub mod model;
#[path = "../generated/parser_gen.rs"]
mod parser_gen;
#[path = "../generated/sema_gen.rs"]
mod sema_gen;
#[path = "../generated/sema_lib_gen.rs"]
mod sema_lib_gen;
mod syntax_recovery;

pub use interpreter::{Interpreter, RuntimeError, Value};
pub use model::*;
pub type ConstructorCandidate = agsem_runtime::RankedCandidate<FunctionId, Type>;
pub type SourceRange = ast::InputSpan;
pub use parser_gen::PARSER_ALGORITHM;

pub fn parse_source(source: &str) -> Result<ast::AstValue, &'static str> {
    let lexer = lexer::Lexer::new(&parser_gen::LEXER_TABLES).map_err(|_| "lexer setup")?;
    let tokens = lexer
        .tokenize(source.as_bytes())
        .map_err(|_| "lexical error")?;
    let parser = lr::Parser::new(&parser_gen::PARSER_TABLES, lr::RuntimeLimits::default())
        .map_err(|_| "parser setup")?;
    let frontend =
        ast::AstParser::new(parser, &parser_gen::REDUCTION_PROGRAM).map_err(|_| "AST setup")?;
    match frontend
        .parse(source.as_bytes(), &tokens)
        .map_err(|_| "parser error")?
    {
        ast::AstParseOutcome::Accepted(root) => Ok(root),
        ast::AstParseOutcome::SyntaxError(_) => Err("syntax error"),
    }
}

pub fn analyze_source(source: &str) -> Result<Context, &'static str> {
    let recovered = syntax_recovery::parse_source_recovering(source)?;
    let mut context = Context::default();
    context.initial_flow();
    context.diagnostics = recovered.diagnostics;
    context.comments = recovered.comments;
    let span = ast::InputSpan {
        begin_byte: 0,
        end_byte: source.len() as u64,
    };
    let global = context.new_scope(None, span);
    if let Some(root) = recovered.root {
        if let Err(message) = sema_gen::analyze_program(&mut context, global, &root) {
            context.record_error(message, root.source_span);
        }
    } else {
        context.add_module(global, Vec::new());
    }
    Ok(context)
}

#[cfg(test)]
mod parser_tests {
    use super::*;

    #[test]
    fn collection_actions_preserve_empty_single_and_ordered_multiple_arguments() {
        let source = "int zero() { return 4; } int one(int x) { return x; } \
                      int ordered(int a, int b, int c) { return a * 100 + b * 10 + c; } \
                      int main() { return ordered(zero(), one(2), one(7)); }";
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        assert_eq!(
            Interpreter::new(&context).unwrap().call_named("main", vec![]).unwrap(),
            Value::Int(427)
        );
    }

    #[test]
    fn enum_operator_actions_preserve_arithmetic_comparisons_and_assignments() {
        let source = "int main() { int n = 0; n = 10; n += 6; n -= 4; n *= 3; n /= 6; \
                      if (n < 6) return 101; if (n <= 6) {} else return 102; \
                      if (n > 6) return 103; if (n >= 6) {} else return 104; \
                      if (n != 6) return 105; if (n == 7) return 106; \
                      if (n == 6) return 8 + 6 - 3 * 4 / 2; \
                      return 107; }";
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        assert_eq!(
            Interpreter::new(&context).unwrap().call_named("main", vec![]).unwrap(),
            Value::Int(8)
        );
    }

    #[test]
    fn semantic_context_retains_source_comments() {
        let source = "class Item { /* class */ }; // after class\n int main() { return 0; }";
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty());
        assert_eq!(context.comments.len(), 2);
        assert_eq!(context.comments[0].text, "/* class */");
        assert_eq!(context.comments[1].text, "// after class");
    }

    #[test]
    fn parses_class_with_constructor_method_and_destructor() {
        let source = "class Widget { public: Widget(int n) { value = n; } \
                      ~Widget() { value = 0; } int increment() { value++; return value; } \
                      private: int value; }; int main() { Widget item(5); return item.increment(); }";
        assert_eq!(parse_source(source).unwrap().type_name, "program");
    }
}
