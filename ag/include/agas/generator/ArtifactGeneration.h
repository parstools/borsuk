#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "agas/artifact/ArtifactPackage.h"
#include "agas/artifact/GrammarSections.h"
#include "agas/artifact/LexerSection.h"
#include "agas/generator/LexerGeneration.h"
#include "agas/generator/ContextualGeneration.h"
#include "agas/generator/ParserGeneration.h"
#include "agas/model/SyntaxModel.h"

namespace agas::generator {

[[nodiscard]] auto
buildGrammarArtifactSections(const model::SyntaxDocument &document,
                             const GeneratedParserTable &generated)
    -> std::pair<artifact::ArtifactSymbols, artifact::ArtifactProductions>;

[[nodiscard]] auto
buildLexerArtifactSection(const GeneratedLexerAutomaton &lexer,
                          const artifact::ArtifactSymbols &symbols, bool allNfas = false)
    -> artifact::ArtifactLexer;

struct ArtifactBuildIdentity {
  std::string generatorVersion;
  std::string zbikRevision;
  std::string unicodeVersion;
};

[[nodiscard]] auto buildParserArtifactPackage(
    const model::SyntaxDocument &document,
    const GeneratedParserTable &generated, const GeneratedLexerAutomaton &lexer,
    std::string_view exactSource, std::string_view expandedSource,
    const ArtifactBuildIdentity &identity,
    std::optional<artifact::ArtifactLexer> contextualLexer = std::nullopt) -> artifact::ArtifactPackage;

[[nodiscard]] auto buildContextualArtifactPackage(
    const model::SyntaxDocument &document, const GeneratedContextualParser &generated,
    std::string_view source, const ArtifactBuildIdentity &identity) -> artifact::ArtifactPackage;

} // namespace agas::generator
