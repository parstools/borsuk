#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "agas/generator/ReductionProgram.h"

namespace agas::runtime {

struct InputSpan {
  std::uint64_t beginByte{};
  std::uint64_t endByte{};

  auto operator==(const InputSpan &) const -> bool = default;
};

enum class AstValueKind {
  Unit,
  Token,
  Node,
  Record,
  Optional,
  List,
};

// Node and record fields use parallel name/value arrays. Optional contains
// zero or one element; list contains any number of elements.
struct AstValue {
  AstValueKind kind{AstValueKind::Unit};
  InputSpan sourceSpan;
  InputSpan recognizedSpan;
  std::string typeName;
  std::string variantName;
  std::uint32_t tokenKind{};
  std::string tokenText;
  std::vector<std::string> fieldNames;
  std::vector<AstValue> elements;

  auto operator==(const AstValue &) const -> bool = default;
};

struct AstStatistics {
  std::size_t values{};
  std::size_t nodes{};
  // Count edges from the root, including list and optional wrappers.
  std::size_t maximumDepth{};
  // Number of named nodes on the longest path (root node counts as one).
  std::size_t maximumNodeDepth{};
};

[[nodiscard]] auto measureAst(const AstValue &root) -> AstStatistics;

// The payload may be forwarded unchanged by an inline rule, while the
// recognized span still covers the complete RHS, including punctuation.
struct ReductionStackValue {
  AstValue payload;
  InputSpan recognizedSpan;

  auto operator==(const ReductionStackValue &) const -> bool = default;
};

enum class ReductionRuntimeErrorCode {
  RhsSizeMismatch,
  InvalidSpan,
  ValueKindMismatch,
};

class ReductionRuntimeError final : public std::runtime_error {
public:
  ReductionRuntimeError(ReductionRuntimeErrorCode code, std::string message);

  [[nodiscard]] auto code() const noexcept -> ReductionRuntimeErrorCode;

private:
  ReductionRuntimeErrorCode code_;
};

[[nodiscard]] auto makeTokenValue(std::uint32_t tokenKind, std::string text,
                                  InputSpan span) -> ReductionStackValue;

[[nodiscard]] auto
executeReduction(const generator::ReductionInstruction &instruction,
                 std::vector<ReductionStackValue> rhs,
                 std::uint64_t lookaheadByte) -> ReductionStackValue;

} // namespace agas::runtime
