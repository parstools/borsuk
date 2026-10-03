#include "agas/generator/AstSchema.h"
#include "agas/bootstrap/AntlrFrontend.h"
#include "agas/generator/ParserGeneration.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string{message});
}

auto field(const std::vector<agas::generator::AstFieldSchema> &fields,
           std::string_view name) -> const agas::generator::AstFieldSchema & {
  for (const auto &field : fields) {
    if (field.name == name)
      return field;
  }
  throw std::runtime_error("missing AST schema field");
}

auto containsType(const std::vector<agas::generator::AstType> &types,
                  agas::generator::AstTypeKind kind,
                  std::string_view argumentName = {}) -> bool {
  for (const auto &type : types) {
    if (type.kind != kind)
      continue;
    if (argumentName.empty())
      return true;
    if (type.name == argumentName)
      return true;
    if (type.arguments.size() == 1 &&
        type.arguments.front().name == argumentName) {
      return true;
    }
  }
  return false;
}

} // namespace

int main() {
  using agas::generator::AstTypeKind;

  try {
    const auto parsed = agas::bootstrap::parseAgas(
        "grammar Schema; "
        "node start : choice=choice variant=variant multi=multi EOF ; "
        "node choice : value=ID optional=item? | value=item+ | other=ID ; "
        "node variant : value=ID #TokenVariant | items=item+ #ItemVariant ; "
        "inline forwarding : value=item ; "
        "inline emptyResult : empty ; "
        "inline multi : first=ID rest=item* ; "
        "node item : value=ID ; "
        "ID : 'i' ;");
    require(parsed.accepted(), "AST schema fixture must parse");

    const agas::generator::AstSchema schema =
        agas::generator::buildAstSchema(*parsed.document);
    const agas::generator::AstSchema repeated =
        agas::generator::buildAstSchema(*parsed.document);
    require(schema.rules() == repeated.rules() && schema.rules().size() == 7,
            "AST schema generation must be complete and deterministic");

    const auto &choice = schema.rule("choice");
    require(choice.resultTypes.size() == 1 &&
                choice.resultTypes.front().kind == AstTypeKind::Node &&
                choice.resultTypes.front().name == "choice" &&
                choice.publicFields.size() == 3,
            "an unlabelled node must expose one merged public shape");
    const auto &value = field(choice.publicFields, "value");
    require(value.absentInSomeAlternatives && value.acceptedTypes.size() == 2 &&
                containsType(value.acceptedTypes, AstTypeKind::Token, "ID") &&
                containsType(value.acceptedTypes, AstTypeKind::NonEmptyList,
                             "item"),
            "different field types must form a neutral sum and preserve +");
    const auto &optional = field(choice.publicFields, "optional");
    require(
        optional.absentInSomeAlternatives &&
            optional.acceptedTypes.size() == 1 &&
            containsType(optional.acceptedTypes, AstTypeKind::Optional, "item"),
        "field absence must remain distinct from Optional<T>");

    const auto &variant = schema.rule("variant");
    require(variant.publicFields.empty() && variant.resultTypes.size() == 2 &&
                variant.alternatives[0].variantName == "TokenVariant" &&
                variant.alternatives[0].resultType.name ==
                    "variant#TokenVariant" &&
                variant.alternatives[1].variantName == "ItemVariant",
            "named node alternatives must retain separate variant schemas");

    const auto &forwarding = schema.rule("forwarding");
    require(forwarding.resultTypes.size() == 1 &&
                forwarding.resultTypes.front().kind == AstTypeKind::Rule &&
                forwarding.resultTypes.front().name == "item",
            "a single-field inline rule must expose the forwarded type");
    const auto &emptyResult = schema.rule("emptyResult");
    require(emptyResult.resultTypes.front().kind == AstTypeKind::Unit,
            "an empty inline rule must expose unit");
    const auto &multi = schema.rule("multi");
    require(multi.resultTypes.front().kind == AstTypeKind::Record &&
                multi.resultTypes.front().name == "multi/alternative:0" &&
                multi.alternatives.front().fields.size() == 2 &&
                containsType(multi.alternatives.front().fields[1].acceptedTypes,
                             AstTypeKind::List, "item"),
            "a multi-field inline rule must expose a typed technical record");

    const auto generated =
        agas::generator::generateParserTable(*parsed.document);
    require(generated.astSchema().rules() == schema.rules(),
            "generated parser tables must own the neutral AST schema");

    std::cout << "AST schema rules=" << schema.rules().size() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
