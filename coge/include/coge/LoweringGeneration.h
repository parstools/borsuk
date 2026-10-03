#pragma once

#include "agsem/ContractEnvironment.h"

#include <map>
#include <optional>
#include <set>
#include <string>

namespace coge {

struct LoweringGenerationInput {
  std::optional<std::map<std::string, std::string>> bindings;
  std::vector<agsem::ContractSymbol> contracts;
  std::map<std::string, agsem::SourceLocation> locations;
};

class CheckedLowering {
public:
  [[nodiscard]] auto loweringName() const -> std::optional<std::string>;
  [[nodiscard]] auto inspection() const -> std::string;

private:
  friend auto prepareLowering(const LoweringGenerationInput &)
      -> CheckedLowering;
  friend auto emitLoweringRust(const CheckedLowering &) -> std::string;
  std::optional<std::map<std::string, std::string>> bindings_;
  std::string inspection_;
};

[[nodiscard]] auto loweringFields() -> const std::set<std::string> &;
[[nodiscard]] auto prepareLowering(const LoweringGenerationInput &input)
    -> CheckedLowering;
[[nodiscard]] auto emitLoweringRust(const CheckedLowering &lowering)
    -> std::string;

} // namespace coge
