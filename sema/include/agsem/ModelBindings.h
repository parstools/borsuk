#pragma once

#include "agsem/ContractEnvironment.h"
#include <map>
#include <memory>

namespace agsem {
class CheckedSemantics;
enum class BindingTargetKind { Field, Method, Variant, Mode };
enum class ReturnRepresentation { Tuple, Record };
enum class CleanupRepresentation { NoScopes, Field };
struct BindingEvidence {
  BindingTargetKind kind{};
  std::string owner;
  std::string target;
  std::optional<TypeRef> requiredType;
  std::optional<TypeRef> actualType;
  std::optional<FunctionContract> requiredFunction;
  std::optional<FunctionContract> actualFunction;
  std::vector<TypeRef> requiredPayload;
  std::vector<TypeRef> actualPayload;
  std::optional<SourceLocation> declaration;
  std::optional<ContractIdentity> contract;
  std::string contractPath;
};
struct CheckedBindingPort {
  std::string family;
  std::string policy;
  std::string port;
  std::string value;
  BindingTargetKind kind{};
  std::optional<ReturnRepresentation> returnRepresentation;
  std::optional<CleanupRepresentation> cleanupRepresentation;
  std::string origin;
  SourceLocation source;
  SourceLocation policySource;
  std::string invocation;
  std::vector<std::string> argumentPassing;
  std::string access;
  std::vector<BindingEvidence> targets;
};
struct CheckedBindingPolicy {
  std::string family;
  std::string policy;
  std::map<std::string, std::string> names;
  std::vector<BindingEvidence> dependencies;
};
struct BindingSchemaIdentity {
  std::string id;
  std::string version;
  std::string sha256;
  SourceLocation source;
};
class CheckedModelBindings {
public:
  [[nodiscard]] auto schema() const
      -> const std::optional<BindingSchemaIdentity> & {
    return schema_;
  }
  [[nodiscard]] auto ports() const -> const std::vector<CheckedBindingPort> & {
    return ports_;
  }
  [[nodiscard]] auto policies() const
      -> const std::vector<CheckedBindingPolicy> & {
    return policies_;
  }
  [[nodiscard]] auto names(std::string_view family,
                           std::string_view policy) const
      -> const std::map<std::string, std::string> &;

private:
  std::optional<BindingSchemaIdentity> schema_;
  std::vector<CheckedBindingPort> ports_;
  std::vector<CheckedBindingPolicy> policies_;
  friend struct ModelBindingAccess;
};
[[nodiscard]] auto checkedModelBindings(const CheckedSemantics &)
    -> const CheckedModelBindings &;
[[nodiscard]] auto inspectModelBindings(const CheckedModelBindings &)
    -> std::string;
} // namespace agsem
