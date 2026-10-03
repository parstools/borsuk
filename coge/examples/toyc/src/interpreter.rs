use agsem_runtime::{AccessError, ValueTree};

use crate::ast::InputSpan;
use crate::model::{
    Constant, Context, ExprId, ExpressionKind, FunctionId, OpId, Operation, Operator, PlaceId,
    RuntimeCheck, StructId, SymbolId, Type,
};

impl<'a> Interpreter<'a> {
    pub fn new(context: &'a Context) -> Result<Self, RuntimeError> {
        let module = context.modules.first().ok_or(RuntimeError {
            message: "missing analyzed module",
            source: InputSpan {
                begin_byte: 0,
                end_byte: 0,
            },
        })?;
        if !module.valid || !context.diagnostics.is_empty() {
            return Err(RuntimeError {
                message: "semantic errors prevent execution",
                source: context
                    .diagnostics
                    .first()
                    .map_or(context.scopes[module.scope.0].source, |error| error.source),
            });
        }
        let mut interpreter = Self {
            context,
            runtime: Runtime {
                store: Default::default(),
                steps_remaining: 1_000_000,
            },
        };
        interpreter.initialize_globals()?;
        Ok(interpreter)
    }

    pub fn set_step_limit(&mut self, limit: usize) {
        self.runtime.steps_remaining = limit;
    }

    pub fn global(&self, name: &str) -> Option<&Value> {
        let module = self.context.modules.first()?;
        let binding = self.context.scopes[module.scope.0].ordinary.get(name)?;
        let crate::model::Binding::Variable(symbol) = binding else {
            return None;
        };
        self.runtime.store.globals().get(symbol)
    }

    pub fn call_named(&mut self, name: &str, arguments: Vec<Value>) -> Result<Value, RuntimeError> {
        let module = self.context.modules.first().ok_or(self.error(
            "missing analyzed module",
            InputSpan {
                begin_byte: 0,
                end_byte: 0,
            },
        ))?;
        let binding = self.context.scopes[module.scope.0].ordinary.get(name);
        let Some(crate::model::Binding::Function(function)) = binding else {
            return Err(self.error(
                "function not declared",
                self.context.scopes[module.scope.0].source,
            ));
        };
        self.call(
            *function,
            arguments,
            self.context.functions[function.0].source,
        )
    }

    fn error(&self, message: &'static str, source: InputSpan) -> RuntimeError {
        RuntimeError { message, source }
    }

    fn module_operations(&self) -> Result<Vec<OpId>, RuntimeError> {
        Ok(self.context.modules[0].declarations.clone())
    }

    fn variable_type(&self, symbol: SymbolId) -> Result<Type, RuntimeError> {
        Ok(self.context.variables[symbol.0].ty.clone())
    }

    fn variable_place(&self, symbol: SymbolId) -> Result<PlaceId, RuntimeError> {
        Ok(self.context.variables[symbol.0].place)
    }

    fn field_types(&self, structure: StructId) -> Result<Vec<Type>, RuntimeError> {
        Ok(self.context.structs[structure.0]
            .fields
            .iter()
            .map(|field| self.context.fields[field.0].ty.clone())
            .collect())
    }

    fn place_view(&self, place: PlaceId) -> Result<PlaceView, RuntimeError> {
        let key = &self.context.places[place.0].key;
        Ok(PlaceView {
            root: key.root,
            fields: key
                .fields
                .iter()
                .map(|field| self.context.fields[field.0].ordinal)
                .collect(),
        })
    }

    fn array_path(
        &self,
        place: PlaceId,
        index: Value,
        source: InputSpan,
    ) -> Result<(SymbolId, Vec<usize>), RuntimeError> {
        let Value::Int(index) = index else {
            return Err(self.error("array index must be int", source));
        };
        let index =
            usize::try_from(index).map_err(|_| self.error("array index out of bounds", source))?;
        let view = self.place_view(place)?;
        let mut path = view.fields;
        path.push(index);
        Ok((view.root, path))
    }

    fn array_load(
        &self,
        place: PlaceId,
        index: Value,
        source: InputSpan,
    ) -> Result<Value, RuntimeError> {
        let (root, path) = self.array_path(place, index, source)?;
        let value = self.runtime.store.load_path(root, path).map_err(|fault| {
            self.error(
                match fault {
                    AccessError::MissingRoot => "missing runtime variable",
                    AccessError::InvalidPath => "array index out of bounds",
                },
                source,
            )
        })?;
        self.initialized_value(value, source)
    }

    fn array_store(
        &mut self,
        place: PlaceId,
        index: Value,
        value: Value,
        source: InputSpan,
    ) -> Result<(), RuntimeError> {
        let (root, path) = self.array_path(place, index, source)?;
        self.runtime
            .store
            .write_path(root, path, value)
            .map_err(|fault| {
                self.error(
                    match fault {
                        AccessError::MissingRoot => "missing runtime variable",
                        AccessError::InvalidPath => "array index out of bounds",
                    },
                    source,
                )
            })
    }

    fn function_view(&self, function: FunctionId) -> Result<FunctionView, RuntimeError> {
        let definition = &self.context.functions[function.0];
        Ok(FunctionView {
            body: definition.body,
            parameters: definition.parameters.clone(),
            symbols: definition
                .scope
                .map(|scope| self.context.scopes[scope.0].symbols.clone()),
            result: definition.result.clone(),
        })
    }

    fn operation_node(&self, operation: OpId) -> Result<crate::model::OperationNode, RuntimeError> {
        Ok(self.context.operations[operation.0].clone())
    }

    fn expression_node(
        &self,
        expression: ExprId,
    ) -> Result<crate::model::Expression, RuntimeError> {
        Ok(self.context.expressions[expression.0].clone())
    }

    fn literal_identity(&self, expression: ExprId) -> Result<usize, RuntimeError> {
        Ok(expression.0)
    }
}

include!("../generated/interpreter_gen.rs");

impl ValueTree for Value {
    fn child(&self, index: usize) -> Option<&Self> {
        match self {
            Value::Struct(values) | Value::Array(values) => values.get(index),
            _ => None,
        }
    }

    fn child_mut(&mut self, index: usize) -> Option<&mut Self> {
        match self {
            Value::Struct(values) | Value::Array(values) => values.get_mut(index),
            _ => None,
        }
    }
}

#[cfg(test)]
#[path = "interpreter_tests.rs"]
mod tests;
