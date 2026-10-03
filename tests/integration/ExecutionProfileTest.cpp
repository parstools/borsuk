#include "CogeDocumentStorage.h"
#include "agsem/DocumentFrontend.h"
#include "coge/GenerationPreparation.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
namespace {
auto read(const char *path) -> std::string {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot read test input");
  return {std::istreambuf_iterator<char>{input}, {}};
}
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 5)
      return 2;
    const agsem::DocumentFrontend frontend{argv[1], argv[2]};
    std::shared_ptr<const coge::CheckedExecutionModel> retained;
    std::optional<coge::CheckedGeneration> generation;
    std::string inspection;
    {
      auto source = read(argv[3]);
      require(source.find("    execution_profile shared_value_v1;\n") !=
                  std::string::npos,
              "missing authored execution profile");
      require(source.find("    function invalid_value(") == std::string::npos,
              "authored model still duplicates the profile member");
      agsem::ContractEnvironment contracts;
      require(contracts.addManifest(read(argv[4])).empty(),
              "invalid contracts");
      auto parsed = frontend.parse(source);
      require(parsed.value.has_value(), "cannot parse profile model");
      auto document = coge::makeCogeDocument(*parsed.value);
      require(document.value.has_value(), "cannot create coge document");
      auto bound = agsem::bindSemantics(document.value->semantics(), contracts);
      require(bound.value.has_value(), "cannot bind semantic model");
      auto checked = coge::checkCoge(*document.value, *bound.value, contracts);
      require(checked.value.has_value(), "cannot check profile model");
      retained = checked.value->execution();
      auto semantics = agsem::prepareSemanticModel(*bound.value);
      require(semantics.value.has_value(), "cannot prepare semantics");
      auto prepared =
          coge::prepareGeneration(*document.value, **semantics.value,
                                  coge::generationSelection(*document.value),
                                  contracts, *checked.value);
      require(prepared.value.has_value(), "cannot prepare checked generation");
      generation = std::move(*prepared.value);
      require(checked.value->completeness().complete(),
              "validation lost its completeness proof");
      auto automatic = coge::prepareGeneration(
          *document.value, **semantics.value,
          coge::generationSelection(*document.value), contracts);
      require(automatic.value.has_value(), "automatic generation admission");
      require(coge::emitCheckedGeneration(*automatic.value).interpreter ==
                  coge::emitCheckedGeneration(*generation).interpreter,
              "generation overloads disagree");
      auto selection = coge::generationSelection(*document.value);
      selection.interpreter = !selection.interpreter;
      auto wrongSelection =
          coge::prepareGeneration(*document.value, **semantics.value, selection,
                                  contracts, *checked.value);
      require(!wrongSelection.value &&
                  wrongSelection.diagnostics.front().code ==
                      "document.identity_mismatch",
              "reused validation with wrong targets");
      agsem::ContractEnvironment otherContracts;
      require(otherContracts.addManifest(read(argv[4]) + "\n").empty(),
              "other contracts");
      auto wrongContracts =
          coge::prepareGeneration(*document.value, **semantics.value,
                                  coge::generationSelection(*document.value),
                                  otherContracts, *checked.value);
      require(!wrongContracts.value &&
                  wrongContracts.diagnostics.front().code ==
                      "document.identity_mismatch",
              "reused validation with wrong contracts");
      coge::GenerationInput raw;
      raw.execution = retained;
      bool rawRejected = false;
      try {
        (void)coge::prepareCheckedGeneration(std::move(raw));
      } catch (const std::runtime_error &e) {
        rawRejected = std::string(e.what()).find(
                          "typed_preparation_required") != std::string::npos;
      }
      require(rawRejected, "raw generation bypassed completeness proof");
      inspection = coge::inspectExecutionModel(*retained);
      auto mismatch = *checked.value;
      auto other = frontend.parse(source + "\n// Different source identity.\n");
      auto otherDocument = coge::makeCogeDocument(*other.value);
      auto rejected = coge::prepareGeneration(
          *otherDocument.value, **semantics.value,
          coge::generationSelection(*otherDocument.value), contracts, mismatch);
      require(!rejected.value && rejected.diagnostics.front().code ==
                                     "document.identity_mismatch",
              "reused model with wrong identity");
      auto legacy =
          coge::tryPrepareGeneration(document.value->execution(), "Context");
      require(!legacy.value && !legacy.diagnostics.empty() &&
                  legacy.diagnostics.front().message.find(
                      "requires bound model contracts") != std::string::npos,
              "profile bypassed bound preparation");
    }
    require(retained->model().profile.has_value() &&
                retained->model().functions.size() == 54,
            "lost checked model");
    require(inspection == coge::inspectExecutionModel(*retained),
            "inspection depends on destroyed inputs");
    const auto rust = coge::emitCheckedGeneration(*generation);
    require(rust.interpreter.find("fn invalid_value") != std::string::npos,
            "emission lost profile member");
    require(rust.properties.find("checked_add_i32") != std::string::npos,
            "properties lost profile functions");
    std::cout << "Checked execution owns data, preserves identity and rejects "
                 "unbound profiles\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
