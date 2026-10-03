#include "agsem/DocumentFrontend.h"
#include "agsem/SemanticBinding.h"
#include "agsem/SemanticCompleteness.h"
#include "agsem/Template.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
int main(int argc, char **argv) {
  try {
    if (argc != 5)
      return 2;
    const agsem::DocumentFrontend frontend{argv[1], argv[2]};
    const auto draft = agsem::templateFromAg(
        "grammar Draft; node start : ID; ID:'a';", frontend);
    if (!draft.value)
      throw std::runtime_error("core-only template generation failed");
    const auto draftParsed = frontend.parse(draft.value->text);
    if (!draftParsed.value ||
        draftParsed.value->analysisStatuses().size() != 1 ||
        draftParsed.value->analysisStatuses().front().declaredState !=
            agsem::ObligationState::Pending)
      throw std::runtime_error("core-only template metadata missing");
    const auto draftReport = agsem::assessCompleteness(*draftParsed.value, {});
    if (!draftReport.value || draftReport.value->complete() ||
        draftReport.value->hasErrors())
      throw std::runtime_error("core-only located completeness report failed");
    agsem::ContractEnvironment contracts;
    if (!contracts
             .addManifest(
                 R"({"format":1,"id":"link-test","version":"1","semantic":[{"name":"Context","kind":"opaque"},{"name":"external","kind":"function","parameters":[{"name":"Int"}],"result":{"name":"Int"},"rust":["crate","external"]}]})")
             .empty())
      return 3;
    const auto &status = draftParsed.value->analysisStatuses().front();
    const auto parsed = frontend.parse(
        "sema Test; grammar Test; semantic_model { rust_context Context; "
        "analyzer start() -> Int; function helper(value: Int) -> Int { return "
        "external(value); } } node start : ID analysis_status { id \"" +
        status.id.value + "\"; syntax_sha256 \"" + status.syntaxSha256 +
        "\"; state implemented; } "
        "analysis { result = helper(1); "
        "}; ID:'a';");
    if (!parsed.value)
      return 4;
    const auto document = agsem::makeSemaDocument(*parsed.value);
    const auto bound = agsem::bindSemantics(*document.value, contracts);
    if (!bound.value)
      return 5;
    const auto checked = agsem::prepareSemanticModel(*bound.value);
    if (!checked.value) {
      for (const auto &error : checked.diagnostics)
        std::cerr << error.message << '\n';
      return 6;
    }
    const auto rust = agsem::emitCheckedSemanticsRust(**checked.value);
    if (rust.sema.empty() ||
        rust.semaLib.find("crate::external(value)") == std::string::npos)
      return 7;
    const auto read = [](const char *path) {
      std::ifstream stream(path);
      return std::string{std::istreambuf_iterator<char>{stream}, {}};
    };
    std::shared_ptr<const agsem::CheckedSemantics> retained;
    {
      agsem::ContractEnvironment localContracts;
      if (!localContracts.addManifest(read(argv[4])).empty())
        return 8;
      const auto localParsed = frontend.parse(read(argv[3]));
      if (!localParsed.value)
        return 9;
      const auto localDocument = agsem::makeSemaDocument(*localParsed.value);
      if (!localDocument.value)
        return 10;
      const auto localBound =
          agsem::bindSemantics(*localDocument.value, localContracts);
      if (!localBound.value)
        return 11;
      const auto localChecked = agsem::prepareSemanticModel(*localBound.value);
      if (!localChecked.value)
        return 12;
      retained = *localChecked.value;
    }
    const auto &bindings = agsem::checkedModelBindings(*retained);
    if (!bindings.schema() || bindings.ports().size() != 30 ||
        bindings.names("condition", "condition_bool").at("emit") !=
            "add_expression")
      return 13;
    if (agsem::emitCheckedSemanticsRust(*retained).semaLib.find(
            "impl agsem_runtime::ReturnPolicyContext") == std::string::npos)
      return 14;
    if (agsem::inspectSemanticModel(*retained).find("standard_semantic_v1") ==
        std::string::npos)
      return 15;
    std::cout
        << "Standalone semantic check and Rust emission without coge OK\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
