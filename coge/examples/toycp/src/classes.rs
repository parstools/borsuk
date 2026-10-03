
use crate::ast::InputSpan;
use crate::model::{
    CallableKind, Context, FieldId, Function, FunctionHeader, FunctionId, Initialization,
    Operation, ParameterId, PlaceId, ScopeId, StructId, SymbolId, Type, Visibility,
};

impl Context {
    pub fn default_constructible(&self, ty: &Type) -> bool {
        match ty {
            Type::Struct(id) => {
                let structure = &self.structs[id.0];
                (structure.constructors.is_empty()
                    || structure
                        .constructors
                        .iter()
                        .any(|constructor| self.functions[constructor.0].parameters.len() == 1))
                    && structure
                        .fields
                        .iter()
                        .all(|field| self.default_constructible(&self.fields[field.0].ty))
            }
            Type::Array(element, _) => self.default_constructible(element),
            _ => true,
        }
    }

    pub fn no_owner(&self) -> StructId {
        StructId(usize::MAX)
    }
    pub fn public_visibility(&self) -> Visibility {
        Visibility::Public
    }
    pub fn protected_visibility(&self) -> Visibility {
        Visibility::Protected
    }
    pub fn private_visibility(&self) -> Visibility {
        Visibility::Private
    }

    pub fn begin_class(
        &mut self,
        scope: ScopeId,
        name: &str,
        source: InputSpan,
    ) -> Result<StructId, &'static str> {
        let id = self.begin_struct(scope, name, source)?;
        self.structs[id.0].is_class = true;
        Ok(id)
    }

    pub fn finish_class(&mut self, owner: StructId) -> Result<(), &'static str> {
        self.finish_struct(owner)
    }

    pub fn lookup_aggregate(&self, scope: ScopeId, name: &str) -> Result<StructId, &'static str> {
        self.lookup_tag(scope, name).ok_or("unknown class")
    }

    pub fn set_base(
        &mut self,
        scope: ScopeId,
        owner: StructId,
        name: &str,
        source: InputSpan,
    ) -> Result<(), &'static str> {
        let base = self.lookup_aggregate(scope, name)?;
        if owner == base || !self.structs[base.0].complete || self.structs[owner.0].base.is_some() {
            return Err("invalid base class");
        }
        let visibility = if self.structs[owner.0].is_class {
            Visibility::Private
        } else {
            Visibility::Public
        };
        let field = self
            .declare_field(owner, "<base>", Type::Struct(base), source)
            .map_err(|_| "invalid base class")?;
        self.fields[field.0].visibility = visibility;
        self.structs[owner.0].base = Some(base);
        Ok(())
    }

    pub fn create_constructor_header(
        &self,
        owner: StructId,
        name: &str,
        parameters: &[ParameterId],
    ) -> Result<FunctionHeader, &'static str> {
        if self
            .structs
            .get(owner.0)
            .is_none_or(|structure| structure.name != name)
        {
            return Err("constructor name does not match class");
        }
        self.create_header(name, Type::Void, parameters)
    }

    pub fn create_destructor_header(
        &self,
        owner: StructId,
        name: &str,
    ) -> Result<FunctionHeader, &'static str> {
        if self
            .structs
            .get(owner.0)
            .is_none_or(|structure| structure.name != name)
        {
            return Err("destructor name does not match class");
        }
        self.create_header(name, Type::Void, &[])
    }

    fn member_key(kind: CallableKind, name: &str) -> String {
        match kind {
            CallableKind::Constructor => "<constructor>".to_owned(),
            CallableKind::Destructor => "<destructor>".to_owned(),
            _ => name.to_owned(),
        }
    }

    fn declare_member(
        &mut self,
        owner: StructId,
        visibility: Visibility,
        header: FunctionHeader,
        kind: CallableKind,
        definition: bool,
        source: InputSpan,
    ) -> Result<FunctionId, &'static str> {
        if owner.0 >= self.structs.len() {
            return Err("unknown class");
        }
        let key = Self::member_key(kind, &header.name);
        let mut parameters = vec![Type::Struct(owner)];
        parameters.extend(
            header
                .parameters
                .iter()
                .map(|id| self.parameters[id.0].ty.clone()),
        );
        let existing = if kind == CallableKind::Constructor {
            self.structs[owner.0]
                .constructors
                .iter()
                .copied()
                .find(|id| self.functions[id.0].parameters == parameters)
        } else {
            self.structs[owner.0].methods.get(&key).copied()
        };
        if let Some(id) = existing {
            let function = &mut self.functions[id.0];
            if function.kind != kind
                || function.result != header.result
                || function.parameters != parameters
                || (definition && function.defined)
            {
                return Err("conflicting member declaration");
            }
            function.defined |= definition;
            return Ok(id);
        }
        let id = FunctionId(self.functions.len());
        let name = format!("{}::{}", self.structs[owner.0].name, header.name);
        self.functions.push(Function {
            name,
            result: header.result,
            parameters,
            defined: definition,
            scope: None,
            body: None,
            source,
            owner: Some(owner),
            visibility,
            kind,
        });
        self.structs[owner.0].methods.entry(key).or_insert(id);
        match kind {
            CallableKind::Constructor => {
                self.structs[owner.0].constructor.get_or_insert(id);
                self.structs[owner.0].constructors.push(id);
            }
            CallableKind::Destructor => self.structs[owner.0].destructor = Some(id),
            _ => {}
        }
        Ok(id)
    }

    fn define_member(
        &mut self,
        owner: StructId,
        visibility: Visibility,
        header: FunctionHeader,
        kind: CallableKind,
        source: InputSpan,
    ) -> Result<ScopeId, &'static str> {
        let parameters = header.parameters.clone();
        let id = self.declare_member(owner, visibility, header, kind, true, source)?;
        let scope = self.new_scope(Some(ScopeId(0)), source);
        self.current_flow = self.global_flow.clone();
        let self_symbol = self
            .declare_variable(scope, "self", Type::Struct(owner), source)
            .map_err(|_| "invalid self parameter")?;
        self.variables[self_symbol.0].is_self = true;
        self.current_flow.set(
            self.variables[self_symbol.0].place,
            Initialization::Initialized,
        );
        for parameter in parameters {
            let parameter = self.parameters[parameter.0].clone();
            let symbol = self
                .declare_variable(scope, &parameter.name, parameter.ty, parameter.source)
                .map_err(|_| "duplicate parameter name")?;
            self.current_flow
                .set(self.variables[symbol.0].place, Initialization::Initialized);
        }
        self.functions[id.0].scope = Some(scope);
        self.active_function = Some(id);
        self.active_function_error = false;
        self.active_scopes.push(scope);
        Ok(scope)
    }

    pub fn declare_callable(
        &mut self,
        scope: ScopeId,
        owner: StructId,
        visibility: Visibility,
        header: FunctionHeader,
        source: InputSpan,
    ) -> Result<(), &'static str> {
        if owner == self.no_owner() {
            self.declare_prototype(scope, header, source)
        } else {
            self.declare_member(
                owner,
                visibility,
                header,
                CallableKind::Method,
                false,
                source,
            )
            .map(|_| ())
        }
    }

    pub fn begin_callable_definition(
        &mut self,
        scope: ScopeId,
        owner: StructId,
        visibility: Visibility,
        header: FunctionHeader,
        source: InputSpan,
    ) -> Result<ScopeId, &'static str> {
        if owner == self.no_owner() {
            let local = self.begin_definition(scope, header, source)?;
            self.active_scopes.push(local);
            Ok(local)
        } else {
            self.define_member(owner, visibility, header, CallableKind::Method, source)
        }
    }

    pub fn predeclare_inline_method(
        &mut self,
        owner: StructId,
        visibility: Visibility,
        header: FunctionHeader,
        source: InputSpan,
    ) -> Result<(), &'static str> {
        self.declare_member(
            owner,
            visibility,
            header,
            CallableKind::Method,
            false,
            source,
        )
        .map(|_| ())
    }

    pub fn declare_constructor(
        &mut self,
        owner: StructId,
        visibility: Visibility,
        header: FunctionHeader,
        source: InputSpan,
    ) -> Result<(), &'static str> {
        self.declare_member(
            owner,
            visibility,
            header,
            CallableKind::Constructor,
            false,
            source,
        )
        .map(|_| ())
    }
    pub fn declare_destructor(
        &mut self,
        owner: StructId,
        visibility: Visibility,
        header: FunctionHeader,
        source: InputSpan,
    ) -> Result<(), &'static str> {
        self.declare_member(
            owner,
            visibility,
            header,
            CallableKind::Destructor,
            false,
            source,
        )
        .map(|_| ())
    }
    pub fn predeclare_inline_constructor(
        &mut self,
        owner: StructId,
        visibility: Visibility,
        header: FunctionHeader,
        source: InputSpan,
    ) -> Result<(), &'static str> {
        self.declare_constructor(owner, visibility, header, source)
    }
    pub fn predeclare_inline_destructor(
        &mut self,
        owner: StructId,
        visibility: Visibility,
        header: FunctionHeader,
        source: InputSpan,
    ) -> Result<(), &'static str> {
        self.declare_destructor(owner, visibility, header, source)
    }
    pub fn begin_constructor_definition(
        &mut self,
        owner: StructId,
        visibility: Visibility,
        header: FunctionHeader,
        source: InputSpan,
    ) -> Result<ScopeId, &'static str> {
        self.define_member(owner, visibility, header, CallableKind::Constructor, source)
    }
    pub fn begin_destructor_definition(
        &mut self,
        owner: StructId,
        visibility: Visibility,
        header: FunctionHeader,
        source: InputSpan,
    ) -> Result<ScopeId, &'static str> {
        self.define_member(owner, visibility, header, CallableKind::Destructor, source)
    }
    pub fn begin_out_of_method_definition(
        &mut self,
        owner: StructId,
        header: FunctionHeader,
        source: InputSpan,
    ) -> Result<ScopeId, &'static str> {
        let key = Self::member_key(CallableKind::Method, &header.name);
        let id = *self.structs[owner.0]
            .methods
            .get(&key)
            .ok_or("member not declared")?;
        self.define_member(
            owner,
            self.functions[id.0].visibility,
            header,
            CallableKind::Method,
            source,
        )
    }
    pub fn begin_out_of_constructor_definition(
        &mut self,
        owner: StructId,
        header: FunctionHeader,
        source: InputSpan,
    ) -> Result<ScopeId, &'static str> {
        let mut parameters = vec![Type::Struct(owner)];
        parameters.extend(
            header
                .parameters
                .iter()
                .map(|id| self.parameters[id.0].ty.clone()),
        );
        let id = self.structs[owner.0]
            .constructors
            .iter()
            .copied()
            .find(|id| self.functions[id.0].parameters == parameters)
            .ok_or("constructor not declared")?;
        self.define_member(
            owner,
            self.functions[id.0].visibility,
            header,
            CallableKind::Constructor,
            source,
        )
    }
    pub fn begin_out_of_destructor_definition(
        &mut self,
        owner: StructId,
        header: FunctionHeader,
        source: InputSpan,
    ) -> Result<ScopeId, &'static str> {
        let id = self.structs[owner.0]
            .destructor
            .ok_or("destructor not declared")?;
        self.define_member(
            owner,
            self.functions[id.0].visibility,
            header,
            CallableKind::Destructor,
            source,
        )
    }

    pub fn begin_construction(
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
        let declaration = self.add_operation(Operation::Declare(symbol), source);
        self.scopes[scope.0].operations.push(declaration);
        self.active_initializer = Some(symbol);
        Ok(symbol)
    }

    pub fn finish_construction(
        &mut self,
        scope: ScopeId,
        symbol: SymbolId,
        arguments: &[crate::model::ExprId],
        source: InputSpan,
    ) -> Result<(), &'static str> {
        crate::sema_lib_gen::complete_construction(self, scope, symbol, arguments, source)
    }

    pub fn active_owner(&self) -> Option<StructId> {
        self.active_function
            .and_then(|id| self.functions[id.0].owner)
    }

    pub fn current_self_place(&self) -> Result<PlaceId, &'static str> {
        let function = self.active_function.ok_or("self outside method")?;
        let scope = self.functions[function.0]
            .scope
            .ok_or("self outside method")?;
        let symbol = *self.scopes[scope.0]
            .symbols
            .first()
            .ok_or("self outside method")?;
        Ok(self.variables[symbol.0].place)
    }

    pub fn accessible(&self, visibility: Visibility, owner: StructId) -> bool {
        match visibility {
            Visibility::Public => true,
            Visibility::Private => self.active_owner() == Some(owner),
            Visibility::Protected => self
                .active_owner()
                .is_some_and(|current| self.is_derived_from(current, owner)),
        }
    }

    pub fn is_derived_from(&self, mut current: StructId, owner: StructId) -> bool {
        loop {
            if current == owner {
                return true;
            }
            let Some(base) = self.structs[current.0].base else {
                return false;
            };
            current = base;
        }
    }

    pub fn check_field_access(&self, path: &[FieldId]) -> Result<(), &'static str> {
        if path.iter().all(|field| {
            let field = &self.fields[field.0];
            self.accessible(field.visibility, field.owner)
        }) {
            Ok(())
        } else {
            Err("member is not accessible")
        }
    }

    pub fn field_path(&self, owner: StructId, name: &str) -> Option<Vec<FieldId>> {
        if let Some(field) = self.lookup_field(owner, name) {
            return Some(vec![field]);
        }
        let base = self.structs[owner.0].base?;
        let base_field = *self.structs[owner.0].fields.first()?;
        let mut path = vec![base_field];
        path.extend(self.field_path(base, name)?);
        Some(path)
    }

    pub fn method_in(&self, owner: StructId, name: &str) -> Option<FunctionId> {
        self.structs[owner.0]
            .methods
            .get(name)
            .copied()
            .or_else(|| {
                self.structs[owner.0]
                    .base
                    .and_then(|base| self.method_in(base, name))
            })
    }
}
