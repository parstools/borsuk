#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "agas/artifact/GrammarSections.h"
#include "agas/artifact/ParserTableSection.h"
#include "agas/artifact/ReductionSection.h"
#include "agas/runtime/ArtifactLexerRuntime.h"
#include "agas/runtime/ReductionRuntime.h"

namespace agas::runtime {

struct ArtifactAstParseError {
  std::uint32_t state{};
  std::size_t tokenIndex{};
  std::uint64_t byteOffset{};
  InputSpan span;
  artifact::ArtifactLookaheadWord lookahead;
  std::vector<artifact::ArtifactLookaheadWord> expected;
  std::string message;

  auto operator==(const ArtifactAstParseError &) const -> bool = default;
};

struct ArtifactAstParseResult {
  std::optional<AstValue> root;
  std::optional<ArtifactAstParseError> error;

  [[nodiscard]] auto accepted() const noexcept -> bool;
};

struct ArtifactParserRuntimeLimits {
  std::uint64_t maximumSteps{100'000'000};
  std::uint64_t maximumStackDepth{10'000'000};
};

enum class ArtifactParserRuntimeErrorCode {
  InvalidInput,
  InvalidTableExecution,
  ResourceLimit,
};

class ArtifactParserRuntimeError final : public std::runtime_error {
public:
  ArtifactParserRuntimeError(ArtifactParserRuntimeErrorCode code,
                             std::string message);

  [[nodiscard]] auto code() const noexcept -> ArtifactParserRuntimeErrorCode;

private:
  ArtifactParserRuntimeErrorCode code_;
};

class ArtifactAstParser {
public:
  ArtifactAstParser(
      artifact::ArtifactParserTable table, artifact::ArtifactSymbols symbols,
      artifact::ArtifactProductions productions,
      artifact::ArtifactReductions reductions,
      ArtifactParserRuntimeLimits limits = ArtifactParserRuntimeLimits{});

  [[nodiscard]] auto parse(const ArtifactLexResult &lexed,
                           std::uint64_t inputSize) const
      -> ArtifactAstParseResult;
  [[nodiscard]] auto parse(std::string_view input, const ArtifactLexerRuntime &lexer) const
      -> ArtifactAstParseResult;

private:
  artifact::ArtifactParserTable table_;
  artifact::ArtifactSymbols symbols_;
  artifact::ArtifactProductions productions_;
  artifact::ArtifactReductions reductions_;
  ArtifactParserRuntimeLimits limits_;
};

} // namespace agas::runtime
