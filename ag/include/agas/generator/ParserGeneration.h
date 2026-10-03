#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "agas/generator/AstSchema.h"
#include "agas/generator/ProductionMetadata.h"
#include "agas/generator/ReductionProgram.h"
#include "agas/model/BnfLowering.h"
#include "agas/model/SyntaxModel.h"
#include "lr/LRkDfa.h"
#include "lr/ParseTable.h"

namespace agas::generator {

enum class ParserAlgorithm { CanonicalLr, Lalr };

struct ParserConfiguration {
  ParserAlgorithm algorithm{ParserAlgorithm::CanonicalLr};
  std::size_t lookahead{1};

  auto operator==(const ParserConfiguration &) const -> bool = default;
};

class ParserConfigurationError final : public std::runtime_error {
public:
  ParserConfigurationError(model::SourceSpan span, std::string message);

  [[nodiscard]] auto span() const noexcept -> const model::SourceSpan &;

private:
  model::SourceSpan span_;
};

struct ResolvedConflict {
  std::size_t preferenceIndex{};
  zbik::Conflict original;
  zbik::Action selected;
};

class GeneratedParserTable {
public:
  GeneratedParserTable(ParserConfiguration configuration, model::BnfModel bnf,
                       AstSchema astSchema, ProductionMetadata productions,
                       AstReductionProgram reductions,
                       zbik::LRkDfaStats dfaStats, zbik::ParseTable table,
                       std::vector<ResolvedConflict> resolvedConflicts = {});

  [[nodiscard]] auto configuration() const noexcept
      -> const ParserConfiguration &;
  [[nodiscard]] auto bnf() const noexcept -> const model::BnfModel &;
  [[nodiscard]] auto astSchema() const noexcept -> const AstSchema &;
  [[nodiscard]] auto productions() const noexcept -> const ProductionMetadata &;
  [[nodiscard]] auto reductions() const noexcept -> const AstReductionProgram &;
  [[nodiscard]] auto dfaStatistics() const noexcept
      -> const zbik::LRkDfaStats &;
  [[nodiscard]] auto table() const noexcept -> const zbik::ParseTable &;
  [[nodiscard]] auto resolvedConflicts() const noexcept
      -> const std::vector<ResolvedConflict> &;

private:
  ParserConfiguration configuration_;
  model::BnfModel bnf_;
  AstSchema astSchema_;
  ProductionMetadata productions_;
  AstReductionProgram reductions_;
  zbik::LRkDfaStats dfaStats_;
  zbik::ParseTable table_;
  std::vector<ResolvedConflict> resolvedConflicts_;
};

[[nodiscard]] auto parserConfiguration(const model::SyntaxDocument &document)
    -> ParserConfiguration;

[[nodiscard]] auto generateParserTable(const model::SyntaxDocument &document)
    -> GeneratedParserTable;

} // namespace agas::generator
