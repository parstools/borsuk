pub use agas_runtime::{ast, lexer, lr};

mod ir_exec;
#[path = "../generated/parser_gen.rs"]
mod parser_gen;
#[path = "../generated/sema_gen.rs"]
mod sema_gen;
mod sema_intr;
#[path = "../generated/sema_lib_gen.rs"]
mod sema_lib_gen;
pub use ir_exec::{ExecutionError, ExecutionState, execute_ir};
pub use parser_gen::PARSER_ALGORITHM;
pub use sema_gen::analyze_program;
pub use sema_intr::{
    Flow, FlowId, InitialState, IntegerAssignment, IrOp, ParameterId, ScopeId, SemaContext,
    StructTag, StructTagId, Symbol, SymbolId, SymbolKind,
};
pub use sema_lib_gen::{
    assign_integer, bind, bind_parameters, bind_tag, contains_local, contains_name,
    declare_function, declare_incomplete_struct, declare_variable, leave_scope, lookup_lexical,
    lookup_tag_lexical, merge_flow, read_symbol, unique_names,
};

pub fn analyze_source(
    ctx: &mut SemaContext,
    flow: FlowId,
    scope: ScopeId,
    source: &str,
) -> Result<SymbolId, String> {
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
        ast::AstParseOutcome::Accepted(root) => {
            let before_analysis = ctx.clone();
            ctx.begin_program_ir(flow);
            match analyze_program(ctx, flow, scope, &root) {
                Ok(symbol) => {
                    ctx.finish_program_ir();
                    Ok(symbol)
                }
                Err(error) => {
                    *ctx = before_analysis;
                    Err(error.to_owned())
                }
            }
        }
        ast::AstParseOutcome::SyntaxError(_) => Err("syntax error".to_owned()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn local_binding_shadows_parent() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let outer = declare_variable(&mut ctx, flow, global, "x").unwrap();
        let local = ctx.add_scope(Some(global));

        assert!(!contains_local(&ctx, local, "x"));
        assert_eq!(lookup_lexical(&ctx, local, "x"), Some(outer));

        let inner = declare_variable(&mut ctx, flow, local, "x").unwrap();
        assert!(contains_local(&ctx, local, "x"));
        assert_eq!(lookup_lexical(&ctx, local, "x"), Some(inner));
        assert_eq!(lookup_lexical(&ctx, global, "x"), Some(outer));
    }

    #[test]
    fn unknown_name_and_duplicate_declaration() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let scope = ctx.add_scope(None);
        assert_eq!(lookup_lexical(&ctx, scope, "missing"), None);
        declare_variable(&mut ctx, flow, scope, "name").unwrap();
        assert_eq!(
            declare_variable(&mut ctx, flow, scope, "name"),
            Err("identifier already declared")
        );
    }

    #[test]
    fn parsed_identifier_resolves_in_scope() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let symbol = declare_variable(&mut ctx, flow, global, "known").unwrap();
        let local = ctx.add_scope(Some(global));
        assert_eq!(
            analyze_source(&mut ctx, flow, local, "  known\n"),
            Ok(symbol)
        );
    }

    #[test]
    fn parsed_missing_identifier_reports_semantic_error() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let scope = ctx.add_scope(None);
        assert_eq!(
            analyze_source(&mut ctx, flow, scope, "missing"),
            Err("undeclared identifier".to_owned())
        );
        assert_eq!(
            analyze_source(&mut ctx, flow, scope, "two names"),
            Err("syntax error".to_owned())
        );
    }

    #[test]
    fn parsed_declaration_binds_name_before_later_read() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let symbol = analyze_source(&mut ctx, flow, global, "int item; item").unwrap();
        assert_eq!(ctx.scope_lookup(global, "item"), Some(symbol));
        assert_eq!(ctx.symbol(symbol).name, "item");
        assert_eq!(
            ctx.flow_state(flow, symbol),
            Some(InitialState::ZeroInitialized)
        );
    }

    #[test]
    fn parsed_local_declaration_rejects_read_before_initialization() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let outer = declare_variable(&mut ctx, flow, global, "item").unwrap();
        let local = ctx.add_scope(Some(global));
        assert_eq!(
            analyze_source(&mut ctx, flow, local, "int item; item"),
            Err("uninitialized variable".to_owned())
        );
        assert_eq!(ctx.scope_lookup(local, "item"), None);
        assert_eq!(lookup_lexical(&ctx, local, "item"), Some(outer));
        assert_eq!(ctx.flow_symbols(flow), vec![outer]);
        let inner = analyze_source(&mut ctx, flow, local, "int item; item = 5; item").unwrap();
        assert_ne!(inner, outer);
        assert_eq!(lookup_lexical(&ctx, local, "item"), Some(inner));
        assert_eq!(analyze_source(&mut ctx, flow, local, "item"), Ok(inner));
    }

    #[test]
    fn parsed_parameter_read_is_initialized() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let local = ctx.add_scope(Some(global));
        let parameter = ctx.add_parameter("value");
        bind_parameters(&mut ctx, flow, local, &[parameter]).unwrap();
        let symbol = ctx.scope_lookup(local, "value").unwrap();
        assert_eq!(analyze_source(&mut ctx, flow, local, "value"), Ok(symbol));
    }

    #[test]
    fn parsed_function_name_is_not_a_variable_read() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let scope = ctx.add_scope(None);
        let function = declare_function(&mut ctx, scope, "work", "int()", false).unwrap();
        assert_eq!(analyze_source(&mut ctx, flow, scope, "work"), Ok(function));
    }

    #[test]
    fn parsed_literal_assignment_initializes_and_records_target() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let local = ctx.add_scope(Some(global));
        let symbol = analyze_source(&mut ctx, flow, local, "int x; x = 5; x").unwrap();
        assert_eq!(ctx.scope_lookup(local, "x"), Some(symbol));
        assert_eq!(
            ctx.flow_state(flow, symbol),
            Some(InitialState::Initialized)
        );
        assert_eq!(
            ctx.program_ir()[0],
            [
                IrOp::Declare {
                    symbol,
                    initial_state: InitialState::Uninitialized,
                },
                IrOp::Assign(IntegerAssignment {
                    target: symbol,
                    value: 5,
                }),
                IrOp::Read { symbol },
            ]
        );
    }

    #[test]
    fn parsed_ir_preserves_statement_order_and_nested_branches() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let scope = ctx.add_scope(None);
        let x = declare_variable(&mut ctx, flow, scope, "x").unwrap();
        let flag = declare_variable(&mut ctx, flow, scope, "flag").unwrap();
        ctx.flow_mark_initialized(flow, flag);

        assert_eq!(
            analyze_source(
                &mut ctx,
                flow,
                scope,
                "{ x = 1; if (flag) { x = 2; if (flag) x = 3; else x = 4; } else { x = 5; } x = 6; } x"
            ),
            Ok(x)
        );
        let [IrOp::Block { scope: outer, body }, IrOp::Read { symbol }] =
            ctx.program_ir()[0].as_slice()
        else {
            panic!("expected a block followed by a read");
        };
        assert_eq!(*symbol, x);
        assert_ne!(*outer, scope);
        let [
            IrOp::Assign(first),
            IrOp::If {
                condition,
                then_body,
                else_body,
            },
            IrOp::Assign(last),
        ] = body.as_slice()
        else {
            panic!("expected ordered operations inside the block");
        };
        assert_eq!((first.target, first.value), (x, 1));
        assert_eq!((last.target, last.value), (x, 6));
        assert_eq!(*condition, flag);
        let [IrOp::Block { body: then_ops, .. }] = then_body.as_slice() else {
            panic!("expected a then block");
        };
        let [IrOp::Block { body: else_ops, .. }] = else_body.as_slice() else {
            panic!("expected an else block");
        };
        let assign = |value| IrOp::Assign(IntegerAssignment { target: x, value });
        assert_eq!(else_ops, &[assign(5)]);
        assert_eq!(then_ops[0], assign(2));
        assert_eq!(
            then_ops[1],
            IrOp::If {
                condition: flag,
                then_body: vec![assign(3)],
                else_body: vec![assign(4)],
            }
        );
        assert!(ctx.flow_ir(flow).is_empty());
    }

    #[test]
    fn parsed_ir_keeps_successive_programs_separate() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let scope = ctx.add_scope(None);

        let x = analyze_source(&mut ctx, flow, scope, "int x; x = 1; x").unwrap();
        let before_failure = ctx.clone();
        assert_eq!(
            analyze_source(&mut ctx, flow, scope, "x = 9223372036854775808; x"),
            Err("invalid integer literal".to_owned())
        );
        assert_eq!(ctx, before_failure);
        assert_eq!(analyze_source(&mut ctx, flow, scope, "x = 2; x"), Ok(x));
        assert_eq!(
            ctx.program_ir(),
            &[
                vec![
                    IrOp::Declare {
                        symbol: x,
                        initial_state: InitialState::ZeroInitialized,
                    },
                    IrOp::Assign(IntegerAssignment {
                        target: x,
                        value: 1,
                    }),
                    IrOp::Read { symbol: x },
                ],
                vec![
                    IrOp::Assign(IntegerAssignment {
                        target: x,
                        value: 2,
                    }),
                    IrOp::Read { symbol: x },
                ],
            ]
        );
    }

    #[test]
    fn parsed_ir_preserves_empty_nested_block_and_reads() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let scope = ctx.add_scope(None);
        let x = declare_variable(&mut ctx, flow, scope, "x").unwrap();

        assert_eq!(analyze_source(&mut ctx, flow, scope, "{ x; {} } x"), Ok(x));
        let [IrOp::Block { scope: outer, body }, IrOp::Read { symbol }] =
            ctx.program_ir()[0].as_slice()
        else {
            panic!("expected a block and final read");
        };
        assert_eq!(*symbol, x);
        let [
            IrOp::Read { symbol },
            IrOp::Block {
                scope: inner,
                body: empty,
            },
        ] = body.as_slice()
        else {
            panic!("expected a read and empty nested block");
        };
        assert_eq!(*symbol, x);
        assert!(empty.is_empty());
        assert_eq!(ctx.scope_chain(*inner), vec![*inner, *outer, scope]);
    }

    #[test]
    fn interpreter_selects_if_branch_using_runtime_condition() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let scope = ctx.add_scope(None);
        let flag = declare_variable(&mut ctx, flow, scope, "flag").unwrap();
        let x = declare_variable(&mut ctx, flow, scope, "x").unwrap();
        assert_eq!(
            analyze_source(&mut ctx, flow, scope, "if (flag) x = 1; else x = 2; x"),
            Ok(x)
        );

        let program = &ctx.program_ir()[0];
        let then_state = execute_ir(&ctx, flow, program, &[(flag, 1)]).unwrap();
        let else_state = execute_ir(&ctx, flow, program, &[(flag, 0)]).unwrap();
        assert_eq!(then_state.value(x), Some(1));
        assert_eq!(then_state.reads(), &[(x, 1)]);
        assert_eq!(else_state.value(x), Some(2));
        assert_eq!(else_state.reads(), &[(x, 2)]);
    }

    #[test]
    fn interpreter_uses_zero_initialized_globals_and_declared_locals() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let outer = declare_variable(&mut ctx, flow, global, "outer").unwrap();
        assert_eq!(analyze_source(&mut ctx, flow, global, "outer"), Ok(outer));
        let global_state = execute_ir(&ctx, flow, &ctx.program_ir()[0], &[]).unwrap();
        assert_eq!(global_state.last_read(), Some(0));

        let local = ctx.add_scope(Some(global));
        let inner = analyze_source(&mut ctx, flow, local, "int inner; inner = 5; inner").unwrap();
        let local_state = execute_ir(&ctx, flow, &ctx.program_ir()[1], &[]).unwrap();
        assert_eq!(local_state.value(inner), Some(5));
        assert_eq!(local_state.last_read(), Some(5));
    }

    #[test]
    fn interpreter_discards_shadowed_block_storage() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let scope = ctx.add_scope(None);
        let outer = declare_variable(&mut ctx, flow, scope, "x").unwrap();
        assert_eq!(
            analyze_source(&mut ctx, flow, scope, "{ int x; x = 7; x; } x"),
            Ok(outer)
        );
        let program = &ctx.program_ir()[0];
        let [IrOp::Block { body, .. }, IrOp::Read { .. }] = program.as_slice() else {
            panic!("expected a block followed by a read");
        };
        let [IrOp::Declare { symbol: inner, .. }, ..] = body.as_slice() else {
            panic!("expected a local declaration");
        };

        let state = execute_ir(&ctx, flow, program, &[(outer, 5)]).unwrap();
        assert_eq!(state.reads(), &[(*inner, 7), (outer, 5)]);
        assert_eq!(state.last_read(), Some(5));
        assert_eq!(state.value(outer), Some(5));
        assert!(!state.contains(*inner));
    }

    #[test]
    fn interpreter_keeps_branch_local_shadowing_out_of_outer_scope() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let scope = ctx.add_scope(None);
        let flag = declare_variable(&mut ctx, flow, scope, "flag").unwrap();
        let outer = declare_variable(&mut ctx, flow, scope, "x").unwrap();
        assert_eq!(
            analyze_source(
                &mut ctx,
                flow,
                scope,
                "if (flag) { int x; x = 7; x; } else { x = 3; } x"
            ),
            Ok(outer)
        );
        let program = &ctx.program_ir()[0];
        let [IrOp::If { then_body, .. }, IrOp::Read { .. }] = program.as_slice() else {
            panic!("expected an if followed by an outer read");
        };
        let [IrOp::Block { body, .. }] = then_body.as_slice() else {
            panic!("expected a then block");
        };
        let [IrOp::Declare { symbol: inner, .. }, ..] = body.as_slice() else {
            panic!("expected a shadowing declaration");
        };

        let taken = execute_ir(&ctx, flow, program, &[(flag, 1), (outer, 10)]).unwrap();
        assert_eq!(taken.reads(), &[(*inner, 7), (outer, 10)]);
        assert_eq!(taken.value(outer), Some(10));
        assert!(!taken.contains(*inner));

        let skipped = execute_ir(&ctx, flow, program, &[(flag, 0), (outer, 10)]).unwrap();
        assert_eq!(skipped.reads(), &[(outer, 3)]);
        assert_eq!(skipped.value(outer), Some(3));
        assert!(!skipped.contains(*inner));
    }

    #[test]
    fn interpreter_rejects_uninitialized_read() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let local = ctx.add_scope(Some(global));
        let x = declare_variable(&mut ctx, flow, local, "x").unwrap();

        assert!(matches!(
            execute_ir(&ctx, flow, &[IrOp::Read { symbol: x }], &[]),
            Err(ExecutionError::Uninitialized(symbol)) if symbol == x
        ));
    }

    #[test]
    fn copied_flows_keep_independent_initialization_states() {
        let mut ctx = SemaContext::new();
        let base = ctx.new_flow();
        let global = ctx.add_scope(None);
        let local = ctx.add_scope(Some(global));
        let symbol = declare_variable(&mut ctx, base, local, "x").unwrap();
        let assigned = ctx.flow_copy(base);
        let untouched = ctx.flow_copy(base);

        assert_eq!(ctx.symbol(symbol).kind, SymbolKind::Variable);
        assert_eq!(
            assign_integer(&mut ctx, assigned, local, "x", "5"),
            Ok(symbol)
        );
        assert_eq!(
            ctx.flow_state(assigned, symbol),
            Some(InitialState::Initialized)
        );
        assert_eq!(
            ctx.flow_state(base, symbol),
            Some(InitialState::Uninitialized)
        );
        assert_eq!(
            ctx.flow_state(untouched, symbol),
            Some(InitialState::Uninitialized)
        );
        assert_eq!(read_symbol(&ctx, assigned, symbol), Ok(symbol));
        assert_eq!(
            read_symbol(&ctx, untouched, symbol),
            Err("uninitialized variable")
        );

        let merged_one = merge_flow(&mut ctx, assigned, untouched);
        assert_eq!(
            ctx.flow_state(merged_one, symbol),
            Some(InitialState::Uninitialized)
        );
        assert_eq!(
            read_symbol(&ctx, merged_one, symbol),
            Err("uninitialized variable")
        );

        assign_integer(&mut ctx, untouched, local, "x", "7").unwrap();
        let merged_both = merge_flow(&mut ctx, assigned, untouched);
        assert_eq!(
            ctx.flow_state(merged_both, symbol),
            Some(InitialState::Initialized)
        );
        assert_eq!(read_symbol(&ctx, merged_both, symbol), Ok(symbol));
        assert_eq!(
            ctx.flow_state(base, symbol),
            Some(InitialState::Uninitialized)
        );

        let branch_scope = ctx.add_scope(Some(local));
        let branch_local = declare_variable(&mut ctx, assigned, branch_scope, "temporary").unwrap();
        let merged_without_local = merge_flow(&mut ctx, assigned, untouched);
        assert_eq!(ctx.flow_state(merged_without_local, branch_local), None);
    }

    #[test]
    fn merging_zero_initialized_paths_preserves_only_common_zero() {
        let mut ctx = SemaContext::new();
        let base = ctx.new_flow();
        let global = ctx.add_scope(None);
        let symbol = declare_variable(&mut ctx, base, global, "x").unwrap();
        let left = ctx.flow_copy(base);
        let right = ctx.flow_copy(base);

        let both_zero = merge_flow(&mut ctx, left, right);
        assert_eq!(
            ctx.flow_state(both_zero, symbol),
            Some(InitialState::ZeroInitialized)
        );

        assign_integer(&mut ctx, right, global, "x", "5").unwrap();
        let mixed = merge_flow(&mut ctx, left, right);
        assert_eq!(
            ctx.flow_state(mixed, symbol),
            Some(InitialState::Initialized)
        );
        assert_eq!(
            ctx.flow_state(left, symbol),
            Some(InitialState::ZeroInitialized)
        );
    }

    #[test]
    fn parsed_if_else_initializes_only_when_both_branches_assign() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let local = ctx.add_scope(Some(global));
        declare_variable(&mut ctx, flow, global, "flag").unwrap();
        let x = declare_variable(&mut ctx, flow, local, "x").unwrap();

        assert_eq!(
            analyze_source(&mut ctx, flow, local, "if (flag) x = 1; else x = 2; x"),
            Ok(x)
        );
        assert_eq!(ctx.flow_state(flow, x), Some(InitialState::Uninitialized));
        assert_eq!(
            analyze_source(&mut ctx, flow, local, "if (flag) x = 3; else ; x"),
            Err("uninitialized variable".to_owned())
        );
        ctx.flow_mark_initialized(flow, x);
        assert_eq!(
            analyze_source(&mut ctx, flow, local, "if (flag) x = 4; else ; x"),
            Ok(x)
        );
    }

    #[test]
    fn parsed_if_else_checks_condition_and_each_branch() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let local = ctx.add_scope(Some(global));
        let x = declare_variable(&mut ctx, flow, local, "x").unwrap();
        let flag = declare_variable(&mut ctx, flow, local, "flag").unwrap();

        assert_eq!(
            analyze_source(&mut ctx, flow, local, "if (flag) x = 1; else x = 2; x"),
            Err("uninitialized variable".to_owned())
        );
        ctx.flow_mark_initialized(flow, flag);
        assert_eq!(
            analyze_source(
                &mut ctx,
                flow,
                local,
                "if (flag) x = 1; else missing = 2; x"
            ),
            Err("undeclared identifier".to_owned())
        );
        assert_eq!(ctx.flow_state(flow, x), Some(InitialState::Uninitialized));
    }

    #[test]
    fn parsed_block_passes_flow_between_statements() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let local = ctx.add_scope(Some(global));
        declare_variable(&mut ctx, flow, global, "flag").unwrap();
        let x = declare_variable(&mut ctx, flow, local, "x").unwrap();

        assert_eq!(
            analyze_source(
                &mut ctx,
                flow,
                local,
                "{ if (flag) x = 1; else ; x = 2; x; } x"
            ),
            Ok(x)
        );
        assert_eq!(ctx.flow_state(flow, x), Some(InitialState::Uninitialized));
        assert_eq!(
            analyze_source(
                &mut ctx,
                flow,
                local,
                "{ x = 3; if (flag) x = 4; else ; x; } x"
            ),
            Ok(x)
        );
    }

    #[test]
    fn parsed_block_rejects_read_before_all_paths_initialize() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let local = ctx.add_scope(Some(global));
        declare_variable(&mut ctx, flow, global, "flag").unwrap();
        let x = declare_variable(&mut ctx, flow, local, "x").unwrap();

        assert_eq!(
            analyze_source(&mut ctx, flow, local, "{ if (flag) x = 1; else ; x; } x"),
            Err("uninitialized variable".to_owned())
        );
        assert_eq!(
            analyze_source(&mut ctx, flow, local, "{} x"),
            Err("uninitialized variable".to_owned())
        );
        assert_eq!(ctx.flow_state(flow, x), Some(InitialState::Uninitialized));
    }

    #[test]
    fn parsed_block_shadows_outer_binding_without_leaking_it() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let outer = declare_variable(&mut ctx, flow, global, "x").unwrap();

        assert_eq!(
            analyze_source(&mut ctx, flow, global, "{ int x; x = 7; x; } x"),
            Ok(outer)
        );
        let [IrOp::Block { scope, body }, IrOp::Read { symbol }] = ctx.program_ir()[0].as_slice()
        else {
            panic!("expected a block and an outer read");
        };
        assert_eq!(*symbol, outer);
        let [
            IrOp::Declare {
                symbol: inner,
                initial_state,
            },
            IrOp::Assign(assignment),
            IrOp::Read { symbol: read },
        ] = body.as_slice()
        else {
            panic!("expected a declaration, assignment, and read inside the block");
        };
        let inner = *inner;
        assert_eq!(*initial_state, InitialState::Uninitialized);
        assert_eq!((assignment.target, assignment.value), (inner, 7));
        assert_eq!(*read, inner);
        assert_eq!(*scope, ctx.symbol(inner).scope);
        assert_ne!(inner, outer);
        assert_ne!(ctx.symbol(inner).scope, global);
        assert_eq!(ctx.scope_lookup(global, "x"), Some(outer));
        assert_eq!(ctx.flow_state(flow, inner), None);
        assert_eq!(
            ctx.flow_state(flow, outer),
            Some(InitialState::ZeroInitialized)
        );

        assert_eq!(
            analyze_source(&mut ctx, flow, global, "{ int hidden; hidden = 2; } hidden"),
            Err("undeclared identifier".to_owned())
        );
    }

    #[test]
    fn parsed_block_preserves_outer_assignment_and_rejects_local_errors() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let local = ctx.add_scope(Some(global));
        let outer = declare_variable(&mut ctx, flow, local, "x").unwrap();

        assert_eq!(
            analyze_source(&mut ctx, flow, local, "{ x = 5; } x"),
            Ok(outer)
        );
        assert_eq!(
            ctx.flow_state(flow, outer),
            Some(InitialState::Uninitialized)
        );
        assert_eq!(
            analyze_source(&mut ctx, flow, local, "{ int x; x; } x"),
            Err("uninitialized variable".to_owned())
        );
        assert_eq!(
            analyze_source(&mut ctx, flow, local, "{ int x; int x; } x"),
            Err("identifier already declared".to_owned())
        );
    }

    #[test]
    fn leaving_scope_removes_only_its_symbols_from_flow() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let child = ctx.scope_new_child(global);
        let outer = declare_variable(&mut ctx, flow, global, "outer").unwrap();
        let inner = declare_variable(&mut ctx, flow, child, "inner").unwrap();
        let after = ctx.flow_copy(flow);
        ctx.flow_mark_initialized(after, outer);

        leave_scope(&mut ctx, after, child);

        assert_eq!(ctx.flow_state(after, inner), None);
        assert_eq!(
            ctx.flow_state(after, outer),
            Some(InitialState::Initialized)
        );
        assert_eq!(
            ctx.flow_state(flow, inner),
            Some(InitialState::Uninitialized)
        );
    }

    #[test]
    fn nested_blocks_restore_each_shadowed_binding() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let outer = declare_variable(&mut ctx, flow, global, "x").unwrap();

        assert_eq!(
            analyze_source(
                &mut ctx,
                flow,
                global,
                "{ int x; x = 1; { int x; x = 2; x; } x; } x"
            ),
            Ok(outer)
        );
        let [IrOp::Block { body, .. }, IrOp::Read { symbol }] = ctx.program_ir()[0].as_slice()
        else {
            panic!("expected a block and an outer read");
        };
        assert_eq!(*symbol, outer);
        let [
            IrOp::Declare { symbol: first, .. },
            IrOp::Assign(first_assignment),
            IrOp::Block {
                body: inner_body, ..
            },
            IrOp::Read {
                symbol: outer_block_read,
            },
        ] = body.as_slice()
        else {
            panic!("expected an ordered outer block");
        };
        let first = *first;
        assert_eq!(
            (first_assignment.target, first_assignment.value),
            (first, 1)
        );
        assert_eq!(*outer_block_read, first);
        let [
            IrOp::Declare { symbol: second, .. },
            IrOp::Assign(second_assignment),
            IrOp::Read { symbol: inner_read },
        ] = inner_body.as_slice()
        else {
            panic!("expected an ordered inner block");
        };
        let second = *second;
        assert_eq!(
            (second_assignment.target, second_assignment.value),
            (second, 2)
        );
        assert_eq!(*inner_read, second);
        assert_ne!(first, outer);
        assert_ne!(second, first);
        assert_eq!(ctx.symbol(first).name, "x");
        assert_eq!(ctx.symbol(second).name, "x");
        assert_eq!(
            ctx.scope_chain(ctx.symbol(second).scope),
            vec![ctx.symbol(second).scope, ctx.symbol(first).scope, global]
        );
        assert_eq!(
            ctx.flow_state(flow, outer),
            Some(InitialState::ZeroInitialized)
        );
    }

    #[test]
    fn nested_blocks_keep_outer_assignments_and_hide_inner_names() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let local = ctx.add_scope(Some(global));
        let outer = declare_variable(&mut ctx, flow, local, "x").unwrap();

        assert_eq!(
            analyze_source(&mut ctx, flow, local, "{ { x = 5; } x; } x"),
            Ok(outer)
        );
        assert_eq!(
            analyze_source(
                &mut ctx,
                flow,
                local,
                "{ { int hidden; hidden = 2; } hidden; } x"
            ),
            Err("undeclared identifier".to_owned())
        );
    }

    #[test]
    fn if_else_blocks_merge_outer_initialization() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let local = ctx.add_scope(Some(global));
        declare_variable(&mut ctx, flow, global, "flag").unwrap();
        let x = declare_variable(&mut ctx, flow, local, "x").unwrap();

        assert_eq!(
            analyze_source(
                &mut ctx,
                flow,
                local,
                "if (flag) { x = 1; } else { x = 2; } x"
            ),
            Ok(x)
        );
        assert_eq!(ctx.flow_state(flow, x), Some(InitialState::Uninitialized));
        assert_eq!(
            analyze_source(&mut ctx, flow, local, "if (flag) { x = 3; } else {} x"),
            Err("uninitialized variable".to_owned())
        );
        assert_eq!(
            analyze_source(
                &mut ctx,
                flow,
                local,
                "{ if (flag) { x = 4; } else { x = 5; } x; } x"
            ),
            Ok(x)
        );
    }

    #[test]
    fn if_else_blocks_keep_branch_declarations_local() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let outer = declare_variable(&mut ctx, flow, global, "x").unwrap();
        declare_variable(&mut ctx, flow, global, "flag").unwrap();

        assert_eq!(
            analyze_source(
                &mut ctx,
                flow,
                global,
                "if (flag) { int x; x = 1; } else { int x; x = 2; } x"
            ),
            Ok(outer)
        );
        let [
            IrOp::If {
                then_body,
                else_body,
                ..
            },
            IrOp::Read { symbol },
        ] = ctx.program_ir()[0].as_slice()
        else {
            panic!("expected a conditional operation and outer read");
        };
        assert_eq!(*symbol, outer);
        let [IrOp::Block { body: then_ops, .. }] = then_body.as_slice() else {
            panic!("expected a then block");
        };
        let [IrOp::Block { body: else_ops, .. }] = else_body.as_slice() else {
            panic!("expected an else block");
        };
        let [
            IrOp::Declare {
                symbol: then_symbol,
                ..
            },
            IrOp::Assign(then_assignment),
        ] = then_ops.as_slice()
        else {
            panic!("expected a declaration and assignment in the then branch");
        };
        let [
            IrOp::Declare {
                symbol: else_symbol,
                ..
            },
            IrOp::Assign(else_assignment),
        ] = else_ops.as_slice()
        else {
            panic!("expected a declaration and assignment in the else branch");
        };
        let then_symbol = *then_symbol;
        let else_symbol = *else_symbol;
        assert_eq!(then_assignment.target, then_symbol);
        assert_eq!(else_assignment.target, else_symbol);
        assert_ne!(then_symbol, else_symbol);
        assert_ne!(then_symbol, outer);
        assert_ne!(else_symbol, outer);
        assert_ne!(ctx.symbol(then_symbol).scope, ctx.symbol(else_symbol).scope);
        assert_eq!(
            ctx.flow_state(flow, outer),
            Some(InitialState::ZeroInitialized)
        );
        assert_eq!(
            analyze_source(
                &mut ctx,
                flow,
                global,
                "if (flag) { int branch_only; } else {} branch_only"
            ),
            Err("undeclared identifier".to_owned())
        );
    }

    #[test]
    fn branch_shadowing_does_not_initialize_outer_variable() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let local = ctx.add_scope(Some(global));
        declare_variable(&mut ctx, flow, global, "flag").unwrap();
        let x = declare_variable(&mut ctx, flow, local, "x").unwrap();

        assert_eq!(
            analyze_source(
                &mut ctx,
                flow,
                local,
                "if (flag) { int x; x = 1; } else { x = 2; } x"
            ),
            Err("uninitialized variable".to_owned())
        );
        assert_eq!(ctx.flow_state(flow, x), Some(InitialState::Uninitialized));
    }

    #[test]
    fn parsed_declaration_without_read_returns_new_symbol() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let local = ctx.add_scope(Some(global));
        let symbol = analyze_source(&mut ctx, flow, local, "int fresh;").unwrap();
        assert_eq!(ctx.scope_lookup(local, "fresh"), Some(symbol));
        assert_eq!(
            ctx.flow_state(flow, symbol),
            Some(InitialState::Uninitialized)
        );
        assert_eq!(
            ctx.program_ir()[0],
            [IrOp::Declare {
                symbol,
                initial_state: InitialState::Uninitialized,
            }]
        );
        assert_eq!(
            analyze_source(&mut ctx, flow, local, "int fresh;"),
            Err("identifier already declared".to_owned())
        );
    }

    #[test]
    fn parsed_assignment_rejects_unknown_or_function_target() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let local = ctx.add_scope(Some(global));
        assert_eq!(
            analyze_source(&mut ctx, flow, local, "int x; missing = 5; x"),
            Err("undeclared identifier".to_owned())
        );
        assert_eq!(ctx.scope_lookup(local, "x"), None);
        assert!(ctx.flow_symbols(flow).is_empty());
        assert!(ctx.program_ir().is_empty());
        assert!(ctx.flow_ir(flow).is_empty());

        declare_function(&mut ctx, global, "work", "int()", false).unwrap();
        assert_eq!(
            analyze_source(&mut ctx, flow, local, "int y; work = 5; y"),
            Err("not assignable".to_owned())
        );
        assert_eq!(ctx.scope_lookup(local, "y"), None);
        assert!(ctx.program_ir().is_empty());
        assert!(ctx.flow_ir(flow).is_empty());
    }

    #[test]
    fn parsed_assignment_rejects_integer_overflow_before_recording() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let local = ctx.add_scope(Some(global));
        assert_eq!(
            analyze_source(&mut ctx, flow, local, "int x; x = 9223372036854775808; x"),
            Err("invalid integer literal".to_owned())
        );
        assert_eq!(ctx.scope_lookup(local, "x"), None);
        assert!(ctx.flow_symbols(flow).is_empty());
        assert!(ctx.program_ir().is_empty());
        assert!(ctx.flow_ir(flow).is_empty());
    }

    #[test]
    fn parsed_declaration_reports_duplicate_and_missing_read() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let scope = ctx.add_scope(None);
        assert_eq!(
            analyze_source(&mut ctx, flow, scope, "int item; absent"),
            Err("undeclared identifier".to_owned())
        );
        assert_eq!(ctx.scope_lookup(scope, "item"), None);
        let first = analyze_source(&mut ctx, flow, scope, "int item; item").unwrap();
        assert_eq!(
            analyze_source(&mut ctx, flow, scope, "int item; item"),
            Err("identifier already declared".to_owned())
        );
        assert_eq!(ctx.scope_lookup(scope, "item"), Some(first));
    }

    #[test]
    fn failed_analysis_restores_symbols_flows_scopes_and_ir() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let scope = ctx.add_scope(None);
        let flag = declare_variable(&mut ctx, flow, scope, "flag").unwrap();
        let x = declare_variable(&mut ctx, flow, scope, "x").unwrap();
        let before = ctx.clone();

        assert_eq!(
            analyze_source(&mut ctx, flow, scope, "x = 7; missing"),
            Err("undeclared identifier".to_owned())
        );
        assert_eq!(ctx, before);

        assert_eq!(
            analyze_source(
                &mut ctx,
                flow,
                scope,
                "if (flag) { x = 1; } else { x = 2; } missing"
            ),
            Err("undeclared identifier".to_owned())
        );
        assert_eq!(ctx, before);

        assert_eq!(analyze_source(&mut ctx, flow, scope, "x"), Ok(x));
        assert_eq!(ctx.program_ir()[0], [IrOp::Read { symbol: x }]);
        assert_eq!(ctx.scope_lookup(scope, "flag"), Some(flag));
    }

    #[test]
    fn parameter_name_helpers_detect_duplicates() {
        let mut ctx = SemaContext::new();
        let _flow = ctx.new_flow();
        let first = ctx.add_parameter("alpha");
        let second = ctx.add_parameter("beta");
        let duplicate = ctx.add_parameter("alpha");

        assert!(!contains_name(&ctx, &[], "alpha"));
        assert!(unique_names(&ctx, &[]));
        assert!(contains_name(&ctx, &[first, second], "beta"));
        assert!(!contains_name(&ctx, &[first, second], "gamma"));
        assert!(unique_names(&ctx, &[first, second]));
        assert!(!unique_names(&ctx, &[first, second, duplicate]));
    }

    #[test]
    fn variable_declarations_bind_symbols_and_set_initial_state() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let outer = declare_variable(&mut ctx, flow, global, "x").unwrap();
        assert_eq!(
            ctx.flow_state(flow, outer),
            Some(InitialState::ZeroInitialized)
        );
        let local = ctx.add_scope(Some(global));
        let inner = declare_variable(&mut ctx, flow, local, "x").unwrap();
        assert_ne!(outer, inner);
        assert_eq!(lookup_lexical(&ctx, local, "x"), Some(inner));
        assert_eq!(
            ctx.flow_state(flow, inner),
            Some(InitialState::Uninitialized)
        );
        assert_eq!(
            declare_variable(&mut ctx, flow, local, "x"),
            Err("identifier already declared")
        );
        assert_eq!(ctx.scope_lookup(local, "x"), Some(inner));
    }

    #[test]
    fn function_prototypes_merge_with_one_definition() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let scope = ctx.add_scope(None);
        let prototype = declare_function(&mut ctx, scope, "f", "int(int)", false).unwrap();
        assert_eq!(
            declare_function(&mut ctx, scope, "f", "int(int)", false),
            Ok(prototype)
        );
        assert_eq!(
            declare_function(&mut ctx, scope, "f", "int(int)", true),
            Ok(prototype)
        );
        assert_eq!(
            ctx.symbol(prototype).kind,
            SymbolKind::Function {
                signature: "int(int)".to_owned(),
                defined: true
            }
        );
        assert_eq!(
            declare_function(&mut ctx, scope, "f", "int(int)", true),
            Err("function already defined")
        );
        assert_eq!(
            declare_function(&mut ctx, scope, "f", "int()", false),
            Err("conflicting function declaration")
        );
        declare_variable(&mut ctx, flow, scope, "x").unwrap();
        assert_eq!(
            declare_function(&mut ctx, scope, "x", "int()", false),
            Err("identifier already declared")
        );
    }

    #[test]
    fn struct_tags_have_a_separate_namespace() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let variable = declare_variable(&mut ctx, flow, global, "Node").unwrap();
        let tag = declare_incomplete_struct(&mut ctx, global, "Node").unwrap();
        assert_eq!(ctx.scope_lookup(global, "Node"), Some(variable));
        assert!(!ctx.tag(tag).complete);
        let local = ctx.add_scope(Some(global));
        assert_eq!(lookup_tag_lexical(&ctx, local, "Node"), Some(tag));
        let inner = declare_incomplete_struct(&mut ctx, local, "Node").unwrap();
        assert_eq!(lookup_tag_lexical(&ctx, local, "Node"), Some(inner));
        assert_eq!(
            declare_incomplete_struct(&mut ctx, local, "Node"),
            Err("tag already declared")
        );
    }

    #[test]
    fn parameter_binding_checks_all_names_before_mutation() {
        let mut ctx = SemaContext::new();
        let flow = ctx.new_flow();
        let global = ctx.add_scope(None);
        let local = ctx.add_scope(Some(global));
        let first = ctx.add_parameter("a");
        let second = ctx.add_parameter("b");
        assert_eq!(
            bind_parameters(&mut ctx, flow, local, &[first, second]),
            Ok(())
        );
        for name in ["a", "b"] {
            let symbol = ctx.scope_lookup(local, name).unwrap();
            assert_eq!(
                ctx.flow_state(flow, symbol),
                Some(InitialState::Initialized)
            );
        }

        let another_scope = ctx.add_scope(Some(global));
        let duplicate = ctx.add_parameter("a");
        assert_eq!(
            bind_parameters(&mut ctx, flow, another_scope, &[first, duplicate]),
            Err("duplicate parameter")
        );
        assert_eq!(ctx.scope_lookup(another_scope, "a"), None);
        declare_variable(&mut ctx, flow, another_scope, "b").unwrap();
        assert_eq!(
            bind_parameters(&mut ctx, flow, another_scope, &[first, second]),
            Err("identifier already declared")
        );
        assert_eq!(ctx.scope_lookup(another_scope, "a"), None);
    }
}
