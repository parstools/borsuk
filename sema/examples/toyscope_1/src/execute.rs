use std::collections::HashMap;

use crate::{
    BigInt, ExprId, Expression, OpId, Operation, OperatorKind, PlaceId, ProgramId, ScopeId,
    SymbolId, ToyScopeContext,
};

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ExecutionResult {
    pub root_values: HashMap<SymbolId, Option<BigInt>>,
}

struct Frame {
    scope: ScopeId,
    parent: Option<usize>,
    cells: HashMap<PlaceId, CellState>,
}

#[derive(Clone)]
enum CellState {
    Reserved,
    Uninitialized,
    Initialized(BigInt),
}

fn frame_for_symbol(
    context: &ToyScopeContext,
    frames: &[Frame],
    current: usize,
    symbol: SymbolId,
) -> usize {
    let owner = context.symbols[symbol.0].owner;
    let mut frame = current;
    loop {
        if frames[frame].scope == owner {
            return frame;
        }
        frame = frames[frame]
            .parent
            .expect("a bound symbol has a lexical frame");
    }
}

fn evaluate(
    context: &ToyScopeContext,
    frames: &[Frame],
    current: usize,
    expression: ExprId,
) -> Result<BigInt, &'static str> {
    match &context.expressions[expression.0] {
        Expression::Constant(value) => Ok(value.clone()),
        Expression::Load(symbol) => {
            let frame = frame_for_symbol(context, frames, current, *symbol);
            match &frames[frame].cells[&context.symbols[symbol.0].place] {
                CellState::Initialized(value) => Ok(value.clone()),
                CellState::Reserved | CellState::Uninitialized => {
                    Err("variable read before initialization")
                }
            }
        }
        Expression::Unary { operator, operand } => {
            let value = evaluate(context, frames, current, *operand)?;
            match operator {
                OperatorKind::Plus => Ok(value),
                OperatorKind::Minus => Ok(-value),
                _ => Err("invalid unary operator"),
            }
        }
        Expression::Binary {
            left,
            operator,
            right,
            checks,
        } => {
            let left_value = evaluate(context, frames, current, *left)?;
            let right_value = evaluate(context, frames, current, *right)?;
            if !checks.is_empty() && right_value == BigInt::from(0) {
                return Err("division by zero");
            }
            match operator {
                OperatorKind::Plus => Ok(left_value + right_value),
                OperatorKind::Minus => Ok(left_value - right_value),
                OperatorKind::Multiply => Ok(left_value * right_value),
                OperatorKind::Divide | OperatorKind::Remainder
                    if right_value == BigInt::from(0) =>
                {
                    Err("division by zero")
                }
                OperatorKind::Divide => Ok(left_value / right_value),
                OperatorKind::Remainder => Ok(left_value % right_value),
            }
        }
    }
}

fn execute_operation(
    context: &ToyScopeContext,
    frames: &mut Vec<Frame>,
    current: usize,
    operation: OpId,
    policy3: bool,
) -> Result<(), &'static str> {
    match &context.operations[operation.0] {
        Operation::Declare {
            symbol,
            initializer,
        } => {
            if let Some(expression) = initializer {
                let value = evaluate(context, frames, current, *expression)?;
                let frame = frame_for_symbol(context, frames, current, *symbol);
                frames[frame].cells.insert(
                    context.symbols[symbol.0].place,
                    CellState::Initialized(value),
                );
            } else {
                let frame = frame_for_symbol(context, frames, current, *symbol);
                frames[frame]
                    .cells
                    .insert(context.symbols[symbol.0].place, CellState::Uninitialized);
            }
        }
        Operation::Assign { symbol, value } => {
            let frame = frame_for_symbol(context, frames, current, *symbol);
            if policy3
                && matches!(
                    frames[frame].cells[&context.symbols[symbol.0].place],
                    CellState::Reserved
                )
            {
                return Err("variable assigned before declaration");
            }
            let value = evaluate(context, frames, current, *value)?;
            frames[frame].cells.insert(
                context.symbols[symbol.0].place,
                CellState::Initialized(value),
            );
        }
        Operation::Block { scope } => {
            execute_scope(context, frames, *scope, Some(current), policy3)?;
        }
        Operation::Error => return Err("invalid program"),
    }
    Ok(())
}

fn execute_scope(
    context: &ToyScopeContext,
    frames: &mut Vec<Frame>,
    scope: ScopeId,
    parent: Option<usize>,
    policy3: bool,
) -> Result<Frame, &'static str> {
    let mut cells = HashMap::new();
    for symbol in &context.scopes[scope.0].symbols {
        cells.insert(
            context.symbols[symbol.0].place,
            if policy3 {
                CellState::Reserved
            } else {
                CellState::Uninitialized
            },
        );
    }
    let current = frames.len();
    frames.push(Frame {
        scope,
        parent,
        cells,
    });
    let mut result = Ok(());
    for operation in &context.scopes[scope.0].operations {
        if let Err(error) = execute_operation(context, frames, current, *operation, policy3) {
            result = Err(error);
            break;
        }
    }
    let frame = frames.pop().expect("active lexical frame");
    result.map(|()| frame)
}

pub fn execute_program(
    context: &ToyScopeContext,
    program: ProgramId,
) -> Result<ExecutionResult, &'static str> {
    if !context.programs[program.0].valid {
        return Err("invalid program");
    }
    let scope = context.programs[program.0].scope;
    let frame = execute_scope(
        context,
        &mut Vec::new(),
        scope,
        None,
        context.programs[program.0].runtime_policy3,
    )?;
    let root_values = context.scopes[scope.0]
        .symbols
        .iter()
        .map(|symbol| {
            (
                *symbol,
                match &frame.cells[&context.symbols[symbol.0].place] {
                    CellState::Initialized(value) => Some(value.clone()),
                    CellState::Reserved | CellState::Uninitialized => None,
                },
            )
        })
        .collect();
    Ok(ExecutionResult { root_values })
}
