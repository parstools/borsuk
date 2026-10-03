#include "CogeDocumentStorage.h"
#include "PreparedSemanticModel.h"
#include "coge/GenerationPreparation.h"

namespace agsem {

auto prepareGenerationModel(const PreparedSemanticModel &model,
                            const agas::runtime::AstValue &legacyRoot)
    -> PreparedGenerationModel {
  return coge::prepareGeneration(coge::DocumentAccess::legacyInput(legacyRoot),
                                 semanticContextType(model));
}

auto emitRust(const PreparedSemanticModel &model,
              const PreparedGenerationModel &generation) -> RustFiles {
  auto semantics = emitCheckedSemanticsRust(model);
  auto execution = coge::emitCheckedGeneration(generation);
  return {std::move(semantics.sema),        std::move(semantics.semaLib),
          std::move(execution.interpreter), std::move(execution.properties),
          std::move(execution.lowering),    std::move(execution.backendC),
          std::move(execution.backendLlvm), std::move(semantics.modules)};
}

auto emitRust(const PreparedSemanticModel &model,
              const agas::runtime::AstValue &legacyRoot) -> RustFiles {
  return emitRust(model, prepareGenerationModel(model, legacyRoot));
}

auto emitRust(const agas::runtime::AstValue &root,
              const agas::model::SyntaxDocument &sourceGrammar) -> RustFiles {
  const auto prepared = prepareSemanticModel(root, sourceGrammar);
  return emitRust(*prepared, root);
}

} // namespace agsem
