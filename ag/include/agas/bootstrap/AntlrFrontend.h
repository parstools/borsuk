#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agas/model/SyntaxModel.h"

namespace agas::bootstrap {

struct SyntaxIssue {
  std::size_t line{};
  std::size_t column{};
  std::string message;

  auto operator==(const SyntaxIssue &) const -> bool = default;
};

struct ParseResult {
  std::optional<model::SyntaxDocument> document;
  std::vector<SyntaxIssue> issues;

  [[nodiscard]] auto accepted() const noexcept -> bool {
    return issues.empty() && document.has_value();
  }
};

struct BootstrapLexToken {
  std::string name;
  std::optional<std::string> channel;
  std::size_t byteOffset{};
  std::string text;

  auto operator==(const BootstrapLexToken &) const -> bool = default;
};

struct BootstrapLexResult {
  std::vector<BootstrapLexToken> tokens;
  std::vector<SyntaxIssue> issues;

  [[nodiscard]] auto accepted() const noexcept -> bool {
    return issues.empty();
  }
};

[[nodiscard]] auto parseAgas(std::string_view source) -> ParseResult;
[[nodiscard]] auto parseAgasFile(const std::filesystem::path &path)
    -> ParseResult;
// Test oracle backed by the pinned ANTLR bootstrap lexer. Production Agas
// code must use the generated Zbik lexer instead.
[[nodiscard]] auto lexAgasWithBootstrapAntlr(std::string_view source)
    -> BootstrapLexResult;

} // namespace agas::bootstrap
