#include "coge/ExecutionDeclarations.h"

#include <sstream>

namespace coge {
namespace {
auto rustLocalName(std::string_view name) -> std::string {
  if (name == "type")
    return "r#type";
  std::string result;
  for (const auto character : name) {
    if (character >= 'A' && character <= 'Z') {
      result.push_back('_');
      result.push_back(static_cast<char>(character - 'A' + 'a'));
    } else {
      result.push_back(character);
    }
  }
  return result;
}
} // namespace

auto emitExecutionFunctionSignatureRust(
    const ExecutionFunctionSignature &function) -> std::string {
  std::ostringstream output;
  output << "    fn " << function.name
         << (function.mutates ? "(&mut self" : "(&self");
  for (const auto &parameter : function.parameters)
    output << ", "
           << (parameter.used ? rustLocalName(parameter.name)
                              : "_" + parameter.name)
           << ": " << emitExecutionTypeRust(parameter.type);
  output << ") -> " << emitExecutionTypeRust(function.result) << " {\n";
  return output.str();
}

auto emitExecutionDeclarationsRust(const ExecutionDeclarations &declarations,
                                   std::string_view contextType)
    -> std::string {
  std::ostringstream output;
  for (const auto &declaration : declarations.declarations) {
    if (declaration.kind == ExecutionDeclarationKind::Record) {
      output << "#[derive(Clone, Debug, PartialEq)]\npub struct "
             << declaration.name << " {\n";
      for (const auto &field : declaration.fields)
        output << "    pub " << rustLocalName(field.name) << ": "
               << emitExecutionTypeRust(field.type) << ",\n";
      output << "}\n\n";
    } else {
      output << "#[derive(Clone, Debug, PartialEq)]\npub enum "
             << declaration.name << " {\n";
      for (const auto &variant : declaration.variants) {
        output << "    " << variant.name;
        if (!variant.payload.empty()) {
          output << '(';
          for (std::size_t index = 0; index < variant.payload.size(); ++index) {
            if (index)
              output << ", ";
            output << emitExecutionTypeRust(variant.payload[index]);
          }
          output << ')';
        }
        output << ",\n";
      }
      output << "}\n\n";
    }
  }
  if (declarations.runtimeState) {
    const auto &state = *declarations.runtimeState;
    output << "pub struct Interpreter<'a> {\n"
           << "    context: &'a " << contextType << ",\n"
           << "    " << state.name << ": "
           << emitExecutionTypeRust(state.type) << ",\n"
           << "}\n\n";
  }
  return output.str();
}

} // namespace coge
