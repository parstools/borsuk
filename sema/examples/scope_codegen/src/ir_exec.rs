use std::collections::HashMap;

use crate::{FlowId, InitialState, IrOp, SemaContext, SymbolId, SymbolKind};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ExecutionError {
    MissingStorage(SymbolId),
    Uninitialized(SymbolId),
    FunctionValue(SymbolId),
    InitializedDeclarationWithoutValue(SymbolId),
}

pub struct ExecutionState {
    values: HashMap<SymbolId, Option<i64>>,
    reads: Vec<(SymbolId, i64)>,
}

impl ExecutionState {
    pub fn value(&self, symbol: SymbolId) -> Option<i64> {
        self.values.get(&symbol).copied().flatten()
    }

    pub fn contains(&self, symbol: SymbolId) -> bool {
        self.values.contains_key(&symbol)
    }

    pub fn reads(&self) -> &[(SymbolId, i64)] {
        &self.reads
    }

    pub fn last_read(&self) -> Option<i64> {
        self.reads.last().map(|(_, value)| *value)
    }

    fn read_value(&self, ctx: &SemaContext, symbol: SymbolId) -> Result<i64, ExecutionError> {
        if matches!(ctx.symbol(symbol).kind, SymbolKind::Function { .. }) {
            return Err(ExecutionError::FunctionValue(symbol));
        }
        self.values
            .get(&symbol)
            .ok_or(ExecutionError::MissingStorage(symbol))?
            .ok_or(ExecutionError::Uninitialized(symbol))
    }

    fn execute(&mut self, ctx: &SemaContext, operations: &[IrOp]) -> Result<(), ExecutionError> {
        for operation in operations {
            match operation {
                IrOp::Declare {
                    symbol,
                    initial_state,
                } => {
                    let value = match initial_state {
                        InitialState::Uninitialized => None,
                        InitialState::ZeroInitialized => Some(0),
                        InitialState::Initialized => {
                            return Err(ExecutionError::InitializedDeclarationWithoutValue(
                                *symbol,
                            ));
                        }
                    };
                    self.values.insert(*symbol, value);
                }
                IrOp::Assign(assignment) => {
                    if matches!(
                        ctx.symbol(assignment.target).kind,
                        SymbolKind::Function { .. }
                    ) {
                        return Err(ExecutionError::FunctionValue(assignment.target));
                    }
                    *self
                        .values
                        .get_mut(&assignment.target)
                        .ok_or(ExecutionError::MissingStorage(assignment.target))? =
                        Some(assignment.value);
                }
                IrOp::Read { symbol } => {
                    let value = self.read_value(ctx, *symbol)?;
                    self.reads.push((*symbol, value));
                }
                IrOp::Block { scope, body } => {
                    self.execute(ctx, body)?;
                    for symbol in ctx.scope_symbols(*scope) {
                        self.values.remove(&symbol);
                    }
                }
                IrOp::If {
                    condition,
                    then_body,
                    else_body,
                } => {
                    let branch = if self.read_value(ctx, *condition)? != 0 {
                        then_body
                    } else {
                        else_body
                    };
                    self.execute(ctx, branch)?;
                }
            }
        }
        Ok(())
    }
}

pub fn execute_ir(
    ctx: &SemaContext,
    flow: FlowId,
    operations: &[IrOp],
    inputs: &[(SymbolId, i64)],
) -> Result<ExecutionState, ExecutionError> {
    let mut state = ExecutionState {
        values: ctx
            .flow_symbols(flow)
            .into_iter()
            .map(|symbol| {
                let value = match ctx
                    .flow_state(flow, symbol)
                    .expect("symbol belongs to flow")
                {
                    InitialState::ZeroInitialized => Some(0),
                    InitialState::Uninitialized | InitialState::Initialized => None,
                };
                (symbol, value)
            })
            .collect(),
        reads: Vec::new(),
    };
    for &(symbol, value) in inputs {
        *state
            .values
            .get_mut(&symbol)
            .ok_or(ExecutionError::MissingStorage(symbol))? = Some(value);
    }
    state.execute(ctx, operations)?;
    Ok(state)
}
