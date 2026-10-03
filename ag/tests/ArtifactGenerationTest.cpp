#include "agas/generator/ArtifactGeneration.h"
#include "agas/artifact/ArtifactPackage.h"
#include "agas/artifact/AstSchemaSection.h"
#include "agas/artifact/ParserTableSection.h"
#include "agas/artifact/ReductionSection.h"
#include "agas/artifact/Sha256.h"
#include "agas/bootstrap/AntlrFrontend.h"
#include "agas/generator/TableExport.h"
#include "agas/runtime/ArtifactAstParser.h"
#include "agas/runtime/ArtifactLexerRuntime.h"
#include "agas/runtime/AstParser.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

namespace {

auto readFile(const std::string &path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read " + path);
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

} // namespace

int main() {
  try {
    const auto parsed =
        agas::bootstrap::parseAgas(readFile(AGAS_TEST_GRAMMAR_FILE));
    if (!parsed.accepted())
      throw std::runtime_error("bootstrap must parse Ag.ag");
    const auto generated =
        agas::generator::generateParserTable(*parsed.document);
    const auto [symbols, productions] =
        agas::generator::buildGrammarArtifactSections(*parsed.document,
                                                      generated);
    const auto generatedLexer = agas::generator::compileLexerAutomaton(
        *parsed.document, generated.bnf());
    const auto lexer =
        agas::generator::buildLexerArtifactSection(generatedLexer, symbols);
    if (lexer.dfaStates.size() != generatedLexer.dfaStateCount() ||
        lexer.orderedNfas.size() != lexer.rules.size() ||
        lexer.orderedNfas.empty())
      throw std::runtime_error(
          "Ag.ag lexer artifact omitted DFA or ordered NFA data");
    auto invalidLexer = lexer;
    invalidLexer.dfaStates.front().transitions.front().target =
        static_cast<std::uint32_t>(invalidLexer.dfaStates.size());
    bool rejectedLexerTarget = false;
    try {
      agas::artifact::validateLexerSection(
          invalidLexer, symbols.terminals.size(), symbols.channels.size());
    } catch (const std::invalid_argument &) {
      rejectedLexerTarget = true;
    }
    if (!rejectedLexerTarget)
      throw std::runtime_error(
          "lexer artifact must reject invalid DFA targets");
    const std::string lexerText = agas::artifact::dumpLexerJson(
        lexer, symbols.terminals.size(), symbols.channels.size());
    const auto loadedLexer = agas::artifact::parseLexerJson(
        lexerText, symbols.terminals.size(), symbols.channels.size());
    if (loadedLexer != lexer ||
        agas::artifact::dumpLexerJson(loadedLexer, symbols.terminals.size(),
                                      symbols.channels.size()) != lexerText)
      throw std::runtime_error(
          "Ag.ag lexer differs after canonical round-trip");

    const agas::runtime::ArtifactLexerRuntime artifactLexer{
        loadedLexer, symbols.terminals.size(), symbols.channels.size()};
    const std::string agSource = readFile(AGAS_TEST_GRAMMAR_FILE);
    const auto expectedTokens = generatedLexer.tokenize(agSource);
    const auto actualTokens = artifactLexer.tokenize(agSource);
    if (actualTokens.tokens.size() != expectedTokens.tokens.size())
      throw std::runtime_error(
          "artifact lexer produces a different token count");
    for (std::size_t index = 0; index < actualTokens.tokens.size(); ++index) {
      const auto &actual = actualTokens.tokens[index];
      const auto &expected = expectedTokens.tokens[index];
      std::optional<std::string> actualChannel;
      if (actual.channel)
        actualChannel = symbols.channels.at(*actual.channel).name;
      if (actual.terminal != expected.terminal.value ||
          actualChannel != expected.channel ||
          actual.offset != expected.offset || actual.text != expected.text)
        throw std::runtime_error(
            "artifact lexer token differs from generated lexer token");
    }
    if (actualTokens.parserTerminalIds.size() !=
        expectedTokens.parserTerminalIds.size())
      throw std::runtime_error(
          "artifact lexer produces a different parser token count");
    for (std::size_t index = 0; index < actualTokens.parserTerminalIds.size();
         ++index)
      if (actualTokens.parserTerminalIds[index] !=
          expectedTokens.parserTerminalIds[index].value)
        throw std::runtime_error(
            "artifact lexer parser input differs from generated lexer input");

    const std::string symbolText = agas::artifact::dumpSymbolsJson(symbols);
    const auto loadedSymbols = agas::artifact::parseSymbolsJson(symbolText);
    const std::string productionText =
        agas::artifact::dumpProductionsJson(symbols, productions);
    const auto loadedProductions =
        agas::artifact::parseProductionsJson(productionText, loadedSymbols);
    if (loadedSymbols != symbols || loadedProductions != productions)
      throw std::runtime_error(
          "Ag.ag grammar sections differ after round-trip");
    if (productions.productions.size() != generated.bnf().grammar().ruleCount())
      throw std::runtime_error("artifact omitted generated BNF productions");
    for (const auto &production : productions.productions) {
      const auto &source =
          generated.bnf().grammar().rule(zbik::RuleId{production.id});
      for (std::size_t index = 0; index < production.rhs.size(); ++index) {
        const auto expected = std::visit(
            [](const auto id) {
              using Id = std::remove_cv_t<decltype(id)>;
              if constexpr (std::is_same_v<Id, zbik::TerminalId>)
                return agas::artifact::ArtifactSymbolKind::Terminal;
              else
                return agas::artifact::ArtifactSymbolKind::Nonterminal;
            },
            source.rhs()[index]);
        if (production.rhs[index].kind != expected)
          throw std::runtime_error(
              "artifact production changed a grammar symbol kind");
      }
    }

    const std::string tableText =
        agas::generator::exportCompressedTableDsl(generated).text;
    const auto loadedTable = agas::artifact::parseParserTableDsl(
        tableText, loadedSymbols, loadedProductions);
    const std::string roundTrippedTable = agas::artifact::dumpParserTableDsl(
        loadedTable, loadedSymbols, loadedProductions);
    if (roundTrippedTable != tableText) {
      const auto mismatch =
          std::mismatch(tableText.begin(), tableText.end(),
                        roundTrippedTable.begin(), roundTrippedTable.end());
      throw std::runtime_error(
          "Ag.ag parser table differs after canonical round-trip at byte " +
          std::to_string(
              static_cast<std::size_t>(mismatch.first - tableText.begin())));
    }
    auto invalidTable = loadedTable;
    invalidTable.actionStateRows.front() =
        static_cast<std::uint32_t>(invalidTable.actionRows.size());
    bool rejectedTableReference = false;
    try {
      agas::artifact::validateParserTableSection(invalidTable, loadedSymbols,
                                                 loadedProductions);
    } catch (const agas::artifact::ParserTableSectionError &error) {
      rejectedTableReference =
          error.code() ==
          agas::artifact::ParserTableSectionErrorCode::InvalidReference;
    }
    if (!rejectedTableReference)
      throw std::runtime_error(
          "parser-table artifact must reject invalid row references");

    const std::string reductionText =
        agas::artifact::dumpReductionsJson(productions, generated.reductions());
    const auto loadedReductions =
        agas::artifact::parseReductionsJson(reductionText, loadedProductions);
    if (loadedReductions.program.instructions() !=
        generated.reductions().instructions())
      throw std::runtime_error("Ag.ag reductions differ after round-trip");

    auto mismatchedProductions = loadedProductions;
    mismatchedProductions.productions.front().rhs.push_back(
        {agas::artifact::ArtifactSymbolKind::Terminal, 0});
    bool rejectedMismatch = false;
    try {
      static_cast<void>(agas::artifact::parseReductionsJson(
          reductionText, mismatchedProductions));
    } catch (const agas::artifact::GrammarSectionError &error) {
      rejectedMismatch =
          error.code() ==
          agas::artifact::GrammarSectionErrorCode::InvalidReference;
    }
    if (!rejectedMismatch)
      throw std::runtime_error(
          "reduction loader must reject mismatched productions");

    const std::string schemaText =
        agas::artifact::dumpAstSchemaJson(generated.astSchema());
    const auto loadedSchema = agas::artifact::parseAstSchemaJson(schemaText);
    if (loadedSchema.schema.rules() != generated.astSchema().rules())
      throw std::runtime_error("Ag.ag AST schema differs after round-trip");
    std::string invalidSchema = schemaText;
    const std::size_t ruleType = invalidSchema.find("\"kind\": \"rule\"");
    if (ruleType == std::string::npos)
      throw std::runtime_error("Ag.ag schema lacks a rule reference fixture");
    const std::size_t nameStart =
        invalidSchema.find("\"name\": \"", ruleType) + 9;
    const std::size_t nameEnd = invalidSchema.find('"', nameStart);
    if (nameEnd == std::string::npos)
      throw std::runtime_error("invalid AST schema test fixture");
    invalidSchema.replace(nameStart, nameEnd - nameStart, "missingRule");
    bool rejectedUnknownRule = false;
    try {
      static_cast<void>(agas::artifact::parseAstSchemaJson(invalidSchema));
    } catch (const agas::artifact::GrammarSectionError &error) {
      rejectedUnknownRule =
          error.code() ==
          agas::artifact::GrammarSectionErrorCode::InvalidReference;
    }
    if (!rejectedUnknownRule)
      throw std::runtime_error(
          "AST schema loader must reject unknown rule references");

    const std::string sourceHash = agas::artifact::sha256Hex(agSource);
    const std::string settings =
        "lalr:" + std::to_string(loadedTable.lookahead);
    const agas::artifact::ArtifactManifest packageIdentity{
        1,
        "lalr",
        loadedTable.lookahead,
        parsed.document->parserRules.front().name,
        parsed.document->parserRules.front().name,
        "agas-cpp-test",
        "zbik-workspace",
        "15.1",
        sourceHash,
        sourceHash,
        agas::artifact::sha256Hex(settings),
        {}};
    const auto package = agas::artifact::makeArtifactPackage(
        packageIdentity, {symbolText, lexerText, tableText, productionText,
                          reductionText, schemaText, std::nullopt});
    const auto loadedPackage = agas::artifact::loadArtifactPackage(
        package.manifestJson, package.sections);
    const agas::runtime::ArtifactLexerRuntime packageLexer{
        loadedPackage.lexer, loadedPackage.symbols.terminals.size(),
        loadedPackage.symbols.channels.size()};
    const agas::runtime::ArtifactAstParser packageParser{
        loadedPackage.parserTable, loadedPackage.symbols,
        loadedPackage.productions, loadedPackage.reductions};
    const agas::runtime::GeneratedAstParser generatedParser{generated};
    const auto packageTokens = packageLexer.tokenize(agSource);
    const auto expectedAst =
        generatedParser.parse(expectedTokens, agSource.size());
    const auto packageAst = packageParser.parse(packageTokens, agSource.size());
    if (packageTokens != actualTokens || !expectedAst.accepted() ||
        !packageAst.accepted() || expectedAst.root != packageAst.root)
      throw std::runtime_error(
          "Ag.ag package differs from generated lexer or parser runtime");

    const std::string invalidSource = "grammar ;";
    const auto expectedInvalidTokens = generatedLexer.tokenize(invalidSource);
    const auto packageInvalidTokens = packageLexer.tokenize(invalidSource);
    if (generatedParser.parse(expectedInvalidTokens, invalidSource.size())
            .accepted() ||
        packageParser.parse(packageInvalidTokens, invalidSource.size())
            .accepted())
      throw std::runtime_error(
          "generated or packaged parser accepted invalid Ag syntax");

    std::cout << "Ag.ag artifact terminals=" << symbols.terminals.size()
              << " nonterminals=" << symbols.nonterminals.size()
              << " productions=" << productions.productions.size()
              << " reductions="
              << loadedReductions.program.instructions().size()
              << " AST-rules=" << loadedSchema.schema.rules().size()
              << " lexer-rules=" << loadedLexer.rules.size()
              << " DFA-states=" << loadedLexer.dfaStates.size()
              << " parser-states=" << loadedTable.actionStateRows.size()
              << " tokens=" << actualTokens.tokens.size() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
