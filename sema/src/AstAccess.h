#pragma once
#include "agas/runtime/ReductionRuntime.h"
#include "agsem/Diagnostics.h"
#include <algorithm>
#include <stdexcept>
#include <utility>
namespace agsem::ast {
using Value = agas::runtime::AstValue;
using Kind = agas::runtime::AstValueKind;
inline auto find(const Value &value, std::string_view name) -> const Value * {
  const auto it = std::ranges::find(value.fieldNames, name);
  return it == value.fieldNames.end()
             ? nullptr
             : &value.elements.at(it - value.fieldNames.begin());
}
inline auto field(const Value &value, std::string_view name) -> const Value & {
  const auto *result = find(value, name);
  if (!result)
    throw std::runtime_error("missing AST field: " + std::string{name});
  return *result;
}
inline auto field(Value &value, std::string_view name) -> Value & {
  return const_cast<Value &>(field(std::as_const(value), name));
}
inline auto optional(const Value &value) -> const Value * {
  return value.elements.empty() ? nullptr : &value.elements.front();
}
inline auto token(const Value &value) -> std::string {
  if (value.kind == Kind::Token)
    return value.tokenText;
  if (value.elements.size() == 1)
    return token(value.elements.front());
  return {};
}
inline auto location(const Value &value) -> SourceLocation {
  return {0, value.sourceSpan.beginByte, value.sourceSpan.endByte};
}
} // namespace agsem::ast
