#include "agas/artifact/ArtifactPackage.h"
#include "agas/generator/ArtifactGeneration.h"
#include "agas/generator/LexerGeneration.h"
#include "agas/generator/ParserGeneration.h"
#include "agas/model/Validation.h"
#include "agas/runtime/ArtifactAstParser.h"
#include "agas/runtime/ArtifactLexerRuntime.h"
#include "agas/runtime/AstParser.h"
#include "agas/runtime/PackagedAgFrontend.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

auto readFile(const char *path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read conflict grammar");
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

auto field(const agas::runtime::AstValue &value, std::string_view name)
    -> const agas::runtime::AstValue & {
  for (std::size_t index = 0; index < value.fieldNames.size(); ++index) {
    if (value.fieldNames[index] == name)
      return value.elements.at(index);
  }
  throw std::runtime_error("expected AST field is absent");
}

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

} // namespace

int main() {
  try {
    const agas::runtime::PackagedAgFrontend frontend{AGAS_PINNED_ARTIFACT_DIR};
    const auto baseline = frontend.parse(readFile(AGAS_IF_ELSE_GRAMMAR));
    require(baseline.accepted(), "baseline grammar must parse");
    const auto baselineTable =
        agas::generator::generateParserTable(*baseline.document);
    require(baselineTable.table().conflicts().size() == 1,
            "baseline dangling-else grammar must have one conflict");

    const std::string grammarSource =
        readFile(AGAS_RESOLVED_IF_ELSE_GRAMMAR);
    const auto parsed = frontend.parse(grammarSource);
    require(parsed.accepted(), "conflict policy grammar must parse");
    require(agas::model::validateSyntaxModel(*parsed.document).valid(),
            "conflict policy grammar must validate");
    const auto generated =
        agas::generator::generateParserTable(*parsed.document);
    require(!generated.table().hasConflicts(),
            "exact policy must remove the dangling-else conflict");
    require(generated.resolvedConflicts().size() == 1,
            "exact policy must resolve only one ACTION cell");

    const auto lexer = agas::generator::compileLexerAutomaton(
        *parsed.document, generated.bnf());
    const agas::runtime::GeneratedAstParser parser{generated};
    constexpr std::string_view input = "if b if b if b {} else {}";
    const auto result = parser.parse(lexer.tokenize(input), input.size());
    require(result.accepted(), "nested dangling-else input must be accepted");
    const auto &outer = field(*result.root, "value");
    require(outer.variantName == "IfStatement",
            "outer if must have no else branch");
    const auto &middle = field(outer, "thenBranch");
    require(middle.variantName == "IfStatement",
            "middle if must have no else branch");
    const auto &inner = field(middle, "thenBranch");
    require(inner.variantName == "IfElseStatement",
            "else must bind to the innermost if");
    require(field(inner, "elseBranch").typeName == "compoundStatement",
            "innermost else branch must contain the final block");

    const auto package = agas::generator::buildParserArtifactPackage(
        *parsed.document, generated, lexer, grammarSource, grammarSource,
        {"test-generator", "test-zbik", "test-unicode"});
    require(package.sections.diagnostics.has_value() &&
                package.sections.diagnostics->find("\"selected\": \"shift\"") !=
                    std::string::npos,
            "package must retain the chosen conflict and its source policy");
    const auto loaded = agas::artifact::loadArtifactPackage(
        package.manifestJson, package.sections);
    const agas::runtime::ArtifactLexerRuntime artifactLexer{
        loaded.lexer, loaded.symbols.terminals.size(),
        loaded.symbols.channels.size()};
    const agas::runtime::ArtifactAstParser artifactParser{
        loaded.parserTable, loaded.symbols, loaded.productions,
        loaded.reductions};
    const auto artifactResult =
        artifactParser.parse(artifactLexer.tokenize(input), input.size());
    require(artifactResult.accepted() && artifactResult.root == result.root,
            "exported and reloaded policy table must build the same AST");

    std::string reduceSource = grammarSource;
    constexpr std::string_view shiftPolicy =
        "prefer shift ELSE over reduce ifStatement#IfStatement;";
    const auto policyPosition = reduceSource.find(shiftPolicy);
    require(policyPosition != std::string::npos, "shift policy must be present");
    reduceSource.replace(policyPosition, shiftPolicy.size(),
                         "prefer reduce ifStatement#IfStatement over shift ELSE;");
    const auto reduceParsed = frontend.parse(reduceSource);
    require(reduceParsed.accepted(), "reverse policy syntax must parse");
    require(agas::model::validateSyntaxModel(*reduceParsed.document).valid(),
            "reverse policy must validate");
    const auto reduceTable =
        agas::generator::generateParserTable(*reduceParsed.document);
    require(!reduceTable.table().hasConflicts() &&
                reduceTable.resolvedConflicts().size() == 1 &&
                std::holds_alternative<zbik::Reduce>(
                    reduceTable.resolvedConflicts().front().selected),
            "reverse policy must deterministically select reduce");
    const auto reduceLexer = agas::generator::compileLexerAutomaton(
        *reduceParsed.document, reduceTable.bnf());
    const agas::runtime::GeneratedAstParser reduceParser{reduceTable};
    constexpr std::string_view shortIf = "if b {}";
    require(reduceParser.parse(reduceLexer.tokenize(shortIf), shortIf.size())
                .accepted(),
            "reduce policy must still accept a short if");
    constexpr std::string_view elseIf = "if b {} else {}";
    require(reduceParser.parse(reduceLexer.tokenize(elseIf), elseIf.size())
                .accepted(),
            "reduce policy must accept an unambiguous if-else sentence");
    const auto reduceNested =
        reduceParser.parse(reduceLexer.tokenize(input), input.size());
    require(reduceNested.accepted(), "reverse policy must accept nested input");
    const auto &reduceOuter = field(*reduceNested.root, "value");
    require(reduceOuter.variantName == "IfElseStatement",
            "reduce policy must bind else to the outermost if");
    const auto &reduceMiddle = field(reduceOuter, "thenBranch");
    require(reduceMiddle.variantName == "IfStatement" &&
                field(reduceMiddle, "thenBranch").variantName == "IfStatement",
            "inner if statements must have no else branch under reduce policy");
    const auto reducePackage = agas::generator::buildParserArtifactPackage(
        *reduceParsed.document, reduceTable, reduceLexer, reduceSource,
        reduceSource, {"test-generator", "test-zbik", "test-unicode"});
    require(reducePackage.sections.diagnostics.has_value() &&
                reducePackage.sections.diagnostics->find("\"selected\": \"reduce\"") !=
                    std::string::npos,
            "reverse policy must be recorded in package diagnostics");

    auto noMatch = *parsed.document;
    noMatch.conflictPreferences.front().shiftTerminal = "B";
    bool rejectedNoMatch = false;
    try {
      static_cast<void>(agas::generator::generateParserTable(noMatch));
    } catch (const agas::generator::ParserConfigurationError &) {
      rejectedNoMatch = true;
    }
    require(rejectedNoMatch, "unused conflict policy must be rejected");

    auto lr2 = *parsed.document;
    const auto lookahead = std::find_if(
        lr2.options.begin(), lr2.options.end(),
        [](const agas::model::Option &option) {
          return option.name == "lookahead";
        });
    lookahead->value.spelling = "2";
    const auto lr2Table = agas::generator::generateParserTable(lr2);
    require(!lr2Table.table().hasConflicts() &&
                !lr2Table.resolvedConflicts().empty(),
            "ELSE policy must match full LR(2) words by their first symbol");

    auto overlap = *parsed.document;
    overlap.conflictPreferences.push_back(overlap.conflictPreferences.front());
    require(!agas::model::validateSyntaxModel(overlap).valid(),
            "duplicate conflict policy must fail validation");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
