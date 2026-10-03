#pragma once
#include "agsem/ContractEnvironment.h"
#include "coge/InterpreterGeneration.h"
#include "coge/PropertyGeneration.h"
#include <memory>
namespace coge {
struct ExecutionOrigin {
  std::string kind = "explicit";
  agsem::SourceLocation source;
  std::string member;
};
struct ExecutionFunction {
  ExecutionFunctionSignature signature;
  ExecutionBody body;
  ExecutionOrigin origin;
};
struct ExecutionIntrinsic {
  ExecutionFunctionSignature signature;
  ExecutionOrigin origin;
};
struct ExecutionHandler {
  ExecutionArm arm;
  ExecutionOrigin origin;
};
struct ExecutionProfileIdentity {
  std::string id, version, sha256;
  agsem::SourceLocation source;
};
struct ExecutionDependency {
  std::string member, kind, owner, name;
  std::optional<agsem::TypeRef> type;
  std::optional<agsem::FunctionContract> function;
  std::vector<agsem::TypeRef> payload;
  std::optional<agsem::ContractOrigin> contract;
  std::optional<agsem::SourceLocation> declaration;
  std::string path;
};
struct ExpandedExecutionModel {
  std::string sourceIdentity, contractsIdentity, contextType;
  agsem::SourceLocation source;
  bool interpreterRequested = false;
  std::optional<ExecutionProfileIdentity> profile;
  std::optional<ExecutionDeclarations> declarations;
  std::map<std::string, agsem::SourceLocation> typeOrigins;
  std::vector<ExecutionFunction> functions;
  std::vector<ExecutionIntrinsic> intrinsics;
  std::vector<ExecutionHandler> operations, expressions;
  std::optional<PropertyGenerationPlan> properties;
};
class CheckedExecutionModel {
public:
  [[nodiscard]] auto model() const -> const ExpandedExecutionModel & {
    return model_;
  }
  [[nodiscard]] auto interpreter() const -> const InterpreterGenerationPlan & {
    return interpreter_;
  }
  [[nodiscard]] auto dependencies() const
      -> const std::vector<ExecutionDependency> & {
    return dependencies_;
  }
  [[nodiscard]] auto effectiveHash() const -> const std::string & {
    return effectiveHash_;
  }

private:
  CheckedExecutionModel() = default;
  ExpandedExecutionModel model_;
  InterpreterGenerationPlan interpreter_;
  std::vector<ExecutionDependency> dependencies_;
  std::string effectiveHash_;
  friend struct ExecutionModelAccess;
};
[[nodiscard]] auto inspectExecutionModel(const CheckedExecutionModel &)
    -> std::string;
[[nodiscard]] auto executionCalls(const CheckedExecutionModel &)
    -> std::vector<std::string>;
} // namespace coge
