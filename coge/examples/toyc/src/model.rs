use std::collections::HashMap;

use crate::ast::InputSpan;

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct ScopeId(pub usize);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct SymbolId(pub usize);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct FunctionId(pub usize);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct StructId(pub usize);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct FieldId(pub usize);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct PlaceId(pub usize);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct ExprId(pub usize);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct OpId(pub usize);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct FlowId(pub usize);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct ModuleId(pub usize);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct ParameterId(pub usize);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DeclarationTarget {
    Ordinary,
    Field(StructId),
}

#[derive(Clone, Debug)]
pub struct Parameter {
    pub name: String,
    pub ty: Type,
    pub source: InputSpan,
}

#[derive(Clone, Debug)]
pub struct FunctionHeader {
    pub name: String,
    pub result: Type,
    pub parameters: Vec<ParameterId>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Type {
    Int,
    Float,
    Bool,
    Char,
    CharPointer,
    Void,
    Struct(StructId),
    Array(Box<Type>, usize),
    NullPointer,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Initialization {
    Uninitialized,
    Initializing,
    Initialized,
    Poisoned,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Binding {
    Variable(SymbolId),
    Function(FunctionId),
}

#[derive(Clone, Debug)]
pub struct Scope {
    pub parent: Option<ScopeId>,
    pub source: InputSpan,
    pub ordinary: HashMap<String, Binding>,
    pub tags: HashMap<String, StructId>,
    pub symbols: Vec<SymbolId>,
    pub operations: Vec<OpId>,
}

#[derive(Clone, Debug)]
pub struct Variable {
    pub name: String,
    pub ty: Type,
    pub owner: ScopeId,
    pub place: PlaceId,
    pub source: InputSpan,
}

#[derive(Clone, Debug)]
pub struct Function {
    pub name: String,
    pub result: Type,
    pub parameters: Vec<Type>,
    pub defined: bool,
    pub scope: Option<ScopeId>,
    pub body: Option<OpId>,
    pub source: InputSpan,
}

#[derive(Clone, Debug)]
pub struct Struct {
    pub name: String,
    pub fields: Vec<FieldId>,
    pub complete: bool,
    pub source: InputSpan,
}

#[derive(Clone, Debug)]
pub struct Field {
    pub name: String,
    pub ty: Type,
    pub owner: StructId,
    pub ordinal: usize,
    pub source: InputSpan,
}

#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub struct PlaceKey {
    pub root: SymbolId,
    pub fields: Vec<FieldId>,
}

#[derive(Clone, Debug)]
pub struct Place {
    pub key: PlaceKey,
    pub ty: Type,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Diagnostic {
    pub message: String,
    pub source: InputSpan,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SourceComment {
    pub text: String,
    pub source: InputSpan,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Operator {
    Add,
    Subtract,
    Multiply,
    Divide,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Equal,
    NotEqual,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AssignmentOp {
    Set,
    Add,
    Subtract,
    Multiply,
    Divide,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Constant {
    Int(i32),
    Float(f32),
    Bool(bool),
    Char(u8),
    String(String),
    Null,
}

#[derive(Clone, Debug, PartialEq)]
pub enum ExpressionKind {
    Error,
    Constant(Constant),
    Load(PlaceId),
    IndexLoad {
        array: PlaceId,
        index: ExprId,
    },
    Convert {
        value: ExprId,
        target: Type,
    },
    Negate(ExprId),
    Binary {
        operator: Operator,
        left: ExprId,
        right: ExprId,
    },
    Call {
        function: FunctionId,
        arguments: Vec<ExprId>,
    },
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum RuntimeCheck {
    Nonzero(ExprId),
}

#[derive(Clone, Debug, PartialEq)]
pub struct Expression {
    pub kind: ExpressionKind,
    pub ty: Type,
    pub source: InputSpan,
    pub checks: Vec<RuntimeCheck>,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Operation {
    Declare(SymbolId),
    Initialize {
        symbol: SymbolId,
        value: ExprId,
    },
    Store {
        place: PlaceId,
        value: ExprId,
    },
    IndexStore {
        array: PlaceId,
        index: ExprId,
        value: ExprId,
    },
    IndexUpdate {
        array: PlaceId,
        index: ExprId,
        operator: Operator,
        value: ExprId,
    },
    Call(ExprId),
    Block {
        scope: ScopeId,
        body: Vec<OpId>,
    },
    If {
        condition: ExprId,
        then_branch: OpId,
        else_branch: Option<OpId>,
    },
    While {
        condition: ExprId,
        body: OpId,
    },
    For {
        scope: ScopeId,
        initialization: OpId,
        condition: ExprId,
        body: OpId,
        update: OpId,
    },
    Return(Option<ExprId>),
    Error,
}

#[derive(Clone, Debug, PartialEq)]
pub struct OperationNode {
    pub kind: Operation,
    pub source: InputSpan,
}

#[derive(Clone, Debug)]
pub struct Module {
    pub scope: ScopeId,
    pub declarations: Vec<OpId>,
    pub valid: bool,
}

#[derive(Clone, Debug, Default)]
pub struct Flow {
    pub states: HashMap<PlaceId, Initialization>,
    pub reachable: bool,
}

impl Flow {
    pub fn reachable() -> Self {
        Self {
            states: HashMap::new(),
            reachable: true,
        }
    }

    pub fn state(&self, place: PlaceId) -> Initialization {
        self.states
            .get(&place)
            .copied()
            .unwrap_or(Initialization::Uninitialized)
    }

    pub fn set(&mut self, place: PlaceId, state: Initialization) {
        self.states.insert(place, state);
    }
}

#[derive(Default)]
pub struct Context {
    pub scopes: Vec<Scope>,
    pub variables: Vec<Variable>,
    pub functions: Vec<Function>,
    pub parameters: Vec<Parameter>,
    pub structs: Vec<Struct>,
    pub fields: Vec<Field>,
    pub places: Vec<Place>,
    pub diagnostics: Vec<Diagnostic>,
    pub comments: Vec<SourceComment>,
    pub expressions: Vec<Expression>,
    pub operations: Vec<OperationNode>,
    pub modules: Vec<Module>,
    pub global_flow: Flow,
    pub current_flow: Flow,
    pub flow_snapshots: Vec<Flow>,
    pub active_initializer: Option<SymbolId>,
    pub active_function: Option<FunctionId>,
    pub active_function_error: bool,
    pub module_declarations: Vec<OpId>,
    place_index: HashMap<PlaceKey, PlaceId>,
}

impl Context {
    pub fn poisoned_expression(&self, value: ExprId) -> bool {
        matches!(self.expressions[value.0].kind, ExpressionKind::Error)
    }

    pub fn complete_object_type(&self, ty: &Type) -> bool {
        match ty {
            Type::Int | Type::Float | Type::Bool | Type::Char | Type::CharPointer => true,
            Type::Struct(id) => self.structs[id.0].complete,
            Type::Array(element, length) => *length > 0 && self.complete_object_type(element),
            Type::Void | Type::NullPointer => false,
        }
    }

    pub fn new_scope(&mut self, parent: Option<ScopeId>, source: InputSpan) -> ScopeId {
        let id = ScopeId(self.scopes.len());
        self.scopes.push(Scope {
            parent,
            source,
            ordinary: HashMap::new(),
            tags: HashMap::new(),
            symbols: Vec::new(),
            operations: Vec::new(),
        });
        id
    }

    pub fn lookup_ordinary(&self, scope: ScopeId, name: &str) -> Option<Binding> {
        let mut current = Some(scope);
        while let Some(id) = current {
            if let Some(binding) = self.scopes[id.0].ordinary.get(name) {
                return Some(*binding);
            }
            current = self.scopes[id.0].parent;
        }
        None
    }

    pub fn lookup_tag(&self, scope: ScopeId, name: &str) -> Option<StructId> {
        let mut current = Some(scope);
        while let Some(id) = current {
            if let Some(tag) = self.scopes[id.0].tags.get(name) {
                return Some(*tag);
            }
            current = self.scopes[id.0].parent;
        }
        None
    }

    pub fn declare_variable(
        &mut self,
        scope: ScopeId,
        name: &str,
        ty: Type,
        source: InputSpan,
    ) -> Result<SymbolId, Diagnostic> {
        if self.scopes[scope.0].ordinary.contains_key(name) {
            return Err(Diagnostic {
                message: "identifier already declared".to_owned(),
                source,
            });
        }
        let symbol = SymbolId(self.variables.len());
        let place = self.intern_place(
            PlaceKey {
                root: symbol,
                fields: Vec::new(),
            },
            ty.clone(),
        );
        self.variables.push(Variable {
            name: name.to_owned(),
            ty,
            owner: scope,
            place,
            source,
        });
        self.scopes[scope.0]
            .ordinary
            .insert(name.to_owned(), Binding::Variable(symbol));
        self.scopes[scope.0].symbols.push(symbol);
        Ok(symbol)
    }

    pub fn declare_function(
        &mut self,
        scope: ScopeId,
        name: &str,
        result: Type,
        parameters: Vec<Type>,
        definition: bool,
        source: InputSpan,
    ) -> Result<FunctionId, Diagnostic> {
        if let Some(existing) = self.scopes[scope.0].ordinary.get(name).copied() {
            let Binding::Function(id) = existing else {
                return Err(Diagnostic {
                    message: "conflicting function declaration".to_owned(),
                    source,
                });
            };
            let function = &mut self.functions[id.0];
            if function.result != result
                || function.parameters != parameters
                || (definition && function.defined)
            {
                return Err(Diagnostic {
                    message: "conflicting function declaration".to_owned(),
                    source,
                });
            }
            function.defined |= definition;
            return Ok(id);
        }
        let id = FunctionId(self.functions.len());
        self.functions.push(Function {
            name: name.to_owned(),
            result,
            parameters,
            defined: definition,
            scope: None,
            body: None,
            source,
        });
        self.scopes[scope.0]
            .ordinary
            .insert(name.to_owned(), Binding::Function(id));
        Ok(id)
    }

    pub fn declare_struct(
        &mut self,
        scope: ScopeId,
        name: &str,
        source: InputSpan,
    ) -> Result<StructId, Diagnostic> {
        if self.scopes[scope.0].tags.contains_key(name) {
            return Err(Diagnostic {
                message: "struct already declared".to_owned(),
                source,
            });
        }
        let id = StructId(self.structs.len());
        self.structs.push(Struct {
            name: name.to_owned(),
            fields: Vec::new(),
            complete: false,
            source,
        });
        self.scopes[scope.0].tags.insert(name.to_owned(), id);
        Ok(id)
    }

    pub fn declare_field(
        &mut self,
        owner: StructId,
        name: &str,
        ty: Type,
        source: InputSpan,
    ) -> Result<FieldId, Diagnostic> {
        if self.structs[owner.0].complete {
            return Err(Diagnostic {
                message: "struct already complete".to_owned(),
                source,
            });
        }
        if !self.complete_object_type(&ty) {
            return Err(Diagnostic {
                message: "invalid field type".to_owned(),
                source,
            });
        }
        if self.structs[owner.0]
            .fields
            .iter()
            .any(|id| self.fields[id.0].name == name)
        {
            return Err(Diagnostic {
                message: "identifier already declared".to_owned(),
                source,
            });
        }
        let ordinal = self.structs[owner.0].fields.len();
        let id = FieldId(self.fields.len());
        self.fields.push(Field {
            name: name.to_owned(),
            ty,
            owner,
            ordinal,
            source,
        });
        self.structs[owner.0].fields.push(id);
        Ok(id)
    }

    pub fn complete_struct(&mut self, id: StructId) {
        self.structs[id.0].complete = true;
    }

    pub fn lookup_field(&self, owner: StructId, name: &str) -> Option<FieldId> {
        self.structs[owner.0]
            .fields
            .iter()
            .copied()
            .find(|id| self.fields[id.0].name == name)
    }

    pub fn intern_place(&mut self, key: PlaceKey, ty: Type) -> PlaceId {
        if let Some(id) = self.place_index.get(&key) {
            return *id;
        }
        let id = PlaceId(self.places.len());
        self.places.push(Place {
            key: key.clone(),
            ty,
        });
        self.place_index.insert(key, id);
        id
    }

    pub fn field_place(&mut self, base: PlaceId, field: FieldId) -> PlaceId {
        let mut key = self.places[base.0].key.clone();
        key.fields.push(field);
        self.intern_place(key, self.fields[field.0].ty.clone())
    }

    fn effective_state(&self, flow: &Flow, place: PlaceId) -> Initialization {
        if flow.state(place) == Initialization::Poisoned {
            return Initialization::Poisoned;
        }
        if flow.state(place) == Initialization::Initialized {
            return Initialization::Initialized;
        }
        let key = &self.places[place.0].key;
        for length in 0..key.fields.len() {
            let ancestor = PlaceKey {
                root: key.root,
                fields: key.fields[..length].to_vec(),
            };
            if let Some(id) = self.place_index.get(&ancestor)
                && flow.state(*id) == Initialization::Initialized
            {
                return Initialization::Initialized;
            }
        }
        if let Type::Struct(owner) = &self.places[place.0].ty
            && !self.structs[owner.0].fields.is_empty()
            && self.structs[owner.0].fields.iter().all(|field| {
                let mut child = key.clone();
                child.fields.push(*field);
                self.place_index.get(&child).is_some_and(|id| {
                    self.effective_state(flow, *id) == Initialization::Initialized
                })
            })
        {
            return Initialization::Initialized;
        }
        flow.state(place)
    }

    pub fn read(&self, flow: &Flow, place: PlaceId) -> Result<(), &'static str> {
        if !flow.reachable {
            return Ok(());
        }
        match self.effective_state(flow, place) {
            Initialization::Initialized => Ok(()),
            Initialization::Poisoned => Err("dependent error"),
            Initialization::Initializing | Initialization::Uninitialized => {
                Err("variable used before initialization")
            }
        }
    }

    pub fn merge_flow(&self, left: &Flow, right: &Flow) -> Flow {
        if !left.reachable {
            return right.clone();
        }
        if !right.reachable {
            return left.clone();
        }
        let mut merged = Flow::reachable();
        for index in 0..self.places.len() {
            let place = PlaceId(index);
            let lhs = self.effective_state(left, place);
            let rhs = self.effective_state(right, place);
            let state = if lhs == Initialization::Poisoned || rhs == Initialization::Poisoned {
                Initialization::Poisoned
            } else if lhs == Initialization::Initialized && rhs == Initialization::Initialized {
                Initialization::Initialized
            } else {
                Initialization::Uninitialized
            };
            merged.set(place, state);
        }
        merged
    }

    pub fn mark_initialized(&self, flow: &mut Flow, place: PlaceId) {
        flow.set(place, Initialization::Initialized);
        let key = &self.places[place.0].key;
        for (other_key, other_id) in &self.place_index {
            if other_key.root == key.root && other_key.fields.starts_with(&key.fields) {
                flow.set(*other_id, Initialization::Initialized);
            }
        }
    }

    pub fn add_expression(&mut self, kind: ExpressionKind, ty: Type, source: InputSpan) -> ExprId {
        let id = ExprId(self.expressions.len());
        self.expressions.push(Expression {
            kind,
            ty,
            source,
            checks: Vec::new(),
        });
        id
    }

    pub fn add_operation(&mut self, kind: Operation, source: InputSpan) -> OpId {
        let id = OpId(self.operations.len());
        self.operations.push(OperationNode { kind, source });
        id
    }

    pub fn add_module(&mut self, scope: ScopeId, declarations: Vec<OpId>) -> ModuleId {
        let id = ModuleId(self.modules.len());
        self.modules.push(Module {
            scope,
            declarations,
            valid: self.diagnostics.is_empty(),
        });
        id
    }

    pub fn conversion_allowed(source: &Type, target: &Type) -> bool {
        source == target
            || matches!(
                (source, target),
                (Type::Char, Type::Int | Type::Float)
                    | (Type::Int, Type::Float)
                    | (Type::NullPointer, Type::CharPointer)
            )
    }

    pub fn convert(&mut self, value: ExprId, target: Type) -> Result<ExprId, &'static str> {
        if self.poisoned_expression(value) {
            return Ok(value);
        }
        let source = self.expressions[value.0].ty.clone();
        if source == target {
            return Ok(value);
        }
        if !Self::conversion_allowed(&source, &target) {
            return Err("incompatible conversion");
        }
        let span = self.expressions[value.0].source;
        Ok(self.add_expression(
            ExpressionKind::Convert {
                value,
                target: target.clone(),
            },
            target,
            span,
        ))
    }

    fn numeric(ty: &Type) -> bool {
        matches!(ty, Type::Int | Type::Float | Type::Char)
    }

    pub fn binary(
        &mut self,
        operator: Operator,
        left: ExprId,
        right: ExprId,
        source: InputSpan,
    ) -> Result<ExprId, &'static str> {
        if self.poisoned_expression(left) {
            return Ok(left);
        }
        if self.poisoned_expression(right) {
            return Ok(right);
        }
        let lhs = self.expressions[left.0].ty.clone();
        let rhs = self.expressions[right.0].ty.clone();
        let numeric_pair = Self::numeric(&lhs) && Self::numeric(&rhs);
        let equality = matches!(operator, Operator::Equal | Operator::NotEqual);
        let comparison = matches!(
            operator,
            Operator::Less
                | Operator::LessEqual
                | Operator::Greater
                | Operator::GreaterEqual
                | Operator::Equal
                | Operator::NotEqual
        );
        let pointer_pair = matches!(
            (&lhs, &rhs),
            (Type::CharPointer, Type::CharPointer | Type::NullPointer)
                | (Type::NullPointer, Type::CharPointer | Type::NullPointer)
        );
        if !numeric_pair && !(equality && (pointer_pair || lhs == Type::Bool && rhs == Type::Bool))
        {
            return Err(if comparison {
                "incompatible comparison operands"
            } else {
                "invalid arithmetic operands"
            });
        }
        let (left, right, ty) = if numeric_pair {
            let common = if lhs == Type::Float || rhs == Type::Float {
                Type::Float
            } else {
                Type::Int
            };
            let left = self.convert(left, common.clone())?;
            let right = self.convert(right, common.clone())?;
            (left, right, if comparison { Type::Bool } else { common })
        } else if pointer_pair {
            let left = self.convert(left, Type::CharPointer)?;
            let right = self.convert(right, Type::CharPointer)?;
            (left, right, Type::Bool)
        } else {
            (left, right, Type::Bool)
        };
        let result = self.add_expression(
            ExpressionKind::Binary {
                operator,
                left,
                right,
            },
            ty,
            source,
        );
        if operator == Operator::Divide && self.expressions[result.0].ty == Type::Int {
            self.expressions[result.0]
                .checks
                .push(RuntimeCheck::Nonzero(right));
        }
        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn span(byte: u64) -> InputSpan {
        InputSpan {
            begin_byte: byte,
            end_byte: byte + 1,
        }
    }

    #[test]
    fn lexical_lookup_and_namespaces() {
        let mut context = Context::default();
        let root = context.new_scope(None, span(0));
        let outer = context
            .declare_variable(root, "x", Type::Int, span(1))
            .unwrap();
        context.declare_struct(root, "x", span(2)).unwrap();
        let child = context.new_scope(Some(root), span(3));
        assert_eq!(
            context.lookup_ordinary(child, "x"),
            Some(Binding::Variable(outer))
        );
        assert!(context.lookup_tag(child, "x").is_some());
        let inner = context
            .declare_variable(child, "x", Type::Int, span(4))
            .unwrap();
        assert_eq!(
            context.lookup_ordinary(child, "x"),
            Some(Binding::Variable(inner))
        );
        assert_eq!(
            context
                .declare_variable(child, "x", Type::Int, span(5))
                .unwrap_err()
                .message,
            "identifier already declared"
        );
    }

    #[test]
    fn field_places_have_stable_identity_and_flow_merges_by_intersection() {
        let mut context = Context::default();
        let root = context.new_scope(None, span(0));
        let structure = context.declare_struct(root, "Pair", span(1)).unwrap();
        let field = context
            .declare_field(structure, "left", Type::Int, span(2))
            .unwrap();
        context.complete_struct(structure);
        let symbol = context
            .declare_variable(root, "p", Type::Struct(structure), span(3))
            .unwrap();
        let place = context.field_place(context.variables[symbol.0].place, field);
        assert_eq!(
            context.field_place(context.variables[symbol.0].place, field),
            place
        );
        let mut then_flow = Flow::reachable();
        context.mark_initialized(&mut then_flow, place);
        assert!(context.read(&then_flow, place).is_ok());
        let else_flow = Flow::reachable();
        let merged = context.merge_flow(&then_flow, &else_flow);
        assert_eq!(
            context.read(&merged, place),
            Err("variable used before initialization")
        );
        let joined_with_return = context.merge_flow(&then_flow, &Flow::default());
        assert!(context.read(&joined_with_return, place).is_ok());
    }

    #[test]
    fn struct_fields_are_unique_and_require_complete_object_types() {
        let mut context = Context::default();
        let root = context.new_scope(None, span(0));
        let inner = context.declare_struct(root, "Inner", span(1)).unwrap();
        let outer = context.declare_struct(root, "Outer", span(2)).unwrap();
        assert!(!context.complete_object_type(&Type::Struct(inner)));
        assert_eq!(
            context
                .declare_field(outer, "nested", Type::Struct(inner), span(3))
                .unwrap_err()
                .message,
            "invalid field type"
        );
        assert_eq!(
            context
                .declare_field(outer, "recursive", Type::Struct(outer), span(4))
                .unwrap_err()
                .message,
            "invalid field type"
        );
        context.complete_struct(inner);
        let nested = context
            .declare_field(outer, "nested", Type::Struct(inner), span(5))
            .unwrap();
        assert_eq!(context.fields[nested.0].ordinal, 0);
        assert_eq!(context.lookup_field(outer, "nested"), Some(nested));
        assert_eq!(
            context
                .declare_field(outer, "nested", Type::Int, span(6))
                .unwrap_err()
                .message,
            "identifier already declared"
        );
        context.complete_struct(outer);
        assert!(context.complete_object_type(&Type::Array(Box::new(Type::Struct(outer)), 2)));
        assert_eq!(
            context
                .declare_field(outer, "late", Type::Int, span(7))
                .unwrap_err()
                .message,
            "struct already complete"
        );
    }

    #[test]
    fn initialized_struct_fields_merge_with_a_whole_object_write() {
        let mut context = Context::default();
        let scope = context.new_scope(None, span(0));
        let structure = context.declare_struct(scope, "Pair", span(1)).unwrap();
        let left = context
            .declare_field(structure, "left", Type::Int, span(2))
            .unwrap();
        let right = context
            .declare_field(structure, "right", Type::Int, span(3))
            .unwrap();
        context.complete_struct(structure);
        let symbol = context
            .declare_variable(scope, "pair", Type::Struct(structure), span(4))
            .unwrap();
        let root = context.variables[symbol.0].place;
        let left_place = context.field_place(root, left);
        let right_place = context.field_place(root, right);
        let mut whole = Flow::reachable();
        context.mark_initialized(&mut whole, root);
        let mut fields = Flow::reachable();
        context.mark_initialized(&mut fields, left_place);
        assert_eq!(
            context.read(&fields, root),
            Err("variable used before initialization")
        );
        context.mark_initialized(&mut fields, right_place);
        assert!(context.read(&fields, root).is_ok());
        let merged = context.merge_flow(&whole, &fields);
        assert!(context.read(&merged, root).is_ok());
        assert!(context.read(&merged, left_place).is_ok());
        assert!(context.read(&merged, right_place).is_ok());
    }

    #[test]
    fn arithmetic_records_conversions_and_runtime_checks() {
        let mut context = Context::default();
        let left = context.add_expression(
            ExpressionKind::Constant(Constant::Char(b'a')),
            Type::Char,
            span(0),
        );
        let right = context.add_expression(
            ExpressionKind::Constant(Constant::Int(2)),
            Type::Int,
            span(4),
        );
        let result = context
            .binary(Operator::Divide, left, right, span(0))
            .unwrap();
        assert_eq!(context.expressions[result.0].ty, Type::Int);
        assert_eq!(
            context.expressions[result.0].checks,
            vec![RuntimeCheck::Nonzero(right)]
        );
        let ExpressionKind::Binary {
            left: converted, ..
        } = context.expressions[result.0].kind
        else {
            panic!("binary expression expected");
        };
        assert!(matches!(
            context.expressions[converted.0].kind,
            ExpressionKind::Convert {
                target: Type::Int,
                ..
            }
        ));
        let boolean = context.add_expression(
            ExpressionKind::Constant(Constant::Bool(true)),
            Type::Bool,
            span(6),
        );
        assert_eq!(
            context.binary(Operator::Add, boolean, right, span(6)),
            Err("invalid arithmetic operands")
        );
    }
}
