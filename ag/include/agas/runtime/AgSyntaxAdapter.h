#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "agas/artifact/GrammarSections.h"
#include "agas/model/SyntaxModel.h"
#include "agas/runtime/ReductionRuntime.h"
#include "grammar/Grammar.h"

namespace agas::runtime {

class AgSyntaxAdapterError final : public std::runtime_error {
public:
  AgSyntaxAdapterError(InputSpan span, std::string message);

  [[nodiscard]] auto span() const noexcept -> const InputSpan &;

private:
  InputSpan span_;
};

[[nodiscard]] auto adaptAgSyntaxDocument(const AstValue &root,
                                         std::string_view source,
                                         const zbik::Grammar &grammar)
    -> model::SyntaxDocument;

[[nodiscard]] auto
adaptAgSyntaxDocument(const AstValue &root, std::string_view source,
                      const artifact::ArtifactSymbols &symbols)
    -> model::SyntaxDocument;

} // namespace agas::runtime
