#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace agsem {

using SourceId = std::uint32_t;

enum class Severity { Error, Warning };

struct SourceLocation {
  SourceId source{};
  std::uint64_t beginByte{};
  std::uint64_t endByte{};

  auto operator==(const SourceLocation &) const -> bool = default;
};

struct Diagnostic {
  Severity severity{Severity::Error};
  std::string code;
  std::string message;
  std::optional<SourceLocation> location;
  std::string subject;
  std::vector<SourceLocation> related;
};

template <class T> struct Outcome {
  std::optional<T> value;
  std::vector<Diagnostic> diagnostics;
};

} // namespace agsem
