#include "agas/artifact/ParserTableSection.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view validTable = R"DSL(compressed-table "LR(2)" {
  start-state 0;

  action-row 0 {
    ["id", "EOF"] => shift 1;
    ["EOF", EOF] => reduce 0;
    any => error;
  }
  action-row 1 {
    [EOF] => accept;
    any => reduce 0;
  }

  action-state-rows [0, 1];

  goto-row 0 {
    "S" => 1;
  }
  goto-row 1 {
  }

  goto-state-rows [0, 1];
}
)DSL";

auto symbols() -> agas::artifact::ArtifactSymbols {
  return {.version = 1,
          .terminals = {{0, "id"}, {1, "EOF"}},
          .nonterminals = {{0, "S"}},
          .channels = {}};
}

auto productions() -> agas::artifact::ArtifactProductions {
  using agas::artifact::ArtifactSymbolKind;
  return {.version = 1,
          .productions = {
              {0, 0, {{ArtifactSymbolKind::Terminal, 0}}},
          }};
}

void expectError(std::string text,
                 agas::artifact::ParserTableSectionErrorCode expected) {
  try {
    static_cast<void>(
        agas::artifact::parseParserTableDsl(text, symbols(), productions()));
  } catch (const agas::artifact::ParserTableSectionError &error) {
    if (error.code() == expected)
      return;
    throw std::runtime_error("parser table reported the wrong error code");
  }
  throw std::runtime_error("invalid parser table was accepted");
}

auto replaceOnce(std::string text, std::string_view oldText,
                 std::string_view newText) -> std::string {
  const std::size_t offset = text.find(oldText);
  if (offset == std::string::npos)
    throw std::runtime_error("invalid parser-table test fixture");
  text.replace(offset, oldText.size(), newText);
  return text;
}

} // namespace

int main() {
  try {
    const auto loaded = agas::artifact::parseParserTableDsl(
        validTable, symbols(), productions());
    if (loaded.algorithm != agas::artifact::ArtifactParserAlgorithm::Lr ||
        loaded.lookahead != 2 || loaded.startState != 0 ||
        loaded.actionRows.size() != 2 || loaded.gotoRows.size() != 2 ||
        agas::artifact::dumpParserTableDsl(loaded, symbols(), productions()) !=
            validTable)
      throw std::runtime_error("parser-table canonical round-trip failed");
    auto reordered = loaded;
    std::ranges::reverse(reordered.actionRows.front().entries);
    if (agas::artifact::dumpParserTableDsl(reordered, symbols(),
                                           productions()) != validTable)
      throw std::runtime_error("parser-table dump is not canonical");

    expectError(replaceOnce(std::string{validTable}, "\"id\"", "\"missing\""),
                agas::artifact::ParserTableSectionErrorCode::InvalidReference);
    expectError(replaceOnce(std::string{validTable}, "[\"id\", \"EOF\"]",
                            "[EOF, \"id\"]"),
                agas::artifact::ParserTableSectionErrorCode::InvalidReference);
    expectError(std::string{validTable} + "trailing",
                agas::artifact::ParserTableSectionErrorCode::InvalidDsl);
    expectError(replaceOnce(std::string{validTable}, "\"LR(2)\"", "\"LR(65)\""),
                agas::artifact::ParserTableSectionErrorCode::ResourceLimit);
    expectError(
        replaceOnce(std::string{validTable}, "action-row 1", "action-row 0"),
        agas::artifact::ParserTableSectionErrorCode::InvalidId);

    std::cout << "parser-table section tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
