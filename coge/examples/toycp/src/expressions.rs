use crate::ast::InputSpan;
use crate::model::{
    Binding, Constant, Context, ExprId, ExpressionKind, FunctionId, Initialization, Operation,
    Operator, PlaceId, ScopeId, SymbolId, Type,
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
        let mut body = self.scopes[scope.0].operations.clone();
        let symbols = self.scopes[scope.0].symbols.clone();
        for symbol in symbols.into_iter().rev() {
            if !self.variables[symbol.0].is_self
                && self.type_needs_destruction(&self.variables[symbol.0].ty)
            {
                body.push(self.add_operation(Operation::Destroy(symbol), source));
            }
        }
        let block = self.add_operation(Operation::Block { scope, body }, source);
        if self.active_scopes.last() == Some(&scope) {
            self.active_scopes.pop();
        }
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

    pub fn lookup_place(&mut self, scope: ScopeId, name: &str) -> Result<PlaceId, &'static str> {
        let mut current = Some(scope);
        while let Some(id) = current {
            if self.scopes[id.0].parent.is_none() {
                break;
            }
            if let Some(Binding::Variable(symbol)) = self.scopes[id.0].ordinary.get(name) {
                return Ok(self.variables[symbol.0].place);
            }
            current = self.scopes[id.0].parent;
        }
        if let Some(owner) = self.active_owner() {
            if let Some(path) = self.field_path(owner, name) {
                self.check_field_access(&path)?;
                let mut place = self.current_self_place()?;
                for field in path {
                    place = self.field_place(place, field);
                }
                return Ok(place);
            }
        }
        match self.lookup_ordinary(scope, name) {
            Some(Binding::Variable(symbol)) => Ok(self.variables[symbol.0].place),
            _ => Err("variable not declared"),
        }
    }

    pub fn named_field(
        &mut self,
        _scope: ScopeId,
        base: PlaceId,
        name: &str,
    ) -> Result<PlaceId, &'static str> {
        let Type::Struct(structure) = &self.places[base.0].ty else {
            return Err("member access on non-struct");
        };
        if !self.structs[structure.0].complete {
            return Err("member access on non-struct");
        }
        let path = self
            .field_path(*structure, name)
            .ok_or("unknown struct field")?;
        self.check_field_access(&path)?;
        let mut place = base;
        for field in path {
            place = self.field_place(place, field);
        }
        Ok(place)
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
        let runtime_index = self.places[place.0]
            .key
            .fields
            .iter()
            .any(|field| self.fields[field.0].index_expression.is_some());
        if !runtime_index {
            self.read(&self.current_flow, place)?;
        }
        Ok(self.add_expression(ExpressionKind::Load(place), ty, source))
    }

    pub fn lookup_function(&self, scope: ScopeId, name: &str) -> Result<FunctionId, &'static str> {
        if let Some(owner) = self.active_owner() {
            if let Some(function) = self.method_in(owner, name) {
                return Ok(function);
            }
        }
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
        if signature.owner.is_some() {
            let receiver = self.current_self_place()?;
            return self.create_method_call(function, receiver, arguments, source);
        }
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

    pub fn lookup_method(
        &self,
        _scope: ScopeId,
        receiver: PlaceId,
        name: &str,
    ) -> Result<FunctionId, &'static str> {
        let Type::Struct(owner) = self.places[receiver.0].ty else {
            return Err("method call on non-class");
        };
        let function = self.method_in(owner, name).ok_or("method not declared")?;
        let member = &self.functions[function.0];
        let target = member.owner.unwrap_or(owner);
        if !self.accessible(member.visibility, target) {
            return Err("member is not accessible");
        }
        let mut current = owner;
        while current != target {
            let base_field = *self.structs[current.0]
                .fields
                .first()
                .ok_or("method owner does not match receiver")?;
            self.check_field_access(&[base_field])?;
            current = self.structs[current.0]
                .base
                .ok_or("method owner does not match receiver")?;
        }
        Ok(function)
    }

    pub fn create_method_call(
        &mut self,
        function: FunctionId,
        receiver: PlaceId,
        arguments: &[ExprId],
        source: InputSpan,
    ) -> Result<ExprId, &'static str> {
        let signature = self.functions[function.0].clone();
        if signature.owner.is_none() {
            return Err("method not declared");
        }
        if arguments.len() + 1 != signature.parameters.len() {
            return Err("wrong number of arguments");
        }
        let converted = arguments
            .iter()
            .zip(signature.parameters.into_iter().skip(1))
            .map(|(&value, ty)| {
                self.convert(value, ty)
                    .map_err(|_| "incompatible argument type")
            })
            .collect::<Result<Vec<_>, _>>()?;
        Ok(self.add_expression(
            ExpressionKind::MethodCall {
                function,
                receiver,
                arguments: converted,
            },
            signature.result,
            source,
        ))
    }

    pub fn index_place(
        &mut self,
        base: PlaceId,
        index: ExprId,
        source: InputSpan,
    ) -> Result<PlaceId, &'static str> {
        let Type::Array(element, length) = self.places[base.0].ty.clone() else {
            return Err("indexing non-array");
        };
        let converted = self
            .convert(index, Type::Int)
            .map_err(|_| "invalid array index")?;
        let offset = if let ExpressionKind::Constant(Constant::Int(number)) =
            self.expressions[converted.0].kind
        {
            let offset = usize::try_from(number).map_err(|_| "array index out of bounds")?;
            if offset >= length {
                return Err("array index out of bounds");
            }
            offset
        } else {
            0
        };
        let field = crate::model::FieldId(self.fields.len());
        self.fields.push(crate::model::Field {
            name: format!("[{offset}]"),
            ty: (*element).clone(),
            owner: crate::model::StructId(usize::MAX),
            ordinal: offset,
            source,
            visibility: crate::model::Visibility::Public,
            index_expression: Some(converted),
        });
        Ok(self.field_place(base, field))
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
