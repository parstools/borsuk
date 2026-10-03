#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agas/generator/LexerGeneration.h"
#include "agas/generator/ParserGeneration.h"
#include "agas/model/SyntaxModel.h"
#include "agas/runtime/AstParser.h"
#include "first/LookaheadWord.h"
#include "lr/Identifiers.h"

namespace agas::runtime {

enum class AgFrontendIssueKind { Lexical, Syntax, Adapter };

struct AgFrontendIssue {
  AgFrontendIssueKind kind{};
  InputSpan span;
  std::optional<zbik::StateId> state;
  std::optional<zbik::LookaheadWord> lookahead;
  std::vector<zbik::LookaheadWord> expected;
  std::string message;

  auto operator==(const AgFrontendIssue &) const -> bool = default;
};

struct AgFrontendResult {
  std::optional<model::SyntaxDocument> document;
  std::vector<AgFrontendIssue> issues;

  [[nodiscard]] auto accepted() const noexcept -> bool;
};

// Own lexer, LR(k) machine and Ag-specific AST adapter. The bootstrap model is
// needed only while constructing the generated components; parse() does not
// call ANTLR or retain the source SyntaxDocument.
class SelfHostedAgFrontend {
public:
  explicit SelfHostedAgFrontend(const model::SyntaxDocument &agasDefinition);
  SelfHostedAgFrontend(const SelfHostedAgFrontend &) = delete;
  auto operator=(const SelfHostedAgFrontend &)
      -> SelfHostedAgFrontend & = delete;
  SelfHostedAgFrontend(SelfHostedAgFrontend &&) = delete;
  auto operator=(SelfHostedAgFrontend &&) -> SelfHostedAgFrontend & = delete;

  [[nodiscard]] auto parse(std::string_view source) const -> AgFrontendResult;
  [[nodiscard]] auto generatedParser() const noexcept
      -> const generator::GeneratedParserTable &;

private:
  generator::GeneratedParserTable generated_;
  generator::GeneratedLexerAutomaton lexer_;
  GeneratedAstParser parser_;
};

} // namespace agas::runtime
