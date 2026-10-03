#pragma once

#include "agas/runtime/ReductionRuntime.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace agsem {

enum class ReturnTarget { EnclosingFunction };
enum class ReturnValueRule { AbsentForVoidOtherwiseRequired };
enum class ReturnConversion { ImplicitConversion };
enum class ReturnCleanup { NoCleanup, ExitedAutomaticLifetimes };
enum class ReturnEvaluation { CaptureValueBeforeCleanup };
enum class ReturnFlow { UnreachableAfterSuccess };
enum class ReturnIr { Return };

struct ReturnPolicy {
  std::string name;
  ReturnTarget target;
  ReturnValueRule value;
  ReturnConversion conversion;
  ReturnCleanup cleanup;
  ReturnEvaluation evaluation;
  ReturnFlow flow;
  ReturnIr ir;
  std::string missingTarget;
  std::string wrongPresence;
  std::string failedConversion;
};

struct ReturnModelBindings {
  std::string policyName;
  std::map<std::string, std::string> fields;
};

[[nodiscard]] auto parseReturnPolicies(const agas::runtime::AstValue &root)
    -> std::vector<ReturnPolicy>;
[[nodiscard]] auto emitReturnPolicyBody(const ReturnPolicy &policy)
    -> std::string;
[[nodiscard]] auto parseReturnModelBindings(const agas::runtime::AstValue &root)
    -> std::vector<ReturnModelBindings>;
[[nodiscard]] auto emitReturnModelRust(const ReturnModelBindings &bindings,
                                       std::string_view contextType)
    -> std::string;

} // namespace agsem
