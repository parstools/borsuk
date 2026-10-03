use agsem_runtime::{AccessError, ValueTree};

use crate::ast::InputSpan;
use crate::model::{
    Constant, Context, ExprId, Expression, ExpressionKind, FunctionId, OpId, Operation,
    OperationNode, Operator, PlaceId, RuntimeCheck, StructId, SymbolId, Type,
};

impl<'a> Interpreter<'a> {
    pub fn new(context: &'a Context) -> Result<Self, RuntimeError> {
        let module = context.modules.first().ok_or(RuntimeError {
            message: "missing analyzed module",
            source: zero_span(),
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
                globals_finished: false,
                bound_places: Vec::new(),
            },
        };
        interpreter.initialize_globals()?;
        Ok(interpreter)
    }

    pub fn set_step_limit(&mut self, limit: usize) {
        self.runtime.steps_remaining = limit;
    }

    pub fn call_named(&mut self, name: &str, arguments: Vec<Value>) -> Result<Value, RuntimeError> {
        let id = self
            .context
            .functions
            .iter()
            .position(|function| function.name == name)
            .ok_or_else(|| self.error("function not declared", zero_span()))?;
        self.call(FunctionId(id), arguments, self.context.functions[id].source)
    }

    pub fn global(&self, symbol: SymbolId) -> Option<&Value> {
        self.runtime.store.globals().get(&symbol)
    }

    pub fn finish_globals(&mut self) -> Result<(), RuntimeError> {
        if self.runtime.globals_finished {
            return Ok(());
        }
        let declarations = self.module_operations()?;
        for operation in declarations.into_iter().rev() {
            if let Operation::Construct(symbol, _, _) = self.operation_node(operation)?.kind {
                self.destroy(symbol, zero_span())?;
            }
        }
        self.runtime.globals_finished = true;
        Ok(())
    }

    pub fn run_main(&mut self) -> Result<Value, RuntimeError> {
        let result = self.call_named("main", Vec::new());
        let cleanup = self.finish_globals();
        match (result, cleanup) {
            (Err(error), _) => Err(error),
            (_, Err(error)) => Err(error),
            (Ok(value), Ok(())) => Ok(value),
        }
    }

    fn error(&self, message: &'static str, source: InputSpan) -> RuntimeError {
        RuntimeError { message, source }
    }

    fn module_operations(&self) -> Result<Vec<OpId>, RuntimeError> {
        self.context
            .modules
            .first()
            .map(|module| module.declarations.clone())
            .ok_or_else(|| self.error("missing analyzed module", zero_span()))
    }

    fn variable_type(&self, symbol: SymbolId) -> Result<Type, RuntimeError> {
        self.context
            .variables
            .get(symbol.0)
            .map(|variable| variable.ty.clone())
            .ok_or_else(|| self.error("invalid symbol", zero_span()))
    }

    fn variable_place(&self, symbol: SymbolId) -> Result<PlaceId, RuntimeError> {
        self.context
            .variables
            .get(symbol.0)
            .map(|variable| variable.place)
            .ok_or_else(|| self.error("invalid symbol", zero_span()))
    }

    fn field_types(&self, structure: StructId) -> Result<Vec<Type>, RuntimeError> {
        self.context
            .structs
            .get(structure.0)
            .map(|structure| {
                structure
                    .fields
                    .iter()
                    .map(|field| self.context.fields[field.0].ty.clone())
                    .collect()
            })
            .ok_or_else(|| self.error("invalid class", zero_span()))
    }

    fn place_view(&mut self, place: PlaceId) -> Result<PlaceView, RuntimeError> {
        if let Some(bound) = self
            .runtime
            .bound_places
            .iter()
            .rev()
            .find(|bound| bound.place == place && bound.depth == self.runtime.store.depth())
        {
            return Ok(bound.view.clone());
        }
        let place = self
            .context
            .places
            .get(place.0)
            .ok_or_else(|| self.error("invalid place", zero_span()))?;
        let root = place.key.root;
        let projections = place.key.fields.clone();
        let mut ty = self.variable_type(root)?;
        let mut fields = Vec::new();
        for projection in projections {
            let field = self.context.fields[projection.0].clone();
            let offset = if let Some(index) = field.index_expression {
                let Value::Int(number) = self.evaluate(index)? else {
                    return Err(self.error("invalid array index", field.source));
                };
                let Type::Array(_, length) = ty else {
                    return Err(self.error("indexing non-array", field.source));
                };
                let offset = usize::try_from(number)
                    .map_err(|_| self.error("array index out of bounds", field.source))?;
                if offset >= length {
                    return Err(self.error("array index out of bounds", field.source));
                }
                offset
            } else {
                field.ordinal
            };
            ty = field.ty;
            fields.push(offset);
        }
        self.resolve_reference(PlaceView { root, fields })
    }

    fn execute_compound_store(
        &mut self,
        place: PlaceId,
        value: ExprId,
        source: InputSpan,
    ) -> Result<(), RuntimeError> {
        let view = self.place_view(place)?;
        let depth = self.runtime.store.depth();
        self.runtime
            .bound_places
            .push(BoundPlace { place, view, depth });
        let outcome = self
            .evaluate(value)
            .and_then(|computed| self.write(place, computed, source));
        self.runtime.bound_places.pop();
        outcome
    }

    fn resolve_reference(&self, mut place: PlaceView) -> Result<PlaceView, RuntimeError> {
        for _ in 0..256 {
            match self.runtime.store.get(&place.root) {
                Some(Value::Reference(referred)) => {
                    let mut fields = referred.fields.clone();
                    fields.extend(place.fields);
                    place = PlaceView {
                        root: referred.root,
                        fields,
                    };
                }
                _ => return Ok(place),
            }
        }
        Err(self.error("reference cycle", zero_span()))
    }

    fn reference_matches_type(&self, place: PlaceView, ty: Type) -> Result<bool, RuntimeError> {
        let place = self.resolve_reference(place)?;
        let mut actual = self.variable_type(place.root)?;
        for index in place.fields {
            actual = match actual {
                Type::Struct(id) => self
                    .field_types(id)?
                    .get(index)
                    .cloned()
                    .ok_or_else(|| self.error("invalid runtime field", zero_span()))?,
                Type::Array(element, length) if index < length => *element,
                _ => return Err(self.error("invalid runtime field", zero_span())),
            };
        }
        Ok(actual == ty)
    }

    fn function_view(&self, function: FunctionId) -> Result<FunctionView, RuntimeError> {
        let function = self
            .context
            .functions
            .get(function.0)
            .ok_or_else(|| self.error("invalid function", zero_span()))?;
        Ok(FunctionView {
            body: function.body,
            parameters: function.parameters.clone(),
            symbols: function
                .scope
                .map(|scope| self.context.scopes[scope.0].symbols.clone()),
            result: function.result.clone(),
        })
    }

    fn operation_node(&self, operation: OpId) -> Result<OperationNode, RuntimeError> {
        self.context
            .operations
            .get(operation.0)
            .cloned()
            .ok_or_else(|| self.error("invalid operation", zero_span()))
    }

    fn expression_node(&self, expression: ExprId) -> Result<Expression, RuntimeError> {
        self.context
            .expressions
            .get(expression.0)
            .cloned()
            .ok_or_else(|| self.error("invalid expression", zero_span()))
    }

    fn literal_identity(&self, expression: ExprId) -> Result<usize, RuntimeError> {
        Ok(expression.0)
    }

    fn evaluate_method_call(
        &mut self,
        callee: FunctionId,
        receiver: PlaceId,
        arguments: Vec<ExprId>,
        source: InputSpan,
    ) -> Result<Value, RuntimeError> {
        let mut place = self.place_view(receiver)?;
        let owner = self
            .context
            .functions
            .get(callee.0)
            .and_then(|function| function.owner)
            .ok_or_else(|| self.error("method not declared", source))?;
        let mut actual = match self.context.places[receiver.0].ty {
            Type::Struct(actual) => actual,
            _ => return Err(self.error("method call on non-class", source)),
        };
        while actual != owner {
            let base = self.context.structs[actual.0]
                .base
                .ok_or_else(|| self.error("method owner does not match receiver", source))?;
            place.fields.push(0);
            actual = base;
        }
        let mut values = vec![Value::Reference(place)];
        for argument in arguments {
            values.push(self.evaluate(argument)?);
        }
        self.call(callee, values, source)
    }

    fn construct(
        &mut self,
        symbol: SymbolId,
        constructor: Option<FunctionId>,
        arguments: Vec<ExprId>,
        source: InputSpan,
    ) -> Result<Control, RuntimeError> {
        if self.runtime.store.get(&symbol).is_none() {
            self.declare(symbol, source)?;
        }
        let mut values = Vec::new();
        for argument in arguments {
            values.push(self.evaluate(argument)?);
        }
        let place = self.place_view(self.variable_place(symbol)?)?;
        let ty = self.variable_type(symbol)?;
        self.construct_at(place, ty, constructor, values, source)?;
        Ok(Control::Continue)
    }

    fn construct_at(
        &mut self,
        place: PlaceView,
        ty: Type,
        selected: Option<FunctionId>,
        arguments: Vec<Value>,
        source: InputSpan,
    ) -> Result<(), RuntimeError> {
        match ty {
            Type::Struct(id) => {
                let structure = self
                    .context
                    .structs
                    .get(id.0)
                    .ok_or_else(|| self.error("invalid class", source))?
                    .clone();
                for (index, field) in structure.fields.iter().enumerate() {
                    let field = &self.context.fields[field.0].ty;
                    if matches!(field, Type::Struct(_) | Type::Array(_, _)) {
                        let mut child = place.clone();
                        child.fields.push(index);
                        self.construct_at(child, field.clone(), None, Vec::new(), source)?;
                    }
                }
                let constructor = if let Some(selected) = selected {
                    if !structure.constructors.contains(&selected) {
                        return Err(self.error("constructor does not match class", source));
                    }
                    Some(selected)
                } else if structure.constructors.is_empty() {
                    None
                } else {
                    structure
                        .constructors
                        .iter()
                        .copied()
                        .find(|id| self.context.functions[id.0].parameters.len() == 1)
                };
                if let Some(constructor) = constructor {
                    let mut values = vec![Value::Reference(place)];
                    values.extend(arguments);
                    self.call(constructor, values, source)?;
                } else if !arguments.is_empty() || !structure.constructors.is_empty() {
                    return Err(self.error("constructor not declared", source));
                }
            }
            Type::Array(element, length) => {
                if !arguments.is_empty() {
                    return Err(self.error("array constructor arguments", source));
                }
                for index in 0..length {
                    let mut child = place.clone();
                    child.fields.push(index);
                    self.construct_at(child, (*element).clone(), None, Vec::new(), source)?;
                }
            }
            _ if !arguments.is_empty() => {
                return Err(self.error("constructor not declared", source));
            }
            _ => {}
        }
        Ok(())
    }

    fn destroy(&mut self, symbol: SymbolId, source: InputSpan) -> Result<(), RuntimeError> {
        let place = self.place_view(self.variable_place(symbol)?)?;
        let ty = self.variable_type(symbol)?;
        self.destroy_at(place, ty, source)
    }

    fn destroy_at(
        &mut self,
        place: PlaceView,
        ty: Type,
        source: InputSpan,
    ) -> Result<(), RuntimeError> {
        match ty {
            Type::Struct(id) => {
                let structure = self
                    .context
                    .structs
                    .get(id.0)
                    .ok_or_else(|| self.error("invalid class", source))?
                    .clone();
                if let Some(destructor) = structure.destructor {
                    self.call(destructor, vec![Value::Reference(place.clone())], source)?;
                }
                for (index, field) in structure.fields.into_iter().enumerate().rev() {
                    let mut child = place.clone();
                    child.fields.push(index);
                    self.destroy_at(child, self.context.fields[field.0].ty.clone(), source)?;
                }
            }
            Type::Array(element, length) => {
                for index in (0..length).rev() {
                    let mut child = place.clone();
                    child.fields.push(index);
                    self.destroy_at(child, (*element).clone(), source)?;
                }
            }
            _ => {}
        }
        self.runtime
            .store
            .write_path(place.root, place.fields, Value::Uninitialized)
            .map_err(|_| self.error("invalid runtime field", source))
    }
}

include!("../generated/interpreter_gen.rs");

impl ValueTree for Value {
    fn child(&self, index: usize) -> Option<&Self> {
        match self {
            Value::Struct(fields) | Value::Array(fields) => fields.get(index),
            _ => None,
        }
    }

    fn child_mut(&mut self, index: usize) -> Option<&mut Self> {
        match self {
            Value::Struct(fields) | Value::Array(fields) => fields.get_mut(index),
            _ => None,
        }
    }
}

fn zero_span() -> InputSpan {
    InputSpan {
        begin_byte: 0,
        end_byte: 0,
    }
}

#[cfg(test)]
#[path = "interpreter_tests.rs"]
mod tests;
