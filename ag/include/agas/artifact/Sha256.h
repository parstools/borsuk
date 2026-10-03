#pragma once

#include <string>
#include <string_view>

namespace agas::artifact {

[[nodiscard]] auto sha256Hex(std::string_view bytes) -> std::string;

} // namespace agas::artifact
