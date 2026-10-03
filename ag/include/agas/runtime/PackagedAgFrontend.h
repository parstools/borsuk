#pragma once

#include <filesystem>
#include <string_view>

#include "agas/artifact/ArtifactPackage.h"
#include "agas/runtime/ArtifactAstParser.h"
#include "agas/runtime/ArtifactLexerRuntime.h"
#include "agas/runtime/SelfHostedAgFrontend.h"
#include "agas/model/GrammarDocument.h"

namespace agas::runtime {

struct GrammarDocumentResult {
  std::optional<model::GrammarDocument> document;
  std::vector<AgFrontendIssue> issues;
};

// Normal Agas frontend. It loads the pinned parser package and never invokes
// ANTLR or regenerates lexer/parser tables while parsing an .ag source.
class PackagedAgFrontend {
public:
  explicit PackagedAgFrontend(const std::filesystem::path &artifactDirectory);

  [[nodiscard]] auto parse(std::string_view source) const -> AgFrontendResult;
  [[nodiscard]] auto parseDocument(std::string_view source) const
      -> GrammarDocumentResult;
  [[nodiscard]] auto package() const noexcept
      -> const artifact::LoadedArtifactPackage &;

private:
  artifact::LoadedArtifactPackage package_;
  ArtifactLexerRuntime lexer_;
  ArtifactAstParser parser_;
};

} // namespace agas::runtime
