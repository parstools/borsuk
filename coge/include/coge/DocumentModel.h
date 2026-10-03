#pragma once
#include "agsem/DocumentModel.h"
namespace coge {
struct DocumentAccess;
class ExecutionInput {
public:
  [[nodiscard]] auto identity() const -> const std::string &;

private:
  struct Impl;
  explicit ExecutionInput(std::shared_ptr<const Impl> data)
      : data_(std::move(data)) {}
  std::shared_ptr<const Impl> data_;
  friend struct DocumentAccess;
};
class CogeDocument {
public:
  [[nodiscard]] auto semantics() const -> const agsem::SemaDocument &;
  [[nodiscard]] auto execution() const -> const ExecutionInput &;
  [[nodiscard]] auto identity() const -> const std::string &;

private:
  struct Impl;
  explicit CogeDocument(std::shared_ptr<const Impl> data)
      : data_(std::move(data)) {}
  std::shared_ptr<const Impl> data_;
  friend struct DocumentAccess;
};
[[nodiscard]] auto makeCogeDocument(const agsem::ParsedDocument &document)
    -> agsem::Outcome<CogeDocument>;
} // namespace coge
