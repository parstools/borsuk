use crate::core_ir as core;
use crate::structured_ir as structured;
use std::collections::HashMap;

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum EmitError {
    InvalidCore(structured::LowerError),
    Unsupported(&'static str),
    InvalidComment,
}

struct Emitter<'a> {
    output: String,
    comments: Vec<&'a structured::SourceComment>,
    next_comment: usize,
    checked_binary: Vec<core::BinaryOp>,
    function_names: &'a [String],
    types: &'a [core::Type],
    slot_types: &'a [core::TypeId],
    global_types: &'a [core::TypeId],
    value_types: HashMap<core::ValueId, core::TypeId>,
    array_addresses: HashMap<core::ValueId, String>,
    aggregate_bases: HashMap<core::ValueId, (usize, usize)>,
    aggregate_copy: bool,
    float_bits: bool,
    checked_array: bool,
    float_to_i32: bool,
    float_to_u8: bool,
    is_main: bool,
}

fn c_type(types: &[core::Type], id: core::TypeId) -> Result<&'static str, EmitError> {
    match types.get(id.0) {
        Some(core::Type::I32) => Ok("int32_t"),
        Some(core::Type::F32) => Ok("float"),
        Some(core::Type::Bool) => Ok("bool"),
        Some(core::Type::U8) => Ok("uint8_t"),
        _ => Err(EmitError::Unsupported("scalar type")),
    }
}

fn c_value_type(types: &[core::Type], id: core::TypeId) -> Result<String, EmitError> {
    if let Ok(scalar) = c_type(types, id) {
        return Ok(scalar.to_owned());
    }
    if matches!(types.get(id.0), Some(core::Type::Aggregate(_))) {
        return Ok(format!("agsem_t{}", id.0));
    }
    Err(EmitError::Unsupported("value type"))
}

fn aggregate_fields(types: &[core::Type], id: core::TypeId) -> Option<&[core::TypeId]> {
    let core::Type::Aggregate(fields) = types.get(id.0)? else {
        return None;
    };
    if aggregate_leaf_count(types, id).is_none() {
        return None;
    }
    Some(fields)
}

fn aggregate_leaf_count(types: &[core::Type], id: core::TypeId) -> Option<usize> {
    fn count(
        types: &[core::Type],
        id: core::TypeId,
        stack: &mut Vec<core::TypeId>,
    ) -> Option<usize> {
        if c_type(types, id).is_ok() {
            return Some(1);
        }
        let core::Type::Aggregate(fields) = types.get(id.0)? else {
            return None;
        };
        if fields.is_empty() || stack.contains(&id) {
            return None;
        }
        stack.push(id);
        let result = fields.iter().try_fold(0usize, |total, field| {
            total.checked_add(count(types, *field, stack)?)
        });
        stack.pop();
        result
    }
    if !matches!(types.get(id.0), Some(core::Type::Aggregate(_))) {
        return None;
    }
    count(types, id, &mut Vec::new())
}

fn field_leaf_offset(types: &[core::Type], id: core::TypeId, field: usize) -> Option<usize> {
    let fields = aggregate_fields(types, id)?;
    if field >= fields.len() {
        return None;
    }
    fields[..field].iter().try_fold(0usize, |total, ty| {
        let count = if c_type(types, *ty).is_ok() {
            1
        } else {
            aggregate_leaf_count(types, *ty)?
        };
        total.checked_add(count)
    })
}

fn emit_aggregate_typedef(
    output: &mut String,
    types: &[core::Type],
    id: core::TypeId,
    emitted: &mut std::collections::HashSet<core::TypeId>,
) -> Result<(), EmitError> {
    if emitted.contains(&id) {
        return Ok(());
    }
    let fields = aggregate_fields(types, id).ok_or(EmitError::Unsupported("aggregate fields"))?;
    for field in fields {
        if matches!(types.get(field.0), Some(core::Type::Aggregate(_))) {
            emit_aggregate_typedef(output, types, *field, emitted)?;
        }
    }
    output.push_str("typedef struct {\n");
    for (field, ty) in fields.iter().enumerate() {
        output.push_str(&format!(
            "    {} field{field};\n",
            c_value_type(types, *ty)?
        ));
    }
    output.push_str(&format!("}} agsem_t{};\n", id.0));
    emitted.insert(id);
    Ok(())
}

fn valid_slot_type(types: &[core::Type], id: core::TypeId) -> bool {
    match types.get(id.0) {
        Some(core::Type::Array { element, length }) => {
            *length > 0 && i32::try_from(*length).is_ok() && c_type(types, *element).is_ok()
        }
        _ => c_type(types, id).is_ok() || aggregate_fields(types, id).is_some(),
    }
}

impl Emitter<'_> {
    fn line(&mut self, depth: usize, text: &str) {
        self.output.push_str(&"    ".repeat(depth));
        self.output.push_str(text);
        self.output.push('\n');
    }

    fn comments_before(&mut self, position: u32, depth: usize) {
        while let Some(comment) = self.comments.get(self.next_comment) {
            if comment.source.start > position {
                break;
            }
            let text = comment.text.clone();
            self.line(depth, &text);
            self.next_comment += 1;
        }
    }

    fn remaining_comments(&mut self, depth: usize) {
        self.comments_before(u32::MAX, depth);
    }

    fn instruction(
        &mut self,
        instruction: &core::Instruction,
        depth: usize,
    ) -> Result<(), EmitError> {
        if let Some(source) = instruction.source {
            self.comments_before(source.start, depth);
        }
        match (&instruction.kind, instruction.result) {
            (core::InstructionKind::Constant(core::Constant::I32(value)), Some((id, _))) => {
                self.line(
                    depth,
                    &format!("int32_t agsem_v{} = (int32_t)({value}LL);", id.0),
                );
            }
            (core::InstructionKind::Constant(core::Constant::Bool(value)), Some((id, _))) => {
                self.line(depth, &format!("bool agsem_v{} = {};", id.0, value));
            }
            (core::InstructionKind::Constant(core::Constant::F32Bits(bits)), Some((id, _))) => {
                self.float_bits = true;
                self.line(
                    depth,
                    &format!(
                        "float agsem_v{} = agsem_f32_from_bits(UINT32_C(0x{bits:08x}));",
                        id.0
                    ),
                );
            }
            (core::InstructionKind::Constant(core::Constant::U8(value)), Some((id, _))) => {
                self.line(
                    depth,
                    &format!("uint8_t agsem_v{} = UINT8_C({value});", id.0),
                );
            }
            (
                core::InstructionKind::Binary {
                    operation,
                    left,
                    right,
                    mode: core::ArithmeticMode::IeeeF32,
                },
                Some((id, _)),
            ) => {
                let operator = match operation {
                    core::BinaryOp::Add => "+",
                    core::BinaryOp::Subtract => "-",
                    core::BinaryOp::Multiply => "*",
                    core::BinaryOp::Divide => "/",
                };
                self.line(
                    depth,
                    &format!(
                        "float agsem_v{} = agsem_v{} {operator} agsem_v{};",
                        id.0, left.0, right.0
                    ),
                );
            }
            (
                core::InstructionKind::Binary {
                    operation,
                    left,
                    right,
                    mode: core::ArithmeticMode::CheckedI32,
                },
                Some((id, _)),
            ) => {
                let name = match operation {
                    core::BinaryOp::Add => "add",
                    core::BinaryOp::Subtract => "sub",
                    core::BinaryOp::Multiply => "mul",
                    core::BinaryOp::Divide => "div",
                };
                if !self.checked_binary.contains(operation) {
                    self.checked_binary.push(*operation);
                }
                self.line(
                    depth,
                    &format!(
                        "int32_t agsem_v{} = agsem_checked_{name}_i32(agsem_v{}, agsem_v{});",
                        id.0, left.0, right.0,
                    ),
                );
            }
            (core::InstructionKind::SlotAddress(slot), Some((id, _))) => {
                let core::Type::Address(inner) = self
                    .types
                    .get(instruction.result.unwrap().1.0)
                    .ok_or(EmitError::Unsupported("address type"))?
                else {
                    return Err(EmitError::Unsupported("address type"));
                };
                let ty = c_value_type(self.types, *inner)?;
                self.line(
                    depth,
                    &format!("{ty} *agsem_v{} = &agsem_slot{};", id.0, slot.0),
                );
                if aggregate_fields(self.types, *inner).is_some() {
                    self.aggregate_bases.insert(id, (slot.0, 0));
                }
            }
            (core::InstructionKind::GlobalAddress(global), Some((id, _))) => {
                let ty = c_value_type(self.types, self.global_types[global.0])?;
                self.line(
                    depth,
                    &format!("{ty} *agsem_v{} = &agsem_global{};", id.0, global.0),
                );
            }
            (core::InstructionKind::FieldAddress { base, field }, Some((id, ty))) => {
                let base_type = *self
                    .value_types
                    .get(base)
                    .ok_or(EmitError::Unsupported("field base type"))?;
                let core::Type::Address(parent) = self
                    .types
                    .get(base_type.0)
                    .ok_or(EmitError::Unsupported("field base type"))?
                else {
                    return Err(EmitError::Unsupported("field base type"));
                };
                let core::Type::Address(element) = self
                    .types
                    .get(ty.0)
                    .ok_or(EmitError::Unsupported("field address type"))?
                else {
                    return Err(EmitError::Unsupported("field address type"));
                };
                let field_type = c_value_type(self.types, *element)?;
                self.line(
                    depth,
                    &format!(
                        "{field_type} *agsem_v{} = &agsem_v{}->field{field};",
                        id.0, base.0
                    ),
                );
                if let Some(&(slot, parent_offset)) = self.aggregate_bases.get(base) {
                    let offset = parent_offset
                        + field_leaf_offset(self.types, *parent, *field)
                            .ok_or(EmitError::Unsupported("field offset"))?;
                    if aggregate_fields(self.types, *element).is_some() {
                        self.aggregate_bases.insert(id, (slot, offset));
                    } else {
                        self.array_addresses
                            .insert(id, format!("&agsem_slot{slot}_init[{offset}]"));
                    }
                }
            }
            (core::InstructionKind::CopyAggregate { source, target }, None) => {
                let source_ty = *self
                    .value_types
                    .get(source)
                    .ok_or(EmitError::Unsupported("aggregate source"))?;
                let core::Type::Address(aggregate) = self
                    .types
                    .get(source_ty.0)
                    .ok_or(EmitError::Unsupported("aggregate source type"))?
                else {
                    return Err(EmitError::Unsupported("aggregate source type"));
                };
                let count = aggregate_leaf_count(self.types, *aggregate)
                    .ok_or(EmitError::Unsupported("aggregate copy type"))?;
                self.aggregate_copy = true;
                self.line(
                    depth,
                    &format!(
                        "memmove(agsem_v{}, agsem_v{}, sizeof(*agsem_v{}));",
                        target.0, source.0, target.0
                    ),
                );
                if let Some(&(target_slot, target_offset)) = self.aggregate_bases.get(target) {
                    if let Some(&(source_slot, source_offset)) = self.aggregate_bases.get(source) {
                        self.line(depth, &format!("memmove(&agsem_slot{target_slot}_init[{target_offset}], &agsem_slot{source_slot}_init[{source_offset}], {count} * sizeof(bool));"));
                    } else {
                        for leaf in 0..count {
                            self.line(
                                depth,
                                &format!(
                                    "agsem_slot{target_slot}_init[{}] = true;",
                                    target_offset + leaf
                                ),
                            );
                        }
                    }
                }
            }
            (core::InstructionKind::ResetAggregate(slot), None) => {
                let count = aggregate_leaf_count(self.types, self.slot_types[slot.0])
                    .ok_or(EmitError::Unsupported("aggregate reset slot"))?;
                for field in 0..count {
                    self.line(
                        depth,
                        &format!("agsem_slot{}_init[{field}] = false;", slot.0),
                    );
                }
            }
            (core::InstructionKind::ResetArray(slot), None) => {
                let Some(core::Type::Array { length, .. }) =
                    self.types.get(self.slot_types[slot.0].0)
                else {
                    return Err(EmitError::Unsupported("array reset slot"));
                };
                self.line(depth, &format!(
                    "for (int32_t agsem_reset = 0; agsem_reset < {length}; ++agsem_reset) agsem_slot{}_init[agsem_reset] = false;",
                    slot.0,
                ));
            }
            (core::InstructionKind::BoundsCheck { index, length }, None) => {
                self.checked_array = true;
                self.line(depth, &format!(
                    "if (agsem_v{} < 0 || agsem_v{} >= agsem_v{}) agsem_array_error(\"array index out of bounds\");",
                    index.0, index.0, length.0,
                ));
            }
            (core::InstructionKind::IndexAddress { slot, index }, Some((id, _))) => {
                let Some(core::Type::Array { element, length }) =
                    self.types.get(self.slot_types[slot.0].0)
                else {
                    return Err(EmitError::Unsupported("array slot type"));
                };
                let ty = c_type(self.types, *element)?;
                self.checked_array = true;
                self.line(depth, &format!(
                    "if (agsem_v{} < 0 || agsem_v{} >= {length}) agsem_array_error(\"array index out of bounds\");",
                    index.0, index.0,
                ));
                self.line(
                    depth,
                    &format!(
                        "{ty} *agsem_v{} = &agsem_slot{}[agsem_v{}];",
                        id.0, slot.0, index.0,
                    ),
                );
                let flag = format!("agsem_init_v{}", id.0);
                self.line(
                    depth,
                    &format!(
                        "bool *{flag} = &agsem_slot{}_init[agsem_v{}];",
                        slot.0, index.0,
                    ),
                );
                self.array_addresses.insert(id, flag);
            }
            (core::InstructionKind::GlobalIndexAddress { global, index }, Some((id, _))) => {
                let Some(core::Type::Array { element, length }) =
                    self.types.get(self.global_types[global.0].0)
                else {
                    return Err(EmitError::Unsupported("global array type"));
                };
                let ty = c_type(self.types, *element)?;
                self.checked_array = true;
                self.line(depth, &format!(
                    "if (agsem_v{} < 0 || agsem_v{} >= {length}) agsem_array_error(\"array index out of bounds\");",
                    index.0, index.0,
                ));
                self.line(
                    depth,
                    &format!(
                        "{ty} *agsem_v{} = &agsem_global{}[agsem_v{}];",
                        id.0, global.0, index.0,
                    ),
                );
            }
            (core::InstructionKind::Load(address), Some((id, _))) => {
                if let Some(flag) = self.array_addresses.get(address) {
                    self.checked_array = true;
                    self.line(depth, &format!(
                        "if (!*{flag}) agsem_array_error(\"variable used before initialization\");"
                    ));
                }
                let ty = c_type(self.types, instruction.result.unwrap().1)?;
                self.line(
                    depth,
                    &format!("{ty} agsem_v{} = *agsem_v{};", id.0, address.0),
                );
            }
            (core::InstructionKind::Store { address, value }, None) => {
                self.line(
                    depth,
                    &format!("*agsem_v{} = agsem_v{};", address.0, value.0),
                );
                if let Some(flag) = self.array_addresses.get(address) {
                    self.line(depth, &format!("*{flag} = true;"));
                }
            }
            (core::InstructionKind::Convert(value), Some((id, target))) => {
                let source = *self
                    .value_types
                    .get(value)
                    .ok_or(EmitError::Unsupported("conversion source"))?;
                let from = self
                    .types
                    .get(source.0)
                    .ok_or(EmitError::Unsupported("conversion source type"))?;
                let to = self
                    .types
                    .get(target.0)
                    .ok_or(EmitError::Unsupported("conversion target type"))?;
                if !matches!(
                    (from, to),
                    (
                        core::Type::U8,
                        core::Type::I32 | core::Type::F32 | core::Type::Bool
                    ) | (
                        core::Type::I32,
                        core::Type::F32 | core::Type::Bool | core::Type::U8
                    ) | (
                        core::Type::F32,
                        core::Type::Bool | core::Type::I32 | core::Type::U8
                    )
                ) && source != target
                {
                    return Err(EmitError::Unsupported("conversion"));
                }
                let ty = c_type(self.types, target)?;
                let expression = match (from, to) {
                    (core::Type::F32, core::Type::I32) => {
                        self.float_to_i32 = true;
                        format!("agsem_f32_to_i32(agsem_v{})", value.0)
                    }
                    (core::Type::F32, core::Type::U8) => {
                        self.float_to_u8 = true;
                        format!("agsem_f32_to_u8(agsem_v{})", value.0)
                    }
                    _ => format!("({ty})agsem_v{}", value.0),
                };
                self.line(depth, &format!("{ty} agsem_v{} = {expression};", id.0));
            }
            (
                core::InstructionKind::Call {
                    function,
                    arguments,
                },
                Some((id, _)),
            ) => {
                let name = self
                    .function_names
                    .get(function.0)
                    .ok_or(EmitError::Unsupported("call target"))?;
                let arguments = arguments
                    .iter()
                    .map(|argument| format!("agsem_v{}", argument.0))
                    .collect::<Vec<_>>()
                    .join(", ");
                let ty = c_type(self.types, instruction.result.unwrap().1)?;
                self.line(
                    depth,
                    &format!("{ty} agsem_v{} = {name}({arguments});", id.0),
                );
            }
            (
                core::InstructionKind::Compare {
                    operation,
                    left,
                    right,
                },
                Some((id, _)),
            ) => {
                let operator = match operation {
                    core::CompareOp::Equal => "==",
                    core::CompareOp::NotEqual => "!=",
                    core::CompareOp::Less => "<",
                    core::CompareOp::LessEqual => "<=",
                    core::CompareOp::Greater => ">",
                    core::CompareOp::GreaterEqual => ">=",
                };
                self.line(
                    depth,
                    &format!(
                        "bool agsem_v{} = agsem_v{} {operator} agsem_v{};",
                        id.0, left.0, right.0
                    ),
                );
            }
            _ => return Err(EmitError::Unsupported("instruction")),
        }
        if let Some((id, ty)) = instruction.result {
            self.value_types.insert(id, ty);
        }
        Ok(())
    }

    fn block(&mut self, block: &structured::Block, depth: usize) -> Result<(), EmitError> {
        for statement in &block.statements {
            match statement {
                structured::Statement::Instruction(instruction) => {
                    self.instruction(instruction, depth)?;
                }
                structured::Statement::Block(inner) => {
                    self.line(depth, "{");
                    self.block(inner, depth + 1)?;
                    self.line(depth, "}");
                }
                structured::Statement::Return {
                    value: Some(value),
                    source,
                } => {
                    if let Some(source) = source {
                        self.comments_before(source.start, depth);
                    }
                    if self.is_main {
                        self.line(depth, &format!("return (int)agsem_v{};", value.0));
                    } else {
                        self.line(depth, &format!("return agsem_v{};", value.0));
                    }
                }
                structured::Statement::Return { value: None, .. } => {
                    return Err(EmitError::Unsupported("void return"));
                }
                structured::Statement::If {
                    condition,
                    then_branch,
                    else_branch,
                    source,
                } => {
                    if let Some(source) = source {
                        self.comments_before(source.start, depth);
                    }
                    self.line(depth, &format!("if (agsem_v{}) {{", condition.0));
                    self.block(then_branch, depth + 1)?;
                    if let Some(other) = else_branch {
                        self.line(depth, "} else {");
                        self.block(other, depth + 1)?;
                    }
                    self.line(depth, "}");
                }
                structured::Statement::While {
                    condition_instructions,
                    condition_value,
                    body,
                    source,
                } => {
                    if let Some(source) = source {
                        self.comments_before(source.start, depth);
                    }
                    self.line(depth, "while (1) {");
                    for instruction in condition_instructions {
                        self.instruction(instruction, depth + 1)?;
                    }
                    self.line(
                        depth + 1,
                        &format!("if (!agsem_v{}) break;", condition_value.0),
                    );
                    self.block(body, depth + 1)?;
                    self.line(depth, "}");
                }
                structured::Statement::For {
                    initialization,
                    condition_instructions,
                    condition_value,
                    body,
                    update,
                    source,
                } => {
                    if let Some(source) = source {
                        self.comments_before(source.start, depth);
                    }
                    self.line(depth, "{");
                    self.block(initialization, depth + 1)?;
                    self.line(depth + 1, "while (1) {");
                    for instruction in condition_instructions {
                        self.instruction(instruction, depth + 2)?;
                    }
                    self.line(
                        depth + 2,
                        &format!("if (!agsem_v{}) break;", condition_value.0),
                    );
                    self.block(body, depth + 2)?;
                    self.block(update, depth + 2)?;
                    self.line(depth + 1, "}");
                    self.line(depth, "}");
                }
            }
        }
        Ok(())
    }
}

fn valid_comment(text: &str) -> bool {
    (text.starts_with("//") && !text.contains(['\n', '\r']))
        || (text.len() >= 4
            && text.starts_with("/*")
            && text.ends_with("*/")
            && !text[2..text.len() - 2].contains("*/"))
}

pub fn emit_c(module: &structured::Module) -> Result<String, EmitError> {
    structured::lower_to_cfg(module).map_err(EmitError::InvalidCore)?;
    let main_functions = module
        .functions
        .iter()
        .filter(|function| function.name == "main")
        .count();
    if main_functions != 1 {
        return Err(EmitError::Unsupported("main function count"));
    }
    for function in &module.functions {
        let result = function
            .result
            .ok_or(EmitError::Unsupported("result type"))?;
        if (function.name == "main"
            && (!function.parameters.is_empty()
                || module.types.get(result.0) != Some(&core::Type::I32)))
            || c_type(&module.types, result).is_err()
        {
            return Err(EmitError::Unsupported("function signature"));
        }
        if function
            .slots
            .iter()
            .any(|slot| !valid_slot_type(&module.types, *slot))
        {
            return Err(EmitError::Unsupported("slot type"));
        }
    }
    if module
        .globals
        .iter()
        .any(|ty| !valid_slot_type(&module.types, *ty))
    {
        return Err(EmitError::Unsupported("global type"));
    }
    if module
        .comments
        .iter()
        .any(|comment| !valid_comment(&comment.text))
    {
        return Err(EmitError::InvalidComment);
    }
    let function_names: Vec<_> = module
        .functions
        .iter()
        .enumerate()
        .map(|(index, function)| {
            if function.name == "main" {
                "main".to_string()
            } else {
                format!("agsem_f{index}")
            }
        })
        .collect();
    let mut bodies = String::new();
    let mut checked_binary = Vec::new();
    let mut float_bits = false;
    let mut checked_array = false;
    let mut float_to_i32 = false;
    let mut float_to_u8 = false;
    let mut aggregate_copy = false;
    for (index, function) in module.functions.iter().enumerate() {
        let body_source = function.body.source;
        let mut emitter = Emitter {
            output: String::new(),
            comments: module
                .comments
                .iter()
                .filter(|comment| {
                    body_source.is_some_and(|body| {
                        body.file == comment.source.file
                            && body.start <= comment.source.start
                            && comment.source.end <= body.end
                    })
                })
                .collect(),
            next_comment: 0,
            checked_binary: Vec::new(),
            function_names: &function_names,
            types: &module.types,
            slot_types: &function.slots,
            global_types: &module.globals,
            value_types: HashMap::new(),
            array_addresses: HashMap::new(),
            aggregate_bases: HashMap::new(),
            aggregate_copy: false,
            float_bits: false,
            checked_array: false,
            float_to_i32: false,
            float_to_u8: false,
            is_main: function.name == "main",
        };
        emitter
            .comments
            .sort_by_key(|comment| (comment.source.file, comment.source.start));
        let parameters = if function.name == "main" {
            "void".to_string()
        } else {
            function
                .parameters
                .iter()
                .enumerate()
                .map(|(position, slot)| {
                    Ok(format!(
                        "{} agsem_p{position}",
                        c_type(&module.types, function.slots[slot.0])?
                    ))
                })
                .collect::<Result<Vec<_>, EmitError>>()?
                .join(", ")
        };
        let prefix = if function.name == "main" {
            "int".to_string()
        } else {
            format!(
                "static {}",
                c_type(&module.types, function.result.unwrap())?
            )
        };
        emitter.line(
            0,
            &format!("{prefix} {}({parameters}) {{", function_names[index]),
        );
        if function.name == "main" {
            for global in 0..module.globals.len() {
                emitter.line(1, &format!("(void)agsem_global{global};"));
            }
        }
        for slot in 0..function.slots.len() {
            if let Some(count) = aggregate_leaf_count(&module.types, function.slots[slot]) {
                let ty = c_value_type(&module.types, function.slots[slot])?;
                emitter.line(1, &format!("{ty} agsem_slot{slot};"));
                emitter.line(
                    1,
                    &format!("bool agsem_slot{slot}_init[{count}] = {{false}};"),
                );
                emitter.line(1, &format!("(void)&agsem_slot{slot};"));
                emitter.line(1, &format!("(void)agsem_slot{slot}_init;"));
                continue;
            }
            if let Some(core::Type::Array { element, length }) =
                module.types.get(function.slots[slot].0)
            {
                let ty = c_type(&module.types, *element)?;
                emitter.line(1, &format!("{ty} agsem_slot{slot}[{length}];"));
                emitter.line(
                    1,
                    &format!("bool agsem_slot{slot}_init[{length}] = {{false}};"),
                );
                emitter.line(1, &format!("(void)agsem_slot{slot};"));
                emitter.line(1, &format!("(void)agsem_slot{slot}_init;"));
                continue;
            }
            let ty = c_type(&module.types, function.slots[slot])?;
            if let Some(position) = function
                .parameters
                .iter()
                .position(|parameter| parameter.0 == slot)
            {
                emitter.line(1, &format!("{ty} agsem_slot{slot} = agsem_p{position};"));
            } else {
                emitter.line(1, &format!("{ty} agsem_slot{slot};"));
            }
            emitter.line(1, &format!("(void)&agsem_slot{slot};"));
        }
        emitter.block(&function.body, 1)?;
        emitter.remaining_comments(1);
        emitter.line(0, "}");
        bodies.push_str(&emitter.output);
        float_bits |= emitter.float_bits;
        checked_array |= emitter.checked_array;
        float_to_i32 |= emitter.float_to_i32;
        float_to_u8 |= emitter.float_to_u8;
        aggregate_copy |= emitter.aggregate_copy;
        for operation in emitter.checked_binary {
            if !checked_binary.contains(&operation) {
                checked_binary.push(operation);
            }
        }
    }

    let mut output = String::from("#include <stdbool.h>\n#include <stdint.h>\n");
    if aggregate_copy {
        output.push_str("#include <string.h>\n");
    }
    output.push_str("_Static_assert(sizeof(int) >= sizeof(int32_t), \"32-bit int required\");\n");
    let mut emitted_aggregates = std::collections::HashSet::new();
    for (index, ty) in module.types.iter().enumerate() {
        if let core::Type::Aggregate(_) = ty {
            emit_aggregate_typedef(
                &mut output,
                &module.types,
                core::TypeId(index),
                &mut emitted_aggregates,
            )?;
        }
    }
    if float_to_i32 {
        output.push_str("static int32_t agsem_f32_to_i32(float value) {\n    if (value != value) return 0;\n    if (value >= 2147483648.0f) return INT32_MAX;\n    if (value <= -2147483648.0f) return INT32_MIN;\n    return (int32_t)value;\n}\n");
    }
    if float_to_u8 {
        output.push_str("static uint8_t agsem_f32_to_u8(float value) {\n    if (value != value || value <= 0.0f) return 0;\n    if (value >= 255.0f) return UINT8_MAX;\n    return (uint8_t)value;\n}\n");
    }
    if float_bits {
        output.push_str("#include <float.h>\n#include <string.h>\n");
        output.push_str("_Static_assert(sizeof(float) == sizeof(uint32_t) && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128, \"IEEE binary32 required\");\n");
        output.push_str("static float agsem_f32_from_bits(uint32_t bits) {\n    float value;\n    memcpy(&value, &bits, sizeof(value));\n    return value;\n}\n");
    }
    if checked_array {
        output.push_str("#include <stdio.h>\n#include <stdlib.h>\n");
        output.push_str("static void agsem_array_error(const char *message) {\n    fputs(message, stderr);\n    fputc('\\n', stderr);\n    exit(EXIT_FAILURE);\n}\n");
    }
    if !checked_binary.is_empty() {
        output.push_str("#include <stdio.h>\n#include <stdlib.h>\n");
        output.push_str(
            r#"static void agsem_arithmetic_error(const char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}
"#,
        );
        for operation in &checked_binary {
            let (name, symbol) = match operation {
                core::BinaryOp::Add => ("add", "+"),
                core::BinaryOp::Subtract => ("sub", "-"),
                core::BinaryOp::Multiply => ("mul", "*"),
                core::BinaryOp::Divide => ("div", "/"),
            };
            output.push_str("static int32_t agsem_checked_");
            output.push_str(name);
            output.push_str("_i32(int32_t left, int32_t right) {\n");
            if *operation == core::BinaryOp::Divide {
                output.push_str(
                    "    if (right == 0) agsem_arithmetic_error(\"division by zero\");\n",
                );
                output.push_str("    if (left == INT32_MIN && right == -1) agsem_arithmetic_error(\"integer overflow\");\n");
                output.push_str("    return left / right;\n");
            } else {
                output.push_str("    int64_t result = (int64_t)left ");
                output.push_str(symbol);
                output.push_str(" (int64_t)right;\n");
                output.push_str("    if (result < INT32_MIN || result > INT32_MAX) agsem_arithmetic_error(\"integer overflow\");\n");
                output.push_str("    return (int32_t)result;\n");
            }
            output.push_str("}\n");
        }
    }
    for (index, global) in module.globals.iter().enumerate() {
        match &module.types[global.0] {
            core::Type::Array { element, length } => {
                output.push_str(&format!(
                    "static {} agsem_global{index}[{length}];\n",
                    c_type(&module.types, *element)?
                ));
            }
            core::Type::Aggregate(_) => output.push_str(&format!(
                "static {} agsem_global{index};\n",
                c_value_type(&module.types, *global)?
            )),
            _ => output.push_str(&format!(
                "static {} agsem_global{index};\n",
                c_type(&module.types, *global)?
            )),
        }
    }
    for (index, function) in module.functions.iter().enumerate() {
        let parameters = if function.parameters.is_empty() {
            "void".to_string()
        } else {
            function
                .parameters
                .iter()
                .map(|slot| c_type(&module.types, function.slots[slot.0]))
                .collect::<Result<Vec<_>, _>>()?
                .join(", ")
        };
        let prefix = if function.name == "main" {
            "int".to_string()
        } else {
            format!(
                "static {}",
                c_type(&module.types, function.result.unwrap())?
            )
        };
        output.push_str(&format!(
            "{prefix} {}({parameters});\n",
            function_names[index]
        ));
    }
    output.push_str(&bodies);
    Ok(output)
}
