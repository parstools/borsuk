#include "agas/bootstrap/AntlrFrontend.h"
#include "agas/generator/LexerGeneration.h"
#include "agas/generator/ParserGeneration.h"
#include "agas/generator/TableExport.h"
#include "agas/model/BnfLowering.h"
#include "agas/model/Validation.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

auto hasIssue(const agas::model::ValidationResult &validation,
              agas::model::DiagnosticSeverity severity, std::string_view text)
    -> bool {
  for (const agas::model::ValidationIssue &issue : validation.issues) {
    if (issue.severity == severity &&
        issue.message.find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
}

} // namespace

int main() {
  const agas::bootstrap::ParseResult bootstrap =
      agas::bootstrap::parseAgasFile(AGAS_TEST_GRAMMAR_FILE);
  require(bootstrap.accepted(),
          "Ag.ag must be accepted by the bootstrap grammar");
  require(bootstrap.document->grammarName == "Ag",
          "the bootstrap grammar name must be preserved");
  require(bootstrap.document->options.size() == 3,
          "all bootstrap options must be modeled");
  require(bootstrap.document->channels.empty(),
          "the absence of a channels block must be preserved");
  require(bootstrap.document->parserRules.size() == 39,
          "all parser rules must be modeled");
  require(bootstrap.document->lexerRules.size() == 46,
          "all lexer rules must be preserved");
  require(agas::model::validateSyntaxModel(*bootstrap.document).valid(),
          "Ag.ag must produce a semantically valid syntax model");
  const agas::model::BnfModel bootstrapBnf =
      agas::model::lowerToBnf(*bootstrap.document);
  require(bootstrapBnf.grammar().ruleCount() >
                  bootstrap.document->parserRules.size() &&
              bootstrapBnf.origins().size() ==
                  bootstrapBnf.grammar().ruleCount(),
          "Ag.ag must lower through Zbik with complete production origins");
  const agas::generator::GeneratedLexerAutomaton bootstrapLexer =
      agas::generator::compileLexerAutomaton(*bootstrap.document, bootstrapBnf);
  std::ifstream bootstrapInput(AGAS_TEST_GRAMMAR_FILE, std::ios::binary);
  require(static_cast<bool>(bootstrapInput), "Ag.ag must be readable");
  const std::string bootstrapSource{
      std::istreambuf_iterator<char>{bootstrapInput},
      std::istreambuf_iterator<char>{}};
  const agas::generator::AgasLexResult bootstrapTokens =
      bootstrapLexer.tokenize(bootstrapSource);
  const auto grammarTerminal = bootstrapBnf.grammar().findTerminal("GRAMMAR");
  require(grammarTerminal && bootstrapLexer.rules().size() == 40 &&
              bootstrapLexer.dfaStateCount() == 139 &&
              bootstrapLexer.transitionRangeCount() == 693 &&
              bootstrapTokens.tokens.size() == 782 &&
              bootstrapTokens.tokens.front().terminal == *grammarTerminal &&
              bootstrapTokens.tokens.front().text == "grammar" &&
              bootstrapTokens.tokens.size() ==
                  bootstrapTokens.parserTerminalIds.size(),
          "the generated Ag.ag lexer must retain its pinned structure and "
          "tokenize its own source while dropping comments and whitespace");
  const agas::generator::GeneratedLexerAutomaton repeatedBootstrapLexer =
      agas::generator::compileLexerAutomaton(*bootstrap.document, bootstrapBnf);
  require(bootstrapLexer.dfaStateCount() ==
                  repeatedBootstrapLexer.dfaStateCount() &&
              bootstrapLexer.transitionRangeCount() ==
                  repeatedBootstrapLexer.transitionRangeCount() &&
              bootstrapTokens ==
                  repeatedBootstrapLexer.tokenize(bootstrapSource),
          "the generated Ag.ag lexer and its token stream must be stable");
  const agas::generator::GeneratedParserTable bootstrapTable =
      agas::generator::generateParserTable(*bootstrap.document);
  require(bootstrapTable.configuration().algorithm ==
                  agas::generator::ParserAlgorithm::Lalr &&
              bootstrapTable.configuration().lookahead == 2 &&
              bootstrapTable.table().maxLength() == 2 &&
              bootstrapTable.dfaStatistics().states > 0,
          "Ag.ag options must build its direct LALR(2) table through Zbik");
  require(!bootstrapTable.table().hasConflicts(),
          "the bootstrap Agas grammar must be LALR(2)");
  const agas::generator::CompressedTableDsl bootstrapDsl =
      agas::generator::exportCompressedTableDsl(bootstrapTable);
  require(bootstrapDsl.text.starts_with(
              "compressed-table \"LALR(2)\" {\n  start-state 0;") &&
              bootstrapDsl.text.find("action-state-rows") !=
                  std::string::npos &&
              bootstrapDsl.text.find("goto-state-rows") != std::string::npos,
          "the Agas table must use the neutral compressed-table DSL");
  require(bootstrapDsl.text ==
              agas::generator::exportCompressedTableDsl(bootstrapTable).text,
          "the compressed-table DSL export must be deterministic");
  require(bootstrapDsl.storage.compressedBytes <
              bootstrapDsl.storage.uncompressedBytes,
          "the Agas LALR(2) table must benefit from row compression");

  const agas::bootstrap::ParseResult model = agas::bootstrap::parseAgas(
      std::string{"grammar Sample;\n"
                  "options { algorithm = LR; lookahead = 2; title = 'x'; "
                  "enabled = true; disabled = false; }\n"
                  "channels { HIDDEN, COMMENTS, }\n"
                  "node start : left=item? TOKEN* 'x'+ External.member "
                  "#Full | empty #Empty ;\n"
                  "inline item : RULE ;\n"
                  "node plain : item ;\n"
                  "fragment PART : [a-z]+? ;\n"
                  "TOKEN : PART? | 'x'* | .+ | ~'q' | ~[0-9] | "
                  "('a' | empty)+ ;\n"
                  "RULE : 'r' ;\n"
                  "COMMENT : '//' ~[\\r\\n]* -> channel(COMMENTS) ;\n"
                  "WS : [ \\t\\r\\n]+ -> skip ;\n"});
  require(model.accepted(), "the complete model fixture must be accepted");
  const agas::model::SyntaxDocument &document = *model.document;
  require(document.grammarName == "Sample", "the grammar name must be owned");
  require(document.span.begin == agas::model::SourcePosition{0, 1, 0},
          "the document must start at the first source character");
  require(document.options.size() == 5, "all option entries must be modeled");
  require(document.options[0].value.kind ==
                  agas::model::OptionValueKind::Identifier &&
              document.options[0].value.spelling == "LR",
          "identifier option values must retain their spelling");
  require(document.options[1].value.kind ==
                  agas::model::OptionValueKind::Integer &&
              document.options[1].value.spelling == "2",
          "integer option values must retain their spelling");
  require(document.options[2].value.kind ==
                  agas::model::OptionValueKind::String &&
              document.options[2].value.spelling == "'x'",
          "string option values must retain their literal spelling");
  require(document.options[3].value.kind ==
                  agas::model::OptionValueKind::Boolean &&
              document.options[4].value.kind ==
                  agas::model::OptionValueKind::Boolean,
          "both Boolean spellings must have the Boolean kind");
  require(document.channels.size() == 2 &&
              document.channels[0].name == "HIDDEN" &&
              document.channels[1].name == "COMMENTS",
          "channels and a trailing comma must be modeled");

  const auto policies = agas::bootstrap::parseAgas(
      "grammar Policies; conflicts { "
      "prefer shift ELSE over reduce ifStatement#Short; "
      "prefer reduce ifStatement#Short over shift END; "
      "} node ifStatement : IF #Short | IF ELSE #Long; "
      "IF : 'if'; ELSE : 'else'; END : 'end';");
  require(policies.accepted() &&
              policies.document->conflictPreferences.size() == 2 &&
              policies.document->conflictPreferences[0].choice ==
                  agas::model::ConflictPreference::Choice::Shift &&
              policies.document->conflictPreferences[0].shiftTerminal == "ELSE" &&
              policies.document->conflictPreferences[1].choice ==
                  agas::model::ConflictPreference::Choice::Reduce &&
              policies.document->conflictPreferences[1].shiftTerminal == "END" &&
              policies.document->conflictPreferences[1].reduceAlternative == "Short",
          "ANTLR bootstrap must preserve both conflict-policy orders");

  const agas::bootstrap::ParseResult eagerLexer = agas::bootstrap::parseAgas(
      "grammar EagerLexer; channels { COMMENTS } "
      "node start : 'if' ID ; fragment LETTER : [a-z] ; ID : LETTER+ ; "
      "COMMENT : '//' ~[\\r\\n]* -> channel(COMMENTS) ; "
      "WS : [ ]+ -> skip ;");
  require(eagerLexer.accepted() &&
              agas::model::validateSyntaxModel(*eagerLexer.document).valid(),
          "the eager lexer-generation fixture must be valid");
  const agas::model::BnfModel eagerLexerBnf =
      agas::model::lowerToBnf(*eagerLexer.document);
  const agas::generator::GeneratedLexerAutomaton generatedLexer =
      agas::generator::compileLexerAutomaton(*eagerLexer.document,
                                             eagerLexerBnf);
  require(generatedLexer.rules().size() == 4 &&
              generatedLexer.rules()[1].name == "ID" &&
              generatedLexer.rules()[2].name == "COMMENT" &&
              generatedLexer.rules()[3].name == "WS",
          "implicit literals must precede expanded non-fragment token rules");
  const agas::generator::AgasLexResult lexed =
      generatedLexer.tokenize("if name //note");
  const auto implicitIf =
      eagerLexerBnf.grammar().findTerminal(generatedLexer.rules()[0].name);
  const auto identifier = eagerLexerBnf.grammar().findTerminal("ID");
  const auto commentTerminal = eagerLexerBnf.grammar().findTerminal("COMMENT");
  require(implicitIf && identifier && commentTerminal &&
              lexed.tokens.size() == 3 &&
              lexed.tokens[0] == agas::generator::AgasLexedToken{*implicitIf,
                                                                 std::nullopt,
                                                                 0, "if"} &&
              lexed.tokens[1] == agas::generator::AgasLexedToken{*identifier,
                                                                 std::nullopt,
                                                                 3, "name"} &&
              lexed.tokens[2] ==
                  agas::generator::AgasLexedToken{
                      *commentTerminal, std::string{"COMMENTS"}, 8, "//note"},
          "Agas tokenization must preserve BNF terminals, channels, and byte "
          "offsets while dropping skipped tokens");
  require(lexed.parserTerminalIds ==
              std::vector<zbik::TerminalId>{*implicitIf, *identifier},
          "only default-channel terminal IDs must reach the parser");

  const agas::bootstrap::ParseResult lazyLexer = agas::bootstrap::parseAgas(
      "grammar LazyLexer; channels { HIDDEN } node start : ID ID ; "
      "ID : [a-z]+ ; "
      "BLOCK_COMMENT : '/*' .*? '*/' -> channel(HIDDEN) ; "
      "WS : [ ]+ -> skip ;");
  require(lazyLexer.accepted() &&
              agas::model::validateSyntaxModel(*lazyLexer.document).valid(),
          "the lazy lexer-generation fixture must be valid");
  const agas::model::BnfModel lazyLexerBnf =
      agas::model::lowerToBnf(*lazyLexer.document);
  const agas::generator::GeneratedLexerAutomaton generatedLazyLexer =
      agas::generator::compileLexerAutomaton(*lazyLexer.document, lazyLexerBnf);
  const agas::generator::AgasLexResult lazyLexed =
      generatedLazyLexer.tokenize("one /* first */ two /* second */ three");
  require(lazyLexed.tokens.size() == 5 &&
              lazyLexed.tokens[1].channel == "HIDDEN" &&
              lazyLexed.tokens[1].text == "/* first */" &&
              lazyLexed.tokens[3].channel == "HIDDEN" &&
              lazyLexed.tokens[3].text == "/* second */" &&
              lazyLexed.parserTerminalIds.size() == 3,
          "lazy wildcard repetition must stop each comment at its first "
          "terminator without hiding channel tokens");

  const auto rejectsLexerGeneration = [](std::string_view source,
                                         std::string_view diagnostic) {
    const agas::bootstrap::ParseResult parsed =
        agas::bootstrap::parseAgas(source);
    if (!parsed.accepted())
      return false;
    try {
      const agas::model::ValidationResult validation =
          agas::model::validateSyntaxModel(*parsed.document);
      if (!validation.valid())
        return false;
      const agas::model::BnfModel bnf =
          agas::model::lowerToBnf(*parsed.document);
      static_cast<void>(
          agas::generator::compileLexerAutomaton(*parsed.document, bnf));
    } catch (const agas::generator::LexerGenerationError &error) {
      return std::string_view{error.what()}.find(diagnostic) !=
             std::string_view::npos;
    }
    return false;
  };
  require(rejectsLexerGeneration(
              "grammar Cycle; node start : TOKEN ; fragment A : B ; "
              "fragment B : A ; TOKEN : A ;",
              "recursive lexer-rule reference"),
          "recursive fragment references must be rejected");
  require(rejectsLexerGeneration(
              "grammar Nullable; node start : TOKEN ; TOKEN : 'x'* ;",
              "must not accept the empty string"),
          "nullable token rules must be rejected with their source rule");
  const std::string unicodeSource = "grammar Unicode;\n"
                                    "// ą😀\n"
                                    "node start : TOKEN ;\n"
                                    "TOKEN : 'ą😀' ;\n";
  const agas::bootstrap::ParseResult unicodeModel =
      agas::bootstrap::parseAgas(unicodeSource);
  require(unicodeModel.accepted(),
          "Unicode lexer literals must be accepted by the bootstrap");
  const agas::model::LexerRule &unicodeRule =
      unicodeModel.document->lexerRules.front();
  const agas::model::LexerAtom &unicodeAtom =
      unicodeRule.alternatives.front().elements.front().atom;
  require(unicodeRule.span.begin.offset == unicodeSource.find("TOKEN :") &&
              unicodeRule.span.begin.line == 4 &&
              unicodeRule.span.begin.column == 0,
          "source spans must map ANTLR code-point indexes to UTF-8 bytes");
  require(
      unicodeAtom.span.begin.offset == unicodeSource.find("'ą😀'") &&
          unicodeAtom.span.end.offset ==
              unicodeSource.find("'ą😀'") + std::string("'ą😀'").size() &&
          unicodeAtom.span.end.column - unicodeAtom.span.begin.column == 4,
      "Unicode literal spans must keep byte offsets and code-point columns");

  const std::string invalidUtf8 =
      std::string("grammar Bad; // ") + std::string("\xC0\xAF", 2);
  const agas::bootstrap::ParseResult invalidUtf8Result =
      agas::bootstrap::parseAgas(invalidUtf8);
  require(!invalidUtf8Result.accepted() && !invalidUtf8Result.issues.empty(),
          "invalid UTF-8 must produce a bootstrap diagnostic");

  require(document.parserRules.size() == 3, "all parser rules must be modeled");
  const agas::model::ParserRule &start = document.parserRules[0];
  require(start.treeModifier == agas::model::TreeModifier::Node &&
              start.name == "start" && start.alternatives.size() == 2,
          "node rules and alternatives must be modeled");
  require(start.span.begin.line == 4 && start.span.begin.column == 0,
          "rule source positions must be retained");
  const agas::model::ParserAlternative &full = start.alternatives[0];
  require(full.label == "Full" && full.elements.size() == 4,
          "alternative labels and elements must be modeled");
  require(
      full.elements[0].fieldName == "left" &&
          full.elements[0].symbol.kind ==
              agas::model::ParserSymbolKind::RuleReference &&
          full.elements[0].symbol.name == "item" &&
          full.elements[0].quantifier == agas::model::Quantifier::Optional,
      "field labels, rule references and optional suffixes must be modeled");
  require(full.elements[1].symbol.kind ==
                  agas::model::ParserSymbolKind::TokenReference &&
              full.elements[1].quantifier ==
                  agas::model::Quantifier::ZeroOrMore,
          "token references and star suffixes must be modeled");
  require(full.elements[2].symbol.kind ==
                  agas::model::ParserSymbolKind::Literal &&
              full.elements[2].symbol.name == "'x'" &&
              full.elements[2].quantifier == agas::model::Quantifier::OneOrMore,
          "literals and plus suffixes must be modeled");
  require(full.elements[3].symbol.kind ==
                  agas::model::ParserSymbolKind::QualifiedReference &&
              full.elements[3].symbol.qualifier == "External" &&
              full.elements[3].symbol.name == "member",
          "qualified references must be split into both names");
  require(start.alternatives[1].explicitEmpty &&
              start.alternatives[1].label == "Empty" &&
              start.alternatives[1].elements.empty(),
          "explicit empty labelled alternatives must be modeled");
  require(document.parserRules[1].treeModifier ==
                  agas::model::TreeModifier::Inline &&
              document.parserRules[2].treeModifier ==
                  agas::model::TreeModifier::Node,
          "inline and node tree modifiers must remain distinct");

  require(document.lexerRules.size() == 5 && document.lexerRules[0].fragment &&
              document.lexerRules[0].name == "PART" &&
              document.lexerRules[0].alternatives.size() == 1,
          "fragment lexer rules must be modeled");
  const agas::model::LexerElement &part =
      document.lexerRules[0].alternatives[0].elements[0];
  require(part.atom.kind == agas::model::LexerAtomKind::CharacterSet &&
              part.atom.spelling == "[a-z]" &&
              part.quantifier == agas::model::LexerQuantifier::OneOrMore &&
              part.lazy,
          "character sets and lazy lexer suffixes must be modeled");

  const agas::model::LexerRule &token = document.lexerRules[1];
  require(!token.fragment && token.name == "TOKEN" &&
              token.alternatives.size() == 6,
          "lexer alternatives must be modeled");
  require(token.alternatives[0].elements[0].atom.kind ==
                  agas::model::LexerAtomKind::TokenReference &&
              token.alternatives[0].elements[0].quantifier ==
                  agas::model::LexerQuantifier::Optional,
          "token references and optional lexer suffixes must be modeled");
  require(token.alternatives[1].elements[0].atom.kind ==
                  agas::model::LexerAtomKind::Literal &&
              token.alternatives[1].elements[0].quantifier ==
                  agas::model::LexerQuantifier::ZeroOrMore &&
              !token.alternatives[1].elements[0].lazy,
          "literals and greedy star suffixes must be modeled");
  require(token.alternatives[2].elements[0].atom.kind ==
                  agas::model::LexerAtomKind::Wildcard &&
              token.alternatives[2].elements[0].quantifier ==
                  agas::model::LexerQuantifier::OneOrMore,
          "wildcards and plus suffixes must be modeled");
  require(token.alternatives[3].elements[0].atom.kind ==
                  agas::model::LexerAtomKind::NegatedLiteral &&
              token.alternatives[3].elements[0].atom.spelling == "'q'" &&
              token.alternatives[4].elements[0].atom.kind ==
                  agas::model::LexerAtomKind::NegatedCharacterSet &&
              token.alternatives[4].elements[0].atom.spelling == "[0-9]",
          "both negated lexer atom forms must be modeled");
  const agas::model::LexerElement &group = token.alternatives[5].elements[0];
  require(
      group.atom.kind == agas::model::LexerAtomKind::Group &&
          group.quantifier == agas::model::LexerQuantifier::OneOrMore &&
          group.atom.groupAlternatives.size() == 2 &&
          group.atom.groupAlternatives[1].explicitEmpty,
      "nested groups and explicit empty lexer alternatives must be modeled");
  require(token.commands.empty(),
          "default-channel lexer rules must preserve an empty command list");
  const agas::model::LexerRule &comment = document.lexerRules[3];
  const agas::model::LexerRule &whitespace = document.lexerRules[4];
  require(comment.commands.size() == 1 &&
              comment.commands[0].name == "channel" &&
              comment.commands[0].argument == "COMMENTS" &&
              whitespace.commands.size() == 1 &&
              whitespace.commands[0].name == "skip" &&
              !whitespace.commands[0].argument.has_value(),
          "supported lexer commands and their arguments must be modeled");
  require(agas::model::validateSyntaxModel(document).valid(),
          "the complete model fixture must pass semantic validation");

  const agas::bootstrap::ParseResult invalidModel = agas::bootstrap::parseAgas(
      "grammar Invalid;\n"
      "options { mode = first; mode = second; }\n"
      "channels { EXTRA, EXTRA }\n"
      "inline duplicate : field=missing field=missing #Same | missing "
      "#Same ;\n"
      "node partial : duplicate #Named | duplicate ;\n"
      "node partial : empty ;\n"
      "node misplacedEof : EOF TOKEN ;\n"
      "node fragmentUse : PART ;\n"
      "TOKEN : UNKNOWN ;\n"
      "TOKEN : 'x' ;\n"
      "fragment PART : 'p' ;\n");
  require(invalidModel.accepted(),
          "semantic errors must remain valid bootstrap syntax");
  const agas::model::ValidationResult invalidValidation =
      agas::model::validateSyntaxModel(*invalidModel.document);
  require(!invalidValidation.valid(),
          "semantic errors must invalidate the syntax model");
  require(hasIssue(invalidValidation, agas::model::DiagnosticSeverity::Error,
                   "duplicate option"),
          "duplicate options must be diagnosed");
  require(hasIssue(invalidValidation, agas::model::DiagnosticSeverity::Error,
                   "duplicate channel"),
          "duplicate channels must be diagnosed");
  require(hasIssue(invalidValidation, agas::model::DiagnosticSeverity::Error,
                   "inline rule"),
          "inline alternative labels must be diagnosed");
  require(hasIssue(invalidValidation, agas::model::DiagnosticSeverity::Error,
                   "duplicate alternative name"),
          "duplicate alternative labels must be diagnosed");
  require(hasIssue(invalidValidation, agas::model::DiagnosticSeverity::Error,
                   "duplicate field name"),
          "duplicate fields must be diagnosed");
  require(hasIssue(invalidValidation, agas::model::DiagnosticSeverity::Error,
                   "must name either every alternative or none"),
          "partly labelled node rules must be diagnosed");
  require(hasIssue(invalidValidation, agas::model::DiagnosticSeverity::Error,
                   "duplicate parser rule"),
          "duplicate parser rules must be diagnosed");
  require(hasIssue(invalidValidation, agas::model::DiagnosticSeverity::Error,
                   "duplicate lexer rule"),
          "duplicate lexer rules must be diagnosed");
  require(hasIssue(invalidValidation, agas::model::DiagnosticSeverity::Error,
                   "undefined parser rule"),
          "undefined parser references must be diagnosed");
  require(hasIssue(invalidValidation, agas::model::DiagnosticSeverity::Error,
                   "undefined lexer rule"),
          "undefined lexer references must be diagnosed");
  require(hasIssue(invalidValidation, agas::model::DiagnosticSeverity::Error,
                   "EOF is allowed only"),
          "misplaced structural EOF references must be diagnosed");
  require(hasIssue(invalidValidation, agas::model::DiagnosticSeverity::Error,
                   "cannot reference fragment"),
          "parser references to lexer fragments must be diagnosed");

  const agas::bootstrap::ParseResult labelledEof =
      agas::bootstrap::parseAgas("grammar LabelledEof; node start : end=EOF ;");
  const agas::model::ValidationResult labelledEofValidation =
      agas::model::validateSyntaxModel(*labelledEof.document);
  require(!labelledEofValidation.valid() &&
              hasIssue(labelledEofValidation,
                       agas::model::DiagnosticSeverity::Error,
                       "structural EOF cannot be stored"),
          "structural EOF must not create an unavailable AST field");

  const agas::bootstrap::ParseResult invalidLexerCommands =
      agas::bootstrap::parseAgas(
          "grammar InvalidCommands; channels { HIDDEN } "
          "node start : OFF_CHANNEL SKIPPED ; "
          "fragment PART : 'p' -> skip ; "
          "OFF_CHANNEL : 'o' -> channel(MISSING), skip ; "
          "SKIPPED : 's' -> skip(HIDDEN), skip ; "
          "UNKNOWN : 'u' -> more ; "
          "NO_ARGUMENT : 'n' -> channel ;");
  require(invalidLexerCommands.accepted(),
          "invalid lexer commands must remain valid bootstrap syntax");
  const agas::model::ValidationResult invalidLexerCommandValidation =
      agas::model::validateSyntaxModel(*invalidLexerCommands.document);
  require(!invalidLexerCommandValidation.valid(),
          "invalid lexer commands must invalidate the syntax model");
  require(hasIssue(invalidLexerCommandValidation,
                   agas::model::DiagnosticSeverity::Error,
                   "fragment lexer rule"),
          "commands on fragment rules must be diagnosed");
  require(hasIssue(invalidLexerCommandValidation,
                   agas::model::DiagnosticSeverity::Error,
                   "undefined lexer channel"),
          "channel commands must name a declared channel");
  require(hasIssue(invalidLexerCommandValidation,
                   agas::model::DiagnosticSeverity::Error,
                   "cannot combine `skip` and `channel`"),
          "mutually exclusive emission commands must be diagnosed");
  require(hasIssue(invalidLexerCommandValidation,
                   agas::model::DiagnosticSeverity::Error,
                   "does not take an argument"),
          "skip arguments must be diagnosed");
  require(hasIssue(invalidLexerCommandValidation,
                   agas::model::DiagnosticSeverity::Error,
                   "duplicate lexer command"),
          "duplicate lexer commands must be diagnosed");
  require(hasIssue(invalidLexerCommandValidation,
                   agas::model::DiagnosticSeverity::Error,
                   "unsupported lexer command"),
          "unknown lexer commands must not be ignored");
  require(hasIssue(invalidLexerCommandValidation,
                   agas::model::DiagnosticSeverity::Error,
                   "requires an argument"),
          "channel commands without arguments must be diagnosed");
  require(hasIssue(invalidLexerCommandValidation,
                   agas::model::DiagnosticSeverity::Error,
                   "does not emit on the default channel"),
          "parser references to hidden or skipped tokens must be diagnosed");

  const agas::bootstrap::ParseResult legacyHiddenReference =
      agas::bootstrap::parseAgas(
          "grammar Legacy; options { legacy = true; } channels { HIDDEN } "
          "node start : HIDDEN_TOKEN ; "
          "HIDDEN_TOKEN : 'x' -> channel(HIDDEN) ;");
  const agas::model::ValidationResult legacyHiddenValidation =
      agas::model::validateSyntaxModel(*legacyHiddenReference.document);
  require(
      legacyHiddenValidation.valid() &&
          hasIssue(legacyHiddenValidation,
                   agas::model::DiagnosticSeverity::Warning,
                   "does not emit on the default channel"),
      "legacy imports must retain unreachable-token diagnostics as warnings");

  const agas::bootstrap::ParseResult duplicateProduction =
      agas::bootstrap::parseAgas(
          "grammar Duplicate; node start : TOKEN #First | TOKEN #Second ; "
          "TOKEN : 'x' ;");
  require(duplicateProduction.accepted(),
          "the duplicate-production fixture must parse");
  const agas::model::ValidationResult duplicateValidation =
      agas::model::validateSyntaxModel(*duplicateProduction.document);
  require(duplicateValidation.valid(),
          "a duplicate production warning must not discard either RuleId");
  require(hasIssue(duplicateValidation,
                   agas::model::DiagnosticSeverity::Warning,
                   "reduce/reduce conflict"),
          "grammar-equivalent alternatives must produce a warning");

  const auto unreachable = agas::bootstrap::parseAgas(
      "grammar Reachability; "
      "node start : used EOF ; "
      "inline used : TOKEN | used TOKEN ; "
      "node orphan : other ; "
      "inline other : TOKEN ; "
      "TOKEN : 'x' ;");
  require(unreachable.accepted(), "reachability fixture must parse");
  const auto unreachableValidation =
      agas::model::validateSyntaxModel(*unreachable.document);
  require(unreachableValidation.valid() &&
              hasIssue(unreachableValidation,
                       agas::model::DiagnosticSeverity::Warning,
                       "parser rule `orphan` is unreachable") &&
              hasIssue(unreachableValidation,
                       agas::model::DiagnosticSeverity::Warning,
                       "parser rule `other` is unreachable") &&
              !hasIssue(unreachableValidation,
                        agas::model::DiagnosticSeverity::Warning,
                        "parser rule `used` is unreachable"),
          "reachability must traverse referenced rules from the start rule");

  const auto strictReachability = agas::bootstrap::parseAgas(
      "grammar StrictReachability; "
      "options { warningsAsErrors = true; } "
      "node start : TOKEN EOF ; "
      "node orphan : TOKEN ; "
      "TOKEN : 'x' ;");
  require(strictReachability.accepted(), "strict warning fixture must parse");
  const auto strictValidation =
      agas::model::validateSyntaxModel(*strictReachability.document);
  require(!strictValidation.valid() &&
              hasIssue(strictValidation, agas::model::DiagnosticSeverity::Error,
                       "parser rule `orphan` is unreachable"),
          "warningsAsErrors must reject unreachable rules");

  const auto invalidWarningPolicy = agas::bootstrap::parseAgas(
      "grammar InvalidWarningPolicy; "
      "options { warningsAsErrors = enabled; } "
      "node start : TOKEN EOF ; TOKEN : 'x' ;");
  require(invalidWarningPolicy.accepted(), "invalid warning policy must parse");
  const auto invalidWarningValidation =
      agas::model::validateSyntaxModel(*invalidWarningPolicy.document);
  require(!invalidWarningValidation.valid() &&
              hasIssue(invalidWarningValidation,
                       agas::model::DiagnosticSeverity::Error,
                       "option `warningsAsErrors` must be"),
          "warningsAsErrors must have a boolean value");

  const agas::bootstrap::ParseResult ebnf = agas::bootstrap::parseAgas(
      "grammar Lower;\n"
      "node start : optional=item? repeated=ITEM* required=ITEM+ "
      "text='with space' EOF ;\n"
      "inline item : empty | ITEM ;\n"
      "ITEM : 'i' ;\n");
  require(ebnf.accepted() &&
              agas::model::validateSyntaxModel(*ebnf.document).valid(),
          "the EBNF lowering fixture must be valid");
  const agas::model::BnfModel bnf = agas::model::lowerToBnf(*ebnf.document);
  require(bnf.grammar().ruleCount() == 9 &&
              bnf.grammar().nonterminalCount() == 5,
          "three EBNF suffixes must create three two-production helpers");
  require(bnf.grammar().terminalCount() == 2,
          "lexer tokens and parser literals must form the terminal alphabet");
  require(bnf.grammar().nonterminalName(bnf.grammar().start()) == "start" &&
              bnf.grammar().rule(zbik::RuleId{0}).size() == 4,
          "the first parser rule must remain the start and structural EOF "
          "must not become a grammar symbol");
  require(!bnf.grammar().findTerminal("EOF").has_value(),
          "EOF must be represented by LR acceptance, not a second terminal");
  require(bnf.origins().size() == bnf.grammar().ruleCount() &&
              bnf.origin(zbik::RuleId{0}).sourceRuleIndex == 0 &&
              !bnf.origin(zbik::RuleId{0}).elementIndex.has_value() &&
              bnf.origin(zbik::RuleId{3}).elementIndex == 0 &&
              bnf.origin(zbik::RuleId{3}).sourceSpan.begin.line == 2,
          "every source and helper production must retain its Agas origin");
  require(bnf.terminals().size() == 2 &&
              bnf.terminals()[0].kind ==
                  agas::model::TerminalBindingKind::LexerRule &&
              bnf.terminals()[1].kind ==
                  agas::model::TerminalBindingKind::Literal &&
              bnf.terminals()[1].spelling == "'with space'" &&
              bnf.terminals()[1].internalName == "__agas_terminal_0",
          "literals must be interned without losing their spelling");

  bool rejectedInvalidLowering = false;
  try {
    static_cast<void>(agas::model::lowerToBnf(*invalidModel.document));
  } catch (const std::invalid_argument &) {
    rejectedInvalidLowering = true;
  }
  require(rejectedInvalidLowering,
          "an invalid syntax model must not be lowered to BNF");

  const agas::bootstrap::ParseResult literalBinding =
      agas::bootstrap::parseAgas("grammar Literals; node start : 'if' ID ; "
                                 "IF : '\\x69f' ; ID : [a-z]+ ;");
  require(
      literalBinding.accepted() &&
          agas::model::validateSyntaxModel(*literalBinding.document).valid(),
      "the equivalent literal binding fixture must be valid");
  const agas::model::BnfModel literalBindingBnf =
      agas::model::lowerToBnf(*literalBinding.document);
  require(literalBindingBnf.grammar().terminalCount() == 2 &&
              literalBindingBnf.grammar().findTerminal("IF").has_value() &&
              literalBindingBnf.terminals().size() == 2,
          "parser literals must reuse equivalent named lexer terminals");

  const agas::bootstrap::ParseResult lrTwo = agas::bootstrap::parseAgas(
      "grammar Two; options { parser = LR; lookahead = 2; } "
      "node start : TOKEN ; TOKEN : 'x' ;");
  require(lrTwo.accepted(), "the LR(2) configuration fixture must parse");
  const agas::generator::GeneratedParserTable lrTwoTable =
      agas::generator::generateParserTable(*lrTwo.document);
  require(lrTwoTable.configuration().lookahead == 2 &&
              lrTwoTable.configuration().algorithm ==
                  agas::generator::ParserAlgorithm::CanonicalLr &&
              lrTwoTable.table().maxLength() == 2 &&
              !lrTwoTable.table().hasConflicts(),
          "lookahead = 2 must build a conflict-free canonical LR(2) table");

  const agas::bootstrap::ParseResult lalrTwo = agas::bootstrap::parseAgas(
      "grammar Two; options { parser = LALR; lookahead = 2; } "
      "node start : TOKEN ; TOKEN : 'x' ;");
  require(lalrTwo.accepted(), "the LALR(2) configuration fixture must parse");
  const agas::generator::GeneratedParserTable lalrTwoTable =
      agas::generator::generateParserTable(*lalrTwo.document);
  require(lalrTwoTable.configuration().algorithm ==
                  agas::generator::ParserAlgorithm::Lalr &&
              lalrTwoTable.table().isLalr() &&
              lalrTwoTable.table().maxLength() == 2 &&
              !lalrTwoTable.table().hasConflicts(),
          "parser = LALR must build a direct LALR(2) table");

  const agas::bootstrap::ParseResult invalidConfiguration =
      agas::bootstrap::parseAgas(
          "grammar BadOption; options { lookahead = 0; } "
          "node start : 'x' ;");
  bool rejectedConfiguration = false;
  try {
    static_cast<void>(
        agas::generator::generateParserTable(*invalidConfiguration.document));
  } catch (const agas::generator::ParserConfigurationError &error) {
    rejectedConfiguration = error.span().begin.line == 1;
  }
  require(rejectedConfiguration,
          "invalid parser options must report their source position");

  const agas::bootstrap::ParseResult ambiguous = agas::bootstrap::parseAgas(
      "grammar Ambiguous; node start : first | second ; "
      "inline first : TOKEN ; inline second : TOKEN ; TOKEN : 'x' ;");
  const agas::generator::GeneratedParserTable ambiguousTable =
      agas::generator::generateParserTable(*ambiguous.document);
  require(ambiguousTable.table().hasConflicts(),
          "the ambiguous export fixture must retain its conflict");
  bool rejectedConflictingExport = false;
  try {
    static_cast<void>(
        agas::generator::exportCompressedTableDsl(ambiguousTable));
  } catch (const agas::generator::TableExportError &error) {
    rejectedConflictingExport = error.conflictCount() > 0;
  }
  require(rejectedConflictingExport,
          "a conflicting table must not be exported as executable DSL");

  const agas::bootstrap::ParseResult malformed =
      agas::bootstrap::parseAgas("grammar Broken; node start : ;");
  require(!malformed.accepted(),
          "an implicit empty alternative must be rejected");
  require(!malformed.issues.empty(),
          "a rejected document must contain a diagnostic");
  require(!malformed.document.has_value(),
          "a recovered ANTLR tree must not escape as a valid model");
  require(malformed.issues.front().line == 1,
          "the diagnostic must preserve its source line");

  const agas::bootstrap::ParseResult missingTreeModifier =
      agas::bootstrap::parseAgas("grammar Broken; start : 'x' ;");
  require(!missingTreeModifier.accepted(),
          "every parser rule must explicitly choose node or inline");

  const agas::bootstrap::ParseResult lexerError =
      agas::bootstrap::parseAgas("grammar Broken; @");
  require(!lexerError.accepted(),
          "an unknown input character must be rejected");

  return 0;
}
