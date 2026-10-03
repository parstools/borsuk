#include "agas/runtime/AstParser.h"
#include "agas/bootstrap/AntlrFrontend.h"
#include "agas/generator/LexerGeneration.h"
#include "agas/generator/ParserGeneration.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string{message});
}

auto field(const agas::runtime::AstValue &value, std::string_view name)
    -> const agas::runtime::AstValue & {
  for (std::size_t index = 0; index < value.fieldNames.size(); ++index) {
    if (value.fieldNames[index] == name)
      return value.elements.at(index);
  }
  throw std::runtime_error("missing parsed AST field");
}

} // namespace

int main() {
  using agas::runtime::AstValueKind;
  using agas::runtime::InputSpan;

  try {
    const auto sourceGrammar = agas::bootstrap::parseAgas(
        "grammar Lr2Ast; "
        "options { parser = LALR; lookahead = 2; } "
        "channels { HIDDEN } "
        "node document : value=start EOF ; "
        "node start : first=A value=middle last=A #AForm "
        "| first=B value=middle separator=B last=A #BForm ; "
        "node middle : value=B #Present | empty #Absent ; "
        "A : 'a' ; B : 'b' ; HIDDEN_TOKEN : '!' -> channel(HIDDEN) ; "
        "WS : [ \\t\\r\\n]+ -> skip ;");
    require(sourceGrammar.accepted(), "LR(2) AST fixture must parse");

    const auto generated =
        agas::generator::generateParserTable(*sourceGrammar.document);
    require(generated.table().maxLength() == 2 &&
                !generated.table().hasConflicts(),
            "AST runtime fixture must exercise a conflict-free LALR(2) table");
    const auto lexer = agas::generator::compileLexerAutomaton(
        *sourceGrammar.document, generated.bnf());
    const agas::runtime::GeneratedAstParser parser{generated};

    const std::string presentSource = " a b a ";
    const auto present =
        parser.parse(lexer.tokenize(presentSource), presentSource.size());
    require(present.accepted() && present.root->kind == AstValueKind::Node &&
                present.root->typeName == "document" &&
                present.root->sourceSpan == InputSpan{1, 6},
            "the LR machine must build the root node and exclude skipped edge "
            "text from its source span");
    const auto &presentStart = field(*present.root, "value");
    require(presentStart.kind == AstValueKind::Node &&
                presentStart.variantName == "AForm" &&
                field(presentStart, "first").tokenText == "a" &&
                field(presentStart, "last").tokenText == "a",
            "shifted tokens and a named alternative must reach the AST");
    const auto &presentMiddle = field(presentStart, "value");
    require(presentMiddle.variantName == "Present" &&
                field(presentMiddle, "value").tokenText == "b" &&
                presentMiddle.sourceSpan == InputSpan{3, 4},
            "nested reductions must build their node and byte range");

    const std::string emptySource = "a a";
    const auto empty =
        parser.parse(lexer.tokenize(emptySource), emptySource.size());
    require(empty.accepted(), "the parser must execute an empty reduction");
    const auto &emptyMiddle = field(field(*empty.root, "value"), "value");
    require(emptyMiddle.variantName == "Absent" &&
                emptyMiddle.sourceSpan == InputSpan{2, 2},
            "an empty node must use the next token boundary as its point span");

    const std::string hiddenSource = "a ! a";
    const auto hiddenLexed = lexer.tokenize(hiddenSource);
    require(
        hiddenLexed.tokens.size() == 3 &&
            hiddenLexed.parserTerminalIds.size() == 2,
        "the lexer fixture must retain a hidden token outside parser input");
    const auto hidden = parser.parse(hiddenLexed, hiddenSource.size());
    require(hidden.accepted() &&
                field(field(*hidden.root, "value"), "value").sourceSpan ==
                    InputSpan{4, 4} &&
                hidden.root->sourceSpan == InputSpan{0, 5},
            "the parser must ignore hidden-channel tokens while preserving "
            "source byte boundaries");

    const std::string secondFormSource = "b b b a";
    const auto secondForm =
        parser.parse(lexer.tokenize(secondFormSource), secondFormSource.size());
    require(secondForm.accepted() &&
                field(*secondForm.root, "value").variantName == "BForm",
            "the LR(2) machine must select the second source alternative");

    const std::string invalidSource = "a b";
    const auto invalid =
        parser.parse(lexer.tokenize(invalidSource), invalidSource.size());
    require(!invalid.accepted() && invalid.error.has_value() &&
                invalid.error->byteOffset == 2 &&
                invalid.error->lookahead.size() == 2 &&
                invalid.error->lookahead.endsWithEndOfInput() &&
                !invalid.error->expected.empty(),
            "LR(2) syntax errors must retain their earliest byte position, "
            "complete lookahead and expected words");

    std::cout << "AST parser states=" << generated.table().stateCount()
              << " lookahead=" << generated.table().maxLength() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
