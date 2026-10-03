#pragma once

#include "agas/model/SyntaxModel.h"
#include "agas/runtime/ReductionRuntime.h"

#include <map>
#include <string>

namespace agsem {

struct RustFiles {
  std::string sema;
  std::string semaLib;
  std::string interpreter;
  std::string properties;
  std::string lowering;
  std::string backendC;
  std::string backendLlvm;
  std::map<std::string, std::string> modules;
};

// Compile typed semantic actions using declared or inferred analyzer signatures.
[[nodiscard]] auto emitRust(const agas::runtime::AstValue &root,
                            const agas::model::SyntaxDocument &sourceGrammar)
    -> RustFiles;

} // namespace agsem
