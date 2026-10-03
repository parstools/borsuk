#include "agas/runtime/SourceText.h"

#include <stdexcept>

namespace agas::runtime {
auto captureSourceText(std::string source, const ArtifactLexResult &lexed)
    -> model::SourceText {
  model::SourceText result{std::move(source), {}};
  std::size_t cursor = 0;
  for (const auto &token : lexed.tokens) {
    if (token.offset < cursor || token.offset > result.text.size() ||
        token.text.size() > result.text.size() - token.offset ||
        result.text.compare(token.offset, token.text.size(), token.text) != 0)
      throw std::invalid_argument("tokens do not match the source text");
    if (token.offset > cursor)
      result.pieces.push_back(
          {model::SourcePieceKind::Trivia, cursor, token.offset, {}});
    cursor = token.offset + token.text.size();
    result.pieces.push_back({token.channel ? model::SourcePieceKind::Trivia
                                           : model::SourcePieceKind::Token,
                             token.offset, cursor, token.terminal});
  }
  if (cursor < result.text.size())
    result.pieces.push_back(
        {model::SourcePieceKind::Trivia, cursor, result.text.size(), {}});
  return result;
}
} // namespace agas::runtime
