#pragma once

#include "agas/model/GrammarDocument.h"
#include "agas/runtime/ArtifactLexerRuntime.h"

namespace agas::runtime {
[[nodiscard]] auto captureSourceText(std::string source,
                                     const ArtifactLexResult &lexed)
    -> model::SourceText;
} // namespace agas::runtime
