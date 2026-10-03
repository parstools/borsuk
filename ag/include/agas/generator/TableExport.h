#pragma once

#include <stdexcept>
#include <string>

#include "agas/generator/ParserGeneration.h"
#include "lr/CompressedParseTable.h"

namespace agas::generator {

struct CompressedTableDsl {
  std::string text;
  zbik::TableStorageStats storage;
};

class TableExportError final : public std::runtime_error {
public:
  explicit TableExportError(std::size_t conflictCount);

  [[nodiscard]] auto conflictCount() const noexcept -> std::size_t;

private:
  std::size_t conflictCount_{};
};

// Returns deterministic text; the caller decides whether to display or store
// it.
[[nodiscard]] auto
exportCompressedTableDsl(const GeneratedParserTable &generated)
    -> CompressedTableDsl;

} // namespace agas::generator
