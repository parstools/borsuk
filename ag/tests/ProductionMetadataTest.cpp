#include "agas/bootstrap/AntlrFrontend.h"
#include "agas/generator/ParserGeneration.h"
#include "agas/generator/ProductionMetadata.h"
#include "agas/model/BnfLowering.h"
#include "agas/model/Validation.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string{message});
}

} // namespace

int main() {
  try {
    const agas::bootstrap::ParseResult parsed = agas::bootstrap::parseAgas(
        "grammar Metadata; "
        "node start : maybe=a? many=b* some=c+ EOF #Full ; "
        "node a : A ; node b : B ; node c : C ; "
        "node duplicate : A | A ; "
        "A : 'a'; B : 'b'; C : 'c';");
    require(parsed.accepted(), "metadata fixture must parse");
    require(agas::model::validateSyntaxModel(*parsed.document).valid(),
            "metadata fixture must validate");
    const agas::model::BnfModel bnf =
        agas::model::lowerToBnf(*parsed.document);
    const agas::generator::ProductionMetadata metadata =
        agas::generator::buildProductionMetadata(*parsed.document, bnf);
    const agas::generator::ProductionMetadata repeated =
        agas::generator::buildProductionMetadata(*parsed.document, bnf);

    require(metadata.runtime().size() == 12 &&
                metadata.diagnostic().size() == 12,
            "six source alternatives and three EBNF helpers need metadata");
    require(metadata.runtime() == repeated.runtime() &&
                metadata.diagnostic() == repeated.diagnostic(),
            "production identities and metadata must be deterministic");

    std::unordered_set<std::string> identities;
    for (const zbik::Rule &rule : bnf.grammar().rules()) {
      const auto &runtime = metadata.runtime(rule.id());
      const auto &diagnostic = metadata.diagnostic(rule.id());
      require(runtime.rule == rule.id() && runtime.lhs == rule.lhs() &&
                  runtime.rhsLength == rule.size(),
              "runtime metadata must mirror the generated BNF production");
      require(identities.insert(diagnostic.stableIdentity).second,
              "stable production identities must be unique");
      require(diagnostic.sourceSpan.begin.offset <=
                  diagnostic.sourceSpan.end.offset,
              "diagnostic metadata must retain a valid source range");
    }

    const auto findHelper = [&](std::string_view field,
                                zbik::EbnfGeneratedRuleRole role)
        -> const agas::generator::DiagnosticProductionMetadata & {
      for (const auto &diagnostic : metadata.diagnostic()) {
        if (diagnostic.fieldName == field && diagnostic.role == role) {
          return diagnostic;
        }
      }
      throw std::runtime_error("missing helper metadata for field " +
                               std::string{field});
    };
    const auto &optionalPresent = findHelper(
        "maybe", zbik::EbnfGeneratedRuleRole::OptionalPresent);
    const auto &optionalEmpty =
        findHelper("maybe", zbik::EbnfGeneratedRuleRole::OptionalEmpty);
    const auto &starRecursive = findHelper(
        "many", zbik::EbnfGeneratedRuleRole::RepetitionRecursive);
    const auto &starBase =
        findHelper("many", zbik::EbnfGeneratedRuleRole::RepetitionBase);
    const auto &plusRecursive = findHelper(
        "some", zbik::EbnfGeneratedRuleRole::RepetitionRecursive);
    const auto &plusBase =
        findHelper("some", zbik::EbnfGeneratedRuleRole::RepetitionBase);

    require(optionalPresent.repetition == zbik::Repetition::Optional &&
                metadata.runtime(optionalPresent.rule).rhsLength == 1 &&
                metadata.runtime(optionalEmpty.rule).rhsLength == 0,
            "optional helper metadata must distinguish present and empty");
    require(starRecursive.repetition == zbik::Repetition::ZeroOrMore &&
                metadata.runtime(starRecursive.rule).rhsLength == 2 &&
                metadata.runtime(starBase.rule).rhsLength == 0,
            "star helper metadata must retain recursive and empty rules");
    require(plusRecursive.repetition == zbik::Repetition::OneOrMore &&
                metadata.runtime(plusRecursive.rule).rhsLength == 2 &&
                metadata.runtime(plusBase.rule).rhsLength == 1,
            "plus helper metadata must retain recursive and one-item rules");

    std::size_t duplicateAlternatives = 0;
    std::unordered_set<std::string> duplicateIdentities;
    for (const auto &diagnostic : metadata.diagnostic()) {
      if (diagnostic.sourceRuleName == "duplicate" &&
          diagnostic.role ==
              zbik::EbnfGeneratedRuleRole::SourceAlternative) {
        ++duplicateAlternatives;
        duplicateIdentities.insert(diagnostic.stableIdentity);
      }
    }
    require(duplicateAlternatives == 2 && duplicateIdentities.size() == 2,
            "duplicate productions must keep distinct source identities");

    const agas::generator::GeneratedParserTable generated =
        agas::generator::generateParserTable(*parsed.document);
    require(generated.productions().runtime() == metadata.runtime() &&
                generated.productions().diagnostic() == metadata.diagnostic(),
            "the generated parser table must own both metadata layers");

    std::cout << "production metadata rules=" << metadata.runtime().size()
              << " identities=" << identities.size() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
