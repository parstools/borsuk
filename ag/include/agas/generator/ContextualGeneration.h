#pragma once

#include "agas/generator/ParserGeneration.h"
#include "lexer/Utf8Lexer.h"
#include "lr/ScopedLexerGrammar.h"
#include "lr/ContextualLRMachine.h"

namespace agas::generator {

// The source maps preserve AST and coverage identities through specialization.
struct GeneratedContextualParser {
  model::BnfModel source;
  zbik::Utf8Lexer lexer;
  zbik::ScopedLexerGrammar scoped;
  zbik::LRkDfaStats statistics;
  zbik::ParseTable table;
  std::size_t resolved{};
  std::vector<ResolvedConflict> resolvedConflicts;
};

[[nodiscard]] auto generateContextualParser(const model::SyntaxDocument &document)
    -> GeneratedContextualParser;

[[nodiscard]] auto contextualAstTable(const model::SyntaxDocument &document,
                                    const GeneratedContextualParser &generated)
    -> GeneratedParserTable;

} // namespace agas::generator
