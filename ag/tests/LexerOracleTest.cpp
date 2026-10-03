#include "agas/bootstrap/AntlrFrontend.h"
#include "agas/generator/LexerGeneration.h"
#include "agas/model/BnfLowering.h"
#include "agas/model/Validation.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string{message});
}

auto readFile(const std::filesystem::path &path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot read " + path.string());
  }
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

void compareTokens(const agas::generator::GeneratedLexerAutomaton &lexer,
                   const zbik::Grammar &grammar, std::string_view source,
                   std::string_view label) {
  const agas::bootstrap::BootstrapLexResult oracle =
      agas::bootstrap::lexAgasWithBootstrapAntlr(source);
  if (!oracle.accepted()) {
    throw std::runtime_error(std::string{label} +
                             ": ANTLR oracle rejected valid input");
  }
  const agas::generator::AgasLexResult actual = lexer.tokenize(source);
  if (actual.tokens.size() != oracle.tokens.size()) {
    throw std::runtime_error(
        std::string{label} + ": token count differs: own=" +
        std::to_string(actual.tokens.size()) +
        " ANTLR=" + std::to_string(oracle.tokens.size()));
  }
  for (std::size_t index = 0; index < actual.tokens.size(); ++index) {
    const agas::generator::AgasLexedToken &own = actual.tokens[index];
    const agas::bootstrap::BootstrapLexToken &antlr = oracle.tokens[index];
    const std::string &ownName = grammar.terminalName(own.terminal);
    if (ownName != antlr.name || own.channel != antlr.channel ||
        own.offset != antlr.byteOffset || own.text != antlr.text) {
      throw std::runtime_error(
          std::string{label} + ": token " + std::to_string(index) +
          " differs: own=" + ownName + "@" +
          std::to_string(own.offset) + " `" + own.text + "`, ANTLR=" +
          antlr.name + "@" + std::to_string(antlr.byteOffset) + " `" +
          antlr.text + "`");
    }
  }
}

} // namespace

int main() {
  try {
    const std::filesystem::path grammarPath = AGAS_TEST_GRAMMAR_FILE;
    const agas::bootstrap::ParseResult parsed =
        agas::bootstrap::parseAgasFile(grammarPath);
    require(parsed.accepted(), "cannot parse Ag.ag for the lexer oracle");
    require(agas::model::validateSyntaxModel(*parsed.document).valid(),
            "Ag.ag is invalid");
    const agas::model::BnfModel bnf =
        agas::model::lowerToBnf(*parsed.document);
    const agas::generator::GeneratedLexerAutomaton lexer =
        agas::generator::compileLexerAutomaton(*parsed.document, bnf);

    const std::vector<std::string> focusedInputs{
        "grammar Sample; node start : TOKEN_REF EOF;",
        "'ą😀' node lowerName TOKEN_NAME [a-zA-Z_0-9]",
        "/* first */ node // ą😀\ninline /* second */ fragment",
        R"('\n\u0105' '[\]\\\r\n]' grammar grammarName)",
    };
    for (std::size_t index = 0; index < focusedInputs.size(); ++index) {
      compareTokens(lexer, bnf.grammar(), focusedInputs[index],
                    "focused input " + std::to_string(index));
    }

    const std::filesystem::path corpusRoot = grammarPath.parent_path();
    std::vector<std::filesystem::path> corpus;
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(corpusRoot)) {
      if (entry.is_regular_file() && entry.path().extension() == ".ag") {
        corpus.push_back(entry.path());
      }
    }
    std::ranges::sort(corpus);
    for (const std::filesystem::path &path : corpus) {
      compareTokens(lexer, bnf.grammar(), readFile(path),
                    path.lexically_relative(corpusRoot).string());
    }
    require(corpus.size() >= 11, "the lexer corpus is unexpectedly small");

    const auto bothReject = [&lexer](std::string_view source) {
      const agas::bootstrap::BootstrapLexResult oracle =
          agas::bootstrap::lexAgasWithBootstrapAntlr(source);
      bool ownRejected = false;
      try {
        static_cast<void>(lexer.tokenize(source));
      } catch (const zbik::Utf8LexerError &) {
        ownRejected = true;
      }
      return !oracle.accepted() && ownRejected;
    };
    require(bothReject("'unterminated"),
            "both lexers must reject an incomplete string literal");
    require(bothReject("[unterminated"),
            "both lexers must reject an incomplete lexer character set");
    require(bothReject("`"),
            "both lexers must reject an unknown source character");
    std::string invalidUtf8 = "grammar X; ";
    invalidUtf8.append("\xC0\xAF", 2);
    require(bothReject(invalidUtf8),
            "both lexers must reject malformed UTF-8 input");

    const std::string_view unterminatedComment = "/* unterminated";
    require(agas::bootstrap::lexAgasWithBootstrapAntlr(unterminatedComment)
                .accepted(),
            "the pinned ANTLR bootstrap intentionally accepts a comment at "
            "EOF");
    bool ownRejectedComment = false;
    try {
      static_cast<void>(lexer.tokenize(unterminatedComment));
    } catch (const zbik::Utf8LexerError &) {
      ownRejectedComment = true;
    }
    require(ownRejectedComment,
            "Ag.ag intentionally requires an explicit comment terminator");

    std::cout << "ANTLR lexer oracle files=" << corpus.size()
              << " focused=" << focusedInputs.size()
              << " invalid=4 documented_differences=1 mismatches=0\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
