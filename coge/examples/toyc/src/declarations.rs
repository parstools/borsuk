use std::collections::HashSet;

use crate::ast::InputSpan;
use crate::model::{
    Context, DeclarationTarget, Diagnostic, Flow, FunctionHeader, Initialization, ModuleId,
    Operation, Parameter, ParameterId, ScopeId, StructId, Type,
};

impl Context {
    pub fn copy_text(&self, value: &str) -> String {
        value.to_owned()
    }

    pub fn ordinary_target(&self) -> DeclarationTarget {
        DeclarationTarget::Ordinary
    }

    pub fn field_target(&self, structure: StructId) -> DeclarationTarget {
        DeclarationTarget::Field(structure)
    }

    pub fn declare_object(
        &mut self,
        scope: ScopeId,
        target: DeclarationTarget,
        name: &str,
        ty: Type,
        source: InputSpan,
    ) -> Result<(), &'static str> {
        if !self.complete_object_type(&ty) {
            return Err(match target {
                DeclarationTarget::Ordinary => "invalid variable type",
                DeclarationTarget::Field(_) => "invalid field type",
            });
        }
        match target {
            DeclarationTarget::Field(owner) => {
                self.declare_field(owner, name, ty, source)
                    .map_err(|error| error.message_static())?;
            }
            DeclarationTarget::Ordinary => {
                let symbol = self
                    .declare_variable(scope, name, ty, source)
                    .map_err(|error| error.message_static())?;
                let operation = self.add_operation(Operation::Declare(symbol), source);
                if self.scopes[scope.0].parent.is_none() {
                    let place = self.variables[symbol.0].place;
                    self.global_flow.set(place, Initialization::Initialized);
                    self.current_flow.set(place, Initialization::Initialized);
                    self.module_declarations.push(operation);
                } else {
                    self.scopes[scope.0].operations.push(operation);
                }
            }
        }
        Ok(())
    }

    pub fn type_int(&self) -> Type {
        Type::Int
    }

    pub fn type_float(&self) -> Type {
        Type::Float
    }

    pub fn type_bool(&self) -> Type {
        Type::Bool
    }

    pub fn type_char(&self, pointer: bool) -> Type {
        if pointer {
            Type::CharPointer
        } else {
            Type::Char
        }
    }

    pub fn type_void(&self) -> Type {
        Type::Void
    }

    pub fn resolve_type(&self, scope: ScopeId, name: &str) -> Result<Type, &'static str> {
        self.lookup_tag(scope, name)
            .map(Type::Struct)
            .ok_or("unknown type")
    }

    pub fn parse_array_length(&self, value: &str) -> Result<i64, &'static str> {
        let length = value.parse::<i64>().map_err(|_| "invalid array length")?;
        if length <= 0 {
            return Err("invalid array length");
        }
        Ok(length)
    }

    pub fn array_type(&self, element: Type, length: i64) -> Result<Type, &'static str> {
        let size = usize::try_from(length).map_err(|_| "invalid array length")?;
        if size == 0 || !self.complete_object_type(&element) {
            return Err("invalid array element type");
        }
        Ok(Type::Array(Box::new(element), size))
    }

    pub fn create_parameter(
        &mut self,
        name: &str,
        ty: Type,
        source: InputSpan,
    ) -> Result<ParameterId, &'static str> {
        if !self.complete_object_type(&ty) {
            return Err("invalid parameter type");
        }
        let id = ParameterId(self.parameters.len());
        self.parameters.push(Parameter {
            name: name.to_owned(),
            ty,
            source,
        });
        Ok(id)
    }

    pub fn create_header(
        &self,
        name: &str,
        result: Type,
        parameters: &[ParameterId],
    ) -> Result<FunctionHeader, &'static str> {
        let mut names = HashSet::new();
        if parameters
            .iter()
            .any(|id| !names.insert(self.parameters[id.0].name.as_str()))
        {
            return Err("duplicate parameter name");
        }
        if result != Type::Void && !self.complete_object_type(&result) {
            return Err("invalid return type");
        }
        Ok(FunctionHeader {
            name: name.to_owned(),
            result,
            parameters: parameters.to_vec(),
        })
    }

    pub fn declare_prototype(
        &mut self,
        scope: ScopeId,
        header: FunctionHeader,
        source: InputSpan,
    ) -> Result<(), &'static str> {
        let types = header
            .parameters
            .iter()
            .map(|id| self.parameters[id.0].ty.clone())
            .collect();
        self.declare_function(scope, &header.name, header.result, types, false, source)
            .map_err(|error| error.message_static())?;
        Ok(())
    }

    pub fn begin_definition(
        &mut self,
        scope: ScopeId,
        header: FunctionHeader,
        source: InputSpan,
    ) -> Result<ScopeId, &'static str> {
        let types = header
            .parameters
            .iter()
            .map(|id| self.parameters[id.0].ty.clone())
            .collect();
        let function = self
            .declare_function(scope, &header.name, header.result, types, true, source)
            .map_err(|error| error.message_static())?;
        let local = self.new_scope(Some(scope), source);
        self.current_flow = self.global_flow.clone();
        for parameter in header.parameters {
            let value = self.parameters[parameter.0].clone();
            let symbol = self
                .declare_variable(local, &value.name, value.ty, value.source)
                .map_err(|error| error.message_static())?;
            let place = self.variables[symbol.0].place;
            self.current_flow.set(place, Initialization::Initialized);
        }
        self.functions[function.0].scope = Some(local);
        self.active_function = Some(function);
        self.active_function_error = false;
        Ok(local)
    }

    pub fn begin_struct(
        &mut self,
        scope: ScopeId,
        name: &str,
        source: InputSpan,
    ) -> Result<StructId, &'static str> {
        self.declare_struct(scope, name, source)
            .map_err(|error| error.message_static())
    }

    pub fn finish_struct(&mut self, structure: StructId) {
        self.complete_struct(structure);
    }

    pub fn nested_scope(&mut self, parent: ScopeId, enter: bool, source: InputSpan) -> ScopeId {
        if enter {
            self.new_scope(Some(parent), source)
        } else {
            parent
        }
    }

    pub fn record_error(&mut self, message: &str, source: InputSpan) {
        if let Some(symbol) = self.active_initializer.take() {
            let place = self.variables[symbol.0].place;
            self.current_flow.set(place, Initialization::Poisoned);
        }
        if message == "dependent error" {
            return;
        }
        if self.active_function.is_some() {
            self.active_function_error = true;
        }
        self.diagnostics.push(Diagnostic {
            message: message.to_owned(),
            source,
        });
    }

    pub fn create_module(&mut self, scope: ScopeId) -> ModuleId {
        self.add_module(scope, self.module_declarations.clone())
    }

    pub fn initial_flow(&mut self) {
        self.global_flow = Flow::reachable();
        self.current_flow = Flow::reachable();
    }
}

impl Diagnostic {
    fn message_static(&self) -> &'static str {
        match self.message.as_str() {
            "identifier already declared" => "identifier already declared",
            "conflicting function declaration" => "conflicting function declaration",
            "struct already declared" => "struct already declared",
            "struct already complete" => "struct already complete",
            "invalid field type" => "invalid field type",
            _ => "semantic declaration error",
        }
    }
}
