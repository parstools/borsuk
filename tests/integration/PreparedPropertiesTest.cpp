#include "PreparedSemanticModel.h"
#include "agas/artifact/ArtifactPackage.h"
#include "agas/runtime/ArtifactAstParser.h"
#include "agas/runtime/ArtifactLexerRuntime.h"
#include "agas/runtime/PackagedAgFrontend.h"

#include <fstream>
#include <iterator>
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
  const auto grammar = grammarFrontend.parse(readFile(argv[4]));
  if (!grammar.accepted())
    return 3;
  const auto prepare = [&](const std::string &text) {
    const auto parsed = parser.parse(text, lexer);
    if (!parsed.accepted())
      throw std::runtime_error("property fixture did not parse");
    return agsem::prepareSemanticModel(*parsed.root, *grammar.document);
  };
  const auto valid = prepare(source);
  const auto generation =
      agsem::prepareGenerationModel(*valid, *parser.parse(source, lexer).root);
  if (!generation.properties() || generation.properties()->checks.size() != 9)
    return 4;
  const auto first = agsem::emitRust(*valid, generation).properties;
  if (first.empty() || first != agsem::emitRust(*valid, generation).properties)
    return 5;
  auto invalid = source;
  const std::string_view call = "expect checked_add_i32(a, b) ==";
  const auto at = invalid.find(call);
  if (at == std::string::npos)
    return 6;
  invalid.replace(at + 7, std::string_view{"checked_add_i32"}.size(),
                  "missing_prop");
  const auto validSemantics = prepare(invalid);
  try {
    static_cast<void>(agsem::prepareGenerationModel(
        *validSemantics, *parser.parse(invalid, lexer).root));
    return 7;
  } catch (const std::runtime_error &error) {
    if (std::string_view{error.what()} !=
        "unknown property function: missing_prop")
      return 8;
  }
  auto invalidState = source;
  const std::string_view state = "runtime_state runtime: Runtime;";
  const auto stateAt = invalidState.find(state);
  if (stateAt == std::string::npos)
    return 9;
  invalidState.replace(stateAt, state.size(),
                       "runtime_state runtime: MissingState;");
  const auto stateSemantics = prepare(invalidState);
  try {
    static_cast<void>(agsem::prepareGenerationModel(
        *stateSemantics, *parser.parse(invalidState, lexer).root));
    return 10;
  } catch (const std::runtime_error &error) {
    if (std::string_view{error.what()} !=
        "runtime_state must refer to an execution record: MissingState")
      return 11;
  }
  auto invalidSignature = source;
  const std::string_view signature =
      "function checked_add_i32(left: I32, right: I32, source: SourceRange) -> "
      "Result<I32, RuntimeError>";
  const auto signatureAt = invalidSignature.find(signature);
  if (signatureAt == std::string::npos)
    return 12;
  invalidSignature.replace(signatureAt, signature.size(),
                           "function checked_add_i32(left: I32, right: I32, "
                           "source: SourceRange) -> I32");
  const auto signatureSemantics = prepare(invalidSignature);
  try {
    static_cast<void>(agsem::prepareGenerationModel(
        *signatureSemantics, *parser.parse(invalidSignature, lexer).root));
    return 13;
  } catch (const std::runtime_error &error) {
    if (std::string_view{error.what()} !=
        "execution function must return Result: checked_add_i32")
      return 14;
  }
  auto invalidBody = source;
  const std::string_view checkedFunction =
      "function checked_add_i32(left: I32, right: I32, source: SourceRange)";
  const auto functionAt = invalidBody.find(checkedFunction);
  if (functionAt == std::string::npos)
    return 15;
  const auto returnAt = invalidBody.find("return value;", functionAt);
  if (returnAt == std::string::npos)
    return 16;
  invalidBody.replace(returnAt, std::string_view{"return value;"}.size(),
                      "return missing_value;");
  const auto bodySemantics = prepare(invalidBody);
  try {
    static_cast<void>(agsem::prepareGenerationModel(
        *bodySemantics, *parser.parse(invalidBody, lexer).root));
    return 17;
  } catch (const std::runtime_error &error) {
    if (std::string_view{error.what()} !=
        "unbound execution value: missing_value")
      return 18;
  }
  return 0;
}
