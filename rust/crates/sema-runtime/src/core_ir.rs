use std::collections::{HashMap, HashSet};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct TypeId(pub usize);
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct FunctionId(pub usize);
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct BlockId(pub usize);
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct SlotId(pub usize);
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct GlobalId(pub usize);
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct ValueId(pub usize);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SourceSpan {
    pub file: u32,
    pub start: u32,
    pub end: u32,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Type {
    I32,
    F32,
    Bool,
    U8,
    Address(TypeId),
    Array { element: TypeId, length: usize },
    Aggregate(Vec<TypeId>),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Constant {
    I32(i32),
    F32Bits(u32),
    Bool(bool),
    U8(u8),
    Null,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BinaryOp {
    Add,
    Subtract,
    Multiply,
    Divide,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CompareOp {
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ArithmeticMode {
    CheckedI32,
    IeeeF32,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum InstructionKind {
    Constant(Constant),
    SlotAddress(SlotId),
    GlobalAddress(GlobalId),
    FieldAddress {
        base: ValueId,
        field: usize,
    },
    CopyAggregate {
        source: ValueId,
        target: ValueId,
    },
    ResetAggregate(SlotId),
    ResetArray(SlotId),
    IndexAddress {
        slot: SlotId,
        index: ValueId,
    },
    GlobalIndexAddress {
        global: GlobalId,
        index: ValueId,
    },
    Load(ValueId),
    Store {
        address: ValueId,
        value: ValueId,
    },
    Convert(ValueId),
    Binary {
        operation: BinaryOp,
        left: ValueId,
        right: ValueId,
        mode: ArithmeticMode,
    },
    Compare {
        operation: CompareOp,
        left: ValueId,
        right: ValueId,
    },
    Call {
        function: FunctionId,
        arguments: Vec<ValueId>,
    },
    BoundsCheck {
        index: ValueId,
        length: ValueId,
    },
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Instruction {
    pub result: Option<(ValueId, TypeId)>,
    pub kind: InstructionKind,
    pub source: Option<SourceSpan>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum TerminatorKind {
    Jump(BlockId),
    Branch {
        condition: ValueId,
        yes: BlockId,
        no: BlockId,
    },
    Return(Option<ValueId>),
    Fail,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Terminator {
    pub kind: TerminatorKind,
    pub source: Option<SourceSpan>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Block {
    pub instructions: Vec<Instruction>,
    pub terminator: Option<Terminator>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Function {
    pub name: String,
    pub parameters: Vec<SlotId>,
    pub slots: Vec<TypeId>,
    pub result: Option<TypeId>,
    pub blocks: Vec<Block>,
    pub entry: BlockId,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Module {
    pub types: Vec<Type>,
    pub globals: Vec<TypeId>,
    pub functions: Vec<Function>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct VerifyError {
    pub function: Option<FunctionId>,
    pub block: Option<BlockId>,
    pub instruction: Option<usize>,
    pub source: Option<SourceSpan>,
    pub message: String,
}

struct Verifier<'a> {
    module: &'a Module,
    errors: Vec<VerifyError>,
    function: Option<FunctionId>,
    block: Option<BlockId>,
    instruction: Option<usize>,
    source: Option<SourceSpan>,
}

impl Verifier<'_> {
    fn error(&mut self, message: impl Into<String>) {
        self.errors.push(VerifyError {
            function: self.function,
            block: self.block,
            instruction: self.instruction,
            source: self.source,
            message: message.into(),
        });
    }

    fn valid_type(&mut self, id: TypeId) -> bool {
        if id.0 < self.module.types.len() {
            true
        } else {
            self.error(format!("unknown type {}", id.0));
            false
        }
    }

    fn value(&mut self, values: &HashMap<ValueId, TypeId>, id: ValueId) -> Option<TypeId> {
        let Some(&ty) = values.get(&id) else {
            self.error(format!("value {} is not available in this block", id.0));
            return None;
        };
        Some(ty)
    }

    fn same_type(&mut self, actual: Option<TypeId>, expected: TypeId) {
        if let Some(actual) = actual {
            if actual != expected {
                self.error(format!(
                    "type mismatch: expected {}, found {}",
                    expected.0, actual.0
                ));
            }
        }
    }

    fn result_type(&mut self, instruction: &Instruction, expected: Option<TypeId>) {
        match (instruction.result, expected) {
            (Some((_, actual)), Some(expected)) => self.same_type(Some(actual), expected),
            (None, Some(_)) => self.error("value-producing instruction has no result"),
            (Some(_), None) => self.error("effect instruction cannot produce a value"),
            (None, None) => {}
        }
    }

    fn instruction(
        &mut self,
        function: &Function,
        instruction: &Instruction,
        values: &mut HashMap<ValueId, TypeId>,
    ) {
        let expected = match &instruction.kind {
            InstructionKind::Constant(value) => {
                let matches = |ty: &Type| {
                    matches!(
                        (value, ty),
                        (Constant::I32(_), Type::I32)
                            | (Constant::F32Bits(_), Type::F32)
                            | (Constant::Bool(_), Type::Bool)
                            | (Constant::U8(_), Type::U8)
                            | (Constant::Null, Type::Address(_))
                    )
                };
                if let Some((_, ty)) = instruction.result {
                    if self.module.types.get(ty.0).is_some_and(matches) {
                        Some(ty)
                    } else {
                        self.error("constant has incompatible result type");
                        None
                    }
                } else {
                    self.error("constant has no result");
                    None
                }
            }
            InstructionKind::SlotAddress(slot) => {
                let Some(&slot_ty) = function.slots.get(slot.0) else {
                    self.error(format!("unknown slot {}", slot.0));
                    return;
                };
                if let Some((_, ty)) = instruction.result {
                    if self.module.types.get(ty.0) == Some(&Type::Address(slot_ty)) {
                        Some(ty)
                    } else {
                        self.error("slot address has incompatible result type");
                        None
                    }
                } else {
                    self.error("slot address has no result");
                    None
                }
            }
            InstructionKind::GlobalAddress(global) => {
                let Some(&global_ty) = self.module.globals.get(global.0) else {
                    self.error(format!("unknown global {}", global.0));
                    return;
                };
                if let Some((_, ty)) = instruction.result {
                    if self.module.types.get(ty.0) == Some(&Type::Address(global_ty)) {
                        Some(ty)
                    } else {
                        self.error("global address has incompatible result type");
                        None
                    }
                } else {
                    self.error("global address has no result");
                    None
                }
            }
            InstructionKind::FieldAddress { base, field } => {
                let base_ty = self.value(values, *base);
                let Some(Type::Address(aggregate)) =
                    base_ty.and_then(|ty| self.module.types.get(ty.0))
                else {
                    self.error("field address requires an aggregate address");
                    return;
                };
                let Some(Type::Aggregate(fields)) = self.module.types.get(aggregate.0) else {
                    self.error("field address requires an aggregate address");
                    return;
                };
                let Some(&field_ty) = fields.get(*field) else {
                    self.error("field address has invalid field index");
                    return;
                };
                if let Some((_, result_ty)) = instruction.result {
                    if self.module.types.get(result_ty.0) != Some(&Type::Address(field_ty)) {
                        self.error("field address has incompatible result type");
                    }
                } else {
                    self.error("field address has no result");
                }
                instruction.result.map(|(_, ty)| ty)
            }
            InstructionKind::CopyAggregate { source, target } => {
                let source_ty = self.value(values, *source);
                let target_ty = self.value(values, *target);
                match (source_ty, target_ty) {
                    (Some(source_ty), Some(target_ty)) if source_ty == target_ty => {
                        if !matches!(self.module.types.get(source_ty.0), Some(Type::Address(inner)) if matches!(self.module.types.get(inner.0), Some(Type::Aggregate(_))))
                        {
                            self.error("aggregate copy requires aggregate addresses");
                        }
                    }
                    (Some(_), Some(_)) => self.error("aggregate copy requires matching types"),
                    _ => {}
                }
                self.result_type(instruction, None);
                None
            }
            InstructionKind::ResetAggregate(slot) => {
                let Some(&slot_ty) = function.slots.get(slot.0) else {
                    self.error(format!("unknown slot {}", slot.0));
                    return;
                };
                if !matches!(self.module.types.get(slot_ty.0), Some(Type::Aggregate(_))) {
                    self.error("aggregate reset requires aggregate slot");
                }
                None
            }
            InstructionKind::ResetArray(slot) => {
                let Some(&slot_ty) = function.slots.get(slot.0) else {
                    self.error(format!("unknown slot {}", slot.0));
                    return;
                };
                if !matches!(self.module.types.get(slot_ty.0), Some(Type::Array { .. })) {
                    self.error("array reset requires array slot");
                }
                None
            }
            InstructionKind::IndexAddress { slot, index } => {
                if let Some(index_ty) = self.value(values, *index)
                    && self.module.types.get(index_ty.0) != Some(&Type::I32)
                {
                    self.error("index address requires i32 index");
                }
                let Some(&slot_ty) = function.slots.get(slot.0) else {
                    self.error(format!("unknown slot {}", slot.0));
                    return;
                };
                let Some(Type::Array { element, .. }) = self.module.types.get(slot_ty.0) else {
                    self.error("index address requires array slot");
                    return;
                };
                let element = *element;
                if let Some((_, result_ty)) = instruction.result {
                    if self.module.types.get(result_ty.0) != Some(&Type::Address(element)) {
                        self.error("index address has incompatible result type");
                    }
                } else {
                    self.error("index address has no result");
                }
                instruction.result.map(|(_, ty)| ty)
            }
            InstructionKind::GlobalIndexAddress { global, index } => {
                if let Some(index_ty) = self.value(values, *index)
                    && self.module.types.get(index_ty.0) != Some(&Type::I32)
                {
                    self.error("global index address requires i32 index");
                }
                let Some(&global_ty) = self.module.globals.get(global.0) else {
                    self.error(format!("unknown global {}", global.0));
                    return;
                };
                let Some(Type::Array { element, .. }) = self.module.types.get(global_ty.0) else {
                    self.error("global index address requires array global");
                    return;
                };
                let element = *element;
                if let Some((_, result_ty)) = instruction.result {
                    if self.module.types.get(result_ty.0) != Some(&Type::Address(element)) {
                        self.error("global index address has incompatible result type");
                    }
                } else {
                    self.error("global index address has no result");
                }
                instruction.result.map(|(_, ty)| ty)
            }
            InstructionKind::Load(address) => {
                self.value(values, *address)
                    .and_then(|ty| match self.module.types.get(ty.0) {
                        Some(Type::Address(inner)) => Some(*inner),
                        _ => {
                            self.error("load requires an address");
                            None
                        }
                    })
            }
            InstructionKind::Store { address, value } => {
                let address_ty = self.value(values, *address);
                let value_ty = self.value(values, *value);
                if let Some(address_ty) = address_ty {
                    match self.module.types.get(address_ty.0) {
                        Some(Type::Address(inner)) => self.same_type(value_ty, *inner),
                        _ => self.error("store requires an address"),
                    }
                }
                None
            }
            InstructionKind::Convert(value) => {
                let from = self.value(values, *value);
                let to = instruction.result.map(|(_, ty)| ty);
                if to.is_none() {
                    self.error("conversion has no result");
                }
                if let (Some(from), Some(to)) = (from, to) {
                    if !self.convertible(from, to) {
                        self.error("unsupported conversion");
                    }
                }
                to
            }
            InstructionKind::Binary {
                left, right, mode, ..
            } => {
                let left_ty = self.value(values, *left);
                let right_ty = self.value(values, *right);
                if let Some(left_ty) = left_ty {
                    self.same_type(right_ty, left_ty);
                    let valid = matches!(
                        (self.module.types.get(left_ty.0), mode),
                        (Some(Type::I32), ArithmeticMode::CheckedI32)
                            | (Some(Type::F32), ArithmeticMode::IeeeF32)
                    );
                    if !valid {
                        self.error("binary operation has incompatible arithmetic mode");
                    }
                }
                left_ty
            }
            InstructionKind::Compare { left, right, .. } => {
                let left_ty = self.value(values, *left);
                let right_ty = self.value(values, *right);
                if let Some(left_ty) = left_ty {
                    self.same_type(right_ty, left_ty);
                    if !matches!(
                        self.module.types.get(left_ty.0),
                        Some(Type::I32 | Type::F32 | Type::Bool | Type::U8)
                    ) {
                        self.error("comparison requires scalar operands");
                    }
                }
                let result = instruction.result.map(|(_, ty)| ty);
                if result.is_some_and(|ty| self.module.types.get(ty.0) != Some(&Type::Bool)) {
                    self.error("comparison requires bool result type");
                }
                result.or_else(|| {
                    self.error("comparison has no result");
                    None
                })
            }
            InstructionKind::Call {
                function: target,
                arguments,
            } => {
                if let Some(callee) = self.module.functions.get(target.0) {
                    let parameters: Vec<_> = callee
                        .parameters
                        .iter()
                        .filter_map(|slot| callee.slots.get(slot.0).copied())
                        .collect();
                    if parameters.len() != arguments.len() {
                        self.error("call argument count does not match function signature");
                    }
                    for (argument, parameter) in arguments.iter().zip(parameters) {
                        let actual = self.value(values, *argument);
                        self.same_type(actual, parameter);
                    }
                    callee.result
                } else {
                    self.error(format!("unknown function {}", target.0));
                    None
                }
            }
            InstructionKind::BoundsCheck { index, length } => {
                for operand in [index, length] {
                    let actual = self.value(values, *operand);
                    if let Some(actual) = actual {
                        if self.module.types.get(actual.0) != Some(&Type::I32) {
                            self.error("bounds check requires i32 operands");
                        }
                    }
                }
                None
            }
        };
        self.result_type(instruction, expected);
        if let Some((id, ty)) = instruction.result {
            self.valid_type(ty);
            if values.insert(id, ty).is_some() {
                self.error(format!(
                    "value {} is defined more than once in this block",
                    id.0
                ));
            }
        }
    }

    fn convertible(&self, from: TypeId, to: TypeId) -> bool {
        if from == to {
            return true;
        }
        matches!(
            (self.module.types.get(from.0), self.module.types.get(to.0)),
            (Some(Type::U8), Some(Type::I32 | Type::F32 | Type::Bool))
                | (Some(Type::I32), Some(Type::F32 | Type::Bool | Type::U8))
                | (Some(Type::F32), Some(Type::Bool | Type::I32 | Type::U8))
        )
    }

    fn target(&mut self, function: &Function, block: BlockId) {
        if block.0 >= function.blocks.len() {
            self.error(format!("unknown target block {}", block.0));
        }
    }

    fn terminator(
        &mut self,
        function: &Function,
        terminator: &Terminator,
        values: &HashMap<ValueId, TypeId>,
    ) {
        match &terminator.kind {
            TerminatorKind::Jump(target) => self.target(function, *target),
            TerminatorKind::Branch { condition, yes, no } => {
                if let Some(ty) = self.value(values, *condition) {
                    if self.module.types.get(ty.0) != Some(&Type::Bool) {
                        self.error("branch condition must be bool");
                    }
                }
                self.target(function, *yes);
                self.target(function, *no);
            }
            TerminatorKind::Return(value) => match (value, function.result) {
                (Some(value), Some(expected)) => {
                    let actual = self.value(values, *value);
                    self.same_type(actual, expected);
                }
                (None, None) => {}
                _ => self.error("return value does not match function signature"),
            },
            TerminatorKind::Fail => {}
        }
    }

    fn function(&mut self, id: FunctionId, function: &Function) {
        self.function = Some(id);
        self.block = None;
        self.instruction = None;
        self.source = None;
        if function.blocks.is_empty() || function.entry.0 >= function.blocks.len() {
            self.error("function has no valid entry block");
        }
        if let Some(result) = function.result {
            self.valid_type(result);
        }
        for &ty in &function.slots {
            self.valid_type(ty);
        }
        let mut parameters = HashSet::new();
        for slot in &function.parameters {
            if slot.0 >= function.slots.len() {
                self.error(format!("unknown parameter slot {}", slot.0));
            } else if !parameters.insert(*slot) {
                self.error(format!("parameter slot {} is listed twice", slot.0));
            }
        }
        for (number, block) in function.blocks.iter().enumerate() {
            self.block = Some(BlockId(number));
            let mut values = HashMap::new();
            for (index, instruction) in block.instructions.iter().enumerate() {
                self.instruction = Some(index);
                self.source = instruction.source;
                self.instruction(function, instruction, &mut values);
            }
            self.instruction = None;
            if let Some(terminator) = &block.terminator {
                self.source = terminator.source;
                self.terminator(function, terminator, &values);
            } else {
                self.source = None;
                self.error("block has no terminator");
            }
        }
    }
}

pub fn verify(module: &Module) -> Result<(), Vec<VerifyError>> {
    let mut verifier = Verifier {
        module,
        errors: Vec::new(),
        function: None,
        block: None,
        instruction: None,
        source: None,
    };
    for ty in &module.types {
        match ty {
            Type::Address(inner) | Type::Array { element: inner, .. } => {
                verifier.valid_type(*inner);
            }
            Type::Aggregate(fields) => {
                for field in fields {
                    verifier.valid_type(*field);
                }
            }
            _ => {}
        }
    }
    for global in &module.globals {
        verifier.valid_type(*global);
    }
    for (index, function) in module.functions.iter().enumerate() {
        verifier.function(FunctionId(index), function);
    }
    if verifier.errors.is_empty() {
        Ok(())
    } else {
        Err(verifier.errors)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn instruction(result: Option<(usize, usize)>, kind: InstructionKind) -> Instruction {
        Instruction {
            result: result.map(|(value, ty)| (ValueId(value), TypeId(ty))),
            kind,
            source: None,
        }
    }

    fn block(instructions: Vec<Instruction>, kind: TerminatorKind) -> Block {
        Block {
            instructions,
            terminator: Some(Terminator { kind, source: None }),
        }
    }

    fn module() -> Module {
        Module {
            types: vec![Type::I32, Type::Bool, Type::Address(TypeId(0))],
            globals: vec![],
            functions: vec![Function {
                name: "main".into(),
                parameters: vec![],
                slots: vec![TypeId(0)],
                result: Some(TypeId(0)),
                entry: BlockId(0),
                blocks: vec![block(
                    vec![
                        instruction(Some((0, 2)), InstructionKind::SlotAddress(SlotId(0))),
                        instruction(Some((1, 0)), InstructionKind::Constant(Constant::I32(5))),
                        instruction(
                            None,
                            InstructionKind::Store {
                                address: ValueId(0),
                                value: ValueId(1),
                            },
                        ),
                        instruction(Some((2, 0)), InstructionKind::Load(ValueId(0))),
                    ],
                    TerminatorKind::Return(Some(ValueId(2))),
                )],
            }],
        }
    }

    fn has_error(module: &Module, text: &str) -> bool {
        verify(module)
            .unwrap_err()
            .iter()
            .any(|error| error.message.contains(text))
    }

    #[test]
    fn accepts_typed_slot_round_trip() {
        assert!(verify(&module()).is_ok());
    }

    #[test]
    fn verifies_integer_comparison_types() {
        let mut module = module();
        module.functions[0].blocks[0].instructions.push(instruction(
            Some((3, 1)),
            InstructionKind::Compare {
                operation: CompareOp::Equal,
                left: ValueId(1),
                right: ValueId(2),
            },
        ));
        assert!(verify(&module).is_ok());
        module.functions[0].blocks[0].instructions[4].result = Some((ValueId(3), TypeId(0)));
        assert!(has_error(&module, "comparison requires bool result type"));
        module.functions[0].blocks[0].instructions[4].result = Some((ValueId(3), TypeId(1)));
        module.functions[0].blocks[0].instructions.insert(
            4,
            instruction(
                Some((4, 1)),
                InstructionKind::Constant(Constant::Bool(true)),
            ),
        );
        module.functions[0].blocks[0].instructions[5].kind = InstructionKind::Compare {
            operation: CompareOp::Equal,
            left: ValueId(4),
            right: ValueId(2),
        };
        assert!(has_error(&module, "type mismatch"));
    }

    #[test]
    fn rejects_value_from_another_block() {
        let mut module = module();
        module.functions[0].blocks[0].terminator = Some(Terminator {
            kind: TerminatorKind::Jump(BlockId(1)),
            source: None,
        });
        module.functions[0]
            .blocks
            .push(block(vec![], TerminatorKind::Return(Some(ValueId(2)))));
        assert!(has_error(&module, "not available in this block"));
    }

    #[test]
    fn rejects_wrong_store_type_and_missing_terminator() {
        let mut module = module();
        module.functions[0].blocks[0].instructions.insert(
            2,
            instruction(
                Some((3, 1)),
                InstructionKind::Constant(Constant::Bool(true)),
            ),
        );
        module.functions[0].blocks[0].instructions[3].kind = InstructionKind::Store {
            address: ValueId(0),
            value: ValueId(3),
        };
        module.functions[0].blocks[0].terminator = None;
        assert!(has_error(&module, "type mismatch"));
        assert!(has_error(&module, "no terminator"));
    }

    #[test]
    fn rejects_wrong_branch_condition_and_target() {
        let mut module = module();
        module.functions[0].blocks[0].terminator = Some(Terminator {
            kind: TerminatorKind::Branch {
                condition: ValueId(1),
                yes: BlockId(0),
                no: BlockId(9),
            },
            source: None,
        });
        assert!(has_error(&module, "branch condition must be bool"));
        assert!(has_error(&module, "unknown target block"));
    }

    #[test]
    fn rejects_call_with_wrong_signature() {
        let mut module = module();
        module.functions.push(Function {
            name: "callee".into(),
            parameters: vec![SlotId(0)],
            slots: vec![TypeId(1)],
            result: None,
            entry: BlockId(0),
            blocks: vec![block(vec![], TerminatorKind::Return(None))],
        });
        module.functions[0].blocks[0].instructions.push(instruction(
            None,
            InstructionKind::Call {
                function: FunctionId(1),
                arguments: vec![ValueId(1)],
            },
        ));
        assert!(has_error(&module, "type mismatch"));
    }

    #[test]
    fn rejects_conversion_without_result() {
        let mut module = module();
        module.functions[0].blocks[0]
            .instructions
            .push(instruction(None, InstructionKind::Convert(ValueId(1))));
        assert!(has_error(&module, "conversion has no result"));
    }

    #[test]
    fn verifies_array_element_address_types() {
        let mut module = module();
        module.types.push(Type::Array {
            element: TypeId(0),
            length: 2,
        });
        module.functions[0].slots[0] = TypeId(3);
        module.functions[0].blocks[0] = block(
            vec![
                instruction(Some((0, 0)), InstructionKind::Constant(Constant::I32(1))),
                instruction(Some((1, 0)), InstructionKind::Constant(Constant::I32(2))),
                instruction(
                    None,
                    InstructionKind::BoundsCheck {
                        index: ValueId(0),
                        length: ValueId(1),
                    },
                ),
                instruction(
                    Some((2, 2)),
                    InstructionKind::IndexAddress {
                        slot: SlotId(0),
                        index: ValueId(0),
                    },
                ),
                instruction(Some((3, 0)), InstructionKind::Load(ValueId(2))),
            ],
            TerminatorKind::Return(Some(ValueId(3))),
        );
        assert!(verify(&module).is_ok());
        module.functions[0].blocks[0].instructions[3].result = Some((ValueId(2), TypeId(0)));
        assert!(has_error(
            &module,
            "index address has incompatible result type"
        ));
    }

    #[test]
    fn verifies_aggregate_field_address_and_reset() {
        let mut module = module();
        module
            .types
            .push(Type::Aggregate(vec![TypeId(0), TypeId(1)]));
        module.types.push(Type::Address(TypeId(3)));
        module.functions[0].slots[0] = TypeId(3);
        module.functions[0].blocks[0] = block(
            vec![
                instruction(None, InstructionKind::ResetAggregate(SlotId(0))),
                instruction(Some((0, 4)), InstructionKind::SlotAddress(SlotId(0))),
                instruction(
                    Some((1, 2)),
                    InstructionKind::FieldAddress {
                        base: ValueId(0),
                        field: 0,
                    },
                ),
                instruction(Some((2, 0)), InstructionKind::Constant(Constant::I32(7))),
                instruction(
                    None,
                    InstructionKind::Store {
                        address: ValueId(1),
                        value: ValueId(2),
                    },
                ),
                instruction(Some((3, 0)), InstructionKind::Load(ValueId(1))),
            ],
            TerminatorKind::Return(Some(ValueId(3))),
        );
        assert!(verify(&module).is_ok());
        module.functions[0].blocks[0].instructions[2].kind = InstructionKind::FieldAddress {
            base: ValueId(0),
            field: 2,
        };
        assert!(has_error(&module, "invalid field index"));
    }

    #[test]
    fn verifies_aggregate_copy_addresses() {
        let mut module = module();
        module.types.push(Type::Aggregate(vec![TypeId(0)]));
        module.types.push(Type::Address(TypeId(3)));
        module.functions[0].slots = vec![TypeId(3), TypeId(3)];
        module.functions[0].blocks[0] = block(
            vec![
                instruction(Some((0, 4)), InstructionKind::SlotAddress(SlotId(0))),
                instruction(Some((1, 4)), InstructionKind::SlotAddress(SlotId(1))),
                instruction(
                    None,
                    InstructionKind::CopyAggregate {
                        source: ValueId(0),
                        target: ValueId(1),
                    },
                ),
                instruction(Some((2, 0)), InstructionKind::Constant(Constant::I32(7))),
            ],
            TerminatorKind::Return(Some(ValueId(2))),
        );
        assert!(verify(&module).is_ok());
        module.functions[0].blocks[0].instructions[2].result = Some((ValueId(3), TypeId(0)));
        assert!(has_error(
            &module,
            "effect instruction cannot produce a value"
        ));
    }
}
