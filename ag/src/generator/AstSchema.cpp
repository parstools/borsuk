#include "agas/generator/AstSchema.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "agas/model/Validation.h"
#include "agas/model/AstChain.h"

namespace agas::generator {
namespace {

auto toSchemaIndex(std::size_t value) -> std::uint32_t {
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    throw std::overflow_error("AST schema index exceeds uint32 range");
  }
  return static_cast<std::uint32_t>(value);
}

auto baseType(const model::ParserSymbol &symbol) -> AstType {
  switch (symbol.kind) {
  case model::ParserSymbolKind::RuleReference:
    return {AstTypeKind::Rule, symbol.name, {}};
  case model::ParserSymbolKind::TokenReference:
  case model::ParserSymbolKind::Literal:
    return {AstTypeKind::Token, symbol.name, {}};
  case model::ParserSymbolKind::QualifiedReference:
    return {AstTypeKind::Token, *symbol.qualifier + "." + symbol.name, {}};
  }
  throw std::logic_error("unknown parser symbol kind");
}

auto elementType(const model::ParserElement &element) -> AstType {
  AstType base = baseType(element.symbol);
  switch (element.quantifier) {
  case model::Quantifier::One:
    return base;
  case model::Quantifier::Optional:
    return {AstTypeKind::Optional, {}, {std::move(base)}};
  case model::Quantifier::ZeroOrMore:
    return {AstTypeKind::List, {}, {std::move(base)}};
  case model::Quantifier::OneOrMore:
    return {AstTypeKind::NonEmptyList, {}, {std::move(base)}};
  }
  throw std::logic_error("unknown parser quantifier");
}

void appendDistinct(std::vector<AstType> &types, AstType type) {
  if (std::find(types.begin(), types.end(), type) == types.end()) {
    types.push_back(std::move(type));
  }
}

auto alternativeFields(const model::ParserAlternative &alternative)
    -> std::vector<AstFieldSchema> {
  std::vector<AstFieldSchema> result;
  for (const model::ParserElement &element : alternative.elements) {
    if (element.fieldName.has_value()) {
      result.push_back({*element.fieldName, {elementType(element)}, false});
    }
  }
  return result;
}

auto resultType(const model::SyntaxDocument &document,
                const model::ParserRule &rule,
                const model::ParserAlternative &alternative,
                std::size_t alternativeIndex,
                const std::vector<AstFieldSchema> &fields) -> AstType {
  if (rule.treeModifier == model::TreeModifier::Node) {
    const std::string name = alternative.label.has_value()
                                 ? rule.name + "#" + *alternative.label
                                 : rule.name;
    AstType node{AstTypeKind::Node, name, {}};
    if (const auto operand = model::astChainOperand(document, alternative)) {
      auto forwarded = elementType(alternative.elements[*operand]);
      if (fields.size() == 1) return forwarded;
      return {AstTypeKind::Choice, {}, {std::move(node), std::move(forwarded)}};
    }
    return node;
  }
  if (fields.empty()) {
    return {AstTypeKind::Unit, {}, {}};
  }
  if (fields.size() == 1) {
    return fields.front().acceptedTypes.front();
  }
  return {AstTypeKind::Record,
          rule.name + "/alternative:" + std::to_string(alternativeIndex),
          {}};
}

auto mergedPublicFields(const model::ParserRule &rule)
    -> std::vector<AstFieldSchema> {
  struct MergedField {
    AstFieldSchema schema;
    std::size_t presentCount{};
  };
  std::vector<MergedField> merged;
  std::unordered_map<std::string, std::size_t> indices;
  for (const model::ParserAlternative &alternative : rule.alternatives) {
    for (const model::ParserElement &element : alternative.elements) {
      if (!element.fieldName.has_value())
        continue;
      auto [position, inserted] =
          indices.emplace(*element.fieldName, merged.size());
      if (inserted) {
        merged.push_back({{*element.fieldName, {}, false}, 0});
      }
      MergedField &field = merged[position->second];
      appendDistinct(field.schema.acceptedTypes, elementType(element));
      ++field.presentCount;
    }
  }

  std::vector<AstFieldSchema> result;
  result.reserve(merged.size());
  for (MergedField &field : merged) {
    field.schema.absentInSomeAlternatives =
        field.presentCount != rule.alternatives.size();
    result.push_back(std::move(field.schema));
  }
  return result;
}

auto buildRuleSchema(const model::SyntaxDocument &document,
                     const model::ParserRule &rule) -> AstRuleSchema {
  AstRuleSchema result;
  result.name = rule.name;
  result.treeModifier = rule.treeModifier;
  result.alternatives.reserve(rule.alternatives.size());
  for (std::size_t index = 0; index < rule.alternatives.size(); ++index) {
    const model::ParserAlternative &alternative = rule.alternatives[index];
    std::vector<AstFieldSchema> fields = alternativeFields(alternative);
    AstType alternativeResult = resultType(document, rule, alternative, index, fields);
    if (alternativeResult.kind == AstTypeKind::Choice) {
      for (const auto &type : alternativeResult.arguments)
        appendDistinct(result.resultTypes, type);
    } else {
      appendDistinct(result.resultTypes, alternativeResult);
    }
    result.alternatives.push_back({toSchemaIndex(index), alternative.label,
                                   std::move(alternativeResult),
                                   std::move(fields)});
  }
  const bool hasNamedVariants =
      std::any_of(rule.alternatives.begin(), rule.alternatives.end(),
                  [](const model::ParserAlternative &alternative) {
                    return alternative.label.has_value();
                  });
  if (rule.treeModifier == model::TreeModifier::Node && !hasNamedVariants) {
    result.publicFields = mergedPublicFields(rule);
  }
  return result;
}

} // namespace

AstSchema::AstSchema(std::vector<AstRuleSchema> rules)
    : rules_(std::move(rules)) {
  std::unordered_set<std::string> names;
  for (const AstRuleSchema &rule : rules_) {
    if (!names.insert(rule.name).second) {
      throw std::invalid_argument("AST schema rule names must be unique");
    }
    if (rule.alternatives.empty() || rule.resultTypes.empty()) {
      throw std::invalid_argument(
          "every AST schema rule requires alternatives and result types");
    }
  }
}

auto AstSchema::rules() const noexcept -> const std::vector<AstRuleSchema> & {
  return rules_;
}

auto AstSchema::rule(std::string_view name) const -> const AstRuleSchema & {
  const auto found =
      std::find_if(rules_.begin(), rules_.end(),
                   [name](const auto &rule) { return rule.name == name; });
  if (found == rules_.end()) {
    throw std::out_of_range("unknown AST schema rule");
  }
  return *found;
}

auto buildAstSchema(const model::SyntaxDocument &document) -> AstSchema {
  if (!model::validateSyntaxModel(document).valid()) {
    throw std::invalid_argument(
        "cannot build an AST schema for an invalid Agas syntax model");
  }
  std::vector<AstRuleSchema> rules;
  rules.reserve(document.parserRules.size());
  for (const model::ParserRule &rule : document.parserRules) {
    rules.push_back(buildRuleSchema(document, rule));
  }
  return AstSchema{std::move(rules)};
}

} // namespace agas::generator
