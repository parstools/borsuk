#pragma once
#include "agsem/Projection.h"
namespace agsem {
struct ProjectedText {
  std::string text;
  std::vector<OriginFragment> origins;
};
[[nodiscard]] auto applySourceEdits(std::string_view source,
                                    std::vector<SourceEdit> edits)
    -> ProjectedText;
} // namespace agsem
