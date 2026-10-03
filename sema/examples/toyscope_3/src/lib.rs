pub use borsuk_toyscope_1::{
    BigInt, Diagnostic, ExprId, Expression, OpId, Operation, OperatorKind, PlaceId, ProgramId,
    RuntimeCheck, Scope, ScopeId, SourceRange, Symbol, SymbolId, ToyScopeContext, ast,
    execute_program, lexer, lr,
};

#[path = "../generated/parser_gen.rs"]
mod parser_gen;
#[path = "../generated/sema_gen.rs"]
mod sema_gen;
#[path = "../generated/sema_lib_gen.rs"]
mod sema_lib_gen;

pub use parser_gen::PARSER_ALGORITHM;
pub use sema_gen::analyze_program;
pub use sema_lib_gen::lookup_lexical;

pub fn analyze_source_partial(
    ctx: &mut ToyScopeContext,
    source: &str,
) -> Result<ProgramId, String> {
    ctx.begin_analysis();
    let lexer = lexer::Lexer::new(&parser_gen::LEXER_TABLES)
        .map_err(|error| format!("lexer setup failed: {error:?}"))?;
    let tokens = lexer
        .tokenize(source.as_bytes())
        .map_err(|error| format!("lexical error: {error:?}"))?;
    let parser = lr::Parser::new(
        &parser_gen::PARSER_TABLES,
        lr::RuntimeLimits::default(),
    )
    .map_err(|error| format!("parser setup failed: {error:?}"))?;
    let frontend = ast::AstParser::new(parser, &parser_gen::REDUCTION_PROGRAM)
        .map_err(|error| format!("AST setup failed: {error:?}"))?;
    match frontend
        .parse(source.as_bytes(), &tokens)
        .map_err(|error| format!("parser error: {error:?}"))?
    {
        ast::AstParseOutcome::Accepted(root) => analyze_program(ctx, &root).map_err(str::to_owned),
        ast::AstParseOutcome::SyntaxError(_) => Err("syntax error".to_owned()),
    }
}

pub fn analyze_source(ctx: &mut ToyScopeContext, source: &str) -> Result<ProgramId, String> {
    let program = analyze_source_partial(ctx, source)?;
    match ctx.diagnostics.first() {
        Some(diagnostic) => Err(diagnostic.message.clone()),
        None => Ok(program),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn predeclared_local_shadows_outer_from_block_start() {
        let mut ctx = ToyScopeContext::default();
        let program = analyze_source(&mut ctx, "int x = 5; { int a = x; int x = x + 1; }").unwrap();
        let outer = ctx.scopes[0].bindings["x"];
        let local = ctx.scopes[1].bindings["x"];
        let Operation::Declare {
            initializer: Some(value),
            ..
        } = ctx.operations[ctx.scopes[1].operations[1].0]
        else {
            panic!("declaration expected");
        };
        let Expression::Binary { left, .. } = ctx.expressions[value.0] else {
            panic!("binary initializer expected");
        };
        assert_eq!(ctx.expressions[left.0], Expression::Load(local));
        assert_ne!(outer, local);
        assert_eq!(
            execute_program(&ctx, program),
            Err("variable read before initialization")
        );
    }

    #[test]
    fn predeclared_name_is_bound_even_if_initializer_fails() {
        let mut ctx = ToyScopeContext::default();
        let program = analyze_source_partial(&mut ctx, "int x = q; int a = 2;").unwrap();
        assert_eq!(ctx.diagnostics.len(), 1);
        assert_eq!(ctx.diagnostics[0].message, "identifier not declared");
        assert!(ctx.scopes[0].bindings.contains_key("x"));
        assert!(ctx.scopes[0].bindings.contains_key("a"));
        assert!(!ctx.programs[program.0].valid);
    }

    #[test]
    fn same_scope_redeclaration_is_reported_and_recovery_continues() {
        let mut ctx = ToyScopeContext::default();
        analyze_source_partial(&mut ctx, "int x = 1; int x = q;").unwrap();
        assert_eq!(ctx.diagnostics.len(), 1);
        assert_eq!(ctx.diagnostics[0].message, "identifier already declared");
    }

    #[test]
    fn assignment_before_declaration_is_runtime_error() {
        let mut ctx = ToyScopeContext::default();
        let program = analyze_source(&mut ctx, "x = 5; int x;").unwrap();
        assert_eq!(
            execute_program(&ctx, program),
            Err("variable assigned before declaration")
        );

        let mut ctx = ToyScopeContext::default();
        let program = analyze_source(&mut ctx, "int x; x = 5;").unwrap();
        let result = execute_program(&ctx, program).unwrap();
        assert_eq!(
            result.root_values[&ctx.scopes[0].bindings["x"]],
            Some(5.into())
        );
    }
}
