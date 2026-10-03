use std::collections::{HashMap, hash_map::Entry};

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct ScopeId(usize);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct SymbolId(usize);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FlowId(usize);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct StructTagId(usize);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct ParameterId(usize);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum InitialState {
    Uninitialized,
    Initialized,
    ZeroInitialized,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum SymbolKind {
    Variable,
    Function { signature: String, defined: bool },
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Symbol {
    pub scope: ScopeId,
    pub name: String,
    pub kind: SymbolKind,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct StructTag {
    pub scope: ScopeId,
    pub name: String,
    pub complete: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct IntegerAssignment {
    pub target: SymbolId,
    pub value: i64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum IrOp {
    Declare {
        symbol: SymbolId,
        initial_state: InitialState,
    },
    Assign(IntegerAssignment),
    Read {
        symbol: SymbolId,
    },
    Block {
        scope: ScopeId,
        body: Vec<IrOp>,
    },
    If {
        condition: SymbolId,
        then_body: Vec<IrOp>,
        else_body: Vec<IrOp>,
    },
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct Flow {
    states: HashMap<SymbolId, InitialState>,
    operations: Vec<IrOp>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct PendingProgramIr {
    flow: FlowId,
    start: usize,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct SemaContext {
    scopes: Vec<Scope>,
    parameters: Vec<String>,
    symbols: Vec<Symbol>,
    tags: Vec<StructTag>,
    flows: Vec<Flow>,
    programs: Vec<Vec<IrOp>>,
    pending_program: Option<PendingProgramIr>,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
struct Scope {
    parent: Option<ScopeId>,
    bindings: HashMap<String, SymbolId>,
    tags: HashMap<String, StructTagId>,
}

impl SemaContext {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn add_parameter(&mut self, name: &str) -> ParameterId {
        let id = ParameterId(self.parameters.len());
        self.parameters.push(name.to_owned());
        id
    }

    pub fn parameter_name(&self, parameter: ParameterId) -> &str {
        &self.parameters[parameter.0]
    }

    pub fn copy_text(&self, value: &str) -> String {
        value.to_owned()
    }

    pub fn add_scope(&mut self, parent: Option<ScopeId>) -> ScopeId {
        if let Some(parent) = parent {
            assert!(parent.0 < self.scopes.len(), "parent scope must exist");
        }
        let id = ScopeId(self.scopes.len());
        self.scopes.push(Scope {
            parent,
            ..Scope::default()
        });
        id
    }

    pub fn scope_new_child(&mut self, parent: ScopeId) -> ScopeId {
        self.add_scope(Some(parent))
    }

    pub fn scope_symbols(&self, scope: ScopeId) -> Vec<SymbolId> {
        let mut symbols: Vec<_> = self.scopes[scope.0].bindings.values().copied().collect();
        symbols.sort_by_key(|symbol| symbol.0);
        symbols
    }

    pub fn new_flow(&mut self) -> FlowId {
        let id = FlowId(self.flows.len());
        self.flows.push(Flow::default());
        id
    }

    pub fn flow_copy(&mut self, source: FlowId) -> FlowId {
        let id = FlowId(self.flows.len());
        let snapshot = self.flows[source.0].clone();
        self.flows.push(snapshot);
        id
    }

    pub fn flow_symbols(&self, flow: FlowId) -> Vec<SymbolId> {
        let mut symbols: Vec<_> = self.flows[flow.0].states.keys().copied().collect();
        symbols.sort_by_key(|symbol| symbol.0);
        symbols
    }

    pub fn flow_contains(&self, flow: FlowId, symbol: SymbolId) -> bool {
        self.flows[flow.0].states.contains_key(&symbol)
    }

    pub fn flow_remove(&mut self, flow: FlowId, symbol: SymbolId) {
        self.flows[flow.0].states.remove(&symbol);
    }

    pub fn flow_state(&self, flow: FlowId, symbol: SymbolId) -> Option<InitialState> {
        self.flows[flow.0].states.get(&symbol).copied()
    }

    pub fn scope_lookup(&self, scope: ScopeId, name: &str) -> Option<SymbolId> {
        self.scopes[scope.0].bindings.get(name).copied()
    }

    pub fn scope_insert(&mut self, scope: ScopeId, name: &str, symbol: SymbolId) {
        assert_eq!(self.symbols[symbol.0].scope, scope);
        assert_eq!(self.symbols[symbol.0].name, name);
        match self.scopes[scope.0].bindings.entry(name.to_owned()) {
            Entry::Vacant(slot) => {
                slot.insert(symbol);
            }
            Entry::Occupied(_) => panic!("binding must be checked before insertion"),
        }
    }

    pub fn fresh_variable(&mut self, scope: ScopeId, name: &str) -> SymbolId {
        let id = SymbolId(self.symbols.len());
        self.symbols.push(Symbol {
            scope,
            name: name.to_owned(),
            kind: SymbolKind::Variable,
        });
        id
    }

    pub fn flow_declare_variable(&mut self, flow: FlowId, symbol: SymbolId) {
        assert!(matches!(self.symbols[symbol.0].kind, SymbolKind::Variable));
        let scope = self.symbols[symbol.0].scope;
        let state = if self.scopes[scope.0].parent.is_none() {
            InitialState::ZeroInitialized
        } else {
            InitialState::Uninitialized
        };
        assert!(self.flows[flow.0].states.insert(symbol, state).is_none());
    }

    pub fn fresh_function(
        &mut self,
        scope: ScopeId,
        name: &str,
        signature: &str,
        definition: bool,
    ) -> SymbolId {
        let id = SymbolId(self.symbols.len());
        self.symbols.push(Symbol {
            scope,
            name: name.to_owned(),
            kind: SymbolKind::Function {
                signature: signature.to_owned(),
                defined: definition,
            },
        });
        id
    }

    pub fn symbol(&self, symbol: SymbolId) -> &Symbol {
        &self.symbols[symbol.0]
    }

    pub fn option_symbol(&self, value: Option<SymbolId>) -> SymbolId {
        value.expect("Action must check Option before extracting a symbol")
    }

    pub fn symbol_is_function(&self, symbol: SymbolId) -> bool {
        matches!(self.symbols[symbol.0].kind, SymbolKind::Function { .. })
    }

    pub fn function_signature(&self, symbol: SymbolId) -> &str {
        match &self.symbols[symbol.0].kind {
            SymbolKind::Function { signature, .. } => signature,
            SymbolKind::Variable => panic!("Action must check function kind first"),
        }
    }

    pub fn function_is_defined(&self, symbol: SymbolId) -> bool {
        match &self.symbols[symbol.0].kind {
            SymbolKind::Function { defined, .. } => *defined,
            SymbolKind::Variable => panic!("Action must check function kind first"),
        }
    }

    pub fn mark_function_defined(&mut self, symbol: SymbolId) {
        match &mut self.symbols[symbol.0].kind {
            SymbolKind::Function { defined, .. } => {
                assert!(!*defined, "Action must reject a second function definition");
                *defined = true;
            }
            SymbolKind::Variable => panic!("Action must check function kind first"),
        }
    }

    pub fn flow_mark_initialized(&mut self, flow: FlowId, symbol: SymbolId) {
        assert!(matches!(self.symbols[symbol.0].kind, SymbolKind::Variable));
        *self.flows[flow.0]
            .states
            .get_mut(&symbol)
            .expect("variable must be declared in this flow") = InitialState::Initialized;
    }

    pub fn flow_mark_uninitialized(&mut self, flow: FlowId, symbol: SymbolId) {
        assert!(matches!(self.symbols[symbol.0].kind, SymbolKind::Variable));
        *self.flows[flow.0]
            .states
            .get_mut(&symbol)
            .expect("variable must be declared in this flow") = InitialState::Uninitialized;
    }

    pub fn flow_mark_zero_initialized(&mut self, flow: FlowId, symbol: SymbolId) {
        assert!(matches!(self.symbols[symbol.0].kind, SymbolKind::Variable));
        *self.flows[flow.0]
            .states
            .get_mut(&symbol)
            .expect("variable must be declared in this flow") = InitialState::ZeroInitialized;
    }

    pub fn parse_integer(&self, value: &str) -> Result<i64, &'static str> {
        value.parse().map_err(|_| "invalid integer literal")
    }

    pub fn record_declaration(&mut self, flow: FlowId, symbol: SymbolId) {
        assert!(matches!(self.symbols[symbol.0].kind, SymbolKind::Variable));
        let initial_state = self
            .flow_state(flow, symbol)
            .expect("symbol must be declared");
        self.flows[flow.0].operations.push(IrOp::Declare {
            symbol,
            initial_state,
        });
    }

    pub fn record_read(&mut self, flow: FlowId, symbol: SymbolId) {
        self.flows[flow.0].operations.push(IrOp::Read { symbol });
    }

    pub fn record_integer_assignment(&mut self, flow: FlowId, symbol: SymbolId, value: i64) {
        assert!(matches!(self.symbols[symbol.0].kind, SymbolKind::Variable));
        self.flows[flow.0]
            .operations
            .push(IrOp::Assign(IntegerAssignment {
                target: symbol,
                value,
            }));
    }

    pub fn record_block(&mut self, result: FlowId, base: FlowId, scope: ScopeId) {
        let prefix = &self.flows[base.0].operations;
        let operations = &self.flows[result.0].operations;
        assert!(operations.starts_with(prefix));
        let body = operations[prefix.len()..].to_vec();
        let mut wrapped = prefix.clone();
        wrapped.push(IrOp::Block { scope, body });
        self.flows[result.0].operations = wrapped;
    }

    pub fn record_if(
        &mut self,
        merged: FlowId,
        base: FlowId,
        condition: SymbolId,
        then_flow: FlowId,
        else_flow: FlowId,
    ) {
        let prefix = &self.flows[base.0].operations;
        let then_operations = &self.flows[then_flow.0].operations;
        let else_operations = &self.flows[else_flow.0].operations;
        assert!(then_operations.starts_with(prefix));
        assert!(else_operations.starts_with(prefix));
        let mut operations = prefix.clone();
        operations.push(IrOp::If {
            condition,
            then_body: then_operations[prefix.len()..].to_vec(),
            else_body: else_operations[prefix.len()..].to_vec(),
        });
        self.flows[merged.0].operations = operations;
    }

    pub fn begin_program_ir(&mut self, flow: FlowId) {
        assert!(self.pending_program.is_none());
        self.pending_program = Some(PendingProgramIr {
            flow,
            start: self.flows[flow.0].operations.len(),
        });
    }

    pub fn record_program_flow(&mut self, flow: FlowId) {
        if let Some(pending) = self.pending_program.as_mut() {
            pending.flow = flow;
        }
    }

    pub fn finish_program_ir(&mut self) {
        let pending = self
            .pending_program
            .take()
            .expect("program IR must be active");
        let operations = &self.flows[pending.flow.0].operations;
        assert!(operations.len() >= pending.start);
        self.programs.push(operations[pending.start..].to_vec());
    }

    pub fn program_ir(&self) -> &[Vec<IrOp>] {
        &self.programs
    }

    pub fn flow_ir(&self, flow: FlowId) -> &[IrOp] {
        &self.flows[flow.0].operations
    }

    pub fn flow_is_initialized(&self, flow: FlowId, symbol: SymbolId) -> bool {
        assert!(matches!(self.symbols[symbol.0].kind, SymbolKind::Variable));
        self.flow_state(flow, symbol)
            .expect("variable must be declared in this flow")
            != InitialState::Uninitialized
    }

    pub fn flow_is_zero_initialized(&self, flow: FlowId, symbol: SymbolId) -> bool {
        assert!(matches!(self.symbols[symbol.0].kind, SymbolKind::Variable));
        self.flow_state(flow, symbol)
            .expect("variable must be declared in this flow")
            == InitialState::ZeroInitialized
    }

    pub fn scope_lookup_tag(&self, scope: ScopeId, name: &str) -> Option<StructTagId> {
        self.scopes[scope.0].tags.get(name).copied()
    }

    pub fn fresh_struct_tag(&mut self, scope: ScopeId, name: &str) -> StructTagId {
        let id = StructTagId(self.tags.len());
        self.tags.push(StructTag {
            scope,
            name: name.to_owned(),
            complete: false,
        });
        id
    }

    pub fn scope_insert_tag(&mut self, scope: ScopeId, name: &str, tag: StructTagId) {
        assert_eq!(self.tags[tag.0].scope, scope);
        assert_eq!(self.tags[tag.0].name, name);
        match self.scopes[scope.0].tags.entry(name.to_owned()) {
            Entry::Vacant(slot) => {
                slot.insert(tag);
            }
            Entry::Occupied(_) => panic!("tag must be checked before insertion"),
        }
    }

    pub fn tag(&self, tag: StructTagId) -> &StructTag {
        &self.tags[tag.0]
    }

    pub fn scope_chain(&self, scope: ScopeId) -> Vec<ScopeId> {
        let mut result = Vec::new();
        let mut current = Some(scope);
        while let Some(id) = current {
            result.push(id);
            current = self.scopes[id.0].parent;
        }
        result
    }
}
