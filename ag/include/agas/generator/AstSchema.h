#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agas/model/SyntaxModel.h"

namespace agas::generator {

enum class AstTypeKind {
  Unit,
  Token,
  Rule,
  Node,
  Record,
  Optional,
  List,
  NonEmptyList,
  Choice,
};

struct AstType {
  AstTypeKind kind{AstTypeKind::Unit};
  std::string name;
  std::vector<AstType> arguments;

  auto operator==(const AstType &) const -> bool = default;
};

struct AstFieldSchema {
  std::string name;
  // More than one entry is a neutral sum type.
  std::vector<AstType> acceptedTypes;
  // This is distinct from Optional<T>: the complete field is absent from at
  // least one alternative of an unlabelled node.
  bool absentInSomeAlternatives{};

  auto operator==(const AstFieldSchema &) const -> bool = default;
};

struct AstAlternativeSchema {
  std::uint32_t sourceAlternativeIndex{};
  std::optional<std::string> variantName;
  AstType resultType;
  std::vector<AstFieldSchema> fields;

  auto operator==(const AstAlternativeSchema &) const -> bool = default;
};

struct AstRuleSchema {
  std::string name;
  model::TreeModifier treeModifier{model::TreeModifier::Node};
  // Distinct alternative results form the rule's neutral sum type.
  std::vector<AstType> resultTypes;
  // Populated only for a node whose alternatives have no #Variant names.
  std::vector<AstFieldSchema> publicFields;
  std::vector<AstAlternativeSchema> alternatives;

  auto operator==(const AstRuleSchema &) const -> bool = default;
};

class AstSchema {
public:
  explicit AstSchema(std::vector<AstRuleSchema> rules);

  [[nodiscard]] auto rules() const noexcept
      -> const std::vector<AstRuleSchema> &;
  [[nodiscard]] auto rule(std::string_view name) const -> const AstRuleSchema &;

private:
  std::vector<AstRuleSchema> rules_;
};

[[nodiscard]] auto buildAstSchema(const model::SyntaxDocument &document)
    -> AstSchema;

} // namespace agas::generator
