#include "agas/runtime/ArtifactAstParser.h"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string{message});
}

auto symbols() -> agas::artifact::ArtifactSymbols {
  return {.version = 1,
          .terminals = {{0, "id"}},
          .nonterminals = {{0, "document"}},
          .channels = {}};
}

auto productions() -> agas::artifact::ArtifactProductions {
  return {.version = 1,
          .productions = {
              {0, 0, {{agas::artifact::ArtifactSymbolKind::Terminal, 0}}},
          }};
}

auto table(bool includeGoto = true) -> agas::artifact::ArtifactParserTable {
  using namespace agas::artifact;
  ArtifactGotoRow initialGoto{0, {}};
  if (includeGoto)
    initialGoto.entries.push_back({0, 2});
  return {
      .algorithm = ArtifactParserAlgorithm::Lr,
      .lookahead = 1,
      .startState = 0,
      .actionRows =
          {
              {0, {{{{0}}, ArtifactShift{1}}}, std::nullopt},
              {1, {}, ArtifactReduce{0}},
              {2, {{{{std::nullopt}}, ArtifactAccept{}}}, std::nullopt},
          },
      .actionStateRows = {0, 1, 2},
      .gotoRows = {std::move(initialGoto), {1, {}}},
      .gotoStateRows = {0, 1, 1},
  };
}

auto reductions() -> agas::artifact::ArtifactReductions {
  using namespace agas::generator;
  return {1, AstReductionProgram{{
                 {zbik::RuleId{0},
                  1,
                  ReductionOpcode::Forward,
                  ReductionSpanPolicy::MatchedRhs,
                  std::nullopt,
                  std::nullopt,
                  {0},
                  {}},
             }}};
}

auto tokenInput() -> agas::runtime::ArtifactLexResult {
  return {{{0, std::nullopt, 0, "x"}}, {0}};
}

} // namespace

int main() {
  try {
    const agas::runtime::ArtifactAstParser parser{table(), symbols(),
                                                  productions(), reductions()};
    const auto accepted = parser.parse(tokenInput(), 1);
    require(accepted.accepted() &&
                accepted.root->kind == agas::runtime::AstValueKind::Token &&
                accepted.root->tokenText == "x" &&
                accepted.root->sourceSpan == agas::runtime::InputSpan{0, 1},
            "artifact parser must execute shift, default reduction and accept");

    const auto rejected = parser.parse({}, 0);
    require(!rejected.accepted() && rejected.error &&
                rejected.error->state == 0 &&
                rejected.error->lookahead.size() == 1 &&
                !rejected.error->lookahead.front().terminal,
            "artifact parser must report a syntax error on EOF");

    const agas::runtime::ArtifactAstParser bounded{
        table(), symbols(), productions(), reductions(), {2, 16}};
    bool reachedStepLimit = false;
    try {
      static_cast<void>(bounded.parse(tokenInput(), 1));
    } catch (const agas::runtime::ArtifactParserRuntimeError &error) {
      reachedStepLimit =
          error.code() ==
          agas::runtime::ArtifactParserRuntimeErrorCode::ResourceLimit;
    }
    require(reachedStepLimit, "artifact parser must enforce its step limit");

    const agas::runtime::ArtifactAstParser missingGoto{
        table(false), symbols(), productions(), reductions()};
    bool rejectedMissingGoto = false;
    try {
      static_cast<void>(missingGoto.parse(tokenInput(), 1));
    } catch (const agas::runtime::ArtifactParserRuntimeError &error) {
      rejectedMissingGoto =
          error.code() ==
          agas::runtime::ArtifactParserRuntimeErrorCode::InvalidTableExecution;
    }
    require(rejectedMissingGoto,
            "artifact parser must reject a missing runtime GOTO");

    std::cout << "artifact AST parser tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
