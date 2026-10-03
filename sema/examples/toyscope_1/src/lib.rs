pub use agas_runtime::{ast, lexer, lr};
pub use num_bigint::BigInt;
pub type SourceRange = ast::InputSpan;

mod execute;
#[path = "../generated/parser_gen.rs"]
mod parser_gen;
#[path = "../generated/sema_gen.rs"]
mod sema_gen;
#[path = "../generated/sema_lib_gen.rs"]
mod sema_lib_gen;

use std::collections::HashMap;

pub use execute::{ExecutionResult, execute_program};
pub use parser_gen::PARSER_ALGORITHM;
pub use sema_gen::analyze_program;
pub use sema_lib_gen::{contains_local, declare_variable, lookup_lexical, read_symbol};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ScopeId(pub usize);

#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq)]
pub struct SymbolId(pub usize);

#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq)]
pub struct PlaceId(pub usize);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ExprId(pub usize);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct OpId(pub usize);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ProgramId(pub usize);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OperatorKind {
    Plus,
    Minus,
    Multiply,
    Divide,
    Remainder,
}

#[derive(Clone, Debug)]
pub struct Scope {
    pub parent: Option<ScopeId>,
    pub source: SourceRange,
    pub bindings: HashMap<String, SymbolId>,
    pub symbols: Vec<SymbolId>,
    pub operations: Vec<OpId>,
}

#[derive(Clone, Debug)]
pub struct Symbol {
    pub name: String,
    pub declaration: SourceRange,
    pub owner: ScopeId,
    pub place: PlaceId,
    pub initialized: bool,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum RuntimeCheck {
    Nonzero(ExprId),
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Expression {
    Constant(BigInt),
    Load(SymbolId),
    Unary {
        operator: OperatorKind,
        operand: ExprId,
    },
    Binary {
        left: ExprId,
        operator: OperatorKind,
        right: ExprId,
        checks: Vec<RuntimeCheck>,
    },
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Operation {
    Declare {
        symbol: SymbolId,
        initializer: Option<ExprId>,
    },
    Assign {
        symbol: SymbolId,
        value: ExprId,
    },
    Block {
        scope: ScopeId,
    },
    Error,
}

#[derive(Clone, Debug)]
pub struct Program {
    pub scope: ScopeId,
    pub valid: bool,
    pub runtime_policy3: bool,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Diagnostic {
    pub message: String,
    pub source: SourceRange,
}

#[derive(Default)]
pub struct ToyScopeContext {
    pub scopes: Vec<Scope>,
    pub symbols: Vec<Symbol>,
    pub expressions: Vec<Expression>,
    pub expression_sources: Vec<SourceRange>,
    pub operations: Vec<Operation>,
    pub operation_sources: Vec<SourceRange>,
    pub programs: Vec<Program>,
    pub diagnostics: Vec<Diagnostic>,
    initializing: Vec<SymbolId>,
    poisoned: Vec<SymbolId>,
    pending_error_source: Option<SourceRange>,
    reserved_declarations: HashMap<(usize, u64), SymbolId>,
    runtime_policy3: bool,
}

impl ToyScopeContext {
    pub fn begin_analysis(&mut self) {
        self.diagnostics.clear();
        self.initializing.clear();
        self.pending_error_source = None;
        self.reserved_declarations.clear();
        self.runtime_policy3 = false;
    }

    pub fn set_runtime_policy3(&mut self) {
        self.runtime_policy3 = true;
    }

    pub fn reserve_direct(&mut self, scope: ScopeId, items: &[ast::AstValue]) {
        for item in items {
            if item.type_name != "declaration" {
                continue;
            }
            let Some(index) = item.field_names.iter().position(|name| name == "name") else {
                continue;
            };
            let Some(name) = item.elements.get(index) else {
                continue;
            };
            if self.scope_lookup_local(scope, &name.token_text).is_none() {
                let symbol = self.fresh_variable(scope, &name.token_text, name.source_span);
                self.scope_bind(scope, &name.token_text, symbol);
                self.reserved_declarations
                    .insert((scope.0, item.source_span.begin_byte), symbol);
            }
        }
    }

    pub fn reserved_symbol(
        &mut self,
        scope: ScopeId,
        declaration: SourceRange,
        name: SourceRange,
    ) -> Result<SymbolId, &'static str> {
        self.reserved_declarations
            .get(&(scope.0, declaration.begin_byte))
            .copied()
            .ok_or_else(|| {
                self.note_error(name);
                "identifier already declared"
            })
    }

    pub fn root_scope(&mut self, source: SourceRange) -> ScopeId {
        self.add_scope(None, source)
    }

    pub fn child_scope(&mut self, parent: ScopeId, source: SourceRange) -> ScopeId {
        self.add_scope(Some(parent), source)
    }

    fn add_scope(&mut self, parent: Option<ScopeId>, source: SourceRange) -> ScopeId {
        let id = ScopeId(self.scopes.len());
        self.scopes.push(Scope {
            parent,
            source,
            bindings: HashMap::new(),
            symbols: Vec::new(),
            operations: Vec::new(),
        });
        id
    }

    pub fn no_op(&self) -> OpId {
        OpId(usize::MAX)
    }

    pub fn note_error(&mut self, source: SourceRange) {
        if self.pending_error_source.is_none() {
            self.pending_error_source = Some(source);
        }
    }

    pub fn record_error_operation(
        &mut self,
        scope: ScopeId,
        source: SourceRange,
        message: &str,
    ) -> OpId {
        let source = self.pending_error_source.take().unwrap_or(source);
        for symbol in self.initializing.drain(..) {
            self.poisoned.push(symbol);
        }
        if message != "dependent error" {
            self.diagnostics.push(Diagnostic {
                message: message.to_owned(),
                source,
            });
        }
        self.append_operation(scope, Operation::Error, source)
    }

    pub fn finish_program(&mut self, scope: ScopeId, _last: OpId) -> ProgramId {
        let id = ProgramId(self.programs.len());
        self.programs.push(Program {
            scope,
            valid: self.diagnostics.is_empty(),
            runtime_policy3: self.runtime_policy3,
        });
        id
    }

    pub fn finish_block(&mut self, scope: ScopeId, _last: OpId) -> OpId {
        let parent = self.scopes[scope.0].parent.expect("block has a parent");
        self.append_operation(
            parent,
            Operation::Block { scope },
            self.scopes[scope.0].source,
        )
    }

    pub fn scope_lookup_local(&self, scope: ScopeId, name: &str) -> Option<SymbolId> {
        self.scopes[scope.0].bindings.get(name).copied()
    }

    pub fn scope_chain(&self, scope: ScopeId) -> Vec<ScopeId> {
        let mut chain = Vec::new();
        let mut current = Some(scope);
        while let Some(id) = current {
            chain.push(id);
            current = self.scopes[id.0].parent;
        }
        chain
    }

    pub fn fresh_variable(&mut self, scope: ScopeId, name: &str, source: SourceRange) -> SymbolId {
        let id = SymbolId(self.symbols.len());
        self.symbols.push(Symbol {
            name: name.to_owned(),
            declaration: source,
            owner: scope,
            place: PlaceId(id.0),
            initialized: false,
        });
        id
    }

    pub fn scope_bind(&mut self, scope: ScopeId, name: &str, symbol: SymbolId) {
        let current = &mut self.scopes[scope.0];
        current.bindings.insert(name.to_owned(), symbol);
        current.symbols.push(symbol);
    }

    pub fn option_symbol(&self, found: Option<SymbolId>) -> SymbolId {
        found.expect("checked symbol lookup")
    }

    pub fn is_initialized(&self, symbol: SymbolId) -> bool {
        self.symbols[symbol.0].initialized
    }

    pub fn is_poisoned(&self, symbol: SymbolId) -> bool {
        self.poisoned.contains(&symbol)
    }

    pub fn begin_initializer(&mut self, symbol: SymbolId) {
        self.initializing.push(symbol);
    }

    pub fn mark_initialized(&mut self, symbol: SymbolId) {
        self.symbols[symbol.0].initialized = true;
        self.initializing.retain(|item| *item != symbol);
    }

    fn append_operation(
        &mut self,
        scope: ScopeId,
        operation: Operation,
        source: SourceRange,
    ) -> OpId {
        let id = OpId(self.operations.len());
        self.operations.push(operation);
        self.operation_sources.push(source);
        self.scopes[scope.0].operations.push(id);
        id
    }

    pub fn record_uninitialized_declaration(
        &mut self,
        scope: ScopeId,
        symbol: SymbolId,
        source: SourceRange,
    ) -> OpId {
        self.append_operation(
            scope,
            Operation::Declare {
                symbol,
                initializer: None,
            },
            source,
        )
    }

    pub fn record_initialized_declaration(
        &mut self,
        scope: ScopeId,
        symbol: SymbolId,
        value: ExprId,
        source: SourceRange,
    ) -> OpId {
        self.append_operation(
            scope,
            Operation::Declare {
                symbol,
                initializer: Some(value),
            },
            source,
        )
    }

    pub fn record_assignment(
        &mut self,
        scope: ScopeId,
        symbol: SymbolId,
        value: ExprId,
        source: SourceRange,
    ) -> OpId {
        self.append_operation(scope, Operation::Assign { symbol, value }, source)
    }

    pub fn parse_integer(&self, text: &str) -> Result<BigInt, &'static str> {
        text.parse().map_err(|_| "invalid integer")
    }

    fn append_expression(&mut self, expression: Expression, source: SourceRange) -> ExprId {
        let id = ExprId(self.expressions.len());
        self.expressions.push(expression);
        self.expression_sources.push(source);
        id
    }

    pub fn make_constant(&mut self, value: BigInt, source: SourceRange) -> ExprId {
        self.append_expression(Expression::Constant(value), source)
    }

    pub fn make_load(&mut self, symbol: SymbolId, source: SourceRange) -> ExprId {
        self.append_expression(Expression::Load(symbol), source)
    }

    pub fn operator_kind(&self, text: &str) -> OperatorKind {
        match text {
            "+" => OperatorKind::Plus,
            "-" => OperatorKind::Minus,
            "*" => OperatorKind::Multiply,
            "/" => OperatorKind::Divide,
            "%" => OperatorKind::Remainder,
            _ => unreachable!("grammar only permits arithmetic operators"),
        }
    }

    pub fn make_unary(
        &mut self,
        operator: OperatorKind,
        operand: ExprId,
        source: SourceRange,
    ) -> ExprId {
        self.append_expression(Expression::Unary { operator, operand }, source)
    }

    fn known_integer(&self, expression: ExprId) -> Option<BigInt> {
        match &self.expressions[expression.0] {
            Expression::Constant(value) => Some(value.clone()),
            Expression::Load(_) => None,
            Expression::Unary { operator, operand } => {
                let value = self.known_integer(*operand)?;
                match operator {
                    OperatorKind::Plus => Some(value),
                    OperatorKind::Minus => Some(-value),
                    _ => None,
                }
            }
            Expression::Binary {
                left,
                operator,
                right,
                ..
            } => {
                let left = self.known_integer(*left)?;
                let right = self.known_integer(*right)?;
                match operator {
                    OperatorKind::Plus => Some(left + right),
                    OperatorKind::Minus => Some(left - right),
                    OperatorKind::Multiply => Some(left * right),
                    OperatorKind::Divide if right != BigInt::from(0) => Some(left / right),
                    OperatorKind::Remainder if right != BigInt::from(0) => Some(left % right),
                    OperatorKind::Divide | OperatorKind::Remainder => None,
                }
            }
        }
    }

    pub fn make_binary(
        &mut self,
        left: ExprId,
        operator: OperatorKind,
        right: ExprId,
        source: SourceRange,
    ) -> Result<ExprId, &'static str> {
        let checks = if matches!(operator, OperatorKind::Divide | OperatorKind::Remainder) {
            match self.known_integer(right) {
                Some(value) if value == BigInt::from(0) => {
                    self.note_error(source);
                    return Err("division by zero");
                }
                Some(_) => Vec::new(),
                None => vec![RuntimeCheck::Nonzero(right)],
            }
        } else {
            Vec::new()
        };
        Ok(self.append_expression(
            Expression::Binary {
                left,
                operator,
                right,
                checks,
            },
            source,
        ))
    }
}

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
    fn declaration_read_and_shadowing_keep_symbol_identity() {
        let mut ctx = ToyScopeContext::default();
        let program = analyze_source(&mut ctx, "int x = 5; { int a = x; }").unwrap();
        let root = ctx.programs[program.0].scope;
        let outer = ctx.scopes[root.0].bindings["x"];
        let block_id = ctx.scopes[root.0].operations[1];
        let Operation::Block { scope: inner } = ctx.operations[block_id.0] else {
            panic!("expected a block operation");
        };
        let local = ctx.scopes[inner.0].bindings["a"];
        let declaration_id = ctx.scopes[inner.0].operations[0];
        let Operation::Declare {
            symbol,
            initializer: Some(initializer),
        } = ctx.operations[declaration_id.0]
        else {
            panic!("expected an initialized declaration");
        };
        assert_eq!(symbol, local);
        assert_eq!(ctx.expressions[initializer.0], Expression::Load(outer));
        assert_eq!(ctx.symbols[local.0].owner, inner);
        assert_eq!(ctx.symbols[outer.0].place, PlaceId(outer.0));
    }

    #[test]
    fn own_initializer_reads_new_symbol_and_fails() {
        let mut ctx = ToyScopeContext::default();
        assert_eq!(
            analyze_source(&mut ctx, "int x = 5; { int a = x; int x = x + 1; }"),
            Err("variable read before initialization".to_owned())
        );
        let outer = ctx.scopes[0].bindings["x"];
        let local = ctx.scopes[1].bindings["x"];
        assert_ne!(outer, local);
        assert_eq!(lookup_lexical(&ctx, ScopeId(1), "x"), Some(local));
        assert!(!ctx.is_initialized(local));
    }

    #[test]
    fn assignment_updates_outer_state_after_rhs() {
        let mut ctx = ToyScopeContext::default();
        let program = analyze_source(&mut ctx, "int x; { x = 5; } int a = x;").unwrap();
        let root = ctx.programs[program.0].scope;
        let x = ctx.scopes[root.0].bindings["x"];
        let a = ctx.scopes[root.0].bindings["a"];
        assert!(ctx.is_initialized(x));
        assert!(ctx.is_initialized(a));
        assert_eq!(ctx.scopes[root.0].operations.len(), 3);
        assert_eq!(
            ctx.operations[ctx.scopes[1].operations[0].0],
            Operation::Assign {
                symbol: x,
                value: ExprId(0)
            }
        );
    }

    #[test]
    fn invalid_reads_and_bindings_have_specific_errors() {
        for (source, expected) in [
            ("int x; int a = x;", "variable read before initialization"),
            ("int x; x = x + 1;", "variable read before initialization"),
            ("int x; int x;", "identifier already declared"),
            ("x = 1;", "identifier not declared"),
            ("int x = 5 / 0;", "division by zero"),
        ] {
            let mut ctx = ToyScopeContext::default();
            assert_eq!(analyze_source(&mut ctx, source), Err(expected.to_owned()));
        }
    }

    #[test]
    fn arithmetic_chain_preserves_left_associativity() {
        let mut ctx = ToyScopeContext::default();
        analyze_source(&mut ctx, "int x = 8 - 3 - 2;").unwrap();
        let Expression::Binary {
            left,
            operator: OperatorKind::Minus,
            right,
            ..
        } = ctx.expressions[4]
        else {
            panic!("expected outer subtraction");
        };
        assert_eq!(ctx.expressions[right.0], Expression::Constant(2.into()));
        assert_eq!(
            ctx.expressions[left.0],
            Expression::Binary {
                left: ExprId(0),
                operator: OperatorKind::Minus,
                right: ExprId(1),
                checks: Vec::new(),
            }
        );
    }

    #[test]
    fn later_local_declaration_does_not_capture_earlier_read() {
        let mut ctx = ToyScopeContext::default();
        analyze_source(&mut ctx, "int x = 5; { int a = x; int x = 6; }").unwrap();
        let outer = ctx.scopes[0].bindings["x"];
        let inner = ctx.scopes[1].bindings["x"];
        assert_ne!(outer, inner);
        let Operation::Declare {
            initializer: Some(read),
            ..
        } = ctx.operations[ctx.scopes[1].operations[0].0]
        else {
            panic!("expected declaration of a");
        };
        assert_eq!(ctx.expressions[read.0], Expression::Load(outer));
    }

    #[test]
    fn parentheses_unary_and_precedence_produce_expression_ir() {
        let mut ctx = ToyScopeContext::default();
        analyze_source(&mut ctx, "int x = -(5 + 2) * 3;").unwrap();
        let root = &ctx.scopes[0];
        let Operation::Declare {
            initializer: Some(value),
            ..
        } = ctx.operations[root.operations[0].0]
        else {
            panic!("expected initialized declaration");
        };
        let Expression::Binary {
            operator: OperatorKind::Multiply,
            left,
            right,
            ..
        } = ctx.expressions[value.0]
        else {
            panic!("expected multiplication");
        };
        assert_eq!(ctx.expressions[right.0], Expression::Constant(3.into()));
        assert!(matches!(
            ctx.expressions[left.0],
            Expression::Unary {
                operator: OperatorKind::Minus,
                ..
            }
        ));
    }

    #[test]
    fn dynamic_divisor_keeps_a_runtime_nonzero_check() {
        let mut ctx = ToyScopeContext::default();
        analyze_source(&mut ctx, "int x = 1; int a = 5 / x;").unwrap();
        let Operation::Declare {
            initializer: Some(value),
            ..
        } = ctx.operations[ctx.scopes[0].operations[1].0]
        else {
            panic!("expected initialized declaration");
        };
        let Expression::Binary {
            operator: OperatorKind::Divide,
            right,
            ref checks,
            ..
        } = ctx.expressions[value.0]
        else {
            panic!("expected division");
        };
        assert_eq!(checks, &vec![RuntimeCheck::Nonzero(right)]);
    }

    #[test]
    fn empty_program_and_block_have_distinct_scopes() {
        let mut empty = ToyScopeContext::default();
        let program = analyze_source(&mut empty, "").unwrap();
        assert!(
            empty.scopes[empty.programs[program.0].scope.0]
                .operations
                .is_empty()
        );

        let mut nested = ToyScopeContext::default();
        analyze_source(&mut nested, "{}").unwrap();
        assert_eq!(nested.scopes.len(), 2);
        assert_eq!(nested.scopes[1].parent, Some(ScopeId(0)));
        assert!(nested.scopes[1].operations.is_empty());
    }

    #[test]
    fn integer_literals_and_constant_arithmetic_exceed_i64() {
        let mut ctx = ToyScopeContext::default();
        analyze_source(&mut ctx, "int x = 999999999999999999999999999999 + 1;").unwrap();
        assert_eq!(
            ctx.known_integer(ExprId(2)),
            Some("1000000000000000000000000000000".parse().unwrap())
        );
    }

    #[test]
    fn execution_uses_lexical_places_and_keeps_outer_assignment() {
        let mut ctx = ToyScopeContext::default();
        let program = analyze_source(&mut ctx, "int x = 5; { int a = x; x = a + 2; }").unwrap();
        let result = execute_program(&ctx, program).unwrap();
        let outer = ctx.scopes[0].bindings["x"];
        assert_eq!(result.root_values[&outer], Some(BigInt::from(7)));
    }

    #[test]
    fn execution_checks_dynamic_zero_and_division_signs() {
        let mut ctx = ToyScopeContext::default();
        let program = analyze_source(&mut ctx, "int x = 0; int a = 5 / x;").unwrap();
        assert_eq!(execute_program(&ctx, program), Err("division by zero"));

        let mut ctx = ToyScopeContext::default();
        let program = analyze_source(&mut ctx, "int x = -7 / 3; int a = -7 % 3;").unwrap();
        let result = execute_program(&ctx, program).unwrap();
        assert_eq!(
            result.root_values[&ctx.scopes[0].bindings["x"]],
            Some((-2).into())
        );
        assert_eq!(
            result.root_values[&ctx.scopes[0].bindings["a"]],
            Some((-1).into())
        );
    }

    #[test]
    fn execution_handles_bigint_without_overflow() {
        let mut ctx = ToyScopeContext::default();
        let program = analyze_source(
            &mut ctx,
            "int x = 999999999999999999999999999999; x = x * 10;",
        )
        .unwrap();
        let result = execute_program(&ctx, program).unwrap();
        assert_eq!(
            result.root_values[&ctx.scopes[0].bindings["x"]],
            Some("9999999999999999999999999999990".parse().unwrap())
        );
    }

    #[test]
    fn semantic_recovery_collects_independent_errors_and_keeps_later_operations() {
        let source = "int x = x; int y = x; int z = q; int a = 2;";
        let mut ctx = ToyScopeContext::default();
        let program = analyze_source_partial(&mut ctx, source).unwrap();
        assert!(!ctx.programs[program.0].valid);
        assert_eq!(ctx.diagnostics.len(), 2);
        assert_eq!(
            ctx.diagnostics[0].message,
            "variable read before initialization"
        );
        assert_eq!(ctx.diagnostics[1].message, "identifier not declared");
        assert_eq!(ctx.diagnostics[0].source.begin_byte, 8);
        assert_eq!(ctx.diagnostics[1].source.begin_byte, 30);
        assert_eq!(ctx.scopes[0].operations.len(), 4);
        assert!(matches!(
            ctx.operations[ctx.scopes[0].operations[0].0],
            Operation::Error
        ));
        assert!(matches!(
            ctx.operations[ctx.scopes[0].operations[3].0],
            Operation::Declare { .. }
        ));
        assert_eq!(execute_program(&ctx, program), Err("invalid program"));
    }

    #[test]
    fn recovery_continues_after_error_inside_block() {
        let mut ctx = ToyScopeContext::default();
        let program =
            analyze_source_partial(&mut ctx, "int x = 1; { q = 2; x = 3; } int a = x;").unwrap();
        assert_eq!(ctx.diagnostics.len(), 1);
        assert_eq!(ctx.diagnostics[0].message, "identifier not declared");
        assert_eq!(ctx.scopes[1].operations.len(), 2);
        assert_eq!(ctx.scopes[0].operations.len(), 3);
        assert!(!ctx.programs[program.0].valid);
    }

    #[test]
    fn source_ranges_follow_symbols_expressions_and_operations() {
        let mut ctx = ToyScopeContext::default();
        analyze_source(&mut ctx, "int x = 5; x = x + 2;").unwrap();
        let symbol = ctx.scopes[0].bindings["x"];
        assert_eq!(ctx.symbols[symbol.0].declaration.begin_byte, 4);
        assert_eq!(ctx.expression_sources[0].begin_byte, 8);
        assert_eq!(ctx.operation_sources[0].begin_byte, 0);
        assert_eq!(ctx.operation_sources[1].begin_byte, 11);
    }
}
