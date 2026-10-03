#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

#include "agas/runtime/ReductionRuntime.h"

namespace agas::runtime {

struct AstWireContext {
  std::uint32_t astSchemaVersion{};
  std::string_view symbolsSha256;
  std::string_view astSchemaSha256;
  std::size_t terminalCount{};
  std::string_view sourceName;
  std::string_view source;
};

// Version-1 file/process boundary. The caller supplies the exact source bytes;
// they are never taken from the untrusted wire document.
[[nodiscard]] auto dumpAstWireJson(const AstValue &root,
                                   AstWireContext context) -> std::string;

[[nodiscard]] auto parseAstWireJson(std::string_view wire,
                                    AstWireContext context) -> AstValue;

} // namespace agas::runtime
