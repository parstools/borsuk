use crate::core_ir as core;
use crate::structured_ir as structured;
use std::collections::HashMap;

#[derive(Clone, Copy, Debug)]
pub struct TargetSpec<'a> {
    pub triple: &'a str,
    pub data_layout: &'a str,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum EmitError {
    InvalidCore(structured::LowerError),
    InvalidTarget,
    Unsupported(&'static str),
}

fn valid_target_field(value: &str) -> bool {
    !value.is_empty()
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b':' | b'.'))
}

fn emit_bounds_check(output: &mut String, step: &str, index: &str, length: &str) {
    output.push_str(&format!(
        "  %negative_{step} = icmp slt i32 {index}, 0\n  %past_end_{step} = icmp sge i32 {index}, {length}\n  %invalid_{step} = or i1 %negative_{step}, %past_end_{step}\n  br i1 %invalid_{step}, label %array_bounds_error, label %bounds_ok_{step}\nbounds_ok_{step}:\n"
    ));
}

fn scalar_type(types: &[core::Type], id: core::TypeId) -> Result<&'static str, EmitError> {
    match types.get(id.0) {
        Some(core::Type::I32) => Ok("i32"),
        Some(core::Type::F32) => Ok("float"),
        Some(core::Type::Bool) => Ok("i1"),
        Some(core::Type::U8) => Ok("i8"),
        _ => Err(EmitError::Unsupported("scalar type")),
    }
}

fn array_type(types: &[core::Type], id: core::TypeId) -> Option<(usize, &'static str)> {
    let core::Type::Array { element, length } = types.get(id.0)? else {
        return None;
    };
    if *length == 0 || i32::try_from(*length).is_err() {
        return None;
    }
    Some((*length, scalar_type(types, *element).ok()?))
}

fn aggregate_type(types: &[core::Type], id: core::TypeId) -> Option<(usize, String)> {
    fn layout(
        types: &[core::Type],
        id: core::TypeId,
        stack: &mut Vec<core::TypeId>,
    ) -> Option<(usize, String)> {
        if let Ok(scalar) = scalar_type(types, id) {
            return Some((1, scalar.to_owned()));
        }
        let core::Type::Aggregate(fields) = types.get(id.0)? else {
            return None;
        };
        if fields.is_empty() || stack.contains(&id) {
            return None;
        }
        stack.push(id);
        let mut count = 0usize;
        let mut elements = Vec::new();
        for field in fields {
            let (leaves, element) = layout(types, *field, stack)?;
            count = count.checked_add(leaves)?;
            elements.push(element);
        }
        stack.pop();
        Some((count, format!("{{ {} }}", elements.join(", "))))
    }
    if !matches!(types.get(id.0), Some(core::Type::Aggregate(_))) {
        return None;
    }
    layout(types, id, &mut Vec::new())
}

fn aggregate_field_offset(types: &[core::Type], id: core::TypeId, field: usize) -> Option<usize> {
    let core::Type::Aggregate(fields) = types.get(id.0)? else {
        return None;
    };
    if field >= fields.len() {
        return None;
    }
    fields[..field].iter().try_fold(0usize, |total, ty| {
        let leaves = if scalar_type(types, *ty).is_ok() {
            1
        } else {
            aggregate_type(types, *ty)?.0
        };
        total.checked_add(leaves)
    })
}

fn address_element_type(types: &[core::Type], id: core::TypeId) -> Result<&'static str, EmitError> {
    let Some(core::Type::Address(element)) = types.get(id.0) else {
        return Err(EmitError::Unsupported("address type"));
    };
    scalar_type(types, *element)
}

pub fn emit_llvm(module: &structured::Module, target: TargetSpec<'_>) -> Result<String, EmitError> {
    let cfg = structured::lower_to_cfg(module).map_err(EmitError::InvalidCore)?;
    if !valid_target_field(target.triple) || !valid_target_field(target.data_layout) {
        return Err(EmitError::InvalidTarget);
    }
    for global in &cfg.globals {
        if scalar_type(&cfg.types, *global).is_err()
            && array_type(&cfg.types, *global).is_none()
            && aggregate_type(&cfg.types, *global).is_none()
        {
            return Err(EmitError::Unsupported("global type"));
        }
    }
    if cfg
        .functions
        .iter()
        .filter(|function| function.name == "main")
        .count()
        != 1
    {
        return Err(EmitError::Unsupported("main function count"));
    }
    for function in &cfg.functions {
        let result = function
            .result
            .ok_or(EmitError::Unsupported("void result"))?;
        if (function.name == "main"
            && (!function.parameters.is_empty()
                || cfg.types.get(result.0) != Some(&core::Type::I32)))
            || scalar_type(&cfg.types, result).is_err()
            || function.slots.iter().any(|slot| {
                scalar_type(&cfg.types, *slot).is_err()
                    && array_type(&cfg.types, *slot).is_none()
                    && aggregate_type(&cfg.types, *slot).is_none()
            })
            || function.parameters.iter().any(|slot| {
                function
                    .slots
                    .get(slot.0)
                    .is_none_or(|ty| scalar_type(&cfg.types, *ty).is_err())
            })
        {
            return Err(EmitError::Unsupported("function signature"));
        }
    }
    let mut output = format!(
        "target datalayout = \"{}\"\ntarget triple = \"{}\"\n\n",
        target.data_layout, target.triple
    );
    for global in 0..cfg.globals.len() {
        if let Ok(ty) = scalar_type(&cfg.types, cfg.globals[global]) {
            let zero = if ty == "float" { "0.000000e+00" } else { "0" };
            output.push_str(&format!(
                "@agsem_global{global} = internal global {ty} {zero}\n"
            ));
        } else if let Some((length, element)) = array_type(&cfg.types, cfg.globals[global]) {
            output.push_str(&format!(
                "@agsem_global{global} = internal global [{length} x {element}] zeroinitializer\n"
            ));
        } else if let Some((_, layout)) = aggregate_type(&cfg.types, cfg.globals[global]) {
            output.push_str(&format!(
                "@agsem_global{global} = internal global {layout} zeroinitializer\n"
            ));
        }
    }
    let function_names: Vec<_> = cfg
        .functions
        .iter()
        .enumerate()
        .map(|(index, function)| {
            if function.name == "main" {
                "main".to_owned()
            } else {
                format!("agsem_f{index}")
            }
        })
        .collect();
    let mut module_needs_overflow = false;
    let mut module_needs_division_by_zero = false;
    let mut module_needs_bounds = false;
    let mut module_needs_uninitialized = false;
    let mut module_needs_f32_to_i32 = false;
    let mut module_needs_f32_to_u8 = false;
    let mut module_needs_memmove = false;
    let mut module_needs_memset = false;
    for (function_index, function) in cfg.functions.iter().enumerate() {
        let parameters = (0..function.parameters.len())
            .map(|index| {
                let ty = function.slots[function.parameters[index].0];
                scalar_type(&cfg.types, ty).map(|ty| format!("{ty} %param{index}"))
            })
            .collect::<Result<Vec<_>, _>>()?
            .join(", ");
        let result_type = scalar_type(&cfg.types, function.result.unwrap())?;
        let linkage = if function.name == "main" {
            ""
        } else {
            "internal "
        };
        output.push_str(&format!(
            "define {linkage}{result_type} @{}({parameters}) {{\nentry:\n",
            function_names[function_index]
        ));
        for slot in 0..function.slots.len() {
            if let Ok(ty) = scalar_type(&cfg.types, function.slots[slot]) {
                output.push_str(&format!("  %slot{slot} = alloca {ty}\n"));
            } else if let Some((length, element)) = array_type(&cfg.types, function.slots[slot]) {
                output.push_str(&format!(
                    "  %slot{slot} = alloca [{length} x {element}]\n  %slot{slot}_init = alloca [{length} x i8]\n  store [{length} x i8] zeroinitializer, ptr %slot{slot}_init\n"
                ));
            } else if let Some((length, layout)) = aggregate_type(&cfg.types, function.slots[slot])
            {
                output.push_str(&format!("  %slot{slot} = alloca {layout}\n  %slot{slot}_init = alloca [{length} x i8]\n  store [{length} x i8] zeroinitializer, ptr %slot{slot}_init\n"));
            }
        }
        for (position, slot) in function.parameters.iter().enumerate() {
            let ty = scalar_type(&cfg.types, function.slots[slot.0])?;
            output.push_str(&format!(
                "  store {ty} %param{position}, ptr %slot{}\n",
                slot.0
            ));
        }
        output.push_str(&format!("  br label %b{}\n", function.entry.0));
        let mut needs_overflow = false;
        let mut needs_division_by_zero = false;
        let mut needs_bounds = false;
        let mut needs_uninitialized = false;
        for (block_number, block) in function.blocks.iter().enumerate() {
            output.push_str(&format!("b{block_number}:\n"));
            let mut values = HashMap::<core::ValueId, String>::new();
            let mut value_types = HashMap::<core::ValueId, core::TypeId>::new();
            let mut array_flags = HashMap::<core::ValueId, String>::new();
            let mut aggregate_bases = HashMap::<core::ValueId, (usize, usize)>::new();
            for (instruction_number, instruction) in block.instructions.iter().enumerate() {
                match (&instruction.kind, instruction.result) {
                    (
                        core::InstructionKind::Constant(core::Constant::I32(number)),
                        Some((id, _)),
                    ) => {
                        values.insert(id, number.to_string());
                    }
                    (
                        core::InstructionKind::Constant(core::Constant::Bool(value)),
                        Some((id, _)),
                    ) => {
                        values.insert(id, if *value { "true" } else { "false" }.to_owned());
                    }
                    (core::InstructionKind::Constant(core::Constant::U8(value)), Some((id, _))) => {
                        values.insert(id, value.to_string());
                    }
                    (
                        core::InstructionKind::Constant(core::Constant::F32Bits(bits)),
                        Some((id, _)),
                    ) => {
                        output.push_str(&format!(
                            "  %b{block_number}_v{} = bitcast i32 {bits} to float\n",
                            id.0
                        ));
                        values.insert(id, format!("%b{block_number}_v{}", id.0));
                    }
                    (core::InstructionKind::SlotAddress(slot), Some((id, _))) => {
                        values.insert(id, format!("%slot{}", slot.0));
                        if function
                            .slots
                            .get(slot.0)
                            .and_then(|ty| aggregate_type(&cfg.types, *ty))
                            .is_some()
                        {
                            aggregate_bases.insert(id, (slot.0, 0));
                        }
                    }
                    (core::InstructionKind::GlobalAddress(global), Some((id, _))) => {
                        values.insert(id, format!("@agsem_global{}", global.0));
                    }
                    (core::InstructionKind::FieldAddress { base, field }, Some((id, _))) => {
                        let base_id = *base;
                        let base_type = *value_types
                            .get(base)
                            .ok_or(EmitError::Unsupported("field base type"))?;
                        let Some(core::Type::Address(aggregate)) = cfg.types.get(base_type.0)
                        else {
                            return Err(EmitError::Unsupported("field base type"));
                        };
                        let (_, layout) = aggregate_type(&cfg.types, *aggregate)
                            .ok_or(EmitError::Unsupported("aggregate type"))?;
                        let base = values
                            .get(base)
                            .ok_or(EmitError::Unsupported("field base"))?;
                        let step = format!("b{block_number}_v{}", id.0);
                        output.push_str(&format!(
                            "  %{step} = getelementptr {layout}, ptr {base}, i32 0, i32 {field}\n"
                        ));
                        values.insert(id, format!("%{step}"));
                        if let Some(&(slot, parent_offset)) = aggregate_bases.get(&base_id) {
                            let offset = parent_offset
                                + aggregate_field_offset(&cfg.types, *aggregate, *field)
                                    .ok_or(EmitError::Unsupported("field offset"))?;
                            let core::Type::Address(element) = cfg
                                .types
                                .get(instruction.result.unwrap().1.0)
                                .ok_or(EmitError::Unsupported("field address type"))?
                            else {
                                return Err(EmitError::Unsupported("field address type"));
                            };
                            if aggregate_type(&cfg.types, *element).is_some() {
                                aggregate_bases.insert(id, (slot, offset));
                            } else {
                                let root_length = aggregate_type(&cfg.types, function.slots[slot])
                                    .ok_or(EmitError::Unsupported("aggregate slot"))?
                                    .0;
                                output.push_str(&format!("  %{step}_init = getelementptr [{root_length} x i8], ptr %slot{slot}_init, i32 0, i32 {offset}\n"));
                                array_flags.insert(id, format!("%{step}_init"));
                            }
                        }
                    }
                    (core::InstructionKind::CopyAggregate { source, target }, None) => {
                        let source_type = *value_types
                            .get(source)
                            .ok_or(EmitError::Unsupported("aggregate source type"))?;
                        let core::Type::Address(aggregate) = cfg
                            .types
                            .get(source_type.0)
                            .ok_or(EmitError::Unsupported("aggregate source type"))?
                        else {
                            return Err(EmitError::Unsupported("aggregate source type"));
                        };
                        let (count, layout) = aggregate_type(&cfg.types, *aggregate)
                            .ok_or(EmitError::Unsupported("aggregate copy type"))?;
                        let source_address = values
                            .get(source)
                            .ok_or(EmitError::Unsupported("aggregate source"))?;
                        let target_address = values
                            .get(target)
                            .ok_or(EmitError::Unsupported("aggregate target"))?;
                        let step = format!("b{block_number}_i{instruction_number}");
                        output.push_str(&format!("  %size_ptr_{step} = getelementptr {layout}, ptr null, i32 1\n  %size_{step} = ptrtoint ptr %size_ptr_{step} to i64\n  call void @llvm.memmove.p0.p0.i64(ptr {target_address}, ptr {source_address}, i64 %size_{step}, i1 false)\n"));
                        module_needs_memmove = true;
                        if let Some(&(target_slot, target_offset)) = aggregate_bases.get(target) {
                            let target_length =
                                aggregate_type(&cfg.types, function.slots[target_slot])
                                    .ok_or(EmitError::Unsupported("aggregate target slot"))?
                                    .0;
                            output.push_str(&format!("  %target_flags_{step} = getelementptr [{target_length} x i8], ptr %slot{target_slot}_init, i32 0, i32 {target_offset}\n"));
                            if let Some(&(source_slot, source_offset)) = aggregate_bases.get(source)
                            {
                                let source_length =
                                    aggregate_type(&cfg.types, function.slots[source_slot])
                                        .ok_or(EmitError::Unsupported("aggregate source slot"))?
                                        .0;
                                output.push_str(&format!("  %source_flags_{step} = getelementptr [{source_length} x i8], ptr %slot{source_slot}_init, i32 0, i32 {source_offset}\n  call void @llvm.memmove.p0.p0.i64(ptr %target_flags_{step}, ptr %source_flags_{step}, i64 {count}, i1 false)\n"));
                            } else {
                                output.push_str(&format!("  call void @llvm.memset.p0.i64(ptr %target_flags_{step}, i8 1, i64 {count}, i1 false)\n"));
                                module_needs_memset = true;
                            }
                        }
                    }
                    (core::InstructionKind::ResetAggregate(slot), None) => {
                        let (length, _) = function
                            .slots
                            .get(slot.0)
                            .and_then(|ty| aggregate_type(&cfg.types, *ty))
                            .ok_or(EmitError::Unsupported("aggregate reset slot"))?;
                        output.push_str(&format!(
                            "  store [{length} x i8] zeroinitializer, ptr %slot{}_init\n",
                            slot.0
                        ));
                    }
                    (core::InstructionKind::ResetArray(slot), None) => {
                        let (length, _) = function
                            .slots
                            .get(slot.0)
                            .and_then(|ty| array_type(&cfg.types, *ty))
                            .ok_or(EmitError::Unsupported("local array"))?;
                        output.push_str(&format!(
                            "  store [{length} x i8] zeroinitializer, ptr %slot{}_init\n",
                            slot.0
                        ));
                    }
                    (core::InstructionKind::BoundsCheck { index, length }, None) => {
                        if !target.triple.starts_with("x86_64-")
                            || !target.triple.contains("-linux-")
                        {
                            return Err(EmitError::Unsupported("checked array target"));
                        }
                        let index = values
                            .get(index)
                            .ok_or(EmitError::Unsupported("bounds index"))?;
                        let length = values
                            .get(length)
                            .ok_or(EmitError::Unsupported("bounds length"))?;
                        emit_bounds_check(
                            &mut output,
                            &format!("b{block_number}_i{instruction_number}"),
                            index,
                            length,
                        );
                        needs_bounds = true;
                    }
                    (core::InstructionKind::IndexAddress { slot, index }, Some((id, _))) => {
                        if !target.triple.starts_with("x86_64-")
                            || !target.triple.contains("-linux-")
                        {
                            return Err(EmitError::Unsupported("checked array target"));
                        }
                        let (length, element) = function
                            .slots
                            .get(slot.0)
                            .and_then(|ty| array_type(&cfg.types, *ty))
                            .ok_or(EmitError::Unsupported("local array"))?;
                        let index = values
                            .get(index)
                            .ok_or(EmitError::Unsupported("array index"))?;
                        let step = format!("b{block_number}_v{}", id.0);
                        emit_bounds_check(&mut output, &step, index, &length.to_string());
                        needs_bounds = true;
                        output.push_str(&format!(
                            "  %{step} = getelementptr [{length} x {element}], ptr %slot{}, i32 0, i32 {index}\n  %{step}_init = getelementptr [{length} x i8], ptr %slot{}_init, i32 0, i32 {index}\n",
                            slot.0, slot.0
                        ));
                        values.insert(id, format!("%{step}"));
                        array_flags.insert(id, format!("%{step}_init"));
                    }
                    (
                        core::InstructionKind::GlobalIndexAddress { global, index },
                        Some((id, _)),
                    ) => {
                        if !target.triple.starts_with("x86_64-")
                            || !target.triple.contains("-linux-")
                        {
                            return Err(EmitError::Unsupported("checked array target"));
                        }
                        let (length, element) = cfg
                            .globals
                            .get(global.0)
                            .and_then(|ty| array_type(&cfg.types, *ty))
                            .ok_or(EmitError::Unsupported("global array"))?;
                        let index = values
                            .get(index)
                            .ok_or(EmitError::Unsupported("array index"))?;
                        let step = format!("b{block_number}_v{}", id.0);
                        emit_bounds_check(&mut output, &step, index, &length.to_string());
                        needs_bounds = true;
                        output.push_str(&format!(
                        "  %{step} = getelementptr [{length} x {element}], ptr @agsem_global{}, i32 0, i32 {index}\n",
                        global.0,
                    ));
                        values.insert(id, format!("%{step}"));
                    }
                    (core::InstructionKind::Load(address), Some((id, _))) => {
                        let address_type = *value_types
                            .get(address)
                            .ok_or(EmitError::Unsupported("load address type"))?;
                        let ty = address_element_type(&cfg.types, address_type)?;
                        if let Some(flag) = array_flags.get(address) {
                            if !target.triple.starts_with("x86_64-")
                                || !target.triple.contains("-linux-")
                            {
                                return Err(EmitError::Unsupported("checked array target"));
                            }
                            let step = format!("b{block_number}_i{instruction_number}");
                            output.push_str(&format!(
                                "  %initialized_{step} = load i8, ptr {flag}\n  %uninitialized_{step} = icmp eq i8 %initialized_{step}, 0\n  br i1 %uninitialized_{step}, label %uninitialized_error, label %initialized_ok_{step}\ninitialized_ok_{step}:\n"
                            ));
                            needs_uninitialized = true;
                        }
                        let address = values
                            .get(address)
                            .ok_or(EmitError::Unsupported("load address"))?;
                        output.push_str(&format!(
                            "  %b{block_number}_v{} = load {ty}, ptr {address}\n",
                            id.0
                        ));
                        values.insert(id, format!("%b{block_number}_v{}", id.0));
                    }
                    (core::InstructionKind::Store { address, value }, None) => {
                        let flag = array_flags.get(address);
                        let value_type = *value_types
                            .get(value)
                            .ok_or(EmitError::Unsupported("store value type"))?;
                        let ty = scalar_type(&cfg.types, value_type)?;
                        let address = values
                            .get(address)
                            .ok_or(EmitError::Unsupported("store address"))?;
                        let value = values
                            .get(value)
                            .ok_or(EmitError::Unsupported("store value"))?;
                        output.push_str(&format!("  store {ty} {value}, ptr {address}\n"));
                        if let Some(flag) = flag {
                            output.push_str(&format!("  store i8 1, ptr {flag}\n"));
                        }
                    }
                    (
                        core::InstructionKind::Call {
                            function,
                            arguments,
                        },
                        Some((id, _)),
                    ) => {
                        let callee = function_names
                            .get(function.0)
                            .ok_or(EmitError::Unsupported("callee"))?;
                        let arguments = arguments
                            .iter()
                            .map(|argument| {
                                let ty = *value_types
                                    .get(argument)
                                    .ok_or(EmitError::Unsupported("call argument type"))?;
                                let ty = scalar_type(&cfg.types, ty)?;
                                let value = values
                                    .get(argument)
                                    .ok_or(EmitError::Unsupported("call argument"))?;
                                Ok(format!("{ty} {value}"))
                            })
                            .collect::<Result<Vec<_>, _>>()?
                            .join(", ");
                        let ty = scalar_type(
                            &cfg.types,
                            cfg.functions[function.0]
                                .result
                                .ok_or(EmitError::Unsupported("callee result"))?,
                        )?;
                        output.push_str(&format!(
                            "  %b{block_number}_v{} = call {ty} @{callee}({arguments})\n",
                            id.0,
                        ));
                        values.insert(id, format!("%b{block_number}_v{}", id.0));
                    }
                    (core::InstructionKind::Convert(value), Some((id, target))) => {
                        let value_type = *value_types
                            .get(value)
                            .ok_or(EmitError::Unsupported("conversion source"))?;
                        let operand = values
                            .get(value)
                            .ok_or(EmitError::Unsupported("conversion value"))?;
                        if value_type == target {
                            values.insert(id, operand.clone());
                        } else {
                            let operation =
                                match (cfg.types.get(value_type.0), cfg.types.get(target.0)) {
                                    (Some(core::Type::I32), Some(core::Type::Bool)) => {
                                        format!("icmp ne i32 {operand}, 0")
                                    }
                                    (Some(core::Type::U8), Some(core::Type::Bool)) => {
                                        format!("icmp ne i8 {operand}, 0")
                                    }
                                    (Some(core::Type::U8), Some(core::Type::I32)) => {
                                        format!("zext i8 {operand} to i32")
                                    }
                                    (Some(core::Type::I32), Some(core::Type::U8)) => {
                                        format!("trunc i32 {operand} to i8")
                                    }
                                    (Some(core::Type::I32), Some(core::Type::F32)) => {
                                        format!("sitofp i32 {operand} to float")
                                    }
                                    (Some(core::Type::U8), Some(core::Type::F32)) => {
                                        format!("uitofp i8 {operand} to float")
                                    }
                                    (Some(core::Type::F32), Some(core::Type::Bool)) => {
                                        format!("fcmp une float {operand}, 0.000000e+00")
                                    }
                                    (Some(core::Type::F32), Some(core::Type::I32)) => {
                                        module_needs_f32_to_i32 = true;
                                        format!(
                                            "call i32 @llvm.fptosi.sat.i32.f32(float {operand})"
                                        )
                                    }
                                    (Some(core::Type::F32), Some(core::Type::U8)) => {
                                        module_needs_f32_to_u8 = true;
                                        format!("call i8 @llvm.fptoui.sat.i8.f32(float {operand})")
                                    }
                                    _ => return Err(EmitError::Unsupported("conversion")),
                                };
                            output
                                .push_str(&format!("  %b{block_number}_v{} = {operation}\n", id.0));
                            values.insert(id, format!("%b{block_number}_v{}", id.0));
                        }
                    }
                    (
                        core::InstructionKind::Compare {
                            operation,
                            left,
                            right,
                        },
                        Some((id, _)),
                    ) => {
                        let left_type = *value_types
                            .get(left)
                            .ok_or(EmitError::Unsupported("comparison type"))?;
                        let ty = scalar_type(&cfg.types, left_type)?;
                        let left = values
                            .get(left)
                            .ok_or(EmitError::Unsupported("comparison left"))?;
                        let right = values
                            .get(right)
                            .ok_or(EmitError::Unsupported("comparison right"))?;
                        let predicate = match operation {
                            core::CompareOp::Equal => "eq",
                            core::CompareOp::NotEqual => "ne",
                            core::CompareOp::Less => {
                                if ty == "i32" {
                                    "slt"
                                } else {
                                    "ult"
                                }
                            }
                            core::CompareOp::LessEqual => {
                                if ty == "i32" {
                                    "sle"
                                } else {
                                    "ule"
                                }
                            }
                            core::CompareOp::Greater => {
                                if ty == "i32" {
                                    "sgt"
                                } else {
                                    "ugt"
                                }
                            }
                            core::CompareOp::GreaterEqual => {
                                if ty == "i32" {
                                    "sge"
                                } else {
                                    "uge"
                                }
                            }
                        };
                        if ty == "float" {
                            let predicate = match operation {
                                core::CompareOp::Equal => "oeq",
                                core::CompareOp::NotEqual => "une",
                                core::CompareOp::Less => "olt",
                                core::CompareOp::LessEqual => "ole",
                                core::CompareOp::Greater => "ogt",
                                core::CompareOp::GreaterEqual => "oge",
                            };
                            output.push_str(&format!(
                                "  %b{block_number}_v{} = fcmp {predicate} float {left}, {right}\n",
                                id.0
                            ));
                        } else {
                            output.push_str(&format!(
                                "  %b{block_number}_v{} = icmp {predicate} {ty} {left}, {right}\n",
                                id.0
                            ));
                        }
                        values.insert(id, format!("%b{block_number}_v{}", id.0));
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
                        let left = values
                            .get(left)
                            .ok_or(EmitError::Unsupported("binary left operand"))?;
                        let right = values
                            .get(right)
                            .ok_or(EmitError::Unsupported("binary right operand"))?;
                        let opcode = match operation {
                            core::BinaryOp::Add => "fadd",
                            core::BinaryOp::Subtract => "fsub",
                            core::BinaryOp::Multiply => "fmul",
                            core::BinaryOp::Divide => "fdiv",
                        };
                        output.push_str(&format!(
                            "  %b{block_number}_v{} = {opcode} float {left}, {right}\n",
                            id.0
                        ));
                        values.insert(id, format!("%b{block_number}_v{}", id.0));
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
                        if !target.triple.starts_with("x86_64-")
                            || !target.triple.contains("-linux-")
                        {
                            return Err(EmitError::Unsupported("checked arithmetic target"));
                        }
                        let left = values
                            .get(left)
                            .ok_or(EmitError::Unsupported("binary left operand"))?;
                        let right = values
                            .get(right)
                            .ok_or(EmitError::Unsupported("binary right operand"))?;
                        let step = format!("b{block_number}_v{}", id.0);
                        match operation {
                            core::BinaryOp::Add
                            | core::BinaryOp::Subtract
                            | core::BinaryOp::Multiply => {
                                needs_overflow = true;
                                let opcode = match operation {
                                    core::BinaryOp::Add => "add",
                                    core::BinaryOp::Subtract => "sub",
                                    core::BinaryOp::Multiply => "mul",
                                    core::BinaryOp::Divide => unreachable!(),
                                };
                                output.push_str(&format!(
                            "  %left64_{step} = sext i32 {left} to i64\n  %right64_{step} = sext i32 {right} to i64\n  %wide_{step} = {opcode} i64 %left64_{step}, %right64_{step}\n  %under_{step} = icmp slt i64 %wide_{step}, -2147483648\n  %over_{step} = icmp sgt i64 %wide_{step}, 2147483647\n  %bad_{step} = or i1 %under_{step}, %over_{step}\n  br i1 %bad_{step}, label %overflow_error, label %continue_{step}\ncontinue_{step}:\n  %{step} = trunc i64 %wide_{step} to i32\n"
                        ));
                            }
                            core::BinaryOp::Divide => {
                                needs_overflow = true;
                                needs_division_by_zero = true;
                                output.push_str(&format!(
                            "  %zero_{step} = icmp eq i32 {right}, 0\n  br i1 %zero_{step}, label %division_by_zero_error, label %nonzero_{step}\nnonzero_{step}:\n  %minimum_{step} = icmp eq i32 {left}, -2147483648\n  %negative_one_{step} = icmp eq i32 {right}, -1\n  %bad_{step} = and i1 %minimum_{step}, %negative_one_{step}\n  br i1 %bad_{step}, label %overflow_error, label %continue_{step}\ncontinue_{step}:\n  %{step} = sdiv i32 {left}, {right}\n"
                        ));
                            }
                        }
                        values.insert(id, format!("%{step}"));
                    }
                    _ => return Err(EmitError::Unsupported("instruction")),
                }
                if let Some((id, ty)) = instruction.result {
                    value_types.insert(id, ty);
                }
            }
            let Some(terminator) = &block.terminator else {
                return Err(EmitError::Unsupported("terminator"));
            };
            match &terminator.kind {
                core::TerminatorKind::Jump(target) => {
                    output.push_str(&format!("  br label %b{}\n", target.0));
                }
                core::TerminatorKind::Branch { condition, yes, no } => {
                    let condition = values
                        .get(condition)
                        .ok_or(EmitError::Unsupported("branch condition"))?;
                    output.push_str(&format!(
                        "  br i1 {condition}, label %b{}, label %b{}\n",
                        yes.0, no.0
                    ));
                }
                core::TerminatorKind::Return(Some(value)) => {
                    let value = values
                        .get(value)
                        .ok_or(EmitError::Unsupported("return value"))?;
                    output.push_str(&format!("  ret {result_type} {value}\n"));
                }
                _ => return Err(EmitError::Unsupported("terminator")),
            }
        }
        if needs_overflow {
            output.push_str("overflow_error:\n  call void @agsem_fail_overflow()\n  unreachable\n");
        }
        if needs_division_by_zero {
            output.push_str(
            "division_by_zero_error:\n  call void @agsem_fail_division_by_zero()\n  unreachable\n",
        );
        }
        if needs_bounds {
            output.push_str(
                "array_bounds_error:\n  call void @agsem_fail_array_bounds()\n  unreachable\n",
            );
        }
        if needs_uninitialized {
            output.push_str(
                "uninitialized_error:\n  call void @agsem_fail_uninitialized()\n  unreachable\n",
            );
        }
        output.push_str("}\n");
        module_needs_overflow |= needs_overflow;
        module_needs_division_by_zero |= needs_division_by_zero;
        module_needs_bounds |= needs_bounds;
        module_needs_uninitialized |= needs_uninitialized;
    }
    if module_needs_overflow
        || module_needs_division_by_zero
        || module_needs_bounds
        || module_needs_uninitialized
    {
        output.push_str("declare i64 @write(i32, ptr, i64)\ndeclare void @exit(i32)\n");
    }
    if module_needs_overflow {
        output.push_str(
            "@agsem_overflow_text = private constant [17 x i8] c\"integer overflow\\0A\"\ndefine internal void @agsem_fail_overflow() {\nentry:\n  %ignored = call i64 @write(i32 2, ptr @agsem_overflow_text, i64 17)\n  call void @exit(i32 1)\n  unreachable\n}\n",
        );
    }
    if module_needs_division_by_zero {
        output.push_str(
            "@agsem_division_text = private constant [17 x i8] c\"division by zero\\0A\"\ndefine internal void @agsem_fail_division_by_zero() {\nentry:\n  %ignored = call i64 @write(i32 2, ptr @agsem_division_text, i64 17)\n  call void @exit(i32 1)\n  unreachable\n}\n",
        );
    }
    if module_needs_bounds {
        output.push_str(
            "@agsem_bounds_text = private constant [26 x i8] c\"array index out of bounds\\0A\"\ndefine internal void @agsem_fail_array_bounds() {\nentry:\n  %ignored = call i64 @write(i32 2, ptr @agsem_bounds_text, i64 26)\n  call void @exit(i32 1)\n  unreachable\n}\n",
        );
    }
    if module_needs_uninitialized {
        output.push_str(
            "@agsem_uninitialized_text = private constant [36 x i8] c\"variable used before initialization\\0A\"\ndefine internal void @agsem_fail_uninitialized() {\nentry:\n  %ignored = call i64 @write(i32 2, ptr @agsem_uninitialized_text, i64 36)\n  call void @exit(i32 1)\n  unreachable\n}\n",
        );
    }
    if module_needs_f32_to_i32 {
        output.push_str("declare i32 @llvm.fptosi.sat.i32.f32(float)\n");
    }
    if module_needs_f32_to_u8 {
        output.push_str("declare i8 @llvm.fptoui.sat.i8.f32(float)\n");
    }
    if module_needs_memmove {
        output.push_str("declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1)\n");
    }
    if module_needs_memset {
        output.push_str("declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)\n");
    }
    Ok(output)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn instruction(
        id: Option<(usize, usize)>,
        kind: core::InstructionKind,
    ) -> structured::Statement {
        structured::Statement::Instruction(core::Instruction {
            result: id.map(|(value, ty)| (core::ValueId(value), core::TypeId(ty))),
            kind,
            source: None,
        })
    }

    #[test]
    fn value_ids_are_local_to_each_cfg_block() {
        let branch = |number| structured::Block {
            statements: vec![
                instruction(
                    Some((0, 0)),
                    core::InstructionKind::Constant(core::Constant::I32(number)),
                ),
                instruction(
                    Some((1, 2)),
                    core::InstructionKind::SlotAddress(core::SlotId(0)),
                ),
                instruction(
                    None,
                    core::InstructionKind::Store {
                        address: core::ValueId(1),
                        value: core::ValueId(0),
                    },
                ),
                instruction(Some((2, 0)), core::InstructionKind::Load(core::ValueId(1))),
                structured::Statement::Return {
                    value: Some(core::ValueId(2)),
                    source: None,
                },
            ],
            source: None,
        };
        let module = structured::Module {
            types: vec![
                core::Type::I32,
                core::Type::Bool,
                core::Type::Address(core::TypeId(0)),
            ],
            globals: vec![],
            functions: vec![structured::Function {
                name: "main".to_owned(),
                parameters: vec![],
                slots: vec![core::TypeId(0)],
                result: Some(core::TypeId(0)),
                body: structured::Block {
                    statements: vec![
                        instruction(
                            Some((0, 1)),
                            core::InstructionKind::Constant(core::Constant::Bool(true)),
                        ),
                        structured::Statement::If {
                            condition: core::ValueId(0),
                            then_branch: branch(3),
                            else_branch: Some(branch(4)),
                            source: None,
                        },
                    ],
                    source: None,
                },
            }],
            comments: vec![],
        };
        let llvm = emit_llvm(
            &module,
            TargetSpec {
                triple: "x86_64-pc-linux-gnu",
                data_layout: "e-m:e-i64:64-n8:16:32:64-S128",
            },
        )
        .unwrap();
        assert!(llvm.contains("%b1_v2 = load i32"));
        assert!(llvm.contains("%b2_v2 = load i32"));
    }
}
