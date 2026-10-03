#include "agas/bootstrap/AntlrFrontend.h"
#include "agas/runtime/PackagedAgFrontend.h"
#include "agas/generator/ContextualGeneration.h"
#include "agas/generator/ArtifactGeneration.h"
#include "agas/runtime/ArtifactAstParser.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

void require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}
std::string read(const std::string &name) {
  std::ifstream input(std::string(AGAS_CONTEXT_FIXTURES) + "/" + name + ".ag");
  std::ostringstream output;
  output << input.rdbuf();
  return output.str();
}
int main() {
  try {
    const agas::runtime::PackagedAgFrontend frontend{AGAS_CONTEXT_PACKAGE};
    for (const auto &name : {"Shift", "GatedBrackets", "Keywords", "Combined", "Nested", "BadContext"}) {
      const auto text = read(name);
      const auto bootstrap = agas::bootstrap::parseAgas(text);
      const auto parsed = frontend.parse(text);
      require(parsed.accepted() && bootstrap.accepted(), std::string(name) + " frontend acceptance");
      require(*parsed.document == *bootstrap.document, std::string(name) + " frontend parity");
      for (const auto algorithm : {"LR", "LALR"}) for (const auto k : {"1", "2"}) {
        auto document = *parsed.document;
        for (auto &option : document.options) {
          if (option.name == "parser") option.value.spelling = algorithm;
          if (option.name == "lookahead") option.value.spelling = k;
        }
        auto generated = agas::generator::generateContextualParser(document);
        require(!generated.table.hasConflicts(), std::string(name) + " parse table conflict");
        if (std::string(name) == "BadContext" ||
            (std::string(name) == "Nested" && std::string(algorithm) == "LALR")) {
          bool rejected = false;
          try {
            const zbik::ContextualLRMachine machine{generated.table, generated.scoped.requirements,
                                                    generated.scoped.sourceTerminals};
          } catch (const zbik::LexerContextError &) { rejected = true; }
          require(rejected, "ambiguous lexer context must be rejected before parsing");
          continue;
        }
        const zbik::ContextualLRMachine machine{generated.table, generated.scoped.requirements,
                                                generated.scoped.sourceTerminals};
        machine.validateLexer(generated.lexer.rules());
        const auto package = agas::generator::buildContextualArtifactPackage(
            document, generated, text, {"test", "test", "test"});
        const auto loaded = agas::artifact::loadArtifactPackage(package.manifestJson, package.sections);
        require(loaded.lexer.version == 2 && loaded.lexer.context.has_value(), "contextual lexer v2");
        const agas::runtime::ArtifactLexerRuntime artifactLexer{loaded.lexer, loaded.symbols.terminals.size(), loaded.symbols.channels.size()};
        const agas::runtime::ArtifactAstParser artifactParser{loaded.parserTable, loaded.symbols, loaded.productions, loaded.reductions};
        auto check = [&](const std::string &input, std::initializer_list<const char *> expected) {
          const auto result = machine.parse(generated.lexer, input);
          require(result.accepted, input + ": " + result.error);
          require(result.tokens.size() == expected.size(), input + " token count");
          const auto artifactTokens = artifactLexer.tokenize(input, loaded.parserTable, loaded.productions);
          require(artifactParser.parse(input, artifactLexer).accepted(), input + " artifact AST acceptance");
          require(artifactTokens.parserTerminalIds.size() == expected.size(), input + " artifact token count");
          std::size_t artifactIndex = 0;
          for (const auto *terminal : expected) {
            const auto scoped = artifactTokens.parserTerminalIds[artifactIndex++];
            const auto original = loaded.lexer.context->originalTerminals[scoped];
            require(loaded.symbols.terminals[original].name == terminal, input + " artifact token identity");
          }
          std::size_t index = 0;
          for (const auto *terminal : expected)
            require(generated.source.grammar().terminalName(result.tokens[index++].terminal) == terminal,
                    input + " token identity");
        };
        if (std::string(name) == "Shift" || std::string(name) == "GatedBrackets") {
          check("a>>b", {"A", "SHR", "B"});
          check("c > > d", {"C", "GT", "GT", "D"});
        } else if (std::string(name) == "Keywords") {
          check("@read;", {"AT", "READ", "END"});
          check("@write;", {"AT", "WRITE", "END"});
          check("#reader;", {"HASH", "IDENT", "END"});
          check("#write;", {"HASH", "IDENT", "END"});
          require(!machine.parse(generated.lexer, "@reader;").accepted, "longest match must preserve reader");
        } else if (std::string(name) == "Combined") {
          check("0read>>;", {"ZERO", "IDENT", "GT", "GT", "END"});
          check("1write>>;", {"ONE", "WRITE", "GT", "GT", "END"});
          check("2read>>;", {"TWO", "IDENT", "SHR", "END"});
          check("3read>>;", {"THREE", "READ", "SHR", "END"});
        } else {
          check("read outer<read<int>>>>write;", {"READ", "IDENT", "LT", "IDENT", "LT", "IDENT", "GT", "GT", "SHR", "WRITE", "END"});
        }
      }
    }
    for (const auto &source : {
        "grammar Bad; lexerClasses { X = 3; } node start : A; A : 'a';",
        "grammar Bad; lexerClasses { X = false; } node start -> enable(X), disable(X) : A; A : 'a';",
        "grammar Bad; node start : A; A : 'a' -> require(UNKNOWN);",
        "grammar Bad; lexerClasses { X = false; X = true; } node start : A; A : 'a';"}) {
      const auto parsed = frontend.parse(source);
      require(parsed.accepted(), "invalid declarations must parse for semantic diagnostics");
      bool rejected = false;
      try { static_cast<void>(agas::generator::generateContextualParser(*parsed.document)); }
      catch (const agas::generator::ParserConfigurationError &) { rejected = true; }
      require(rejected, "invalid class declaration must be rejected");
    }
    std::string classes;
    for (int i = 0; i < 64; ++i)
      classes += "C" + std::to_string(i) + " = false; ";
    const auto high = frontend.parse("grammar High; lexerClasses { " + classes +
        " } node start -> enable(C63) : A; A : 'a' -> require(C63);");
    require(high.accepted(), "64 class declarations must parse");
    const auto generated = agas::generator::generateContextualParser(*high.document);
    const zbik::ContextualLRMachine machine{generated.table, generated.scoped.requirements,
                                            generated.scoped.sourceTerminals};
    require(machine.parse(generated.lexer, "a").accepted, "bit 63 must work without enumerating masks");
    const auto excessive = frontend.parse("grammar High; lexerClasses { " + classes +
        " EXTRA = false; } node start : A; A : 'a';");
    bool rejected = false;
    try { static_cast<void>(agas::generator::generateContextualParser(*excessive.document)); }
    catch (const agas::generator::ParserConfigurationError &) { rejected = true; }
    require(rejected, "65 classes must fail before shifting bits");
    std::cout << "Contextual .ag tests passed (both frontends, LR/LALR, k=1/2).\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
