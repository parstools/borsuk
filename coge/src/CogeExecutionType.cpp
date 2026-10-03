#include "coge/ExecutionType.h"

#include <set>
#include <stdexcept>
#include <utility>

namespace coge {

auto executionTypeKind(const std::string &name) -> ExecutionTypeKind {
  if (name == "Option") return ExecutionTypeKind::Option;
  if (name == "List") return ExecutionTypeKind::List;
  if (name == "Result") return ExecutionTypeKind::Result;
  if (name == "Map") return ExecutionTypeKind::Map;
  if (name == "Store") return ExecutionTypeKind::Store;
  if (name == "I32") return ExecutionTypeKind::I32;
  if (name == "F32") return ExecutionTypeKind::F32;
  if (name == "U8") return ExecutionTypeKind::U8;
  if (name == "U64") return ExecutionTypeKind::U64;
  if (name == "Index") return ExecutionTypeKind::Index;
  if (name == "Bool") return ExecutionTypeKind::Bool;
  if (name == "Unit") return ExecutionTypeKind::Unit;
  if (name == "Text") return ExecutionTypeKind::Text;
  if (name == "SourceRange") return ExecutionTypeKind::SourceRange;
  return ExecutionTypeKind::Named;
}

namespace {
void validateNamedType(const std::string &name) {
  if (name.empty() || name == "Self")
    throw std::runtime_error("unsupported Rust type identifier: " + name);
  const auto candidate = name.front() >= 'A' && name.front() <= 'Z'
                             ? "_" + name : name;
  if (!(candidate.front() == '_' ||
        (candidate.front() >= 'a' && candidate.front() <= 'z')))
    throw std::runtime_error("unsupported Rust identifier: " + candidate);
  for (const auto character : candidate)
    if (!(character == '_' || (character >= 'a' && character <= 'z') ||
          (character >= 'A' && character <= 'Z') ||
          (character >= '0' && character <= '9')))
      throw std::runtime_error("unsupported Rust identifier: " + candidate);
  static const std::set<std::string> keywords{
      "as", "async", "await", "break", "const", "continue", "crate",
      "dyn", "else", "enum", "extern", "false", "fn", "for", "if",
      "impl", "in", "let", "loop", "match", "mod", "move", "mut",
      "pub", "ref", "return", "self", "Self", "static", "struct",
      "super", "trait", "true", "type", "unsafe", "use", "where",
      "while"};
  if (keywords.contains(candidate) && candidate != "type")
    throw std::runtime_error("Rust keyword used as identifier: " + candidate);
}
} // namespace

auto prepareExecutionType(std::string name,
                          std::vector<ExecutionType> arguments)
    -> ExecutionType {
  const auto kind = executionTypeKind(name);
  if ((kind == ExecutionTypeKind::Option || kind == ExecutionTypeKind::List) &&
      arguments.size() != 1)
    throw std::runtime_error(name + " needs one type argument");
  if ((kind == ExecutionTypeKind::Result || kind == ExecutionTypeKind::Map ||
       kind == ExecutionTypeKind::Store) && arguments.size() != 2)
    throw std::runtime_error("execution " + name +
                             " needs two type arguments");
  if (kind != ExecutionTypeKind::Named &&
      kind != ExecutionTypeKind::Option && kind != ExecutionTypeKind::List &&
      kind != ExecutionTypeKind::Result && kind != ExecutionTypeKind::Map &&
      kind != ExecutionTypeKind::Store && !arguments.empty())
    throw std::runtime_error(name + " cannot have type arguments");
  if (kind == ExecutionTypeKind::Named)
    validateNamedType(name);
  if (kind == ExecutionTypeKind::Named && !arguments.empty())
    throw std::runtime_error("unsupported execution type: " + name);
  return {kind, std::move(name), std::move(arguments)};
}

auto emitExecutionTypeRust(const ExecutionType &type) -> std::string {
  const auto &args = type.arguments;
  switch (type.kind) {
  case ExecutionTypeKind::Option:
    return "Option<" + emitExecutionTypeRust(args.at(0)) + ">";
  case ExecutionTypeKind::List:
    return "Vec<" + emitExecutionTypeRust(args.at(0)) + ">";
  case ExecutionTypeKind::Result:
    return "Result<" + emitExecutionTypeRust(args.at(0)) + ", " +
           emitExecutionTypeRust(args.at(1)) + ">";
  case ExecutionTypeKind::Map:
    return "std::collections::HashMap<" +
           emitExecutionTypeRust(args.at(0)) + ", " +
           emitExecutionTypeRust(args.at(1)) + ">";
  case ExecutionTypeKind::Store:
    return "agsem_runtime::Store<" + emitExecutionTypeRust(args.at(0)) +
           ", " + emitExecutionTypeRust(args.at(1)) + ">";
  case ExecutionTypeKind::I32: return "i32";
  case ExecutionTypeKind::F32: return "f32";
  case ExecutionTypeKind::U8: return "u8";
  case ExecutionTypeKind::U64: return "u64";
  case ExecutionTypeKind::Index: return "usize";
  case ExecutionTypeKind::Bool: return "bool";
  case ExecutionTypeKind::Unit: return "()";
  case ExecutionTypeKind::Text: return "&'static str";
  case ExecutionTypeKind::SourceRange: return "InputSpan";
  case ExecutionTypeKind::Named: return type.name;
  }
  throw std::runtime_error("unsupported execution type");
}

} // namespace coge
