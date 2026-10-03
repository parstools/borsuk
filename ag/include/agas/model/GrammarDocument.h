#pragma once

#include "agas/model/SyntaxModel.h"
#include <cstdint>

namespace agas::model {

enum class SourcePieceKind { Token, Trivia };

struct SourcePiece {
  SourcePieceKind kind;
  std::size_t beginByte{};
  std::size_t endByte{};
  std::optional<std::uint32_t> terminal;
};

// Pieces partition the complete UTF-8 source, including skipped comments and
// whitespace. Offsets are byte offsets into the owned text.
struct SourceText {
  std::string text;
  std::vector<SourcePiece> pieces;
};

struct GrammarDocument {
  SyntaxDocument grammar;
  SourceText source;
};

} // namespace agas::model
