#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "agas/generator/LexerGeneration.h"
#include "agas/generator/ParserGeneration.h"
#include "agas/runtime/ReductionRuntime.h"
#include "first/LookaheadWord.h"
#include "grammar/Identifiers.h"
#include "lr/Identifiers.h"

namespace agas::runtime {

struct AstParseError {
  zbik::StateId state;
  std::size_t tokenIndex{};
  std::uint64_t byteOffset{};
  InputSpan span;
  zbik::LookaheadWord lookahead;
  std::vector<zbik::LookaheadWord> expected;
  std::string message;

  auto operator==(const AstParseError &) const -> bool = default;
};

struct AstParseResult {
  std::optional<AstValue> root;
  std::optional<AstParseError> error;

  [[nodiscard]] auto accepted() const noexcept -> bool;
};

class GeneratedAstParser {
public:
  explicit GeneratedAstParser(const generator::GeneratedParserTable &parser);
  GeneratedAstParser(generator::GeneratedParserTable &&) = delete;

  // inputSize is the byte length of the original UTF-8 input, including
  // trailing skipped or hidden text.
  [[nodiscard]] auto parse(const generator::AgasLexResult &lexed,
                           std::uint64_t inputSize) const -> AstParseResult;

private:
  const generator::GeneratedParserTable &parser_;
};

} // namespace agas::runtime
