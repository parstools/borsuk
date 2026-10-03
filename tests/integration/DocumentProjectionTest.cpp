#include "DocumentStorage.h"
#include "PreparedSemanticModel.h"
#include "agas/generator/ContextualGeneration.h"
#include "agsem/DocumentFrontend.h"
#include "agsem/Projection.h"
#include "agsem/SemanticBinding.h"
#include "coge/DocumentModel.h"
#include "coge/GenerationPreparation.h"
#include "coge/Projection.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <tuple>

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
// Only the explicitly migrated operator and collection factories may differ.
void replaceAll(std::string &text, const std::string &from, const std::string &to) {
  std::size_t offset = 0;
  while ((offset = text.find(from, offset)) != std::string::npos) {
    text.replace(offset, from.size(), to);
    offset += to.size();
  }
}
const std::vector<std::tuple<std::string, std::string, std::string>> enumFactories{
    {"op_le", "Operator", "LessEqual"},
    {"op_lt", "Operator", "Less"},
    {"op_gt", "Operator", "Greater"},
    {"op_ge", "Operator", "GreaterEqual"},
    {"op_eq", "Operator", "Equal"},
    {"op_ne", "Operator", "NotEqual"},
    {"op_add", "Operator", "Add"},
    {"op_sub", "Operator", "Subtract"},
    {"op_mul", "Operator", "Multiply"},
    {"op_div", "Operator", "Divide"},
    {"assignment_set", "AssignmentOp", "Set"},
    {"assignment_add", "AssignmentOp", "Add"},
    {"assignment_subtract", "AssignmentOp", "Subtract"},
    {"assignment_multiply", "AssignmentOp", "Multiply"},
    {"assignment_divide", "AssignmentOp", "Divide"},
};
auto migrateFactories(std::string text, bool rust) -> std::string {
  for (const auto &[name, type, variant] : enumFactories) {
    if (!rust) {
      replaceAll(text, "    intrinsic " + name + "() -> " + type + ";\n", "");
      replaceAll(text, name + "()", type + "." + variant);
    } else
      replaceAll(text, "ctx." + name + "()", "crate::" + type + "::" + variant);
  }
  for (const auto &[plural, singular, type] :
       std::vector<std::tuple<std::string, std::string, std::string>>{
           {"arguments", "argument", "ExprId"},
           {"parameters", "parameter", "ParameterId"}}) {
    if (!rust) {
      replaceAll(text, "    intrinsic empty_" + plural + "() -> List<" + type + ">;\n", "");
      replaceAll(text, "    intrinsic singleton_" + singular + "(value: " + type + ") -> List<" + type + ">;\n", "");
      replaceAll(text, "    intrinsic append_" + singular + "(" + plural + ": List<" + type + ">, value: " + type + ") -> List<" + type + ">;\n", "");
      replaceAll(text, "empty_" + plural + "()", "List.empty(" + type + ")");
      replaceAll(text, "singleton_" + singular + "(", "List.single(");
      replaceAll(text, "append_" + singular + "(", "List.append(");
    } else {
      replaceAll(text, "ctx.empty_" + plural + "()", "Vec::<" + type + ">::new()");
      const auto first = plural == "arguments" ? "first_value" : "first_parameter";
      const auto list = plural == "arguments" ? "arguments" : "parameters";
      const auto next = plural == "arguments" ? "next_value" : "parameter";
      replaceAll(text, "ctx.singleton_" + singular + "(" + first + ")", "vec![" + std::string{first} + "]");
      replaceAll(text, "    let result = ctx.append_" + singular + "(&" + list + ", " + next + ");\n",
                 "    let result =\n        (" + std::string{list} + ").iter().cloned().chain(std::iter::once(" + next + ")).collect::<Vec<" + type + ">>();\n");
    }
  }
  if (!rust && text.find("    model_bindings ") != std::string::npos) {
    replaceAll(text, "semantic_model {\n",
               "semantic_model {\n    model_schema standard_semantic_v1;\n");
    for (const auto family : {"model", "assignment", "condition", "statement",
                              "flow", "selection"}) {
      const auto start = text.find("    " + std::string{family} + "_bindings ");
      if (start == std::string::npos)
        continue;
      const auto body = text.find('\n', start) + 1;
      const auto end = text.find("    }\n", body);
      require(end != std::string::npos, "unterminated legacy binding fixture");
      std::string retained;
      for (auto cursor = body; cursor < end;) {
        const auto next = text.find('\n', cursor) + 1;
        const auto line = text.substr(cursor, next - cursor);
        if (line.find("return_ir =") != std::string::npos ||
            line.find("cleanup_scopes =") != std::string::npos ||
            line.find("error_ir =") != std::string::npos ||
            line.find("construction_ir =") != std::string::npos)
          retained += line;
        cursor = next;
      }
      if (retained.empty())
        text.erase(start, end + 6 - start);
      else
        text.replace(body, end - body, retained);
    }
  }
  if (!rust) {
    const auto start = text.find("lowering_model {\n");
    if (start != std::string::npos) {
      const auto end = text.find("execution_model {\n", start);
      require(end != std::string::npos,
              "missing executor after lowering fixture");
      text.replace(start, end - start,
                   "lowering_model {\n    profile = structured_core_v1;\n"
                   "    lower = lower_function_to_structured;\n}\n\n"
                   "backend_c {\n    profile = core_c_v1;\n}\n\n"
                   "backend_llvm {\n    profile = core_llvm_v1;\n}\n\n");
    }
  }
  return text;
}
auto migrateFactories(agsem::RustFiles files) -> agsem::RustFiles {
  files.sema = migrateFactories(std::move(files.sema), true);
  files.semaLib = migrateFactories(std::move(files.semaLib), true);
  for (auto &[name, text] : files.modules)
    text = migrateFactories(std::move(text), true);
  return files;
}
auto sameAnalysisAndBackends(const agsem::RustFiles &a,
                            const agsem::RustFiles &b) -> bool {
  return a.sema == b.sema && a.semaLib == b.semaLib &&
         a.properties == b.properties &&
         a.lowering == b.lowering && a.backendC == b.backendC &&
         a.backendLlvm == b.backendLlvm && a.modules == b.modules;
}
auto same(const agsem::RustFiles &a, const agsem::RustFiles &b) -> bool {
  return sameAnalysisAndBackends(a, b) && a.interpreter == b.interpreter;
}
} // namespace

int main(int argc, char **argv) {
  try {
    require(
        argc == 6,
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
      // Compare the authored standalone source with the legacy parsed spans.
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
      const auto expectedStandalone =
          std::string{"coge "} +
          (std::string{language} == "toyc" ? "ToyCTyped" : "ToyCPTyped") +
          "; format 1;\n" +
          grammar.substr(0,
                         base.document->parserRules.front().span.begin.offset) +
          annotated + "\n" +
          grammar.substr(base.document->lexerRules.front().span.begin.offset);
      const auto standalone =
          read(repo / "coge/examples" / language / (std::string{language} + ".coge"));
      const auto explicitSource = migrateFactories(expectedStandalone, false);
      const auto executionBegin = standalone.find("execution_model {\n");
      const auto executionEnd = standalone.find("generation {\n", executionBegin);
      const auto explicitBegin = explicitSource.find("execution_model {\n");
      const auto explicitEnd = explicitSource.find("generation {\n", explicitBegin);
      require(executionBegin != std::string::npos &&
                  executionEnd != std::string::npos &&
                  explicitBegin != std::string::npos &&
                  explicitEnd != std::string::npos,
              "missing execution sections in migration fixtures");
      // The profile test checks every execution member independently, including
      // the allowed permutation of whole Rust functions and dispatcher arms.
      require(standalone.substr(0, executionBegin) ==
                      explicitSource.substr(0, explicitBegin) &&
                  standalone.substr(executionEnd) ==
                      explicitSource.substr(explicitEnd),
              "authored migration differs from legacy grammar and actions");
      auto document = parse(frontend, standalone);
      require(document.grammar().grammar.grammarName ==
                  base.document->grammarName,
              "grammar identity");
      agsem::ContractEnvironment contracts;
      require(
          contracts
              .addManifest(read(repo / "contracts" /
                                (std::string{language} + "-runtime-v1.json")))
              .empty(),
          "load runtime contract");
      const auto coge = coge::makeCogeDocument(document);
      require(coge.value.has_value(), "make coge");
      const auto unbound = agsem::tryPrepareSemanticModel(
          coge.value->semantics().semanticInput());
      require(
          !unbound.value && !unbound.diagnostics.empty() &&
              unbound.diagnostics.front().code == "sema.missing_binding",
          "schema must not bypass contract binding through the old preparator");
      const auto bound =
          agsem::bindSemantics(coge.value->semantics(), contracts);
      if (!bound.value) {
        for (const auto &error : bound.diagnostics)
          std::cerr << error.code << ": " << error.message << " at "
                    << (error.location ? error.location->beginByte : 0) << '\n';
      }
      require(bound.value.has_value(), "bind " + std::string{language});
      auto projected = coge::projectSema(*coge.value, *bound.value, frontend);
      require(projected.value.has_value(), "project sema");
      const auto sema = parse(frontend, projected.value->text);
      require(sema.kind() == agsem::DocumentKind::Sema, "projected kind");
      const auto directAg = agsem::projectAg(document),
                 viaSema = agsem::projectAg(sema);
      require(directAg.value && viaSema.value &&
                  directAg.value->text == viaSema.value->text,
              "byte projection law");
      const auto directModel = ag.parse(directAg.value->text),
                 viaModel = ag.parse(viaSema.value->text);
      require(directModel.accepted() && viaModel.accepted() &&
                  *directModel.document == *viaModel.document,
              "model projection law");
      const auto composed = agsem::composeOriginMaps(projected.value->origins,
                                                     viaSema.value->origins);
      for (const auto &origin : composed)
        require(!origin.replacement &&
                    directAg.value->text.substr(origin.outputBegin,
                                                origin.outputEnd -
                                                    origin.outputBegin) ==
                        standalone.substr(origin.inputBegin,
                                          origin.inputEnd - origin.inputBegin),
                "composed source map");
      auto checked = agsem::prepareSemanticModel(*bound.value);
      require(checked.value.has_value(), "prepare semantics");
      auto generation = coge::prepareGeneration(
          *coge.value, **checked.value, coge::generationSelection(*coge.value),
          contracts);
      if (!generation.value)
        for (const auto &error : generation.diagnostics)
          std::cerr << error.code << ": " << error.message << '\n';
      require(generation.value.has_value(),
              "prepare coge " + std::string{language});
      const auto oldPrepared =
          agsem::prepareSemanticModel(*legacy.root, *base.document);
      const auto generated =
          agsem::emitRust(**checked.value, *generation.value);
      const auto explicitDocument = parse(frontend, explicitSource);
      const auto explicitCoge = coge::makeCogeDocument(explicitDocument);
      require(explicitCoge.value.has_value(), "make explicit coge");
      const auto explicitBinding =
          agsem::bindSemantics(explicitCoge.value->semantics(), contracts);
      require(explicitBinding.value.has_value(), "bind explicit coge");
      const auto explicitSema = coge::projectSema(
          *explicitCoge.value, *explicitBinding.value, frontend);
      require(explicitSema.value &&
                  explicitSema.value->text == projected.value->text,
              "profile changed semantic projection");
      const auto explicitChecked =
          agsem::prepareSemanticModel(*explicitBinding.value);
      require(explicitChecked.value.has_value(), "prepare explicit semantics");
      const auto explicitGeneration = coge::prepareGeneration(
          *explicitCoge.value, **explicitChecked.value,
          coge::generationSelection(*explicitCoge.value), contracts);
      require(explicitGeneration.value.has_value(), "prepare explicit coge");
      const auto explicitGenerated =
          agsem::emitRust(**explicitChecked.value, *explicitGeneration.value);
      require(same(explicitGenerated,
                   migrateFactories(agsem::emitRust(*oldPrepared, *legacy.root))),
              "new generation differs from legacy");
      require(sameAnalysisAndBackends(generated, explicitGenerated),
              "profile changed non-execution Rust");
      const auto exportedDocument = agsem::makeSemaDocument(sema);
      const auto exportedBinding =
          agsem::bindSemantics(*exportedDocument.value, contracts);
      const auto exportedChecked =
          agsem::prepareSemanticModel(*exportedBinding.value);
      require(exportedChecked.value.has_value(), "prepare exported sema");
      const auto semanticFiles =
          agsem::emitCheckedSemanticsRust(**exportedChecked.value);
      require(semanticFiles.sema == generated.sema &&
                  semanticFiles.semaLib == generated.semaLib &&
                  semanticFiles.modules == generated.modules,
              "exported sema generation differs");
      const std::filesystem::path destination{argv[5]};
      std::filesystem::create_directories(destination);
      std::ofstream(destination / (std::string{language} + ".coge"))
          << standalone;
      std::ofstream(destination / (std::string{language} + ".sema"))
          << projected.value->text;
      std::ofstream(destination / (std::string{language} + ".ag"))
          << directAg.value->text;
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
    const std::string decorated = R"(// Żółć: outside the deleted envelope.
coge Kept; format 1;
grammar Shift;
options { parser = LALR; lookahead = 2; }
lexerClasses { SHIFT = false; }
node start : ID analysis { result = 1; } execution result {};
// This comment and regex remain byte-for-byte.
ID : [a-z]+ -> require(SHIFT);
WS : [ \t\r\n]+ -> skip;
execution_model { /* removed */ }
// Koniec.
)";
    const auto decoratedDocument = parse(frontend, decorated);
    const auto decoratedCoge = coge::makeCogeDocument(decoratedDocument);
    const auto decoratedBound =
        agsem::bindSemantics(decoratedCoge.value->semantics(), {});
    require(decoratedBound.value.has_value(), "decorated binding");
    const auto decoratedSema = coge::projectSema(
        *decoratedCoge.value, *decoratedBound.value, frontend);
    require(decoratedSema.value.has_value(), "decorated sema");
    const auto decoratedAg = agsem::projectAg(decoratedDocument);
    const auto decoratedVia =
        agsem::projectAg(parse(frontend, decoratedSema.value->text));
    require(decoratedAg.value->text == decoratedVia.value->text &&
                decoratedAg.value->text.find("Żółć") != std::string::npos &&
                decoratedAg.value->text.find("[a-z]+") != std::string::npos &&
                decoratedAg.value->text.find("removed") == std::string::npos,
            "UTF-8/trivia/classes projection");
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
