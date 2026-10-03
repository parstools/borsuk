#include "agas/artifact/ArtifactPackage.h"
#include "agas/artifact/Sha256.h"
#include "agas/runtime/ArtifactAstParser.h"
#include "agas/runtime/ArtifactLexerRuntime.h"
#include "agas/runtime/AstWireJson.h"
#include "agas/runtime/AgSyntaxAdapter.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

auto readFile(const std::filesystem::path &path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read parity fixture");
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string{message});
}

void number(std::string &out, std::uint64_t value) {
  out += std::to_string(value);
  out += ';';
}

void bytes(std::string &out, std::string_view value) {
  out += std::to_string(value.size());
  out += ':';
  out += value;
}

void astBytes(std::string &out, const agas::runtime::AstValue &value) {
  number(out, static_cast<unsigned>(value.kind));
  number(out, value.sourceSpan.beginByte);
  number(out, value.sourceSpan.endByte);
  bytes(out, value.typeName);
  bytes(out, value.variantName);
  number(out, value.tokenKind);
  bytes(out, value.tokenText);
  number(out, value.fieldNames.size());
  for (const auto &field : value.fieldNames)
    bytes(out, field);
  number(out, value.elements.size());
  for (const auto &element : value.elements)
    astBytes(out, element);
}

auto tokenDigest(const agas::runtime::ArtifactLexResult &lexed)
    -> std::string {
  std::string out;
  number(out, lexed.tokens.size());
  for (const auto &token : lexed.tokens) {
    number(out, token.terminal);
    number(out, token.channel ? std::uint64_t{*token.channel} + 1 : 0);
    number(out, token.offset);
    number(out, token.offset + token.text.size());
    bytes(out, token.text);
  }
  number(out, lexed.parserTerminalIds.size());
  for (const auto id : lexed.parserTerminalIds)
    number(out, id);
  return agas::artifact::sha256Hex(out);
}

} // namespace

int main(int argc, char **argv) {
  try {
    const auto package = agas::artifact::loadArtifactPackageDirectory(
        AGAS_PINNED_ARTIFACT_DIR);
    const agas::runtime::ArtifactLexerRuntime lexer{
        package.lexer, package.symbols.terminals.size(),
        package.symbols.channels.size()};
    const agas::runtime::ArtifactAstParser parser{
        package.parserTable, package.symbols, package.productions,
        package.reductions};
    const std::string source = readFile(AGAS_TEST_GRAMMAR_FILE);
    const auto lexed = lexer.tokenize(source);
    const auto parsed = parser.parse(lexed, source.size());
    if (!parsed.accepted())
      throw std::runtime_error("Ag.ag was rejected");
    const auto sectionHash = [&](agas::artifact::ArtifactSectionKind kind)
        -> std::string_view {
      for (const auto &section : package.manifest.sections)
        if (section.kind == kind)
          return section.sha256;
      throw std::runtime_error("missing AST wire catalog hash");
    };
    const agas::runtime::AstWireContext wireContext{
        package.astSchema.version,
        sectionHash(agas::artifact::ArtifactSectionKind::Symbols),
        sectionHash(agas::artifact::ArtifactSectionKind::AstSchema),
        package.symbols.terminals.size(),
        "Ag.ag",
        source};
    const auto pureDocument = agas::runtime::adaptAgSyntaxDocument(
        *parsed.root, source, package.symbols);
    const auto wire = agas::runtime::dumpAstWireJson(*parsed.root, wireContext);
    const auto restored = agas::runtime::parseAstWireJson(wire, wireContext);
    require(restored == *parsed.root, "C++ AST wire round-trip changed the AST");
    require(agas::runtime::adaptAgSyntaxDocument(restored, source,
                                                 package.symbols) ==
                pureDocument,
            "C++ AST wire changed SyntaxDocument");
    if (argc == 2 && std::string_view{argv[1]} == "--from-stdin") {
      const std::string rustWire{std::istreambuf_iterator<char>{std::cin},
                                 std::istreambuf_iterator<char>{}};
      const auto rustRoot = agas::runtime::parseAstWireJson(rustWire, wireContext);
      require(agas::runtime::adaptAgSyntaxDocument(
                  rustRoot, source, package.symbols) == pureDocument,
              "Rust AST wire changed SyntaxDocument");
      require(rustWire == wire, "Rust and C++ AST wire bytes differ");
      std::cout << "cross_language_ok\n";
      return 0;
    }
    require(argc == 1, "unknown AST parity test arguments");
    auto badVersion = wire;
    badVersion.replace(badVersion.find("\"wireVersion\": 1"),
                       std::string{"\"wireVersion\": 1"}.size(),
                       "\"wireVersion\": 2");
    const auto rejects = [&](std::string_view candidate,
                             std::string_view candidateSource = {}) {
      try {
        auto context = wireContext;
        context.source = candidateSource.empty() ? std::string_view{source}
                                                 : candidateSource;
        static_cast<void>(agas::runtime::parseAstWireJson(candidate, context));
        return false;
      } catch (const std::invalid_argument &) {
        return true;
      }
    };
    require(rejects(badVersion), "C++ AST wire accepted a wrong version");
    require(rejects(wire.substr(0, wire.size() / 2)),
            "C++ AST wire accepted truncated JSON");
    require(rejects(wire, "grammar Changed;"),
            "C++ AST wire accepted a different source");
    auto duplicate = wire;
    duplicate.replace(duplicate.find("\"wireVersion\": 1"),
                      std::string{"\"wireVersion\": 1"}.size(),
                      "\"wireVersion\": 1, \"wireVersion\": 1");
    require(rejects(duplicate), "C++ AST wire accepted a duplicate field");
    auto unknown = wire;
    unknown.replace(unknown.find("\"wireVersion\": 1"),
                    std::string{"\"wireVersion\": 1"}.size(),
                    "\"wireVersion\": 1, \"unknown\": 1");
    require(rejects(unknown), "C++ AST wire accepted an unknown field");
    auto wrongTokenText = wire;
    const auto tokenText = wrongTokenText.find("\"tokenText\": \"Ag\"");
    require(tokenText != std::string::npos, "missing token text in AST wire");
    wrongTokenText.replace(tokenText,
                           std::string{"\"tokenText\": \"Ag\""}.size(),
                           "\"tokenText\": \"broken\"");
    require(rejects(wrongTokenText), "C++ AST wire accepted wrong token text");
    auto wrongCatalog = wireContext;
    wrongCatalog.symbolsSha256 =
        "0000000000000000000000000000000000000000000000000000000000000000";
    bool rejectedCatalog = false;
    try {
      static_cast<void>(agas::runtime::parseAstWireJson(wire, wrongCatalog));
    } catch (const std::invalid_argument &) {
      rejectedCatalog = true;
    }
    require(rejectedCatalog, "C++ AST wire accepted the wrong symbol catalog");
    std::string ast;
    astBytes(ast, *parsed.root);
    const auto tokensHash = tokenDigest(lexed);
    const auto astHash = agas::artifact::sha256Hex(ast);
    require(tokensHash ==
                "e22559bb112d15330cd1ab7b48feacbe45afce0a92292dfd775e85744ff3f7de",
            "C++ token snapshot changed");
    require(astHash ==
                "c2db1469a4967e4219726fde73790c9702ef08efae162ad4ce7bbcf2eb1673ed",
            "C++ neutral AST snapshot changed");
    require(lexed.tokens.size() == 782 &&
                lexed.parserTerminalIds.size() == 782,
            "C++ token counts changed");
    std::cout << "tokens=" << tokensHash << '\n';
    std::cout << "ast=" << astHash << '\n';
    std::cout << "token_count=" << lexed.tokens.size() << '\n';
    std::cout << "parser_token_count=" << lexed.parserTerminalIds.size() << '\n';

    const std::string malformed = "grammar Broken; start node: ;";
    const auto bad = lexer.tokenize(malformed);
    const auto syntax = parser.parse(bad, malformed.size());
    if (syntax.accepted())
      throw std::runtime_error("malformed Ag source was accepted");
    require(syntax.error->state == 4 && syntax.error->tokenIndex == 2 &&
                syntax.error->span.beginByte == 14 &&
                syntax.error->span.endByte == 15 &&
                syntax.error->expected.size() == 9 &&
                syntax.error->lookahead.size() == 2 &&
                syntax.error->lookahead[0].terminal == 27 &&
                syntax.error->lookahead[1].terminal == 17,
            "C++ syntax error snapshot changed");
    std::cout << "syntax_state=" << syntax.error->state << '\n';
    std::cout << "syntax_token=" << syntax.error->tokenIndex << '\n';
    std::cout << "syntax_begin=" << syntax.error->span.beginByte << '\n';
    std::cout << "syntax_end=" << syntax.error->span.endByte << '\n';
    std::cout << "syntax_lookahead=";
    for (const auto &symbol : syntax.error->lookahead)
      std::cout << (symbol.terminal ? std::to_string(*symbol.terminal) : "EOF")
                << ',';
    std::cout << '\n';
    std::cout << "syntax_expected=" << syntax.error->expected.size() << '\n';

    std::string invalidUtf8 = "grammar Broken;\n";
    invalidUtf8.push_back(static_cast<char>(0xc0));
    try {
      static_cast<void>(lexer.tokenize(invalidUtf8));
      throw std::runtime_error("invalid UTF-8 was accepted");
    } catch (const agas::runtime::ArtifactLexerError &error) {
      require(error.kind() ==
                  agas::runtime::ArtifactLexerErrorKind::InvalidEncoding &&
                  error.tokenStart() == 16 && error.errorOffset() == 17,
              "C++ lexical error snapshot changed");
      std::cout << "lex_kind=" << static_cast<unsigned>(error.kind()) << '\n';
      std::cout << "lex_start=" << error.tokenStart() << '\n';
      std::cout << "lex_offset=" << error.errorOffset() << '\n';
    }
    try {
      static_cast<void>(lexer.tokenize("grammar Broken;\n@"));
      throw std::runtime_error("unmatched character was accepted");
    } catch (const agas::runtime::ArtifactLexerError &error) {
      require(error.kind() ==
                  agas::runtime::ArtifactLexerErrorKind::NoMatchingRule &&
                  error.tokenStart() == 16 && error.errorOffset() == 16,
              "C++ unmatched-character snapshot changed");
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
