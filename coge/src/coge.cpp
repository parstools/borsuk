#include "DocumentCli.h"
#include "DocumentStorage.h"
#include "coge/Completeness.h"
#include "coge/GenerationPreparation.h"
#include "coge/Projection.h"
#include "coge/Template.h"
#include <nlohmann/json.hpp>
int runLegacyCogeCli(int argc, char **argv);
namespace {
auto process(const agsem::ParsedDocument &parsed,
             const agsem::ContractEnvironment &contracts,
             const agsem::DocumentFrontend &frontend,
             agsem::cli::Operation operation)
    -> agsem::Outcome<agsem::cli::Artifacts> {
  using namespace agsem::cli;
  agsem::Outcome<Artifacts> result;
  auto document = coge::makeCogeDocument(parsed);
  if (!document.value) {
    result.diagnostics = std::move(document.diagnostics);
    return result;
  }
  if (operation == Operation::EmitAg) {
    auto projection = agsem::projectAg(parsed);
    result.diagnostics = std::move(projection.diagnostics);
    if (projection.value)
      result.value = Artifacts{std::move(projection.value)};
    return result;
  }
  auto bound = agsem::bindSemantics(document.value->semantics(), contracts);
  if (!bound.value) {
    result.diagnostics = std::move(bound.diagnostics);
    return result;
  }
  if (operation == Operation::EmitSema) {
    auto projection =
        coge::projectSema(*document.value, *bound.value, frontend);
    result.diagnostics = std::move(projection.diagnostics);
    if (projection.value)
      result.value = Artifacts{std::move(projection.value)};
    return result;
  }
  auto checked = coge::checkCoge(*document.value, *bound.value, contracts);
  if (!checked.value) {
    result.diagnostics = std::move(checked.diagnostics);
    return result;
  }
  Artifacts output;
  if (operation == Operation::InspectModel) {
    auto semantics = agsem::prepareSemanticModel(*bound.value);
    if (!semantics.value) {
      result.diagnostics = std::move(semantics.diagnostics);
      return result;
    }
    auto generation = coge::prepareGeneration(
        *document.value, **semantics.value,
        coge::generationSelection(*document.value), contracts, *checked.value);
    if (!generation.value) {
      result.diagnostics = std::move(generation.diagnostics);
      return result;
    }
    auto model = nlohmann::ordered_json::parse(
        agsem::inspectSemanticModel(**semantics.value));
    model["execution_configuration"] = {
        {"format", "coge-checked-configuration-v1"},
        {"scope", "lowering and backend configuration"},
        {"lowering", nlohmann::ordered_json::parse(
                         generation.value->lowering().inspection())}};
    const auto backend = [&](const std::optional<coge::BackendBinding> &binding)
        -> nlohmann::ordered_json {
      if (!binding)
        return nullptr;
      return nlohmann::ordered_json::parse(binding->inspection);
    };
    model["execution_configuration"]["backend_c"] =
        backend(generation.value->backends().c());
    model["execution_configuration"]["backend_llvm"] =
        backend(generation.value->backends().llvm());
    model["execution_model"] = nlohmann::ordered_json::parse(
        coge::inspectExecutionModel(*generation.value->execution()));
    output.model = model.dump(2) + '\n';
  } else if (operation == Operation::EmitRust) {
    auto semantics = agsem::prepareSemanticModel(*bound.value);
    if (!semantics.value) {
      result.diagnostics = std::move(semantics.diagnostics);
      return result;
    }
    auto generation = coge::prepareGeneration(
        *document.value, **semantics.value,
        coge::generationSelection(*document.value), contracts, *checked.value);
    if (!generation.value) {
      result.diagnostics = std::move(generation.diagnostics);
      return result;
    }
    output.generationProvenance =
        agsem::cli::generationProvenance(**semantics.value);
    auto provenance =
        nlohmann::ordered_json::parse(output.generationProvenance);
    provenance["execution_profiles"] = nlohmann::ordered_json::array();
    if (const auto &profile = generation.value->execution()->model().profile)
      provenance["execution_profiles"].push_back({{"id", profile->id},
                                                  {"version", profile->version},
                                                  {"sha256", profile->sha256}});
    output.generationProvenance = provenance.dump(2) + '\n';
    auto sema = agsem::emitCheckedSemanticsRust(**semantics.value);
    auto execution = coge::emitCheckedGeneration(*generation.value);
    output.rust = std::move(sema.modules);
    output.rust.emplace("sema_gen.rs", std::move(sema.sema));
    output.rust.emplace("sema_lib_gen.rs", std::move(sema.semaLib));
    for (auto &[name, text] : std::vector<std::pair<std::string, std::string>>{
             {"interpreter_gen.rs", std::move(execution.interpreter)},
             {"interpreter_properties_gen.rs", std::move(execution.properties)},
             {"lowering_gen.rs", std::move(execution.lowering)},
             {"backend_c_gen.rs", std::move(execution.backendC)},
             {"backend_llvm_gen.rs", std::move(execution.backendLlvm)}})
      if (!text.empty())
        output.rust.emplace(name, std::move(text));
  } else if (operation == Operation::Calls) {
    collectCalls(agsem::DocumentAccess::root(parsed), output.calls);
    for (const auto &call : coge::executionCalls(*checked.value->execution()))
      output.calls.insert(call);
  }
  result.value = std::move(output);
  return result;
}
} // namespace
int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i)
    if (std::string_view{argv[i]} == "--legacy")
      return runLegacyCogeCli(argc, argv);
  return agsem::cli::run(
      argc, argv, "coge", process,
      [](std::string_view source, bool fromAg,
         const std::vector<std::string> &targets,
         const agsem::ContractEnvironment &contracts,
         const agsem::DocumentFrontend &frontend) {
        coge::TemplateOptions options;
        for (const auto &target : targets)
          options.targets.push_back(
              target == "interpreter" ? coge::TemplateTarget::Interpreter
              : target == "c"         ? coge::TemplateTarget::C
                                      : coge::TemplateTarget::Llvm);
        if (fromAg)
          return coge::templateFromAg(source, frontend, contracts, options);
        auto parsed = frontend.parse(source);
        if (!parsed.value)
          return agsem::Outcome<agsem::ProjectionResult>{
              {}, std::move(parsed.diagnostics)};
        return coge::templateFromSema(*parsed.value, frontend, contracts,
                                      options);
      },
      [](const auto &parsed, const auto &contracts, auto targets) {
        return coge::assessCompleteness(parsed, contracts, std::move(targets));
      });
}
