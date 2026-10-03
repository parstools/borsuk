#pragma once
#include "agsem/SemanticCompleteness.h"
namespace coge {
[[nodiscard]] auto
assessCompleteness(const agsem::ParsedDocument &,
                   const agsem::ContractEnvironment &,
                   std::vector<agsem::CompletenessTarget> requested = {})
    -> agsem::Outcome<agsem::CompletenessReport>;
}
