#pragma once
#include "agsem/Template.h"
namespace coge {
enum class TemplateTarget { Interpreter, C, Llvm };
struct TemplateOptions {
  std::vector<TemplateTarget> targets;
};
[[nodiscard]] auto templateFromAg(std::string_view source,
                                  const agsem::DocumentFrontend &frontend,
                                  const agsem::ContractEnvironment &contracts,
                                  const TemplateOptions &options)
    -> agsem::Outcome<agsem::ProjectionResult>;
[[nodiscard]] auto templateFromSema(const agsem::ParsedDocument &document,
                                    const agsem::DocumentFrontend &frontend,
                                    const agsem::ContractEnvironment &contracts,
                                    const TemplateOptions &options)
    -> agsem::Outcome<agsem::ProjectionResult>;
} // namespace coge
