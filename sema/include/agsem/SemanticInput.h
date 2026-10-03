#pragma once

#include <memory>
#include <utility>

namespace agsem {

struct SemanticInputAccess;

class SemanticInput {
public:
  SemanticInput(const SemanticInput &) = default;
  SemanticInput(SemanticInput &&) noexcept = default;
  auto operator=(const SemanticInput &) -> SemanticInput & = default;
  auto operator=(SemanticInput &&) noexcept -> SemanticInput & = default;
  ~SemanticInput() = default;

private:
  struct Impl;
  explicit SemanticInput(std::shared_ptr<const Impl> storage)
      : storage_(std::move(storage)) {}

  std::shared_ptr<const Impl> storage_;
  friend struct SemanticInputAccess;
};

} // namespace agsem
