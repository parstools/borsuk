#pragma once
#include "agsem/Diagnostics.h"
#include <map>
#include <string_view>
namespace agsem {
enum class SymbolOwner { Semantic, Execution, Library };
enum class SymbolKind { Type, Function, Method, Role };
struct TypeRef {
  std::string name;
  std::vector<TypeRef> arguments;
  auto operator==(const TypeRef &) const -> bool = default;
};
struct FunctionContract {
  std::vector<TypeRef> parameters;
  TypeRef result;
  bool mutates{};
  std::string context;
  auto operator==(const FunctionContract &) const -> bool = default;
};
struct TypeContract {
  enum class Kind { Opaque, Alias, Record, Enum } kind{Kind::Opaque};
  std::size_t arity{};
  std::optional<TypeRef> alias;
  std::map<std::string, TypeRef> fields;
  std::map<std::string, std::vector<TypeRef>> variants;
  auto operator==(const TypeContract &) const -> bool = default;
};
struct ContractSymbol {
  std::string name;
  SymbolKind kind;
  SymbolOwner owner;
  std::optional<TypeContract> type;
  std::optional<FunctionContract> function;
  std::optional<TypeRef> roleType;
  std::vector<std::string> rust;
  auto operator==(const ContractSymbol &) const -> bool = default;
};
struct ContractIdentity {
  std::string id;
  std::string version;
  std::string sha256;
  auto operator==(const ContractIdentity &) const -> bool = default;
};
struct ContractOrigin {
  ContractIdentity identity;
  std::string path;
};
class ContractEnvironment {
public:
  [[nodiscard]] auto symbols() const -> const std::vector<ContractSymbol> & {
    return symbols_;
  }
  [[nodiscard]] auto identities() const
      -> const std::vector<ContractIdentity> & {
    return identities_;
  }
  [[nodiscard]] auto origins() const -> const std::vector<ContractOrigin> & {
    return origins_;
  }
  [[nodiscard]] auto addManifest(std::string_view json)
      -> std::vector<Diagnostic>;
  [[nodiscard]] auto fingerprint() const -> std::string;
  [[nodiscard]] auto validate() const -> std::vector<Diagnostic>;

private:
  std::vector<ContractSymbol> symbols_;
  std::vector<ContractIdentity> identities_;
  std::vector<ContractOrigin> origins_;
};
[[nodiscard]] auto typeSpelling(const TypeRef &type) -> std::string;
[[nodiscard]] auto builtinTypeArity(std::string_view name,
                                    bool execution = false)
    -> std::optional<std::size_t>;
} // namespace agsem
