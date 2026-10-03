pub use agas_runtime::{ast, lexer, lr};

#[path = "../generated/backend_c_gen.rs"]
pub mod backend_c_gen;
#[path = "../generated/backend_llvm_gen.rs"]
pub mod backend_llvm_gen;
mod declarations;
mod expressions;
pub mod interpreter;
#[path = "../generated/lowering_gen.rs"]
pub mod lowering_gen;
pub mod model;
#[path = "../generated/parser_gen.rs"]
mod parser_gen;
#[path = "../generated/sema_gen.rs"]
mod sema_gen;
#[path = "../generated/sema_lib_gen.rs"]
mod sema_lib_gen;
mod syntax_recovery;

pub use interpreter::{Interpreter, RuntimeError, Value};
pub use model::{
    AssignmentOp, Context, DeclarationTarget, ExprId, FlowId, FunctionHeader, FunctionId, ModuleId,
    OpId, Operator, ParameterId, PlaceId, ScopeId, StructId, SymbolId, Type,
};
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
mod tests {
    use super::*;
    use agsem_runtime::core_ir::{BinaryOp, Constant, InstructionKind, TerminatorKind};
    use agsem_runtime::{core_ir, structured_ir};

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
    fn generated_lowering_emits_verified_integer_addition() {
        let context = analyze_source("int main() { return 2 + 3; }").unwrap();
        assert!(context.diagnostics.is_empty());
        let structured =
            lowering_gen::lower_function_to_structured(&context, FunctionId(0)).unwrap();
        let module = structured_ir::lower_to_cfg(&structured).unwrap();
        let block = &module.functions[0].blocks[0];
        assert!(matches!(
            block.instructions[0].kind,
            InstructionKind::Constant(Constant::I32(2))
        ));
        assert!(matches!(
            block.instructions[1].kind,
            InstructionKind::Constant(Constant::I32(3))
        ));
        assert!(matches!(
            block.instructions[2].kind,
            InstructionKind::Binary {
                operation: BinaryOp::Add,
                ..
            }
        ));
        assert!(matches!(
            block.terminator.as_ref().unwrap().kind,
            TerminatorKind::Return(Some(core_ir::ValueId(2)))
        ));
        let mut interpreter = Interpreter::new(&context).unwrap();
        assert_eq!(
            interpreter.call_named("main", vec![]).unwrap(),
            Value::Int(5)
        );
    }

    #[test]
    fn generated_lowering_rejects_call_without_definition() {
        let context = analyze_source("int helper(); int main() { return helper(); }").unwrap();
        assert!(context.diagnostics.is_empty());
        assert_eq!(
            lowering_gen::lower_function_to_structured(&context, FunctionId(0)).unwrap_err(),
            "function has no body"
        );
    }

    #[test]
    fn generated_lowering_retains_comments_in_function() {
        let source = "int main() { /* before */ return 2 + 3; // after\n }";
        let context = analyze_source(source).unwrap();
        let structured =
            lowering_gen::lower_function_to_structured(&context, FunctionId(0)).unwrap();
        let comments: Vec<_> = structured
            .comments
            .iter()
            .map(|comment| comment.text.as_str())
            .collect();
        assert_eq!(comments, ["/* before */", "// after"]);
        structured_ir::lower_to_cfg(&structured).unwrap();
    }

    #[test]
    fn semantic_context_retains_source_comments() {
        let source = "int main() { /* before return */ return 2; // after return\n }";
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty());
        assert_eq!(context.comments.len(), 2);
        assert_eq!(context.comments[0].text, "/* before return */");
        assert_eq!(context.comments[1].text, "// after return");
        for comment in &context.comments {
            let start = comment.source.begin_byte as usize;
            let end = comment.source.end_byte as usize;
            assert_eq!(&source[start..end], comment.text);
        }
    }

    #[test]
    fn indexed_array_access_executes_and_checks_bounds() {
        let source =
            "int main() { int data[3]; data[0] = 2; data[1] = data[0] + 3; return data[1]; }";
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        assert_eq!(
            Interpreter::new(&context)
                .unwrap()
                .call_named("main", vec![])
                .unwrap(),
            Value::Int(5)
        );

        let source = "int main() { int data[2]; return data[2]; }";
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        assert_eq!(
            Interpreter::new(&context)
                .unwrap()
                .call_named("main", vec![])
                .unwrap_err()
                .message,
            "array index out of bounds"
        );

        for (source, message) in [
            (
                "int main() { int a[2]; return a[1.0]; }",
                "array index must be int",
            ),
            (
                "int main() { int x = 2; return x[0]; }",
                "indexing non-array",
            ),
        ] {
            let context = analyze_source(source).unwrap();
            assert!(
                context
                    .diagnostics
                    .iter()
                    .any(|diagnostic| diagnostic.message == message),
                "{:?}",
                context.diagnostics
            );
        }
    }

    #[test]
    fn indexed_array_compound_assignments_and_increments() {
        let source = "int main() { int a[1]; a[0] = 2; a[0] += 3; a[0] *= 2; a[0]--; a[0]++; a[0] /= 2; return a[0]; }";
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        assert_eq!(
            Interpreter::new(&context)
                .unwrap()
                .call_named("main", vec![])
                .unwrap(),
            Value::Int(5)
        );

        let source = "int main() { int a[1]; a[0] += 1; return 0; }";
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        assert_eq!(
            Interpreter::new(&context)
                .unwrap()
                .call_named("main", vec![])
                .unwrap_err()
                .message,
            "variable used before initialization"
        );
    }

    #[test]
    fn parser_covers_toyc_declarations_and_control_flow() {
        let root = parse_source(
            "struct Pair { int left; int right; }; int main() { int x = 1; if (x == 1) { x += 2; } return x; }",
        )
        .unwrap();
        assert_eq!(root.type_name, "program");
        assert_eq!(root.elements[0].elements.len(), 2);
    }

    #[test]
    fn syntax_recovery_reports_multiple_repairs_and_continues_semantics() {
        let source = "int x int y; int main() { int a = 1 int b = 2; \
                      int c = unknown; return a + b; }";
        assert_eq!(parse_source(source), Err("syntax error"));
        let context = analyze_source(source).unwrap();
        let messages: Vec<_> = context
            .diagnostics
            .iter()
            .map(|diagnostic| diagnostic.message.as_str())
            .collect();
        assert_eq!(
            messages,
            [
                "syntax error: inserted ';'",
                "syntax error: inserted ';'",
                "variable not declared",
            ]
        );
        assert_eq!(context.variables.len(), 5);
        assert!(!context.modules[0].valid);
        assert_eq!(
            Interpreter::new(&context).err().unwrap().message,
            "semantic errors prevent execution"
        );
    }

    #[test]
    fn syntax_recovery_skips_extra_token_and_closes_block() {
        let extra = analyze_source("int x;; int main() { return 1; }").unwrap();
        assert_eq!(extra.diagnostics[0].message, "syntax error: skipped token");
        assert!(extra.functions[0].body.is_some());

        let missing = analyze_source("int main() { return 3;").unwrap();
        assert_eq!(missing.diagnostics[0].message, "syntax error: inserted '}'");
        assert!(missing.functions[0].body.is_some());

        let incomplete = analyze_source("int").unwrap();
        assert!(!incomplete.modules[0].valid);
        assert!(
            incomplete
                .diagnostics
                .iter()
                .any(|error| error.message.starts_with("syntax error:"))
        );
    }

    #[test]
    fn typed_declarations_build_scopes_and_symbol_tables() {
        let context = analyze_source(
            "struct Pair { int left; float right; }; Pair pair; int data[3]; \
             void update(int value); void update(int input) { int local; { float local; } }",
        )
        .unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        assert_eq!(context.structs.len(), 1);
        assert_eq!(context.structs[0].fields.len(), 2);
        assert!(context.structs[0].complete);
        assert_eq!(context.functions.len(), 1);
        assert!(context.functions[0].defined);
        assert_eq!(context.functions[0].parameters, vec![Type::Int]);
        let global = context.modules[0].scope;
        assert!(matches!(
            context.lookup_ordinary(global, "pair"),
            Some(model::Binding::Variable(_))
        ));
        assert!(matches!(
            context.lookup_ordinary(global, "update"),
            Some(model::Binding::Function(_))
        ));
        assert_eq!(context.modules[0].declarations.len(), 2);
        assert_eq!(context.scopes.len(), 3);
        let function_scope = context.functions[0].scope.unwrap();
        let nested_scope = ScopeId(2);
        let outer_local = context.lookup_ordinary(function_scope, "local");
        let inner_local = context.lookup_ordinary(nested_scope, "local");
        assert_ne!(outer_local, inner_local);
        assert_eq!(
            context.lookup_ordinary(nested_scope, "input"),
            context.lookup_ordinary(function_scope, "input")
        );
    }

    #[test]
    fn semantic_errors_accumulate_across_declarations_and_blocks() {
        let context = analyze_source(
            "int x; int x; struct Pair { int first; int first; }; \
             void f(int value, int value); void g() { int local; int local; }",
        )
        .unwrap();
        let messages: Vec<_> = context
            .diagnostics
            .iter()
            .map(|diagnostic| diagnostic.message.as_str())
            .collect();
        assert_eq!(
            messages,
            [
                "identifier already declared",
                "identifier already declared",
                "duplicate parameter name",
                "identifier already declared"
            ]
        );
        assert!(!context.modules[0].valid);
    }

    #[test]
    fn expressions_and_local_initializers_build_typed_ir() {
        let context = analyze_source(
            "int outer; int inc(int n); struct Pair { int left; }; Pair pair; \
             void f(int p) { int a = outer + p * 2; float b = a + 1.5; \
             bool c = a < 10; char* ptr = null; char ch = 'x'; \
             char* msg = \"hi\"; int d = -a; int e = inc(a); int z = pair.left; }",
        )
        .unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        let function = context
            .functions
            .iter()
            .find(|function| function.name == "f")
            .unwrap();
        let body = function.body.unwrap();
        let model::Operation::Block { scope, body } = &context.operations[body.0].kind else {
            panic!("function body should be a block");
        };
        assert_eq!(*scope, function.scope.unwrap());
        assert_eq!(body.len(), 18);
        assert_eq!(context.variables.len(), 12);
        assert!(
            context.expressions.iter().any(|expression| {
                matches!(expression.kind, model::ExpressionKind::Call { .. })
            })
        );
        assert!(context.expressions.iter().any(|expression| {
            matches!(
                expression.kind,
                model::ExpressionKind::Convert {
                    target: Type::Float,
                    ..
                }
            )
        }));
    }

    #[test]
    fn self_initializer_shadows_global_and_poisoned_reads_do_not_cascade() {
        let context = analyze_source("int x; void f() { int x = x + 1; int y = x; }").unwrap();
        let messages: Vec<_> = context
            .diagnostics
            .iter()
            .map(|diagnostic| diagnostic.message.as_str())
            .collect();
        assert_eq!(messages, ["variable used before initialization"]);
        let global = context.modules[0].scope;
        let local = context.functions[0].scope.unwrap();
        assert_ne!(
            context.lookup_ordinary(global, "x"),
            context.lookup_ordinary(local, "x")
        );
    }

    #[test]
    fn expression_errors_accumulate_and_keep_invalid_initializers_uninitialized() {
        let context = analyze_source(
            "int data[2]; void f() { int a = 2147483648; bool b = 1 + true; \
             int c = data; int d = 4 / 0; int e = d; }",
        )
        .unwrap();
        let messages: Vec<_> = context
            .diagnostics
            .iter()
            .map(|diagnostic| diagnostic.message.as_str())
            .collect();
        assert_eq!(
            messages,
            [
                "integer literal out of range",
                "invalid arithmetic operands",
                "array value is not supported"
            ]
        );
        assert!(context.expressions.iter().any(|expression| {
            expression
                .checks
                .contains(&model::RuntimeCheck::Nonzero(match expression.kind {
                    model::ExpressionKind::Binary { right, .. } => right,
                    _ => return false,
                }))
        }));
    }

    #[test]
    fn independent_expression_operands_report_independent_errors() {
        let context = analyze_source(
            "int pair(int a, int b); void f() { int x = bad + missing * other; \
             int y = pair(first, second); int z = 1; }",
        )
        .unwrap();
        let messages: Vec<_> = context
            .diagnostics
            .iter()
            .map(|diagnostic| diagnostic.message.as_str())
            .collect();
        assert_eq!(
            messages,
            [
                "variable not declared",
                "variable not declared",
                "variable not declared",
                "variable not declared",
                "variable not declared",
            ]
        );
        assert!(!context.modules[0].valid);
    }

    #[test]
    fn precedence_parentheses_calls_and_escapes_have_typed_expressions() {
        let context = analyze_source(
            "int sum(int a, int b); void f() { int x = (1 + 2) * 3 - 4 / 2; \
             bool cmp = x >= 3; int y = sum(x, 2); bool same = null == null; \
             char line = '\\n'; char* text = \"a\\tb\"; bool pointer_eq = text == null; }",
        )
        .unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        assert!(context.expressions.iter().any(|expression| {
            matches!(
                expression.kind,
                model::ExpressionKind::Binary {
                    operator: Operator::Multiply,
                    ..
                }
            )
        }));
        assert!(context.expressions.iter().any(|expression| {
            matches!(
                expression.kind,
                model::ExpressionKind::Constant(model::Constant::Char(b'\n'))
            )
        }));
        assert!(context.expressions.iter().any(|expression| {
            matches!(
                &expression.kind,
                model::ExpressionKind::Constant(model::Constant::String(text)) if text == "a\tb"
            )
        }));
        assert!(context.expressions.iter().any(|expression| {
            matches!(
                expression.kind,
                model::ExpressionKind::Convert {
                    target: Type::CharPointer,
                    ..
                }
            )
        }));
    }

    #[test]
    fn invalid_calls_and_comparisons_report_separate_errors() {
        let context = analyze_source(
            "void no(); int one(int n); void f() { int a = no(); int b = one(); \
             int c = one(true); int d = missing(1); bool e = 1 < null; }",
        )
        .unwrap();
        let messages: Vec<_> = context
            .diagnostics
            .iter()
            .map(|diagnostic| diagnostic.message.as_str())
            .collect();
        assert_eq!(
            messages,
            [
                "void value used as expression",
                "wrong number of arguments",
                "incompatible argument type",
                "function not declared",
                "incompatible comparison operands"
            ]
        );
    }

    #[test]
    fn member_lookup_and_assignment_errors_are_explicit() {
        let context = analyze_source(
            "struct Pair { int left; }; Pair global; void f() { \
             int x = global.left; int y = global.missing; x = 2; }",
        )
        .unwrap();
        let messages: Vec<_> = context
            .diagnostics
            .iter()
            .map(|diagnostic| diagnostic.message.as_str())
            .collect();
        assert_eq!(messages, ["unknown struct field"]);
    }

    #[test]
    fn assignments_and_calls_build_ordered_statement_ir() {
        let context = analyze_source(
            "void ping(); int f(int input) { int x; x = input; x += 2; x *= 3; \
             x -= 1; x /= 2; x++; x--; ping(); return x; }",
        )
        .unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        let body = context.functions[1].body.unwrap();
        let model::Operation::Block { body, .. } = &context.operations[body.0].kind else {
            panic!("function body should be a block");
        };
        assert_eq!(body.len(), 10);
        assert!(matches!(
            context.operations[body[1].0].kind,
            model::Operation::Store { .. }
        ));
        assert!(matches!(
            context.operations[body[8].0].kind,
            model::Operation::Call(_)
        ));
        assert!(matches!(
            context.operations[body[9].0].kind,
            model::Operation::Return(Some(_))
        ));
        assert!(context.expressions.iter().any(|expression| {
            expression
                .checks
                .iter()
                .any(|check| matches!(check, model::RuntimeCheck::Nonzero(_)))
        }));
    }

    #[test]
    fn branches_merge_initialization_and_returns() {
        let context = analyze_source(
            "int yes(bool flag) { int a; if (flag) a = 1; else a = 2; return a; } \
             int maybe(bool flag) { int b; if (flag) b = 1; return b; } \
             int complete(bool flag) { if (flag) return 1; else return 2; }",
        )
        .unwrap();
        let messages: Vec<_> = context
            .diagnostics
            .iter()
            .map(|error| error.message.as_str())
            .collect();
        assert_eq!(messages, ["variable used before initialization"]);
        for function in &context.functions {
            assert!(
                function.body.is_some(),
                "missing IR body for {}",
                function.name
            );
        }
        assert!(context.operations.iter().any(|node| {
            matches!(
                node.kind,
                model::Operation::If {
                    else_branch: Some(_),
                    ..
                }
            )
        }));
    }

    #[test]
    fn loops_preserve_zero_iteration_flow_and_for_scope() {
        let context = analyze_source(
            "void f() { int x; while (false) x = 1; int a = x; \
             for (int i = 0; i < 3; i++) { x = i; } int b = x; int c = i; return; }",
        )
        .unwrap();
        let messages: Vec<_> = context
            .diagnostics
            .iter()
            .map(|error| error.message.as_str())
            .collect();
        assert_eq!(
            messages,
            [
                "variable used before initialization",
                "variable used before initialization",
                "variable not declared",
            ]
        );
        let loop_node = context
            .operations
            .iter()
            .find(|node| matches!(node.kind, model::Operation::For { .. }))
            .unwrap();
        let model::Operation::For {
            scope,
            initialization,
            body,
            update,
            ..
        } = &loop_node.kind
        else {
            unreachable!()
        };
        assert!(matches!(
            context.operations[initialization.0].kind,
            model::Operation::Block { .. }
        ));
        assert!(matches!(
            context.operations[body.0].kind,
            model::Operation::Block { .. }
        ));
        assert!(matches!(
            context.operations[update.0].kind,
            model::Operation::Store { .. }
        ));
        assert!(context.scopes[scope.0].ordinary.contains_key("i"));
    }

    #[test]
    fn assignment_and_return_type_errors_accumulate() {
        let context = analyze_source(
            "int f() { int x; x += 1; bool b = true; x = b; return b; } \
             int missing(bool flag) { if (flag) return 1; }",
        )
        .unwrap();
        let messages: Vec<_> = context
            .diagnostics
            .iter()
            .map(|error| error.message.as_str())
            .collect();
        assert_eq!(
            messages,
            [
                "variable used before initialization",
                "incompatible assignment types",
                "incompatible return type",
                "missing return statement",
            ]
        );
    }

    #[test]
    fn return_presence_and_conversion_errors_accumulate() {
        let context = analyze_source(
            "void has_value() { return 1; } \
             int lacks_value() { return; } \
             int wrong_type() { bool value = true; return value; } \
             int main() { return 0; }",
        )
        .unwrap();
        let messages: Vec<_> = context
            .diagnostics
            .iter()
            .map(|error| error.message.as_str())
            .collect();
        assert_eq!(
            messages
                .iter()
                .filter(|message| **message == "invalid return value")
                .count(),
            2
        );
        assert!(messages.contains(&"incompatible return type"));
    }

    #[test]
    fn errors_in_both_branches_and_later_statements_are_reported() {
        let context = analyze_source(
            "int f(bool flag) { if (flag) missing = 1; else absent = 2; \
             while (flag) unknown = 3; int x = missing; return 1; }",
        )
        .unwrap();
        let messages: Vec<_> = context
            .diagnostics
            .iter()
            .map(|error| error.message.as_str())
            .collect();
        assert_eq!(
            messages,
            [
                "variable not declared",
                "variable not declared",
                "variable not declared",
                "variable not declared",
            ]
        );
        assert!(
            context
                .operations
                .iter()
                .any(|node| matches!(node.kind, model::Operation::If { .. }))
        );
        assert!(
            context
                .operations
                .iter()
                .any(|node| matches!(node.kind, model::Operation::While { .. }))
        );
    }

    #[test]
    fn for_body_and_update_errors_are_both_reported() {
        let context = analyze_source(
            "void f() { for (int i = 0; i < 2; missing++) unknown = i; \
             int after = absent; }",
        )
        .unwrap();
        let messages: Vec<_> = context
            .diagnostics
            .iter()
            .map(|error| error.message.as_str())
            .collect();
        assert_eq!(
            messages,
            [
                "variable not declared",
                "variable not declared",
                "variable not declared"
            ]
        );
    }

    #[test]
    fn compound_assignment_converts_numeric_result_to_target_type() {
        let context = analyze_source(
            "void f() { char c = 'a'; c++; c += 2; float f = 1.0; f *= 2; \
             char* pointer = null; pointer++; }",
        )
        .unwrap();
        let messages: Vec<_> = context
            .diagnostics
            .iter()
            .map(|error| error.message.as_str())
            .collect();
        assert_eq!(messages, ["invalid increment operand"]);
        assert!(context.expressions.iter().any(|expression| {
            matches!(
                expression.kind,
                model::ExpressionKind::Convert {
                    target: Type::Char,
                    ..
                }
            )
        }));
    }

    #[test]
    fn condition_policy_keeps_bool_and_converts_number_and_pointer() {
        let context = analyze_source(
            "void f() { bool ready = true; if (ready) {} if (2) {} \
             char* pointer = null; if (pointer) {} }",
        )
        .unwrap();
        assert!(context.diagnostics.is_empty());
        let conversions = context
            .expressions
            .iter()
            .filter(|expression| {
                matches!(
                    expression.kind,
                    model::ExpressionKind::Convert {
                        target: Type::Bool,
                        ..
                    }
                )
            })
            .count();
        assert_eq!(conversions, 2);
    }

    #[test]
    fn condition_policy_rejects_struct_value() {
        let context =
            analyze_source("struct Box { int value; }; void f(Box box) { if (box) {} }").unwrap();
        let messages: Vec<_> = context
            .diagnostics
            .iter()
            .map(|error| error.message.as_str())
            .collect();
        assert_eq!(messages, ["condition is not convertible to bool"]);
    }

    #[test]
    fn invalid_types_and_conflicting_functions_are_diagnosed() {
        let context = analyze_source(
            "Unknown missing; int invalid[0]; struct Box { Box self; }; \
             int f(int a); float f(int b); int f(int value, int value);",
        )
        .unwrap();
        let messages: Vec<_> = context
            .diagnostics
            .iter()
            .map(|diagnostic| diagnostic.message.as_str())
            .collect();
        assert_eq!(
            messages,
            [
                "unknown type",
                "invalid array length",
                "invalid field type",
                "conflicting function declaration",
                "duplicate parameter name"
            ]
        );
    }
}
