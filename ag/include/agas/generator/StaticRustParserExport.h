#pragma once

#include <string>

#include "agas/artifact/GrammarSections.h"
#include "agas/artifact/LexerSection.h"
#include "agas/artifact/ParserTableSection.h"
#include "agas/generator/ReductionProgram.h"

namespace agas::generator {

// Emits immutable LR(k) execution data. AST reduction programs remain a
// separate generated layer.
[[nodiscard]] auto
exportStaticRustParser(const artifact::ArtifactParserTable &table,
                       const artifact::ArtifactSymbols &symbols,
                       const artifact::ArtifactProductions &productions)
    -> std::string;

[[nodiscard]] auto
exportStaticRustLexer(const artifact::ArtifactLexer &lexer,
                      const artifact::ArtifactSymbols &symbols) -> std::string;

[[nodiscard]] auto
exportStaticRustReductions(const AstReductionProgram &reductions,
                           const artifact::ArtifactProductions &productions)
    -> std::string;

} // namespace agas::generator
