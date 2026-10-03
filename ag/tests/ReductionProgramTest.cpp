#include "agas/generator/ReductionProgram.h"
#include "agas/bootstrap/AntlrFrontend.h"
#include "agas/generator/ParserGeneration.h"
#include "agas/model/BnfLowering.h"
#include "agas/model/Validation.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string{message});
}

auto findInstruction(const agas::generator::AstReductionProgram &program,
                     const agas::model::BnfModel &bnf,
                     std::size_t sourceRuleIndex,
                     zbik::EbnfGeneratedRuleRole role,
                     std::optional<zbik::Repetition> repetition = std::nullopt)
    -> const agas::generator::ReductionInstruction & {
  for (const zbik::Rule &rule : bnf.grammar().rules()) {
    const auto &origin = bnf.origin(rule.id());
    if (origin.role == role && origin.sourceRuleIndex == sourceRuleIndex &&
        (!repetition.has_value() || origin.repetition == *repetition)) {
      return program.instruction(rule.id());
    }
  }
  throw std::runtime_error("missing reduction instruction");
}

} // namespace

int main() {
  try {
    const auto parsed = agas::bootstrap::parseAgas(
        "grammar Reductions; "
        "node start : wrapped=wrapper forwarded=forwarding "
        "punctuation optional=atom? many=atom* some=atom+ blank=blank EOF "
        "#Root ; "
        "inline wrapper : left=LPAREN value=atom right=RPAREN ; "
        "inline forwarding : value=atom ; "
        "inline punctuation : LPAREN RPAREN ; "
        "node atom : value=ID ; "
        "node blank : empty ; "
        "LPAREN : '(' ; RPAREN : ')' ; ID : 'i' ;");
    require(parsed.accepted(), "reduction fixture must parse");
    require(agas::model::validateSyntaxModel(*parsed.document).valid(),
            "reduction fixture must validate");

    const agas::model::BnfModel bnf = agas::model::lowerToBnf(*parsed.document);
    const agas::generator::AstReductionProgram program =
        agas::generator::buildAstReductionProgram(*parsed.document, bnf);
    require(program.instructions().size() == bnf.grammar().ruleCount(),
            "every BNF production must have one reduction instruction");

    const auto &start = findInstruction(
        program, bnf, 0, zbik::EbnfGeneratedRuleRole::SourceAlternative);
    require(start.opcode == agas::generator::ReductionOpcode::ConstructNode &&
                start.typeName == "start" && start.variantName == "Root" &&
                start.fields.size() == 6 &&
                start.spanPolicy ==
                    agas::generator::ReductionSpanPolicy::MatchedRhs,
            "node reductions must retain their type, variant, fields and span");
    require(start.fields.front().name == "wrapped" &&
                start.fields.front().rhsIndex == 0 &&
                start.fields.back().name == "blank" &&
                start.fields.back().rhsIndex == 6,
            "source fields must address the lowered BNF RHS and skip EOF");

    const auto &wrapper = findInstruction(
        program, bnf, 1, zbik::EbnfGeneratedRuleRole::SourceAlternative);
    require(wrapper.opcode ==
                    agas::generator::ReductionOpcode::ConstructRecord &&
                wrapper.typeName == "wrapper" && wrapper.fields.size() == 3,
            "multi-field inline rules must build technical records");
    const auto &forwarding = findInstruction(
        program, bnf, 2, zbik::EbnfGeneratedRuleRole::SourceAlternative);
    require(forwarding.opcode == agas::generator::ReductionOpcode::Forward &&
                forwarding.operands == std::vector<std::uint32_t>{0},
            "single-field inline rules must forward their labelled value");
    const auto &punctuation = findInstruction(
        program, bnf, 3, zbik::EbnfGeneratedRuleRole::SourceAlternative);
    require(punctuation.opcode == agas::generator::ReductionOpcode::Unit,
            "fieldless inline rules must produce unit");
    const auto &blank = findInstruction(
        program, bnf, 5, zbik::EbnfGeneratedRuleRole::SourceAlternative);
    require(blank.opcode == agas::generator::ReductionOpcode::ConstructNode &&
                blank.fields.empty() &&
                blank.spanPolicy ==
                    agas::generator::ReductionSpanPolicy::EmptyAtLookahead,
            "empty node rules must still construct a point-spanned node");

    const auto &optionalPresent = findInstruction(
        program, bnf, 0, zbik::EbnfGeneratedRuleRole::OptionalPresent,
        zbik::Repetition::Optional);
    const auto &optionalEmpty = findInstruction(
        program, bnf, 0, zbik::EbnfGeneratedRuleRole::OptionalEmpty,
        zbik::Repetition::Optional);
    require(optionalPresent.opcode ==
                    agas::generator::ReductionOpcode::OptionalSome &&
                optionalEmpty.opcode ==
                    agas::generator::ReductionOpcode::OptionalNone &&
                optionalEmpty.spanPolicy ==
                    agas::generator::ReductionSpanPolicy::EmptyAtLookahead,
            "optional helpers must distinguish present and absent values");
    const auto &starBase = findInstruction(
        program, bnf, 0, zbik::EbnfGeneratedRuleRole::RepetitionBase,
        zbik::Repetition::ZeroOrMore);
    const auto &plusBase = findInstruction(
        program, bnf, 0, zbik::EbnfGeneratedRuleRole::RepetitionBase,
        zbik::Repetition::OneOrMore);
    require(starBase.opcode == agas::generator::ReductionOpcode::ListEmpty &&
                plusBase.opcode ==
                    agas::generator::ReductionOpcode::ListSingleton,
            "star and plus base reductions must retain different semantics");
    const auto &starRecursive = findInstruction(
        program, bnf, 0, zbik::EbnfGeneratedRuleRole::RepetitionRecursive,
        zbik::Repetition::ZeroOrMore);
    require(starRecursive.opcode ==
                    agas::generator::ReductionOpcode::ListAppend &&
                starRecursive.operands == std::vector<std::uint32_t>({0, 1}),
            "recursive repetition reductions must append in source order");

    const auto generated =
        agas::generator::generateParserTable(*parsed.document);
    require(generated.reductions().instructions() == program.instructions(),
            "generated parser tables must own the neutral reduction program");

    bool rejectedBadOperand = false;
    try {
      static_cast<void>(agas::generator::AstReductionProgram{
          {{zbik::RuleId{0},
            1,
            agas::generator::ReductionOpcode::Forward,
            agas::generator::ReductionSpanPolicy::MatchedRhs,
            std::nullopt,
            std::nullopt,
            {1},
            {}}}});
    } catch (const std::invalid_argument &) {
      rejectedBadOperand = true;
    }
    require(rejectedBadOperand,
            "program validation must reject operands outside the BNF RHS");

    std::cout << "AST reduction instructions=" << program.instructions().size()
              << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
