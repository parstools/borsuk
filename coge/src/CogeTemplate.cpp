#include "DocumentStorage.h"
#include "agas/artifact/Sha256.h"
#include "agsem/SemanticBinding.h"
#include "coge/Template.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>
namespace coge {
namespace {
using namespace agsem;
auto targetName(TemplateTarget target) -> std::string {
  switch (target) {
  case TemplateTarget::Interpreter:
    return "interpreter";
  case TemplateTarget::C:
    return "c";
  case TemplateTarget::Llvm:
    return "llvm";
  }
  throw std::runtime_error("invalid template target");
}
auto executionSlots(const TemplateOptions &options,
                    const ContractEnvironment &contracts,
                    const std::vector<ContractSymbol> &symbols) -> std::string {
  if (options.targets.empty())
    throw std::runtime_error(
        "coge template requires at least one explicit target");
  std::set<std::string> targets;
  for (auto target : options.targets)
    if (!targets.insert(targetName(target)).second)
      throw std::runtime_error("duplicate template target");
  const auto quote = [](std::string_view value) {
    return nlohmann::ordered_json(value).dump();
  };
  std::string text = "execution_obligations {\n  targets";
  for (const auto &target : targets)
    text += " " + quote(target);
  text += ";\n";
  const auto pending = [&](std::string_view kind, std::string_view subject,
                           std::string_view slot, std::string_view reason) {
    text += "  pending " + quote(kind) + " " + quote(subject) + " " +
            quote(slot) + " " + quote(reason) + ";\n";
  };
  if (targets.contains("interpreter")) {
    pending("execution_contract", "runtime", "interpreter",
            "Declare runtime state, execution types and intrinsic contracts.");
    pending(
        "execution_contract", "ir", "schema",
        "Select the execution IR explicitly; no runtime/profile is inferred.");
    pending("execution_function", "interpreter", "entry",
            "Declare the execution model and interpreter entry points.");
    for (const auto &symbol : symbols) {
      if (!symbol.type || symbol.type->kind != TypeContract::Kind::Enum ||
          (symbol.name != "Operation" && symbol.name != "ExpressionKind"))
        continue;
      std::string subject = symbol.name;
      // Preserve the manifest export identity; versions and hashes are
      // provenance.
      for (std::size_t i = 0; i < contracts.symbols().size(); ++i)
        if (contracts.symbols()[i].name == symbol.name) {
          subject = contracts.origins().at(i).identity.id + "::" + symbol.name;
          break;
        }
      for (const auto &[variant, payload] : symbol.type->variants)
        pending(symbol.name == "Operation" ? "execute_handler"
                                           : "evaluate_handler",
                subject, variant,
                "Implement this declared IR variant or select a checked "
                "execution profile.");
    }
  }
  if (targets.contains("c") || targets.contains("llvm"))
    pending("lowering_role", "lowering", "configuration",
            "Declare the lowering profile, model roles and checked adapter "
            "bindings.");
  for (const auto &target : targets)
    if (target != "interpreter")
      pending("backend_binding", target, "configuration",
              "Declare the backend profile and checked emitter bindings.");
  return text + "}";
}
auto finish(ProjectionResult projection, const DocumentFrontend &frontend)
    -> Outcome<ProjectionResult> {
  Outcome<ProjectionResult> result;
  auto parsed = frontend.parse(projection.text);
  result.diagnostics = std::move(parsed.diagnostics);
  if (!parsed.value)
    return result;
  result.diagnostics = validateDocumentSections(*parsed.value);
  if (!result.diagnostics.empty())
    return result;
  projection.outputIdentity = parsed.value->identity();
  result.value = std::move(projection);
  return result;
}
} // namespace

auto templateFromAg(std::string_view source, const DocumentFrontend &frontend,
                    const ContractEnvironment &contracts,
                    const TemplateOptions &options)
    -> Outcome<ProjectionResult> {
  auto result = agsem::templateFromAg(source, frontend);
  if (!result.value)
    return result;
  try {
    auto projection = std::move(*result.value);
    projection.text.replace(0, 4, "coge");
    const auto begin = projection.text.size();
    projection.text +=
        "\n" + executionSlots(options, contracts, contracts.symbols());
    projection.origins.push_back(
        {begin, projection.text.size(), source.size(), source.size(), true});
    projection.contracts = contracts.identities();
    return finish(std::move(projection), frontend);
  } catch (const std::exception &error) {
    result.value.reset();
    result.diagnostics.push_back(
        {Severity::Error, "template.invalid", error.what(), {}, {}});
    return result;
  }
}
auto templateFromSema(const ParsedDocument &document,
                      const DocumentFrontend &frontend,
                      const ContractEnvironment &contracts,
                      const TemplateOptions &options)
    -> Outcome<ProjectionResult> {
  Outcome<ProjectionResult> result;
  auto sema = makeSemaDocument(document);
  result.diagnostics = std::move(sema.diagnostics);
  if (!sema.value)
    return result;
  auto bound = bindSemantics(*sema.value, contracts);
  result.diagnostics = std::move(bound.diagnostics);
  if (!bound.value)
    return result;
  try {
    std::vector<ContractSymbol> symbols;
    for (const auto &entry : bound.value->symbols())
      symbols.push_back(entry.contract);
    auto text = executionSlots(options, contracts, symbols);
    const auto size = document.source().text.size();
    // Horizontal whitespace may still belong to an unterminated // comment.
    // Start the new section on its own line without changing existing bytes.
    if (size && document.source().text.back() != '\n' &&
        document.source().text.back() != '\r')
      text = "\n" + text;
    result = applyProjection(
        document, {{agsem::DocumentAccess::kindLocation(document), "coge"},
                   {{0, size, size}, std::move(text)}});
    if (!result.value)
      return result;
    result.value->contracts = contracts.identities();
    return finish(std::move(*result.value), frontend);
  } catch (const std::exception &error) {
    result.value.reset();
    result.diagnostics.push_back(
        {Severity::Error, "template.invalid", error.what(), {}, {}});
    return result;
  }
}
} // namespace coge
