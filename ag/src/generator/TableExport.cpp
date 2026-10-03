#include "agas/generator/TableExport.h"

#include <string>

namespace agas::generator {

TableExportError::TableExportError(std::size_t conflictCount)
    : std::runtime_error("cannot export a table with " +
                         std::to_string(conflictCount) + " conflict(s)"),
      conflictCount_(conflictCount) {}

auto TableExportError::conflictCount() const noexcept -> std::size_t {
  return conflictCount_;
}

auto exportCompressedTableDsl(const GeneratedParserTable &generated)
    -> CompressedTableDsl {
  if (generated.table().hasConflicts()) {
    throw TableExportError(generated.table().conflicts().size());
  }
  const zbik::CompressedParseTable compressed{generated.table()};
  return {compressed.dumpDsl(), compressed.statistics()};
}

} // namespace agas::generator
