#pragma once

#include "agas/runtime/PackagedAgFrontend.h"
#include "agsem/Diagnostics.h"
#include "agsem/DocumentModel.h"

namespace agsem {

// Version 1 never resolves a neighboring grammar file. The two directories
// are installed parser resources, not dependencies of the input document.
class DocumentFrontend {
public:
  DocumentFrontend(const std::filesystem::path &documentPackage,
                   const std::filesystem::path &agPackage);
  [[nodiscard]] auto parse(std::string_view source) const
      -> Outcome<ParsedDocument>;
  [[nodiscard]] auto parseAg(std::string_view source) const
      -> Outcome<agas::model::GrammarDocument>;
  [[nodiscard]] auto package() const
      -> const agas::artifact::LoadedArtifactPackage & {
    return package_;
  }

private:
  agas::artifact::LoadedArtifactPackage package_;
  agas::runtime::ArtifactLexerRuntime lexer_;
  agas::runtime::ArtifactAstParser parser_;
  agas::runtime::PackagedAgFrontend ag_;
};

} // namespace agsem
