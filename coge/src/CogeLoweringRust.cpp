#include "agas/artifact/Sha256.h"
#include "coge/LoweringGeneration.h"
#include <nlohmann/json.hpp>

#include <cctype>
#include <stdexcept>
#include <string>
#include <tuple>

namespace coge {
namespace {
using Json = nlohmann::ordered_json;
auto logicalType(std::string value) -> agsem::TypeRef {
  const auto open = value.find('<');
  if (open == std::string::npos)
    return {std::move(value), {}};
  return {value.substr(0, open),
          {logicalType(value.substr(open + 1, value.size() - open - 2))}};
}
auto typeJson(const agsem::TypeRef &type) -> Json {
  Json arguments = Json::array();
  for (const auto &argument : type.arguments)
    arguments.push_back(typeJson(argument));
  return {{"name", type.name}, {"arguments", arguments}};
}
auto checkLoweringTypes(const std::map<std::string, std::string> &bindings,
                        const std::vector<agsem::ContractSymbol> &contracts)
    -> Json {
  Json proof = Json::array();
  const auto schema =
      [&](const std::string &owner) -> const agsem::TypeContract & {
    const agsem::ContractSymbol *found = nullptr;
    for (const auto &symbol : contracts)
      if (symbol.name == owner && symbol.kind == agsem::SymbolKind::Type &&
          symbol.owner != agsem::SymbolOwner::Execution) {
        if (found)
          throw std::runtime_error("ambiguous lowering type contract: " +
                                   owner);
        found = &symbol;
      }
    if (!found || !found->type)
      throw std::runtime_error("missing lowering type contract: " + owner);
    return *found->type;
  };
  const auto member = [&](const std::string &owner, const std::string &name,
                          const std::string &expected,
                          const std::string &role = "") {
    const auto &contract = schema(owner);
    if (contract.kind != agsem::TypeContract::Kind::Record)
      throw std::runtime_error("lowering requires record contract: " + owner);
    const auto &fields = contract.fields;
    const auto found = fields.find(name);
    if (found == fields.end() || found->second != logicalType(expected))
      throw std::runtime_error("lowering field type mismatch: " + owner + "." +
                               name + " requires " + expected);
    proof.push_back({{"kind", "field"},
                     {"role", role},
                     {"owner", owner},
                     {"member", name},
                     {"type", typeJson(found->second)}});
  };
  for (const auto &[role, type] :
       std::map<std::string, std::string>{{"functions", "List<Function>"},
                                          {"scopes", "List<Scope>"},
                                          {"operations", "List<OperationNode>"},
                                          {"expressions", "List<Expression>"},
                                          {"variables", "List<Variable>"},
                                          {"places", "List<Place>"}})
    member("Context", bindings.at(role), type, role);
  for (const auto &[role, type] : std::map<std::string, std::string>{
           {"function_body", "Option<OpId>"},
           {"function_result", "Type"},
           {"function_parameters", "List<Type>"},
           {"function_scope", "Option<ScopeId>"}})
    member("Function", bindings.at(role), type, role);
  member("Scope", bindings.at("scope_symbols"), "List<SymbolId>",
         "scope_symbols");
  for (const auto &[owner, name, type] :
       std::vector<std::tuple<std::string, std::string, std::string>>{
           {"Context", "modules", "List<Module>"},
           {"Context", "diagnostics", "List<Diagnostic>"},
           {"Context", "comments", "List<SourceComment>"},
           {"Context", "structs", "List<Struct>"},
           {"Context", "fields", "List<Field>"},
           {"Module", "scope", "ScopeId"},
           {"Module", "valid", "Bool"},
           {"Struct", "fields", "List<FieldId>"},
           {"Struct", "complete", "Bool"},
           {"Field", "ty", "Type"},
           {"Field", "ordinal", "Index"},
           {"Place", "key", "PlaceKey"},
           {"Place", "ty", "Type"},
           {"PlaceKey", "root", "SymbolId"},
           {"PlaceKey", "fields", "List<FieldId>"},
           {"Variable", "ty", "Type"},
           {"Variable", "owner", "ScopeId"},
           {"Variable", "place", "PlaceId"},
           {"Expression", "kind", "ExpressionKind"},
           {"Expression", "ty", "Type"},
           {"Expression", "source", "SourceRange"},
           {"OperationNode", "kind", "Operation"},
           {"OperationNode", "source", "SourceRange"},
           {"Function", "name", "OwnedText"},
           {"SourceComment", "text", "OwnedText"},
           {"SourceComment", "source", "SourceRange"}})
    member(owner, name, type);
  const auto variant = [&](const std::string &owner, const std::string &name,
                           const std::vector<std::string> &payload,
                           const std::string &role = "") {
    const auto &contract = schema(owner);
    if (contract.kind != agsem::TypeContract::Kind::Enum)
      throw std::runtime_error("lowering requires enum contract: " + owner);
    const auto &variants = contract.variants;
    const auto found = variants.find(name);
    std::vector<agsem::TypeRef> expected;
    Json types = Json::array();
    for (const auto &type : payload)
      expected.push_back(logicalType(type));
    if (found == variants.end() || found->second != expected)
      throw std::runtime_error("lowering variant payload mismatch: " + owner +
                               "." + name);
    for (const auto &type : found->second)
      types.push_back(typeJson(type));
    proof.push_back({{"kind", "variant"},
                     {"role", role},
                     {"owner", owner},
                     {"variant", name},
                     {"payload", types}});
  };
  for (const auto &[role, payload] :
       std::map<std::string, std::vector<std::string>>{
           {"block_ir", {"ScopeId", "List<OpId>"}},
           {"declare_ir", {"SymbolId"}},
           {"initialize_ir", {"SymbolId", "ExprId"}},
           {"store_ir", {"PlaceId", "ExprId"}},
           {"index_store_ir", {"PlaceId", "ExprId", "ExprId"}},
           {"index_update_ir", {"PlaceId", "ExprId", "Operator", "ExprId"}},
           {"if_ir", {"ExprId", "OpId", "Option<OpId>"}},
           {"while_ir", {"ExprId", "OpId"}},
           {"for_ir", {"ScopeId", "OpId", "ExprId", "OpId", "OpId"}},
           {"return_ir", {"Option<ExprId>"}}})
    variant("Operation", bindings.at(role), payload, role);
  for (const auto &[role, payload] :
       std::map<std::string, std::vector<std::string>>{
           {"load_ir", {"PlaceId"}},
           {"index_load_ir", {"PlaceId", "ExprId"}},
           {"call_ir", {"FunctionId", "List<ExprId>"}},
           {"constant_ir", {"Constant"}},
           {"convert_ir", {"ExprId", "Type"}},
           {"binary_ir", {"Operator", "ExprId", "ExprId"}},
           {"negate_ir", {"ExprId"}}})
    variant("ExpressionKind", bindings.at(role), payload, role);
  for (const auto &[role, type] :
       std::map<std::string, std::string>{{"integer_ir", "I32"},
                                          {"float_ir", "F32"},
                                          {"boolean_ir", "Bool"},
                                          {"char_ir", "U8"}})
    variant("Constant", bindings.at(role), {type}, role);
  for (const auto &role : {"add_ir", "subtract_ir", "multiply_ir", "divide_ir",
                           "equal_ir", "not_equal_ir", "less_ir",
                           "less_equal_ir", "greater_ir", "greater_equal_ir"})
    variant("Operator", bindings.at(role), {}, role);
  for (const auto &name : {"Int", "Float", "Bool", "Char", "Void"})
    variant("Type", name, {});
  variant("Type", "Struct", {"StructId"});
  variant("Type", "Array", {"Type", "Index"});
  return proof;
}
auto structuredDefaults() -> const std::map<std::string, std::string> & {
  static const std::map<std::string, std::string> values{
      {"functions", "functions"},
      {"function_body", "body"},
      {"function_result", "result"},
      {"function_parameters", "parameters"},
      {"function_scope", "scope"},
      {"scopes", "scopes"},
      {"scope_symbols", "symbols"},
      {"operations", "operations"},
      {"expressions", "expressions"},
      {"variables", "variables"},
      {"places", "places"},
      {"block_ir", "Block"},
      {"declare_ir", "Declare"},
      {"initialize_ir", "Initialize"},
      {"store_ir", "Store"},
      {"index_store_ir", "IndexStore"},
      {"index_update_ir", "IndexUpdate"},
      {"if_ir", "If"},
      {"while_ir", "While"},
      {"for_ir", "For"},
      {"return_ir", "Return"},
      {"load_ir", "Load"},
      {"index_load_ir", "IndexLoad"},
      {"call_ir", "Call"},
      {"constant_ir", "Constant"},
      {"integer_ir", "Int"},
      {"float_ir", "Float"},
      {"boolean_ir", "Bool"},
      {"char_ir", "Char"},
      {"convert_ir", "Convert"},
      {"binary_ir", "Binary"},
      {"negate_ir", "Negate"},
      {"add_ir", "Add"},
      {"subtract_ir", "Subtract"},
      {"multiply_ir", "Multiply"},
      {"divide_ir", "Divide"},
      {"equal_ir", "Equal"},
      {"not_equal_ir", "NotEqual"},
      {"less_ir", "Less"},
      {"less_equal_ir", "LessEqual"},
      {"greater_ir", "Greater"},
      {"greater_equal_ir", "GreaterEqual"}};
  return values;
}
} // namespace

auto loweringFields() -> const std::set<std::string> & {
  static const std::set<std::string> fields{"lower",
                                            "functions",
                                            "function_body",
                                            "function_result",
                                            "function_parameters",
                                            "function_scope",
                                            "scopes",
                                            "scope_symbols",
                                            "operations",
                                            "block_ir",
                                            "declare_ir",
                                            "initialize_ir",
                                            "store_ir",
                                            "index_store_ir",
                                            "index_update_ir",
                                            "if_ir",
                                            "while_ir",
                                            "for_ir",
                                            "return_ir",
                                            "expressions",
                                            "load_ir",
                                            "index_load_ir",
                                            "call_ir",
                                            "variables",
                                            "places",
                                            "constant_ir",
                                            "integer_ir",
                                            "float_ir",
                                            "boolean_ir",
                                            "char_ir",
                                            "convert_ir",
                                            "binary_ir",
                                            "negate_ir",
                                            "add_ir",
                                            "subtract_ir",
                                            "multiply_ir",
                                            "divide_ir",
                                            "equal_ir",
                                            "not_equal_ir",
                                            "less_ir",
                                            "less_equal_ir",
                                            "greater_ir",
                                            "greater_equal_ir"};
  return fields;
}

auto CheckedLowering::loweringName() const -> std::optional<std::string> {
  if (!bindings_)
    return std::nullopt;
  return bindings_->at("lower");
}

auto CheckedLowering::inspection() const -> std::string {
  return inspection_.empty() ? "null" : inspection_;
}

auto prepareLowering(const LoweringGenerationInput &input) -> CheckedLowering {
  CheckedLowering checked;
  if (!input.bindings)
    return checked;
  for (const auto &[name, value] : *input.bindings) {
    static_cast<void>(value);
    if (name != "profile" && !loweringFields().contains(name))
      throw std::runtime_error("lowering_model unknown field " + name);
  }
  auto bindings = *input.bindings;
  Json report{{"profile", nullptr}, {"bindings", Json::array()}};
  if (const auto profile = bindings.find("profile");
      profile != bindings.end()) {
    if (profile->second != "structured_core_v1")
      throw std::runtime_error("unknown lowering profile: " + profile->second);
    if (input.contracts.empty())
      throw std::runtime_error(
          "lowering profile requires explicit model contracts");
    report["profile"] = {{"id", profile->second}};
    bindings.erase("profile");
    for (const auto &[name, value] : structuredDefaults())
      bindings.try_emplace(name, value);
  }
  for (const auto &name : loweringFields())
    if (!bindings.contains(name))
      throw std::runtime_error("lowering_model missing field " + name);
  const auto &lower = bindings.at("lower");
  static const std::set<std::string> helpers{
      "source_span",          "scalar_type",      "value_type",
      "address_type",         "push_value",       "global_id",
      "slot_address",         "indexed_address",  "aggregate_source_place",
      "lower_aggregate_copy", "lower_expression", "lower_operation",
      "lower_block",          "lower_branch",     "reachable_functions"};
  if (helpers.contains(lower))
    throw std::runtime_error(
        "lowering_model function name conflicts with helper");
  for (const auto &[name, value] : bindings) {
    Json entry{{"role", name},
               {"target", value},
               {"origin", input.bindings->contains(name) ? "explicit"
                                                         : "profile_default"}};
    const auto location =
        input.locations.find(input.bindings->contains(name) ? name : "profile");
    if (location != input.locations.end()) {
      entry["begin_byte"] = location->second.beginByte;
      entry["end_byte"] = location->second.endByte;
    }
    report["bindings"].push_back(std::move(entry));
  }
  report["type_checks"] = input.contracts.empty()
                              ? Json::array()
                              : checkLoweringTypes(bindings, input.contracts);
  report["interface"] = {
      {"context", "Context"},
      {"effect", "pure"},
      {"parameters",
       {typeJson(logicalType("Context")), typeJson(logicalType("FunctionId"))}},
      {"result", "Result<agsem_runtime::structured_ir::Module, &'static str>"}};
  report["capabilities"] = {
      "scalar_values",           "arrays",         "aggregates", "calls",
      "structured_control_flow", "source_comments"};
  if (!report["profile"].is_null()) {
    auto requirements = report["type_checks"];
    for (auto &requirement : requirements) {
      const auto role = requirement["role"].get<std::string>();
      if (!role.empty())
        requirement[requirement["kind"] == "field" ? "member" : "variant"] =
            structuredDefaults().at(role);
    }
    report["profile"]["sha256"] = agas::artifact::sha256Hex(Json{
        {"defaults", structuredDefaults()},
        {"requirements", requirements},
        {"interface", report["interface"]},
        {"capabilities", report["capabilities"]}}.dump());
  }
  report["rust_representation_check"] =
      "Rust compiler: variant layout, ID layout and Clone/Copy";
  checked.bindings_ = std::move(bindings);
  checked.inspection_ = report.dump();
  return checked;
}

auto emitLoweringRust(const CheckedLowering &lowering) -> std::string {
  if (!lowering.bindings_)
    return {};
  const auto &bindings = *lowering.bindings_;
  std::string output = R"RUST(// Generated by sema from lowering_model.
use agsem_runtime::core_ir as core;
use agsem_runtime::structured_ir as structured;
use crate::model::{Context, ExprId, FunctionId, OpId, PlaceId, SymbolId};
use std::collections::HashMap;

fn source_span(span: crate::ast::InputSpan) -> Result<core::SourceSpan, &'static str> {
    Ok(core::SourceSpan {
        file: 0,
        start: u32::try_from(span.begin_byte).map_err(|_| "source span too large")?,
        end: u32::try_from(span.end_byte).map_err(|_| "source span too large")?,
    })
}

fn scalar_type(ty: &crate::model::Type) -> Result<core::TypeId, &'static str> {
    match ty {
        crate::model::Type::Int => Ok(core::TypeId(0)),
        crate::model::Type::Float => Ok(core::TypeId(3)),
        crate::model::Type::Bool => Ok(core::TypeId(2)),
        crate::model::Type::Char => Ok(core::TypeId(4)),
        _ => Err("unsupported Core IR scalar type"),
    }
}

fn value_type(ty: &crate::model::Type) -> Result<core::TypeId, &'static str> {
    if let crate::model::Type::Struct(id) = ty {
        return Ok(core::TypeId(8 + id.0 * 2));
    }
    scalar_type(ty)
}

fn address_type(ty: core::TypeId) -> Result<core::TypeId, &'static str> {
    match ty.0 {
        0 => Ok(core::TypeId(1)),
        2 => Ok(core::TypeId(5)),
        3 => Ok(core::TypeId(6)),
        4 => Ok(core::TypeId(7)),
        id if id >= 8 && id % 2 == 0 => Ok(core::TypeId(id + 1)),
        _ => Err("unsupported Core IR address type"),
    }
}

fn push_value(
    instructions: &mut Vec<core::Instruction>,
    next_value: &mut usize,
    kind: core::InstructionKind,
    ty: core::TypeId,
    source: core::SourceSpan,
) -> core::ValueId {
    let id = core::ValueId(*next_value);
    *next_value += 1;
    instructions.push(core::Instruction {
        result: Some((id, ty)), kind, source: Some(source),
    });
    id
}

fn global_id(ctx: &Context, symbol: SymbolId) -> Result<core::GlobalId, &'static str> {
    let module = ctx.modules.first().ok_or("missing analyzed module")?;
    ctx.@SCOPES@[module.scope.0].@SCOPE_SYMBOLS@
        .iter().position(|candidate| *candidate == symbol)
        .map(core::GlobalId).ok_or("nonlocal Core IR place")
}

fn slot_address(
    ctx: &Context,
    place: PlaceId,
    slots: &HashMap<SymbolId, core::SlotId>,
    instructions: &mut Vec<core::Instruction>,
    next_value: &mut usize,
    source: core::SourceSpan,
) -> Result<core::ValueId, &'static str> {
    let place = ctx.@PLACES@.get(place.0).ok_or("unknown place")?;
    let variable = ctx.@VARIABLES@.get(place.key.root.0).ok_or("unknown root variable")?;
    let root_ty = value_type(&variable.ty)?;
    let kind = if let Some(slot) = slots.get(&place.key.root) {
        core::InstructionKind::SlotAddress(*slot)
    } else {
        core::InstructionKind::GlobalAddress(global_id(ctx, place.key.root)?)
    };
    let mut address = push_value(instructions, next_value,
        kind, address_type(root_ty)?, source);
    for field_id in &place.key.fields {
        let field = ctx.fields.get(field_id.0).ok_or("unknown field")?;
        let ty = value_type(&field.ty)?;
        address = push_value(instructions, next_value,
            core::InstructionKind::FieldAddress { base: address, field: field.ordinal },
            address_type(ty)?, source);
    }
    Ok(address)
}

fn indexed_address(
    ctx: &Context,
    place: PlaceId,
    index: core::ValueId,
    slots: &HashMap<SymbolId, core::SlotId>,
    instructions: &mut Vec<core::Instruction>,
    next_value: &mut usize,
    source: core::SourceSpan,
) -> Result<core::ValueId, &'static str> {
    let place = ctx.@PLACES@.get(place.0).ok_or("unknown place")?;
    if !place.key.fields.is_empty() {
        return Err("unsupported Core IR array place");
    }
    let crate::model::Type::Array(element, length) = &place.ty else {
        return Err("indexing non-array");
    };
    let element = scalar_type(element)?;
    let length = i32::try_from(*length).map_err(|_| "array length out of Core IR range")?;
    let size = push_value(instructions, next_value,
        core::InstructionKind::Constant(core::Constant::I32(length)), core::TypeId(0), source);
    instructions.push(core::Instruction {
        result: None,
        kind: core::InstructionKind::BoundsCheck { index, length: size },
        source: Some(source),
    });
    let kind = if let Some(slot) = slots.get(&place.key.root) {
        core::InstructionKind::IndexAddress { slot: *slot, index }
    } else {
        core::InstructionKind::GlobalIndexAddress {
            global: global_id(ctx, place.key.root)?, index,
        }
    };
    Ok(push_value(instructions, next_value, kind, address_type(element)?, source))
}

fn aggregate_source_place(ctx: &Context, expression_id: ExprId) -> Result<PlaceId, &'static str> {
    let expression = ctx.@EXPRESSIONS@.get(expression_id.0).ok_or("unknown expression")?;
    match &expression.kind {
        crate::model::ExpressionKind::@LOAD_IR@(place) => Ok(*place),
        crate::model::ExpressionKind::@CONVERT_IR@ { value, .. } => aggregate_source_place(ctx, *value),
        _ => Err("unsupported Core IR aggregate value"),
    }
}

fn lower_aggregate_copy(
    ctx: &Context,
    value: ExprId,
    target: PlaceId,
    slots: &HashMap<SymbolId, core::SlotId>,
    instructions: &mut Vec<core::Instruction>,
    next_value: &mut usize,
    source: core::SourceSpan,
) -> Result<(), &'static str> {
    let source_place = aggregate_source_place(ctx, value)?;
    let source_address = slot_address(ctx, source_place, slots, instructions, next_value, source)?;
    let target_address = slot_address(ctx, target, slots, instructions, next_value, source)?;
    instructions.push(core::Instruction {
        result: None,
        kind: core::InstructionKind::CopyAggregate { source: source_address, target: target_address },
        source: Some(source),
    });
    Ok(())
}

fn lower_expression(
    ctx: &Context,
    expression_id: ExprId,
    slots: &HashMap<SymbolId, core::SlotId>,
    functions: &HashMap<FunctionId, core::FunctionId>,
    instructions: &mut Vec<core::Instruction>,
    next_value: &mut usize,
) -> Result<core::ValueId, &'static str> {
    let expression = ctx.@EXPRESSIONS@.get(expression_id.0).ok_or("unknown expression")?;
    let ty = scalar_type(&expression.ty)?;
    let source = source_span(expression.source)?;
    let kind = match &expression.kind {
        crate::model::ExpressionKind::@CONSTANT_IR@(crate::model::Constant::@INTEGER_IR@(number)) => {
            core::InstructionKind::Constant(core::Constant::I32(*number))
        }
        crate::model::ExpressionKind::@CONSTANT_IR@(crate::model::Constant::@FLOAT_IR@(number)) => {
            core::InstructionKind::Constant(core::Constant::F32Bits(number.to_bits()))
        }
        crate::model::ExpressionKind::@CONSTANT_IR@(crate::model::Constant::@BOOLEAN_IR@(value)) => {
            core::InstructionKind::Constant(core::Constant::Bool(*value))
        }
        crate::model::ExpressionKind::@CONSTANT_IR@(crate::model::Constant::@CHAR_IR@(value)) => {
            core::InstructionKind::Constant(core::Constant::U8(*value))
        }
        crate::model::ExpressionKind::@LOAD_IR@(place) => {
            let address = slot_address(ctx, *place, slots, instructions, next_value, source)?;
            core::InstructionKind::Load(address)
        }
        crate::model::ExpressionKind::@INDEX_LOAD_IR@ { array, index } => {
            let offset = lower_expression(ctx, *index, slots, functions, instructions, next_value)?;
            let address = indexed_address(ctx, *array, offset, slots,
                instructions, next_value, source)?;
            core::InstructionKind::Load(address)
        }
        crate::model::ExpressionKind::@CONVERT_IR@ { value, target } => {
            if scalar_type(target)? != ty {
                return Err("invalid Core IR conversion target");
            }
            let value = lower_expression(ctx, *value, slots, functions, instructions, next_value)?;
            core::InstructionKind::Convert(value)
        }
        crate::model::ExpressionKind::@NEGATE_IR@(operand) => {
            let value = lower_expression(ctx, *operand, slots, functions, instructions, next_value)?;
            let (zero_constant, mode) = if ty == core::TypeId(3) {
                (core::Constant::F32Bits(0x8000_0000), core::ArithmeticMode::IeeeF32)
            } else {
                (core::Constant::I32(0), core::ArithmeticMode::CheckedI32)
            };
            let zero = push_value(instructions, next_value,
                core::InstructionKind::Constant(zero_constant), ty, source);
            core::InstructionKind::Binary {
                operation: core::BinaryOp::Subtract, left: zero, right: value,
                mode,
            }
        }
        crate::model::ExpressionKind::@BINARY_IR@ { operator, left, right } => {
            let lhs = lower_expression(ctx, *left, slots, functions, instructions, next_value)?;
            let rhs = lower_expression(ctx, *right, slots, functions, instructions, next_value)?;
            let arithmetic_mode = if ty == core::TypeId(3) {
                core::ArithmeticMode::IeeeF32
            } else {
                core::ArithmeticMode::CheckedI32
            };
            match operator {
                crate::model::Operator::@ADD_IR@ => core::InstructionKind::Binary {
                    operation: core::BinaryOp::Add, left: lhs, right: rhs,
                    mode: arithmetic_mode,
                },
                crate::model::Operator::@SUBTRACT_IR@ => core::InstructionKind::Binary {
                    operation: core::BinaryOp::Subtract, left: lhs, right: rhs,
                    mode: arithmetic_mode,
                },
                crate::model::Operator::@MULTIPLY_IR@ => core::InstructionKind::Binary {
                    operation: core::BinaryOp::Multiply, left: lhs, right: rhs,
                    mode: arithmetic_mode,
                },
                crate::model::Operator::@DIVIDE_IR@ => core::InstructionKind::Binary {
                    operation: core::BinaryOp::Divide, left: lhs, right: rhs,
                    mode: arithmetic_mode,
                },
                crate::model::Operator::@EQUAL_IR@ => core::InstructionKind::Compare {
                    operation: core::CompareOp::Equal, left: lhs, right: rhs,
                },
                crate::model::Operator::@NOT_EQUAL_IR@ => core::InstructionKind::Compare {
                    operation: core::CompareOp::NotEqual, left: lhs, right: rhs,
                },
                crate::model::Operator::@LESS_IR@ => core::InstructionKind::Compare {
                    operation: core::CompareOp::Less, left: lhs, right: rhs,
                },
                crate::model::Operator::@LESS_EQUAL_IR@ => core::InstructionKind::Compare {
                    operation: core::CompareOp::LessEqual, left: lhs, right: rhs,
                },
                crate::model::Operator::@GREATER_IR@ => core::InstructionKind::Compare {
                    operation: core::CompareOp::Greater, left: lhs, right: rhs,
                },
                crate::model::Operator::@GREATER_EQUAL_IR@ => core::InstructionKind::Compare {
                    operation: core::CompareOp::GreaterEqual, left: lhs, right: rhs,
                },
            }
        }
        crate::model::ExpressionKind::@CALL_IR@ { function, arguments } => {
            let target = *functions.get(function).ok_or("unknown Core IR callee")?;
            let values = arguments.iter().map(|argument|
                lower_expression(ctx, *argument, slots, functions, instructions, next_value)
            ).collect::<Result<Vec<_>, _>>()?;
            core::InstructionKind::Call { function: target, arguments: values }
        }
        _ => return Err("unsupported Core IR expression"),
    };
    Ok(push_value(instructions, next_value, kind, ty, source))
}

fn lower_operation(
    ctx: &Context,
    operation_id: OpId,
    slots: &mut HashMap<SymbolId, core::SlotId>,
    functions: &HashMap<FunctionId, core::FunctionId>,
    slot_types: &mut Vec<core::TypeId>,
    types: &mut Vec<core::Type>,
    statements: &mut Vec<structured::Statement>,
    next_value: &mut usize,
) -> Result<(), &'static str> {
    let operation = ctx.@OPERATIONS@.get(operation_id.0).ok_or("unknown operation")?;
    let source = source_span(operation.source)?;
    if matches!(&operation.kind, crate::model::Operation::@BLOCK_IR@ { .. }) {
        let block = lower_block(ctx, operation_id, slots, functions, slot_types, types, next_value)?;
        statements.push(structured::Statement::Block(block));
        return Ok(());
    }
    if let crate::model::Operation::@IF_IR@ {
        condition, then_branch, else_branch,
    } = &operation.kind {
        let mut instructions = Vec::new();
        let condition = lower_expression(ctx, *condition, slots, functions,
            &mut instructions, next_value)?;
        statements.extend(instructions.into_iter().map(structured::Statement::Instruction));
        let then_branch = lower_branch(ctx, *then_branch, slots, functions, slot_types, types, next_value)?;
        let else_branch = else_branch.map(|branch|
            lower_branch(ctx, branch, slots, functions, slot_types, types, next_value)
        ).transpose()?;
        statements.push(structured::Statement::If {
            condition, then_branch, else_branch, source: Some(source),
        });
        return Ok(());
    }
    if let crate::model::Operation::@WHILE_IR@ { condition, body } = &operation.kind {
        let mut condition_instructions = Vec::new();
        let condition_value = lower_expression(ctx, *condition, slots, functions,
            &mut condition_instructions, next_value)?;
        let body = lower_branch(ctx, *body, slots, functions, slot_types, types, next_value)?;
        statements.push(structured::Statement::While {
            condition_instructions, condition_value, body, source: Some(source),
        });
        return Ok(());
    }
    if let crate::model::Operation::@FOR_IR@ {
        initialization, condition, body, update, ..
    } = &operation.kind {
        let initialization = lower_branch(ctx, *initialization,
            slots, functions, slot_types, types, next_value)?;
        let mut condition_instructions = Vec::new();
        let condition_value = lower_expression(ctx, *condition, slots, functions,
            &mut condition_instructions, next_value)?;
        let body = lower_branch(ctx, *body, slots, functions, slot_types, types, next_value)?;
        let update = lower_branch(ctx, *update, slots, functions, slot_types, types, next_value)?;
        statements.push(structured::Statement::For {
            initialization, condition_instructions, condition_value,
            body, update, source: Some(source),
        });
        return Ok(());
    }
    let mut instructions = Vec::new();
    let mut return_value = None;
    match &operation.kind {
        crate::model::Operation::@DECLARE_IR@(symbol) => {
            let variable = ctx.@VARIABLES@.get(symbol.0).ok_or("unknown variable")?;
            if slots.contains_key(symbol) {
                return Err("unsupported Core IR declaration");
            }
            let slot = core::SlotId(slot_types.len());
            let ty = match &variable.ty {
                crate::model::Type::Array(element, length) => {
                    let element = scalar_type(element)?;
                    i32::try_from(*length).map_err(|_| "array length out of Core IR range")?;
                    let id = core::TypeId(types.len());
                    types.push(core::Type::Array { element, length: *length });
                    id
                }
                other => value_type(other)?,
            };
            slot_types.push(ty);
            slots.insert(*symbol, slot);
            if matches!(&variable.ty, crate::model::Type::Array(_, _)) {
                instructions.push(core::Instruction {
                    result: None,
                    kind: core::InstructionKind::ResetArray(slot),
                    source: Some(source),
                });
            }
            if matches!(&variable.ty, crate::model::Type::Struct(_)) {
                instructions.push(core::Instruction {
                    result: None,
                    kind: core::InstructionKind::ResetAggregate(slot),
                    source: Some(source),
                });
            }
        }
        crate::model::Operation::@INITIALIZE_IR@ { symbol, value } => {
            let variable = ctx.@VARIABLES@.get(symbol.0).ok_or("unknown variable")?;
            if matches!(variable.ty, crate::model::Type::Struct(_)) {
                lower_aggregate_copy(ctx, *value, variable.place, slots, &mut instructions, next_value, source)?;
            } else {
                let value = lower_expression(ctx, *value, slots, functions, &mut instructions, next_value)?;
                let address = slot_address(ctx, variable.place, slots,
                    &mut instructions, next_value, source)?;
                instructions.push(core::Instruction {
                    result: None, kind: core::InstructionKind::Store { address, value },
                    source: Some(source),
                });
            }
        }
        crate::model::Operation::@STORE_IR@ { place, value } => {
            let target = ctx.@PLACES@.get(place.0).ok_or("unknown place")?;
            if matches!(target.ty, crate::model::Type::Struct(_)) {
                lower_aggregate_copy(ctx, *value, *place, slots, &mut instructions, next_value, source)?;
            } else {
                let value = lower_expression(ctx, *value, slots, functions, &mut instructions, next_value)?;
                let address = slot_address(ctx, *place, slots,
                    &mut instructions, next_value, source)?;
                instructions.push(core::Instruction {
                    result: None, kind: core::InstructionKind::Store { address, value },
                    source: Some(source),
                });
            }
        }
        crate::model::Operation::@INDEX_STORE_IR@ { array, index, value } => {
            let offset = lower_expression(ctx, *index, slots, functions, &mut instructions, next_value)?;
            let value = lower_expression(ctx, *value, slots, functions, &mut instructions, next_value)?;
            let address = indexed_address(ctx, *array, offset, slots,
                &mut instructions, next_value, source)?;
            instructions.push(core::Instruction {
                result: None, kind: core::InstructionKind::Store { address, value },
                source: Some(source),
            });
        }
        crate::model::Operation::@INDEX_UPDATE_IR@ { array, index, operator, value } => {
            let offset = lower_expression(ctx, *index, slots, functions, &mut instructions, next_value)?;
            let address = indexed_address(ctx, *array, offset, slots,
                &mut instructions, next_value, source)?;
            let place = ctx.@PLACES@.get(array.0).ok_or("unknown place")?;
            let crate::model::Type::Array(element, _) = &place.ty else {
                return Err("indexing non-array");
            };
            let target = scalar_type(element)?;
            let rhs_type = scalar_type(&ctx.@EXPRESSIONS@.get(value.0)
                .ok_or("unknown expression")?.ty)?;
            let common = if target == core::TypeId(3) || rhs_type == core::TypeId(3) {
                core::TypeId(3)
            } else {
                core::TypeId(0)
            };
            let old = push_value(&mut instructions, next_value,
                core::InstructionKind::Load(address), target, source);
            let left = if target == common { old } else {
                push_value(&mut instructions, next_value,
                    core::InstructionKind::Convert(old), common, source)
            };
            let rhs = lower_expression(ctx, *value, slots, functions, &mut instructions, next_value)?;
            let right = if rhs_type == common { rhs } else {
                push_value(&mut instructions, next_value,
                    core::InstructionKind::Convert(rhs), common, source)
            };
            let operation = match operator {
                crate::model::Operator::@ADD_IR@ => core::BinaryOp::Add,
                crate::model::Operator::@SUBTRACT_IR@ => core::BinaryOp::Subtract,
                crate::model::Operator::@MULTIPLY_IR@ => core::BinaryOp::Multiply,
                crate::model::Operator::@DIVIDE_IR@ => core::BinaryOp::Divide,
                _ => return Err("unsupported indexed update operator"),
            };
            let mode = if common == core::TypeId(3) {
                core::ArithmeticMode::IeeeF32
            } else {
                core::ArithmeticMode::CheckedI32
            };
            let computed = push_value(&mut instructions, next_value,
                core::InstructionKind::Binary { operation, left, right, mode }, common, source);
            let result = if common == target { computed } else {
                push_value(&mut instructions, next_value,
                    core::InstructionKind::Convert(computed), target, source)
            };
            instructions.push(core::Instruction {
                result: None, kind: core::InstructionKind::Store { address, value: result },
                source: Some(source),
            });
        }
        crate::model::Operation::@RETURN_IR@(Some(expression)) => {
            return_value = Some(lower_expression(ctx, *expression, slots, functions,
                &mut instructions, next_value)?);
        }
        _ => return Err("unsupported Core IR statement"),
    }
    statements.extend(instructions.into_iter().map(structured::Statement::Instruction));
    if let Some(value) = return_value {
        statements.push(structured::Statement::Return {
            value: Some(value), source: Some(source),
        });
    }
    Ok(())
}

fn lower_block(
    ctx: &Context,
    operation_id: OpId,
    slots: &mut HashMap<SymbolId, core::SlotId>,
    functions: &HashMap<FunctionId, core::FunctionId>,
    slot_types: &mut Vec<core::TypeId>,
    types: &mut Vec<core::Type>,
    next_value: &mut usize,
) -> Result<structured::Block, &'static str> {
    let operation = ctx.@OPERATIONS@.get(operation_id.0).ok_or("unknown operation")?;
    let crate::model::Operation::@BLOCK_IR@ { body, .. } = &operation.kind else {
        return Err("unsupported Core IR block");
    };
    let mut statements = Vec::new();
    for statement in body {
        lower_operation(ctx, *statement, slots, functions, slot_types, types, &mut statements, next_value)?;
    }
    Ok(structured::Block {
        statements, source: Some(source_span(operation.source)?),
    })
}

fn lower_branch(
    ctx: &Context,
    operation_id: OpId,
    slots: &mut HashMap<SymbolId, core::SlotId>,
    functions: &HashMap<FunctionId, core::FunctionId>,
    slot_types: &mut Vec<core::TypeId>,
    types: &mut Vec<core::Type>,
    next_value: &mut usize,
) -> Result<structured::Block, &'static str> {
    let operation = ctx.@OPERATIONS@.get(operation_id.0).ok_or("unknown operation")?;
    if matches!(&operation.kind, crate::model::Operation::@BLOCK_IR@ { .. }) {
        return lower_block(ctx, operation_id, slots, functions, slot_types, types, next_value);
    }
    let mut statements = Vec::new();
    lower_operation(ctx, operation_id, slots, functions, slot_types, types, &mut statements, next_value)?;
    Ok(structured::Block {
        statements, source: Some(source_span(operation.source)?),
    })
}

fn reachable_functions(
    ctx: &Context,
    entry: FunctionId,
) -> Result<Vec<FunctionId>, &'static str> {
    let mut ordered = vec![entry];
    let mut seen = std::collections::HashSet::from([entry]);
    let mut index = 0;
    while index < ordered.len() {
        let function = ctx.@FUNCTIONS@.get(ordered[index].0).ok_or("unknown function")?;
        let body_id = function.@FUNCTION_BODY@.ok_or("function has no body")?;
        let body = ctx.@OPERATIONS@.get(body_id.0).ok_or("unknown operation")?;
        for expression in &ctx.@EXPRESSIONS@ {
            if expression.source.begin_byte < body.source.begin_byte
                || expression.source.end_byte > body.source.end_byte {
                continue;
            }
            if let crate::model::ExpressionKind::@CALL_IR@ { function: callee, .. } = &expression.kind {
                if seen.insert(*callee) {
                    ordered.push(*callee);
                }
            }
        }
        index += 1;
    }
    Ok(ordered)
}

pub fn @LOWER@(
    ctx: &Context,
    function_id: FunctionId,
) -> Result<structured::Module, &'static str> {
    if !ctx.diagnostics.is_empty() || ctx.modules.iter().any(|module| !module.valid) {
        return Err("semantic errors prevent Core IR lowering");
    }
    let ordered = reachable_functions(ctx, function_id)?;
    let functions: HashMap<_, _> = ordered.iter().enumerate()
        .map(|(index, source)| (*source, core::FunctionId(index))).collect();
    let mut lowered_functions = Vec::new();
    let mut comments = Vec::new();
    let mut types = vec![core::Type::I32, core::Type::Address(core::TypeId(0)), core::Type::Bool,
        core::Type::F32, core::Type::U8, core::Type::Address(core::TypeId(2)),
        core::Type::Address(core::TypeId(3)), core::Type::Address(core::TypeId(4))];
    for (index, aggregate) in ctx.structs.iter().enumerate() {
        if !aggregate.complete {
            return Err("incomplete Core IR aggregate");
        }
        let fields = aggregate.fields.iter().map(|field| {
            let field = ctx.fields.get(field.0).ok_or("unknown aggregate field")?;
            value_type(&field.ty)
        }).collect::<Result<Vec<_>, &'static str>>()?;
        types.push(core::Type::Aggregate(fields));
        types.push(core::Type::Address(core::TypeId(8 + index * 2)));
    }
    let module_scope = ctx.modules.first().ok_or("missing analyzed module")?.scope;
    let globals = ctx.@SCOPES@[module_scope.0].@SCOPE_SYMBOLS@.iter().map(|symbol| {
        let variable = ctx.@VARIABLES@.get(symbol.0).ok_or("unknown global variable")?;
        match &variable.ty {
            crate::model::Type::Array(element, length) => {
                let element = scalar_type(element)?;
                i32::try_from(*length).map_err(|_| "array length out of Core IR range")?;
                let id = core::TypeId(types.len());
                types.push(core::Type::Array { element, length: *length });
                Ok(id)
            }
            other => value_type(other),
        }
    }).collect::<Result<Vec<_>, &'static str>>()?;
    for source_id in ordered {
        let function = ctx.@FUNCTIONS@.get(source_id.0).ok_or("unknown function")?;
        let result = scalar_type(&function.@FUNCTION_RESULT@)?;
        let scope_id = function.@FUNCTION_SCOPE@.ok_or("function has no scope")?;
        let scope = ctx.@SCOPES@.get(scope_id.0).ok_or("unknown scope")?;
        let mut slots = HashMap::new();
        let mut slot_types = Vec::new();
        let mut parameters = Vec::new();
        for (index, _) in function.@FUNCTION_PARAMETERS@.iter().enumerate() {
            let symbol = *scope.@SCOPE_SYMBOLS@.get(index).ok_or("missing parameter symbol")?;
            let variable = ctx.@VARIABLES@.get(symbol.0).ok_or("unknown parameter variable")?;
            if variable.owner != scope_id || variable.ty != function.@FUNCTION_PARAMETERS@[index] {
                return Err("unsupported Core IR parameter");
            }
            let slot = core::SlotId(slot_types.len());
            slot_types.push(scalar_type(&variable.ty)?);
            slots.insert(symbol, slot);
            parameters.push(slot);
        }
        let body_id = function.@FUNCTION_BODY@.ok_or("function has no body")?;
        let body = ctx.@OPERATIONS@.get(body_id.0).ok_or("unknown operation")?;
        let crate::model::Operation::@BLOCK_IR@ { .. } = &body.kind else {
            return Err("unsupported Core IR function body");
        };
        comments.extend(ctx.comments.iter()
            .filter(|comment| body.source.begin_byte <= comment.source.begin_byte
                && comment.source.end_byte <= body.source.end_byte)
            .map(|comment| Ok(structured::SourceComment {
                text: comment.text.clone(), source: source_span(comment.source)?,
            }))
            .collect::<Result<Vec<_>, &'static str>>()?);
        let mut next_value = 0;
        let lowered = lower_block(ctx, body_id, &mut slots, &functions,
            &mut slot_types, &mut types, &mut next_value)?;
        lowered_functions.push(structured::Function {
            name: function.name.clone(), parameters, slots: slot_types,
            result: Some(result), body: lowered,
        });
    }
    let module = structured::Module {
        types,
        globals,
        functions: lowered_functions,
        comments,
    };
    structured::lower_to_cfg(&module).map_err(|_| "invalid generated Core IR")?;
    Ok(module)
}
)RUST";
  for (const auto &[key, value] : bindings) {
    std::string marker = "@";
    for (const char ch : key)
      marker += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    marker += "@";
    for (std::size_t position = output.find(marker);
         position != std::string::npos;
         position = output.find(marker, position + value.size()))
      output.replace(position, marker.size(), value);
  }
  return output;
}

} // namespace coge
