#pragma once

#include <optional>

#include "agas/model/SyntaxModel.h"

namespace agas::model {

// Infer transparent rule chains and parenthesized grouping from the grammar.
// The returned index addresses the source alternative, before BNF lowering.
[[nodiscard]] auto astChainOperand(const SyntaxDocument &document,
                                   const ParserAlternative &alternative)
    -> std::optional<std::size_t>;

} // namespace agas::model
