pub use agas_runtime::{ast, lexer, lr};

#[path = "../generated/parser_gen.rs"]
mod parser_gen;
#[path = "../generated/sema_gen.rs"]
mod sema_gen;
#[path = "../generated/sema_lib_gen.rs"]
mod sema_lib_gen;

pub use parser_gen::PARSER_ALGORITHM;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TypeKind {
    Void,
    Int,
    Char,
    CharPointer,
}

pub struct MixedContext;

impl MixedContext {
    pub fn void_type(&self) -> TypeKind {
        TypeKind::Void
    }

    pub fn int_type(&self) -> TypeKind {
        TypeKind::Int
    }

    pub fn char_type(&self, pointer: bool) -> TypeKind {
        if pointer {
            TypeKind::CharPointer
        } else {
            TypeKind::Char
        }
    }
}

pub fn analyze_source(source: &str) -> Result<TypeKind, &'static str> {
    let lexer = lexer::Lexer::new(&parser_gen::LEXER_TABLES).map_err(|_| "lexer setup")?;
    let tokens = lexer.tokenize(source.as_bytes()).map_err(|_| "lexical error")?;
    let parser = lr::Parser::new(
        &parser_gen::PARSER_TABLES,
        lr::RuntimeLimits::default(),
    )
    .map_err(|_| "parser setup")?;
    let frontend = ast::AstParser::new(parser, &parser_gen::REDUCTION_PROGRAM)
        .map_err(|_| "AST setup")?;
    match frontend
        .parse(source.as_bytes(), &tokens)
        .map_err(|_| "parser error")?
    {
        ast::AstParseOutcome::Accepted(root) => sema_gen::analyze_program(&mut MixedContext, &root),
        ast::AstParseOutcome::SyntaxError(_) => Err("syntax error"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mixed_inline_shape_and_optional_token() {
        assert_eq!(analyze_source("void"), Ok(TypeKind::Void));
        assert_eq!(analyze_source("int"), Ok(TypeKind::Int));
        assert_eq!(analyze_source("char"), Ok(TypeKind::Char));
        assert_eq!(analyze_source("char *"), Ok(TypeKind::CharPointer));
    }
}
