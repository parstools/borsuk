#include "coge/PropertyGeneration.h"

#include <sstream>

namespace coge {

auto emitPropertiesRust(const std::optional<PropertyGenerationPlan> &plan)
    -> std::string {
  if (!plan)
    return {};
  std::ostringstream out;
  out << "// Generated boundary checks from execution_model.properties; not an exhaustive proof.\n"
      << "fn check_execution_properties(__executor: &Interpreter<'_>) {\n"
      << "    const I32_CASES: &[i32] = &[";
  for (const auto value : plan->boundaryCases)
    out << value << "_i32, ";
  out << "];\n    let __property_source = InputSpan { begin_byte: 0, end_byte: 0 };\n";
  std::size_t index = 0;
  for (const auto &property : plan->checks) {
    ++index;
    std::string call = "__executor." + property.function + "(";
    for (std::size_t arg = 0; arg < property.arguments.size(); ++arg) {
      if (arg)
        call += ", ";
      call += property.arguments[arg];
    }
    if (property.source)
      call += (property.arguments.empty() ? "" : ", ") +
              std::string{"__property_source"};
    call += ")";
    out << "    {\n        let mut __checked = 0usize;\n";
    std::string pad = "        ";
    for (const auto &binding : property.bindings) {
      out << pad << "for &" << binding << " in I32_CASES {\n";
      pad += "    ";
    }
    if (property.constraint) {
      auto condition = emitPropertyExpressionRust(*property.constraint);
      if (property.fitsI32) {
        condition = "i32::try_from(" +
                    emitPropertyArgumentRust(*property.constraint) +
                    ").is_ok()";
        if (property.negatedFits)
          condition = "!" + condition;
      }
      out << pad << "if !(" << condition << ") { continue; }\n";
    }
    std::string message = "\"property " + std::to_string(index) + " (" +
                          property.function + ")";
    for (const auto &binding : property.bindings)
      message += " " + binding + "={" + binding + "}";
    message += "\"";
    out << pad << "__checked += 1;\n";
    if (property.expected) {
      out << pad << "assert_eq!(" << call << ".map(i128::from), Ok("
          << emitPropertyArgumentRust(*property.expected) << "), " << message
          << ");\n";
    } else {
      out << pad << "let __fault = " << call << ".expect_err(&format!("
          << message << "));\n"
          << pad << "assert_eq!(__fault.message, "
          << *property.errorMessageLiteral << ", " << message << ");\n";
      if (property.source)
        out << pad << "assert_eq!(__fault.source, __property_source, "
            << message << ");\n";
    }
    for (std::size_t level = 0; level < property.bindings.size(); ++level) {
      pad.resize(pad.size() - 4);
      out << pad << "}\n";
    }
    out << "        assert!(__checked > 0, \"property " << index
        << " has no matching boundary cases\");\n    }\n";
  }
  out << "}\n";
  return out.str();
}

} // namespace coge
