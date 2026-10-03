#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "agas/artifact/LexerSection.h"
#include "agas/artifact/ParserTableSection.h"

namespace agas::runtime {

enum class ArtifactLexerErrorKind { InvalidEncoding, NoMatchingRule };

class ArtifactLexerError final : public std::runtime_error {
public:
  ArtifactLexerError(ArtifactLexerErrorKind kind, std::size_t tokenStart,
                     std::size_t errorOffset);
  [[nodiscard]] auto kind() const noexcept -> ArtifactLexerErrorKind;
  [[nodiscard]] auto tokenStart() const noexcept -> std::size_t;
  [[nodiscard]] auto errorOffset() const noexcept -> std::size_t;

private:
  ArtifactLexerErrorKind kind_;
  std::size_t tokenStart_{};
  std::size_t errorOffset_{};
};

struct ArtifactLexedToken {
  std::uint32_t terminal{};
  std::optional<std::uint32_t> channel;
  std::size_t offset{};
  std::string text;
  auto operator==(const ArtifactLexedToken &) const -> bool = default;
};

struct ArtifactLexResult {
  std::vector<ArtifactLexedToken> tokens;
  std::vector<std::uint32_t> parserTerminalIds;
  auto operator==(const ArtifactLexResult &) const -> bool = default;
};

class ArtifactLexerRuntime {
public:
  ArtifactLexerRuntime(artifact::ArtifactLexer lexer, std::size_t terminalCount,
                       std::size_t channelCount);

  [[nodiscard]] auto tokenize(std::string_view input) const
      -> ArtifactLexResult;
  [[nodiscard]] auto tokenize(std::string_view input,
      const artifact::ArtifactParserTable &table,
      const artifact::ArtifactProductions &productions,
      std::uint64_t maximumSteps = 100'000'000,
      std::uint64_t maximumStackDepth = 10'000'000) const -> ArtifactLexResult;
  [[nodiscard]] auto artifact() const noexcept
      -> const artifact::ArtifactLexer &;

private:
  artifact::ArtifactLexer lexer_;
};

} // namespace agas::runtime
