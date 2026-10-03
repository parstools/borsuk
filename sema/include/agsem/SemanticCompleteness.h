#pragma once
#include "agsem/Completeness.h"
#include "agsem/SemanticBinding.h"
namespace agsem {
[[nodiscard]] auto assessCompleteness(const ParsedDocument &,
                                      const ContractEnvironment &)
    -> Outcome<CompletenessReport>;
}
