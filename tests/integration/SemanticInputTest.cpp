#include "SemanticInputStorage.h"

#include <string>

namespace {
auto makeInput() -> agsem::SemanticInput {
  agas::runtime::AstValue root;
  root.kind = agas::runtime::AstValueKind::Token;
  root.tokenText = "original";
  agas::model::SyntaxDocument grammar;
  grammar.grammarName = "OriginalGrammar";
  return agsem::SemanticInputAccess::fromAst(root, grammar);
}
} // namespace

int main() {
  const auto input = makeInput();
  const auto retained = input;
  if (agsem::SemanticInputAccess::root(retained).tokenText != "original" ||
      agsem::SemanticInputAccess::grammar(retained).grammarName !=
          "OriginalGrammar")
    return 1;
  return 0;
}
