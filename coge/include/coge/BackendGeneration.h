#pragma once

#include "agsem/Diagnostics.h"
#include <map>
#include <optional>
#include <string>

namespace coge {

struct BackendBinding {
  std::string emit;
  std::string lower;
  std::string profile;
  std::map<std::string, agsem::SourceLocation> locations;
  std::string inspection;
};

struct BackendGenerationInput {
  std::optional<std::string> lowering;
  std::optional<BackendBinding> c;
  std::optional<BackendBinding> llvm;
};

class CheckedBackends {
public:
  [[nodiscard]] auto c() const -> const std::optional<BackendBinding> & {
    return c_;
  }
  [[nodiscard]] auto llvm() const -> const std::optional<BackendBinding> & {
    return llvm_;
  }

private:
  friend auto prepareBackends(const BackendGenerationInput &)
      -> CheckedBackends;
  std::optional<BackendBinding> c_;
  std::optional<BackendBinding> llvm_;
};

[[nodiscard]] auto prepareBackends(const BackendGenerationInput &input)
    -> CheckedBackends;
[[nodiscard]] auto emitCBackendRust(const CheckedBackends &backends)
    -> std::string;
[[nodiscard]] auto emitLlvmBackendRust(const CheckedBackends &backends)
    -> std::string;

} // namespace coge
