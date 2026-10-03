use crate::ast::InputSpan;
use crate::model::{
    AssignmentOp, Binding, Constant, Context, ExprId, ExpressionKind, FunctionId, Initialization,
    Operation, Operator, PlaceId, ScopeId, SymbolId, Type,
};

impl Context {
    pub fn recover_expression(&mut self, message: &str, source: InputSpan) -> ExprId {
        self.record_error(message, source);
        self.add_expression(ExpressionKind::Error, Type::Void, source)
    }

    pub fn begin_initializer(
        &mut self,
        scope: ScopeId,
        name: &str,
        ty: Type,
        source: InputSpan,
    ) -> Result<SymbolId, &'static str> {
        if !self.complete_object_type(&ty) {
            return Err("invalid variable type");
        }
        let symbol = self
            .declare_variable(scope, name, ty, source)
            .map_err(|_| "identifier already declared")?;
        let place = self.variables[symbol.0].place;
        self.current_flow.set(place, Initialization::Initializing);
        self.active_initializer = Some(symbol);
        let declaration = self.add_operation(Operation::Declare(symbol), source);
        self.scopes[scope.0].operations.push(declaration);
        Ok(symbol)
    }

    pub fn finish_initializer(
        &mut self,
        scope: ScopeId,
        symbol: SymbolId,
        value: ExprId,
        source: InputSpan,
    ) -> Result<(), &'static str> {
        if self.poisoned_expression(value) {
            return Err("dependent error");
        }
        let ty = self.variables[symbol.0].ty.clone();
        let converted = self
            .convert(value, ty)
            .map_err(|_| "incompatible initializer type")?;
        let place = self.variables[symbol.0].place;
        let mut flow = self.current_flow.clone();
        self.mark_initialized(&mut flow, place);
        self.current_flow = flow;
        let operation = self.add_operation(
            Operation::Initialize {
                symbol,
                value: converted,
            },
            source,
        );
        self.scopes[scope.0].operations.push(operation);
        self.active_initializer = None;
        Ok(())
    }

    pub fn finish_block(
        &mut self,
        scope: ScopeId,
        entered: bool,
        source: InputSpan,
    ) -> crate::model::OpId {
        let body = self.scopes[scope.0].operations.clone();
        let block = self.add_operation(Operation::Block { scope, body }, source);
        if !entered && let Some(function) = self.active_function.take() {
            self.functions[function.0].body = Some(block);
            if self.functions[function.0].result != Type::Void
                && self.current_flow.reachable
                && !self.active_function_error
            {
                self.record_error("missing return statement", source);
            }
            self.current_flow = self.global_flow.clone();
        }
        block
    }

    pub fn binary_expr(
        &mut self,
        operator: Operator,
        left: ExprId,
        right: ExprId,
        source: InputSpan,
    ) -> Result<ExprId, &'static str> {
        self.binary(operator, left, right, source)
    }

    pub fn integer_literal(
        &mut self,
        value: &str,
        source: InputSpan,
    ) -> Result<ExprId, &'static str> {
        let number = value
            .parse::<i32>()
            .map_err(|_| "integer literal out of range")?;
        Ok(self.add_expression(
            ExpressionKind::Constant(Constant::Int(number)),
            Type::Int,
            source,
        ))
    }

    pub fn float_literal(
        &mut self,
        value: &str,
        source: InputSpan,
    ) -> Result<ExprId, &'static str> {
        let number = value
            .parse::<f32>()
            .map_err(|_| "invalid floating literal")?;
        if !number.is_finite() {
            return Err("invalid floating literal");
        }
        Ok(self.add_expression(
            ExpressionKind::Constant(Constant::Float(number)),
            Type::Float,
            source,
        ))
    }

    pub fn char_literal(&mut self, value: &str, source: InputSpan) -> Result<ExprId, &'static str> {
        let content = value
            .strip_prefix('\'')
            .and_then(|value| value.strip_suffix('\''))
            .ok_or("invalid character literal")?;
        let bytes = decode_literal(content).ok_or("invalid character literal")?;
        if bytes.len() != 1 {
            return Err("invalid character literal");
        }
        Ok(self.add_expression(
            ExpressionKind::Constant(Constant::Char(bytes[0])),
            Type::Char,
            source,
        ))
    }

    pub fn string_literal(
        &mut self,
        value: &str,
        source: InputSpan,
    ) -> Result<ExprId, &'static str> {
        let content = value
            .strip_prefix('"')
            .and_then(|value| value.strip_suffix('"'))
            .ok_or("invalid string literal")?;
        let bytes = decode_literal(content).ok_or("invalid string literal")?;
        let decoded = String::from_utf8(bytes).map_err(|_| "invalid string literal")?;
        Ok(self.add_expression(
            ExpressionKind::Constant(Constant::String(decoded)),
            Type::CharPointer,
            source,
        ))
    }

    pub fn bool_literal_expr(&mut self, value: bool, source: InputSpan) -> ExprId {
        self.add_expression(
            ExpressionKind::Constant(Constant::Bool(value)),
            Type::Bool,
            source,
        )
    }

    pub fn null_literal(&mut self, source: InputSpan) -> ExprId {
        self.add_expression(
            ExpressionKind::Constant(Constant::Null),
            Type::NullPointer,
            source,
        )
    }

    pub fn negate_expr(
        &mut self,
        value: ExprId,
        source: InputSpan,
    ) -> Result<ExprId, &'static str> {
        if self.poisoned_expression(value) {
            return Ok(value);
        }
        let ty = self.expressions[value.0].ty.clone();
        let (value, ty) = match ty {
            Type::Char => (self.convert(value, Type::Int)?, Type::Int),
            Type::Int => (value, Type::Int),
            Type::Float => (value, Type::Float),
            _ => return Err("invalid unary operand"),
        };
        Ok(self.add_expression(ExpressionKind::Negate(value), ty, source))
    }

    pub fn lookup_place(&self, scope: ScopeId, name: &str) -> Result<PlaceId, &'static str> {
        match self.lookup_ordinary(scope, name) {
            Some(Binding::Variable(symbol)) => Ok(self.variables[symbol.0].place),
            _ => Err("variable not declared"),
        }
    }

    pub fn named_field(&mut self, base: PlaceId, name: &str) -> Result<PlaceId, &'static str> {
        let Type::Struct(structure) = &self.places[base.0].ty else {
            return Err("member access on non-struct");
        };
        if !self.structs[structure.0].complete {
            return Err("member access on non-struct");
        }
        let field = self
            .lookup_field(*structure, name)
            .ok_or("unknown struct field")?;
        Ok(self.field_place(base, field))
    }

    pub fn load_place(
        &mut self,
        place: PlaceId,
        source: InputSpan,
    ) -> Result<ExprId, &'static str> {
        let ty = self.places[place.0].ty.clone();
        if matches!(ty, Type::Array(_, _)) {
            return Err("array value is not supported");
        }
        self.read(&self.current_flow, place)?;
        Ok(self.add_expression(ExpressionKind::Load(place), ty, source))
    }

    pub fn load_indexed(
        &mut self,
        array: PlaceId,
        index: ExprId,
        source: InputSpan,
    ) -> Result<ExprId, &'static str> {
        if self.poisoned_expression(index) {
            return Ok(index);
        }
        if self.expressions[index.0].ty != Type::Int {
            return Err("array index must be int");
        }
        let Type::Array(element, _) = &self.places[array.0].ty else {
            return Err("indexing non-array");
        };
        let ty = element.as_ref().clone();
        Ok(self.add_expression(ExpressionKind::IndexLoad { array, index }, ty, source))
    }

    pub fn store_indexed(
        &mut self,
        array: PlaceId,
        index: ExprId,
        value: ExprId,
        source: InputSpan,
    ) -> Result<crate::model::OpId, &'static str> {
        if self.poisoned_expression(index) || self.poisoned_expression(value) {
            return Err("dependent error");
        }
        if self.expressions[index.0].ty != Type::Int {
            return Err("array index must be int");
        }
        let Type::Array(element, _) = &self.places[array.0].ty else {
            return Err("indexing non-array");
        };
        let ty = element.as_ref().clone();
        let value = self
            .convert(value, ty)
            .map_err(|_| "incompatible assignment types")?;
        Ok(self.add_operation(
            Operation::IndexStore {
                array,
                index,
                value,
            },
            source,
        ))
    }

    pub fn assign_indexed(
        &mut self,
        array: PlaceId,
        index: ExprId,
        assignment: AssignmentOp,
        value: ExprId,
        source: InputSpan,
    ) -> Result<crate::model::OpId, &'static str> {
        if assignment == AssignmentOp::Set {
            return self.store_indexed(array, index, value, source);
        }
        if self.poisoned_expression(index) || self.poisoned_expression(value) {
            return Err("dependent error");
        }
        if self.expressions[index.0].ty != Type::Int {
            return Err("array index must be int");
        }
        let Type::Array(element, _) = &self.places[array.0].ty else {
            return Err("indexing non-array");
        };
        if !matches!(element.as_ref(), Type::Int | Type::Float | Type::Char)
            || !matches!(
                self.expressions[value.0].ty,
                Type::Int | Type::Float | Type::Char
            )
        {
            return Err("incompatible assignment types");
        }
        let operator = match assignment {
            AssignmentOp::Add => Operator::Add,
            AssignmentOp::Subtract => Operator::Subtract,
            AssignmentOp::Multiply => Operator::Multiply,
            AssignmentOp::Divide => Operator::Divide,
            AssignmentOp::Set => unreachable!(),
        };
        Ok(self.add_operation(
            Operation::IndexUpdate {
                array,
                index,
                operator,
                value,
            },
            source,
        ))
    }

    pub fn increment_indexed(
        &mut self,
        array: PlaceId,
        index: ExprId,
        increment: bool,
        source: InputSpan,
    ) -> Result<crate::model::OpId, &'static str> {
        let Type::Array(element, _) = &self.places[array.0].ty else {
            return Err("indexing non-array");
        };
        if !matches!(element.as_ref(), Type::Int | Type::Float | Type::Char) {
            return Err("invalid increment operand");
        }
        let one = self.integer_literal("1", source)?;
        self.assign_indexed(
            array,
            index,
            if increment {
                AssignmentOp::Add
            } else {
                AssignmentOp::Subtract
            },
            one,
            source,
        )
    }

    pub fn lookup_function(&self, scope: ScopeId, name: &str) -> Result<FunctionId, &'static str> {
        match self.lookup_ordinary(scope, name) {
            Some(Binding::Function(function)) => Ok(function),
            _ => Err("function not declared"),
        }
    }

    pub fn create_call(
        &mut self,
        function: FunctionId,
        arguments: &[ExprId],
        source: InputSpan,
    ) -> Result<ExprId, &'static str> {
        if let Some(&poisoned) = arguments
            .iter()
            .find(|&&value| self.poisoned_expression(value))
        {
            return Ok(poisoned);
        }
        let signature = self.functions[function.0].clone();
        if arguments.len() != signature.parameters.len() {
            return Err("wrong number of arguments");
        }
        let converted = arguments
            .iter()
            .zip(signature.parameters)
            .map(|(&value, ty)| {
                self.convert(value, ty)
                    .map_err(|_| "incompatible argument type")
            })
            .collect::<Result<Vec<_>, _>>()?;
        Ok(self.add_expression(
            ExpressionKind::Call {
                function,
                arguments: converted,
            },
            signature.result,
            source,
        ))
    }

    pub fn require_value(&self, value: ExprId) -> Result<ExprId, &'static str> {
        if self.poisoned_expression(value) {
            return Ok(value);
        }
        if self.expressions[value.0].ty == Type::Void {
            Err("void value used as expression")
        } else {
            Ok(value)
        }
    }
}

fn decode_literal(content: &str) -> Option<Vec<u8>> {
    let mut decoded = Vec::new();
    let mut bytes = content.bytes();
    while let Some(value) = bytes.next() {
        if value != b'\\' {
            decoded.push(value);
            continue;
        }
        let escaped = match bytes.next()? {
            b'b' => b'\x08',
            b't' => b'\t',
            b'n' => b'\n',
            b'f' => b'\x0c',
            b'r' => b'\r',
            b'"' => b'"',
            b'\'' => b'\'',
            b'\\' => b'\\',
            _ => return None,
        };
        decoded.push(escaped);
    }
    Some(decoded)
}
