#pragma once

#include "agas/runtime/ReductionRuntime.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace agsem {

struct ConditionPolicy {
  std::string name;
  std::string dependent;
  std::string incompatible;
};

struct ConditionBindings {
  std::string policyName;
  std::map<std::string, std::string> fields;
};

[[nodiscard]] auto parseConditionPolicies(const agas::runtime::AstValue &root)
    -> std::vector<ConditionPolicy>;
[[nodiscard]] auto parseConditionBindings(const agas::runtime::AstValue &root)
    -> std::vector<ConditionBindings>;
[[nodiscard]] auto emitConditionPolicyBody(const ConditionPolicy &policy)
    -> std::string;
[[nodiscard]] auto emitConditionModelRust(const ConditionBindings &bindings,
                                          std::string_view contextType)
    -> std::string;

} // namespace agsem
