#pragma once

#include "agas/runtime/ReductionRuntime.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace agsem {

struct AssignmentPolicy {
  std::string name;
  std::string checkName;
  std::string incrementName;
  bool compoundStore;
  std::string invalidTarget;
  std::string invalidIncrement;
  std::string dependent;
  std::string incompatible;
};

struct AssignmentBindings {
  std::string policyName;
  std::map<std::string, std::string> fields;
};

[[nodiscard]] auto parseAssignmentPolicies(const agas::runtime::AstValue &root)
    -> std::vector<AssignmentPolicy>;
[[nodiscard]] auto parseAssignmentBindings(const agas::runtime::AstValue &root)
    -> std::vector<AssignmentBindings>;
[[nodiscard]] auto emitAssignmentPolicyBody(const AssignmentPolicy &policy,
                                            std::string_view functionName)
    -> std::string;
[[nodiscard]] auto emitAssignmentModelRust(const AssignmentPolicy &policy,
                                           const AssignmentBindings &bindings,
                                           std::string_view contextType)
    -> std::string;

} // namespace agsem
