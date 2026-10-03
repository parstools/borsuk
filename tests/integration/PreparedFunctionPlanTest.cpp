#include "CogeDocumentStorage.h"
#include "PreparedSemanticModel.h"
#include "SemanticInputStorage.h"
#include "agas/artifact/ArtifactPackage.h"
#include "agas/runtime/ArtifactAstParser.h"
#include "agas/runtime/ArtifactLexerRuntime.h"
#include "agas/runtime/PackagedAgFrontend.h"
#include "coge/GenerationPreparation.h"

#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
auto readFile(const char *path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot open test fixture");
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

auto same(const agsem::RustFiles &left, const agsem::RustFiles &right) -> bool {
  return left.sema == right.sema && left.semaLib == right.semaLib &&
         left.interpreter == right.interpreter &&
         left.properties == right.properties &&
         left.lowering == right.lowering && left.backendC == right.backendC &&
         left.backendLlvm == right.backendLlvm && left.modules == right.modules;
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 5)
    return 2;
  const auto package = agas::artifact::loadArtifactPackageDirectory(argv[1]);
  const agas::runtime::ArtifactLexerRuntime lexer{
      package.lexer, package.symbols.terminals.size(),
      package.symbols.channels.size()};
  const agas::runtime::ArtifactAstParser parser{
      package.parserTable, package.symbols, package.productions,
      package.reductions};
  const agas::runtime::PackagedAgFrontend grammarFrontend{argv[2]};
  const auto source = readFile(argv[3]);
  const auto grammarSource = readFile(argv[4]);
  const auto prepare = [&](const std::string &input) {
    const auto parsed = parser.parse(input, lexer);
    if (!parsed.accepted())
      throw std::runtime_error("semantic fixture did not parse");
    const auto grammar = grammarFrontend.parse(grammarSource);
    if (!grammar.accepted())
      throw std::runtime_error("grammar fixture did not parse");
    return agsem::prepareSemanticModel(*parsed.root, *grammar.document);
  };
  const auto valid = prepare(source);
  const auto parsedValid = parser.parse(source, lexer);
  const auto grammarValid = grammarFrontend.parse(grammarSource);
  if (!parsedValid.accepted() || !grammarValid.accepted())
    return 19;
  const auto validInput = agsem::SemanticInputAccess::fromAst(
      *parsedValid.root, *grammarValid.document);
  const auto validOutcome = agsem::tryPrepareSemanticModel(validInput);
  if (!validOutcome.value || !validOutcome.diagnostics.empty())
    return 20;
  const auto generation =
      agsem::prepareGenerationModel(*valid, *parser.parse(source, lexer).root);
  const auto first = agsem::emitRust(*valid, generation);
  if (!same(first, agsem::emitRust(*valid, generation)))
    return 3;
  const auto detachedGeneration = [&] {
    const auto temporarySemantics = prepare(source);
    return agsem::prepareGenerationModel(*temporarySemantics,
                                         *parser.parse(source, lexer).root);
  }();
  if (coge::emitCheckedGeneration(detachedGeneration).interpreter !=
      first.interpreter)
    return 24;
  auto invalidBackend = source;
  const auto ruleAt = invalidBackend.find("node program");
  if (ruleAt == std::string::npos)
    return 14;
  invalidBackend.insert(
      ruleAt, "backend_c { emit = emit_c; lower = missing_lower; }\n\n");
  const auto validSemantics = prepare(invalidBackend);
  try {
    static_cast<void>(agsem::prepareGenerationModel(
        *validSemantics, *parser.parse(invalidBackend, lexer).root));
    return 15;
  } catch (const std::runtime_error &error) {
    if (std::string_view{error.what()} != "backend_c requires lowering_model")
      return 16;
  }
  auto invalidExecution = source;
  invalidExecution.insert(ruleAt,
                          "options { generate_interpreter = true; }\n\n");
  const auto executionSemantics = prepare(invalidExecution);
  const auto executionOutcome = coge::tryPrepareGeneration(
      coge::DocumentAccess::legacyInput(
          *parser.parse(invalidExecution, lexer).root),
      agsem::semanticContextType(*executionSemantics));
  if (executionOutcome.value || executionOutcome.diagnostics.size() != 1 ||
      executionOutcome.diagnostics.front().code != "coge.invalid_contract" ||
      !executionOutcome.diagnostics.front().location)
    return 23;
  try {
    static_cast<void>(agsem::prepareGenerationModel(
        *executionSemantics, *parser.parse(invalidExecution, lexer).root));
    return 21;
  } catch (const std::runtime_error &error) {
    if (std::string_view{error.what()} !=
        "generate_interpreter needs execution_contract")
      return 22;
  }
  auto invalid = source;
  const std::string_view original = "return found != none;";
  const auto at = invalid.find(original);
  if (at == std::string::npos)
    return 4;
  invalid.replace(at, original.size(), "return found and none;");
  try {
    static_cast<void>(prepare(invalid));
    return 5;
  } catch (const std::runtime_error &error) {
    if (std::string_view{error.what()} != "and requires Bool operands")
      return 6;
  }
  auto invalidAnalysis = source;
  const std::string_view analysisStatement =
      "let symbol = lookup_lexical(scope, name.text);";
  const auto analysisAt = invalidAnalysis.find(analysisStatement);
  if (analysisAt == std::string::npos)
    return 7;
  invalidAnalysis.replace(
      analysisAt, analysisStatement.size(),
      "let symbol = lookup_lexical(scope, name.text) and scope;");
  try {
    static_cast<void>(prepare(invalidAnalysis));
    return 8;
  } catch (const std::runtime_error &error) {
    if (std::string_view{error.what()} != "and requires Bool operands")
      return 9;
  }
  // Two independent invalid bodies must be reported by preparation together.
  auto invalidBoth = source;
  invalidBoth.replace(at, original.size(), "return found and none;");
  const auto secondAt = invalidBoth.find(analysisStatement);
  if (secondAt == std::string::npos)
    return 10;
  invalidBoth.replace(
      secondAt, analysisStatement.size(),
      "let symbol = lookup_lexical(scope, name.text) and scope;");
  try {
    static_cast<void>(prepare(invalidBoth));
    return 11;
  } catch (const agsem::SemanticPreparationError &error) {
    const auto &diagnostics = error.diagnostics();
    if (diagnostics.size() != 2 ||
        std::string_view{error.what()} != "and requires Bool operands" ||
        diagnostics[0].subject != "contains_local" ||
        diagnostics[1].subject != "program#1" ||
        diagnostics[0].code != "action.invalid_function" ||
        diagnostics[1].code != "action.invalid_alternative")
      return 12;
    for (const auto &diagnostic : diagnostics)
      if (diagnostic.severity != agsem::Severity::Error ||
          diagnostic.message != "and requires Bool operands" ||
          !diagnostic.location || diagnostic.location->source != 0 ||
          diagnostic.location->beginByte >= diagnostic.location->endByte ||
          diagnostic.location->endByte > invalidBoth.size())
        return 13;
  }
  const auto parsedBoth = parser.parse(invalidBoth, lexer);
  const auto grammarBoth = grammarFrontend.parse(grammarSource);
  if (!parsedBoth.accepted() || !grammarBoth.accepted())
    return 17;
  const auto ownedInput = agsem::SemanticInputAccess::fromAst(
      *parsedBoth.root, *grammarBoth.document);
  const auto outcome = agsem::tryPrepareSemanticModel(ownedInput);
  if (outcome.value || outcome.diagnostics.size() != 2 ||
      outcome.diagnostics[0].subject != "contains_local" ||
      outcome.diagnostics[1].subject != "program#1")
    return 18;
  return 0;
}
