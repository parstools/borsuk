#pragma once

#include "agas/model/SyntaxModel.h"

namespace agsem {

// Compose version 1 from canonical Ag productions and the action/model DSL.
// The legacy envelope and its copies of Ag productions are not imported.
[[nodiscard]] auto
composeDocumentGrammar(const agas::model::SyntaxDocument &ag,
                       const agas::model::SyntaxDocument &actions,
                       const agas::model::SyntaxDocument &envelope)
    -> agas::model::SyntaxDocument;

} // namespace agsem
