#pragma once
#include "agas/model/SyntaxModel.h"
#include "agsem/ContractEnvironment.h"
#include "agsem/Diagnostics.h"
#include <variant>
namespace agsem {
enum class ObligationState { Pending, Implemented, NoAction };
struct AlternativeId {
  std::string value;
  auto operator==(const AlternativeId &) const -> bool = default;
};
struct AnalysisStatus {
  AlternativeId id;
  std::string syntaxSha256;
  std::string currentSyntaxSha256;
  ObligationState declaredState{ObligationState::Pending};
  std::string reason, rule;
  std::optional<std::string> label;
  std::size_t ordinal{};
  SourceLocation location, alternative;
};
enum class CompletenessTarget { Analysis, Interpreter, Lowering, C, Llvm };
enum class ObligationOwner { Sema, Coge };
enum class ObligationKind {
  AnalysisAction,
  AnalyzerInterface,
  InheritedInput,
  SemanticFunction,
  SemanticBinding,
  SemanticContract,
  ExecutionFunction,
  ExecuteHandler,
  EvaluateHandler,
  ExecutionContract,
  LoweringRole,
  BackendBinding
};
enum class ObligationValidation { Valid, Invalid, Blocked };
enum class ObligationEvidence {
  CheckedBody,
  CheckedForwarding,
  CheckedProfile,
  CheckedBinding,
  ExternalContract,
  CheckedNoAction
};
struct AnalyzerSubject {
  std::string name;
  auto operator==(const AnalyzerSubject &) const -> bool = default;
};
struct FunctionSubject {
  std::string name;
  auto operator==(const FunctionSubject &) const -> bool = default;
};
struct ContractSubject {
  std::string manifest, owner, name;
  auto operator==(const ContractSubject &) const -> bool = default;
};
struct RoleSubject {
  std::string family, policy, role;
  auto operator==(const RoleSubject &) const -> bool = default;
};
using ObligationSubject =
    std::variant<AlternativeId, AnalyzerSubject, FunctionSubject,
                 ContractSubject, RoleSubject>;
struct ObligationKey {
  ObligationOwner owner;
  ObligationKind kind;
  ObligationSubject subject;
  std::string slot;
  auto operator==(const ObligationKey &) const -> bool = default;
};
struct UnresolvedRequirement {
  std::string reason;
};
struct PayloadRequirement {
  std::vector<TypeRef> types;
};
using ObligationRequirement =
    std::variant<UnresolvedRequirement, TypeRef, FunctionContract,
                 PayloadRequirement>;
struct Obligation {
  ObligationKey key;
  ObligationRequirement requirement{
      UnresolvedRequirement{"requirement is unresolved"}};
  std::optional<ObligationState> declaredState;
  ObligationState state{ObligationState::Pending};
  ObligationValidation validation{ObligationValidation::Blocked};
  std::optional<ObligationEvidence> evidence;
  bool identityPersisted{}, inferred{};
  std::vector<ObligationKey> dependsOn;
  std::vector<CompletenessTarget> requiredFor;
  std::optional<SourceLocation> location;
  std::vector<SourceLocation> related;
  std::vector<ContractOrigin> contractOrigins;
  std::string rule, reason;
  std::optional<std::string> label;
  std::optional<std::size_t> ordinal;
  std::vector<std::size_t> diagnostics;
};
struct TargetCompleteness {
  CompletenessTarget target;
  bool complete{}, canEmit{};
  std::size_t pending{}, implemented{}, noAction{};
};
struct CompletenessReport {
  std::string sourceIdentity;
  std::vector<ContractIdentity> contracts;
  std::vector<CompletenessTarget> targets{CompletenessTarget::Analysis};
  std::vector<Obligation> obligations;
  std::vector<Diagnostic> diagnostics;
  std::vector<TargetCompleteness> results;
  std::vector<CompletenessTarget> emissionBlocked;
  [[nodiscard]] auto complete() const -> bool;
  [[nodiscard]] auto hasErrors() const -> bool;
};
[[nodiscard]] auto obligationKey(const ObligationKey &) -> std::string;
[[nodiscard]] auto targetName(CompletenessTarget) -> std::string_view;
[[nodiscard]] auto serializeCompleteness(const CompletenessReport &,
                                         std::string_view source = {},
                                         std::string_view filename = {})
    -> std::string;
// Finalization resolves dependency closure and produces deterministic target
// summaries.
void finalizeCompleteness(CompletenessReport &);
[[nodiscard]] auto alternativeSyntaxHash(const agas::model::ParserRule &,
                                         const agas::model::ParserAlternative &)
    -> std::string;
[[nodiscard]] auto alternativeIdPrefix(std::string_view grammar,
                                       std::string_view syntaxHash)
    -> std::string;
} // namespace agsem
