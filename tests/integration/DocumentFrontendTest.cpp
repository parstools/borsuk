#include "agsem/DocumentFrontend.h"
#include "DocumentStorage.h"
#include "PreparedSemanticModel.h"
#include "agas/generator/ContextualGeneration.h"
#include "coge/DocumentModel.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using Value = agas::runtime::AstValue;
void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}
auto read(const std::filesystem::path &path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  require(bool(input), "cannot read " + path.string());
  return {std::istreambuf_iterator<char>{input}, {}};
}
auto field(const Value &value, const std::string &name) -> const Value & {
  const auto it = std::ranges::find(value.fieldNames, name);
  require(it != value.fieldNames.end(), "missing field " + name);
  return value.elements.at(it - value.fieldNames.begin());
}
auto parse(const agsem::DocumentFrontend &frontend, const std::string &source)
    -> agsem::ParsedDocument {
  auto result = frontend.parse(source);
  if (!result.value) {
    std::string errors;
    for (const auto &error : result.diagnostics)
      errors += error.message + " at " +
                std::to_string(error.location ? error.location->beginByte : 0) +
                "\n";
    throw std::runtime_error(errors);
  }
  return std::move(*result.value);
}
void partition(const agas::model::SourceText &source) {
  std::size_t cursor = 0;
  bool trivia = false;
  for (const auto &piece : source.pieces) {
    require(piece.beginByte == cursor && piece.endByte > cursor &&
                piece.endByte <= source.text.size(),
            "invalid source partition");
    trivia |= piece.kind == agas::model::SourcePieceKind::Trivia;
    cursor = piece.endByte;
  }
  require(cursor == source.text.size() && trivia, "missing source trivia");
}
auto same(const agsem::RustFiles &a, const agsem::RustFiles &b) -> bool {
  return a.sema == b.sema && a.semaLib == b.semaLib &&
         a.interpreter == b.interpreter && a.properties == b.properties &&
         a.lowering == b.lowering && a.backendC == b.backendC &&
         a.backendLlvm == b.backendLlvm && a.modules == b.modules;
}
} // namespace

int main(int argc, char **argv) {
  try {
    require(
        argc == 5,
        "expected document package, Ag package, legacy package, repository");
    const agsem::DocumentFrontend frontend{argv[1], argv[2]};
    const agas::runtime::PackagedAgFrontend ag{argv[2]};
    const auto legacyPackage =
        agas::artifact::loadArtifactPackageDirectory(argv[3]);
    const agas::runtime::ArtifactLexerRuntime lexer{
        legacyPackage.lexer, legacyPackage.symbols.terminals.size(),
        legacyPackage.symbols.channels.size()};
    const agas::runtime::ArtifactAstParser parser{
        legacyPackage.parserTable, legacyPackage.symbols,
        legacyPackage.productions, legacyPackage.reductions};
    const std::filesystem::path repo{argv[4]};
    for (const auto *language : {"toyc", "toycp"}) {
      const auto grammar = read(repo / "ag/grammars/examples" /
                                (std::string{language} + ".ag"));
      for (const auto *kind : {"sema", "coge"}) {
        auto document =
            parse(frontend, std::string{kind} + " Standalone;\n" + grammar);
        require(document.grammar().source.text == "\n" + grammar,
                "projection changed Ag text");
        const auto original = ag.parse(document.grammar().source.text);
        require(original.accepted() &&
                    *original.document == document.grammar().grammar,
                "projection changed Ag model");
        require(document.specificationName() == "Standalone" &&
                    document.formatVersion() == 1,
                "document identity");
        partition(document.source());
        partition(document.grammar().source);
      }
      // Assemble a test-only standalone input from parsed spans. Authored
      // legacy examples remain unchanged until the migration stage.
      const auto old =
          read(repo / "tests/fixtures/legacy" / (std::string{language} + "_full.sema"));
      const auto legacy = parser.parse(old, lexer);
      require(legacy.accepted(), "legacy fixture");
      const auto base = ag.parse(grammar);
      require(base.accepted(), "Ag fixture");
      const auto header =
          field(*legacy.root, "header").elements.front().recognizedSpan;
      auto annotated = old.substr(header.endByte);
      const auto &settings = field(*legacy.root, "settings");
      if (!settings.elements.empty()) {
        const auto span = settings.elements.front().recognizedSpan;
        annotated.replace(span.beginByte - header.endByte,
                          std::string{"options"}.size(), "generation");
      }
      const auto standalone =
          "coge Standalone; format 1;\n" +
          grammar.substr(0,
                         base.document->parserRules.front().span.begin.offset) +
          annotated + "\n" +
          grammar.substr(base.document->lexerRules.front().span.begin.offset);
      auto document = parse(frontend, standalone);
      require(document.grammar().grammar.grammarName ==
                  base.document->grammarName,
              "grammar identity");
      const auto prepared = agsem::prepareSemanticModel(
          agsem::DocumentAccess::root(document), document.grammar().grammar);
      const auto oldPrepared =
          agsem::prepareSemanticModel(*legacy.root, *base.document);
      require(same(agsem::emitRust(*prepared,
                                   agsem::DocumentAccess::root(document)),
                   agsem::emitRust(*oldPrepared, *legacy.root)),
              "standalone emission differs for " + std::string{language});
      std::cout
          << language
          << ": full Ag, trivia, standalone actions and Rust equality OK\n";
    }

    for (const auto *name :
         {"Shift", "GatedBrackets", "Keywords", "Combined", "Nested"}) {
      const auto source = read(repo / "ag/grammars/contextual-lexer" /
                               (std::string{name} + ".ag"));
      const auto document = parse(frontend, "coge Classes;" + source);
      const auto original = ag.parse(source);
      require(document.grammar().source.text == source && original.accepted() &&
                  document.grammar().grammar == *original.document,
              "contextual fixture projection");
    }
    std::string classes;
    for (int i = 0; i < 64; ++i)
      classes += "C" + std::to_string(i) + " = false; ";
    const auto high =
        parse(frontend,
              "sema High; grammar High; lexerClasses { " + classes +
                  " } node start -> enable(C63) : A; A : 'a' -> require(C63);");
    const auto generated =
        agas::generator::generateContextualParser(high.grammar().grammar);
    const zbik::ContextualLRMachine machine{generated.table,
                                            generated.scoped.requirements,
                                            generated.scoped.sourceTerminals};
    require(machine.parse(generated.lexer, "a").accepted,
            "projection lost lexer mask bit 63");

    const std::string grammar = R"AG(grammar Collision;
options { parser = LALR; lookahead = 2; ast = explicit; }
channels { HIDDEN, }
lexerClasses { CODE = true; STRING = false; }
conflicts { prefer shift ELSE over reduce statement#If; }
/* żółć { analysis execution result } */
node start -> enable(CODE), disable(STRING)
    : execution=analysis+ result? EOF #Start
    ;
inline analysis : execution | result | function ;
inline execution : ID ;
inline result : ID ;
inline function : ID ;
node statement : IF statement #If | IF statement ELSE statement #IfElse | empty #Empty ;
fragment DIGIT : [0-9];
ID : [a-z_] [a-z_0-9]* -> require(CODE);
IF : 'if';
ELSE : 'else';
TEXT : '"' (~["\\] | '\\' .)* '"' -> require(STRING);
COMMENT : '/*' .*? '*/' -> channel(HIDDEN);
SPACE : [ \r\n\t]+ -> skip;
)AG";
    const std::string action =
        R"ACT(analysis { /* } { */ let emit = [1, 2]; let channels = 1; let format = channels; let generation = format; let lower = "{ analysis }"; if true { return none; } }
        execution result { return 1; })ACT";
    auto annotated = grammar;
    annotated.insert(annotated.find("#Start") + 6, " " + action);
    auto document = parse(frontend, "sema Spec; format 1;\n" + annotated);
    auto expected = grammar;
    expected.insert(expected.find("#Start") + 6, " \n        ");
    require(document.grammar().source.text == "\n" + expected,
            "action/trivia projection");
    require(document.grammar().grammar.lexerClasses.size() == 2 &&
                document.grammar().grammar.lexerClasses[0].value.spelling ==
                    "true",
            "lexer classes lost");
    partition(document.source());
    const auto empty = parse(frontend, "sema Empty; grammar EmptyGrammar; node "
                                       "start : empty #Empty analysis {}; ");
    require(empty.grammar()
                .grammar.parserRules.front()
                .alternatives.front()
                .explicitEmpty,
            "empty action alternative lost");
    const auto qualified =
        parse(frontend, "sema Qualified; grammar QualifiedGrammar; node start "
                        ": Item.value?; Item:'i';");
    require(qualified.grammar()
                    .grammar.parserRules.front()
                    .alternatives.front()
                    .elements.front()
                    .symbol.kind ==
                agas::model::ParserSymbolKind::QualifiedReference,
            "qualified reference lost");
    const std::string invalidReference =
        "sema Bad; format 1; grammar BadGrammar; semantic_model {} "
        "node start : missing analysis {};";
    const auto invalidModel = frontend.parse(invalidReference);
    require(!invalidModel.value && !invalidModel.diagnostics.empty() &&
                invalidModel.diagnostics.front().location &&
                invalidModel.diagnostics.front().location->beginByte ==
                    invalidReference.find("missing"),
            "projected diagnostic must point into the original source");
    const auto reject = [&](std::string source) {
      const auto result = frontend.parse(source);
      require(!result.value && !result.diagnostics.empty(),
              "invalid document accepted");
    };
    reject("sema Old for \"missing.ag\";" + grammar);
    reject("sema Spec; format 2;" + grammar);
    reject("sema Spec;" + grammar + "semantic_model {} semantic_model {}");
    reject("sema Spec; grammar X; node start : ID analysis {} ID; ID:'a';");
    reject("sema Spec; grammar X; node start : analysis {}; ID:'a';");
    reject("sema Spec; grammar X; node start : ID; ID:[a-z;");
    reject("sema Spec; grammar X; node start : ID analysis { let x = ; }; "
           "ID:'a';");
    std::cout << "lexer classes, keyword collisions, nested actions and "
                 "invalid inputs OK\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
