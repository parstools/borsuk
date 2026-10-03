pub use agas_runtime::ast;

#[path = "../generated/sema_gen.rs"]
mod sema_gen;

pub struct CalcContext;

impl CalcContext {
    fn parse_decimal(&self, text: &str) -> Result<i64, &'static str> {
        text.parse().map_err(|_| "invalid integer")
    }
}

pub use sema_gen::analyze_program;

#[cfg(test)]
mod tests {
    use super::{CalcContext, analyze_program, ast};

    fn value(kind: ast::AstValueKind, type_name: &str, variant_name: &str) -> ast::AstValue {
        let span = ast::InputSpan {
            begin_byte: 0,
            end_byte: 2,
        };
        ast::AstValue {
            kind,
            source_span: span,
            recognized_span: span,
            type_name: type_name.into(),
            variant_name: variant_name.into(),
            token_kind: 0,
            token_text: String::new(),
            field_names: Vec::new(),
            elements: Vec::new(),
        }
    }

    fn program(literal: &str) -> ast::AstValue {
        let mut number = value(ast::AstValueKind::Token, "", "");
        number.token_text = literal.into();
        let mut expression = value(ast::AstValueKind::Node, "value", "Literal");
        expression.field_names.push("text".into());
        expression.elements.push(number);
        let mut program = value(ast::AstValueKind::Node, "program", "Value");
        program.field_names.push("operand".into());
        program.elements.push(expression);
        program
    }

    #[test]
    fn typed_analyzers_pass_inherited_values_and_use_custom_context() {
        let result = analyze_program(&mut CalcContext, 5, 2, &program("42"));
        assert_eq!(result, Ok(49));
    }

    #[test]
    fn typed_analyzer_propagates_intrinsic_error() {
        let result = analyze_program(&mut CalcContext, 5, 2, &program("bad"));
        assert_eq!(result, Err("invalid integer"));
    }
}
