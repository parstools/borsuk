#include "agas/runtime/PackagedAgFrontend.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string{message});
}

auto readFile(const std::filesystem::path &path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read packaged frontend input");
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

} // namespace

int main() {
  try {
    const agas::runtime::PackagedAgFrontend frontend{AGAS_PINNED_ARTIFACT_DIR};

    const auto valid = frontend.parse(readFile(AGAS_TEST_GRAMMAR_FILE));
    require(valid.accepted(), "pinned frontend must parse Ag.ag");
    require(valid.document->grammarName == "Ag",
            "pinned frontend returned the wrong grammar");

    const auto syntaxError = frontend.parse("grammar Broken; start node: ;");
    require(!syntaxError.accepted() && syntaxError.issues.size() == 1,
            "pinned frontend must reject malformed syntax");
    require(syntaxError.issues.front().kind ==
                agas::runtime::AgFrontendIssueKind::Syntax,
            "malformed syntax must produce a syntax issue");
    require(syntaxError.issues.front().state.has_value() &&
                syntaxError.issues.front().lookahead.has_value() &&
                !syntaxError.issues.front().expected.empty(),
            "syntax issue must preserve LR diagnostics");

    std::string invalidUtf8{"grammar Broken;\n"};
    invalidUtf8.push_back(static_cast<char>(0xC0));
    const auto lexicalError = frontend.parse(invalidUtf8);
    require(!lexicalError.accepted() && lexicalError.issues.size() == 1,
            "pinned frontend must reject invalid UTF-8");
    require(lexicalError.issues.front().kind ==
                agas::runtime::AgFrontendIssueKind::Lexical,
            "invalid UTF-8 must produce a lexical issue");

    std::cout << "packaged frontend grammar=" << valid.document->grammarName
              << " states="
              << frontend.package().parserTable.actionStateRows.size() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
