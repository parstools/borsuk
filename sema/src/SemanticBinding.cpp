#include "CompletenessInternal.h"
#include "agsem/SemanticBinding.h"
#include "AssignmentPolicies.h"
#include "AstAccess.h"
#include "CollectionOperations.h"
#include "ConditionPolicies.h"
#include "DocumentStorage.h"
#include "ModelBindingsInternal.h"
#include "SemanticInputStorage.h"
#include "SemanticPolicies.h"
#include "SemanticValidation.h"
#include <cctype>
#include <set>

namespace agsem {
struct BoundSemantics::Impl {
  SemaDocument document;
  ContractEnvironment contracts;
  std::vector<BoundSymbol> symbols;
  std::vector<ResolvedUse> uses;
  std::shared_ptr<const CheckedModelBindings> modelBindings;
};
struct BindingAccess {
  static auto modelBindings(const BoundSemantics &value) -> const auto & {
    return value.data_->modelBindings;
  }
  static auto make(SemaDocument document, ContractEnvironment contracts,
                   std::vector<BoundSymbol> symbols,
                   std::vector<ResolvedUse> uses,
                   std::shared_ptr<const CheckedModelBindings> bindings)
      -> BoundSemantics {
    return BoundSemantics{std::make_shared<const BoundSemantics::Impl>(
        std::move(document), std::move(contracts), std::move(symbols),
        std::move(uses), std::move(bindings))};
  }
};
auto BoundSemantics::document() const -> const SemaDocument & {
  return data_->document;
}
auto BoundSemantics::contracts() const -> const ContractEnvironment & {
  return data_->contracts;
}
auto BoundSemantics::symbols() const -> const std::vector<BoundSymbol> & {
  return data_->symbols;
}
auto BoundSemantics::uses() const -> const std::vector<ResolvedUse> & {
  return data_->uses;
}
namespace {
using namespace ast;
using Environment = std::map<std::string, TypeRef>;
auto parseType(const Value &value) -> TypeRef {
  if (value.kind == Kind::Token)
    return {value.tokenText, {}};
  if (value.typeName == "typeExpression") {
    TypeRef result{token(field(value, "name")), {}};
    if (const auto *args = optional(field(value, "arguments"))) {
      result.arguments.push_back(parseType(field(*args, "first")));
      for (const auto &argument : field(*args, "rest").elements)
        result.arguments.push_back(parseType(argument));
    }
    return result;
  }
  if (value.elements.size() == 1)
    return parseType(value.elements.front());
  throw std::runtime_error("unsupported type expression");
}
auto list(const Value *value, std::string_view itemType)
    -> std::vector<const Value *> {
  std::vector<const Value *> result;
  if (!value)
    return result;
  const auto collect = [&](auto &&self, const Value &item) -> void {
    if (item.typeName == itemType) {
      result.push_back(&item);
      return;
    }
    for (const auto &child : item.elements)
      self(self, child);
  };
  collect(collect, *value);
  return result;
}
class Binder {
public:
  Binder(const SemaDocument &document, const ContractEnvironment &contracts)
      : document_(document),
        root_(SemanticInputAccess::root(document.semanticInput())),
        contracts_(contracts) {
    errors = contracts.validate();
    for (const auto &value : contracts.symbols())
      add(value, {}, true);
    for (const auto &value : DocumentAccess::foreign(document))
      foreign_.emplace(
          std::pair{value.type ? SymbolKind::Type : SymbolKind::Function,
                    value.name},
          value.location);
  }
  std::vector<Diagnostic> errors;
  std::vector<BoundSymbol> symbols;
  std::vector<ResolvedUse> uses;
  std::shared_ptr<const CheckedModelBindings> modelBindings;
  void run() {
    const auto *model = optional(field(root_, "model"));
    if (model) {
      for (const auto &declaration : field(*model, "declarations").elements)
        registerDeclaration(declaration);
      checkAliases();
      for (const auto &declaration : field(*model, "declarations").elements)
        checkDeclaration(declaration);
    }
    for (const auto &symbol : symbols)
      if (symbol.contract.owner != SymbolOwner::Execution)
        if (const auto foreign =
                foreign_.find({symbol.contract.kind, symbol.contract.name});
            foreign != foreign_.end())
          error("sema.contract_mismatch",
                "semantic/execution declaration collision: " +
                    symbol.contract.name,
                symbol.declaration, symbol.contract.name, {foreign->second});
    if (!errors.empty())
      return;
    auto bindings = bindModelBindings(root_, symbols, contracts_);
    if (!bindings.value) {
      errors = std::move(bindings.diagnostics);
      return;
    }
    modelBindings = *bindings.value;
    Environment inherited;
    if (model)
      for (const auto &declaration : field(*model, "declarations").elements)
        if (declaration.typeName == "inheritedDeclaration")
          inherited.emplace(token(field(declaration, "name")),
                            parseType(field(declaration, "type")));
    for (const auto &rule : field(root_, "rules").elements) {
      const auto name = token(field(rule, "name"));
      const auto grammarRule =
          std::ranges::find(document_.grammar().grammar.parserRules, name,
                            &agas::model::ParserRule::name);
      if (grammarRule == document_.grammar().grammar.parserRules.end())
        continue;
      auto alternatives = list(&rule, "parserAlternative");
      for (std::size_t i = 0;
           i < alternatives.size() && i < grammarRule->alternatives.size();
           ++i) {
        auto locals = inherited;
        locals.emplace("unit", TypeRef{"Unit", {}});
        locals.emplace("self", TypeRef{"Node", {{name, {}}}});
        locals.emplace("result", TypeRef{});
        locals.emplace("node", TypeRef{"Node", {{name, {}}}});
        locals.emplace("ctx", TypeRef{context_, {}});
        for (const auto &element : grammarRule->alternatives[i].elements)
          if (element.fieldName) {
            TypeRef type = element.symbol.kind ==
                                   agas::model::ParserSymbolKind::TokenReference
                               ? TypeRef{"Token", {}}
                               : TypeRef{"Node", {{element.symbol.name, {}}}};
            if (element.quantifier == agas::model::Quantifier::Optional)
              type = {"Option", {type}};
            if (element.quantifier == agas::model::Quantifier::ZeroOrMore ||
                element.quantifier == agas::model::Quantifier::OneOrMore)
              type = {"List", {type}};
            locals.emplace(*element.fieldName, type);
          }
        for (const auto &action : field(*alternatives[i], "actions").elements)
          if (action.typeName == "analysisBlock")
            block(field(action, "body"), locals);
      }
    }
  }

private:
  const SemaDocument &document_;
  const Value &root_;
  std::map<std::pair<SymbolKind, std::string>, std::size_t> names_;
  std::map<std::pair<SymbolKind, std::string>, SourceLocation> foreign_;
  const ContractEnvironment &contracts_;
  std::string context_;
  std::set<std::size_t> imported_;
  std::set<std::string> declaredIntrinsics_;
  void error(std::string code, std::string message, SourceLocation source,
             std::string subject = {},
             std::vector<SourceLocation> related = {}) {
    errors.push_back({Severity::Error, std::move(code), std::move(message),
                      source, std::move(subject), std::move(related)});
  }
  void add(ContractSymbol symbol, SourceLocation source, bool imported = false,
           bool intrinsic = false) {
    const auto key = std::pair{symbol.kind, symbol.name};
    if (const auto old = names_.find(key); old != names_.end()) {
      const auto &previous = symbols[old->second].contract;
      if (intrinsic && imported_.contains(old->second) &&
          previous.owner != SymbolOwner::Execution &&
          previous.function == symbol.function)
        return;
      error("sema.contract_mismatch",
            "duplicate or conflicting declaration: " + symbol.name, source,
            symbol.name, {symbols[old->second].declaration});
      return;
    }
    if (imported)
      imported_.insert(symbols.size());
    names_.emplace(key, symbols.size());
    symbols.push_back({std::move(symbol), source});
  }
  auto resolve(SymbolKind kind, const std::string &name, SourceLocation source)
      -> const ContractSymbol * {
    const auto key = std::pair{kind, name};
    if (const auto found = names_.find(key); found != names_.end()) {
      const auto &value = symbols[found->second];
      if (value.contract.owner == SymbolOwner::Execution) {
        error("sema.execution_dependency",
              "analysis cannot depend on execution symbol: " + name, source,
              name, {value.declaration});
        return nullptr;
      }
      uses.push_back({found->second, source});
      return &value.contract;
    }
    if (const auto found = foreign_.find(key); found != foreign_.end())
      error("sema.execution_dependency",
            "analysis cannot depend on execution symbol: " + name, source, name,
            {found->second});
    else
      error(kind == SymbolKind::Type ? "sema.missing_type_contract"
                                     : "sema.unresolved_symbol",
            "unresolved " +
                std::string{kind == SymbolKind::Type ? "type: " : "symbol: "} +
                name,
            source, name);
    return nullptr;
  }
  void checkType(const TypeRef &type, SourceLocation source) {
    if (type.name == "Node") {
      if (type.arguments.size() != 1 ||
          std::ranges::find(document_.grammar().grammar.parserRules,
                            type.arguments.front().name,
                            &agas::model::ParserRule::name) ==
              document_.grammar().grammar.parserRules.end())
        error("sema.missing_type_contract",
              "Node requires a known grammar rule", source, type.name);
      return;
    }
    const auto builtin = builtinTypeArity(type.name);
    const auto *value =
        builtin ? nullptr : resolve(SymbolKind::Type, type.name, source);
    if ((builtin || value) &&
        type.arguments.size() != (builtin ? *builtin : value->type->arity))
      error("sema.contract_mismatch", "wrong type arity: " + typeSpelling(type),
            source, type.name);
    for (const auto &argument : type.arguments)
      checkType(argument, source);
  }
  auto function(const Value &declaration) -> FunctionContract {
    FunctionContract result;
    for (const auto *parameter : list(
             optional(field(declaration, "parameters")), "functionParameter")) {
      const auto *annotation = optional(field(*parameter, "annotation"));
      if (!annotation) {
        error("sema.missing_type_contract", "parameter requires a type",
              location(*parameter));
        result.parameters.push_back({});
      } else
        result.parameters.push_back(parseType(field(*annotation, "value")));
    }
    const auto &outputValue = field(declaration, "result");
    const auto *output = outputValue.kind == Kind::Optional
                             ? optional(outputValue)
                             : &outputValue;
    if (!output)
      error("sema.missing_type_contract", "function requires a result type",
            location(declaration));
    else
      result.result = parseType(field(*output, "type"));
    if (const auto *effect = find(declaration, "effect"))
      result.mutates = optional(*effect);
    return result;
  }
  void registerDeclaration(const Value &declaration) {
    const auto kind = declaration.typeName;
    const auto *nameField = find(declaration, "name");
    if (kind == "rustContextDeclaration") {
      context_ = token(*nameField);
      return;
    }
    if (kind == "functionDeclaration" || kind == "intrinsicDeclaration") {
      ContractSymbol symbol{token(*nameField), SymbolKind::Function,
                            SymbolOwner::Semantic};
      if (kind == "intrinsicDeclaration" &&
          !declaredIntrinsics_.insert(symbol.name).second) {
        error("sema.contract_mismatch",
              "duplicate intrinsic declaration: " + symbol.name,
              location(declaration));
        return;
      }
      symbol.function = function(declaration);
      add(std::move(symbol), location(declaration), false,
          kind == "intrinsicDeclaration");
    } else if (kind == "typeAlias" || kind == "recordDeclaration" ||
               kind == "enumDeclaration" || kind == "entityDeclaration") {
      ContractSymbol symbol{token(*nameField), SymbolKind::Type,
                            SymbolOwner::Semantic};
      TypeContract type;
      if (kind == "typeAlias") {
        type.kind = TypeContract::Kind::Alias;
        type.alias = parseType(field(declaration, "value"));
      } else if (kind == "recordDeclaration" || kind == "entityDeclaration") {
        type.kind = TypeContract::Kind::Record;
        const auto *fields = find(declaration, "fields");
        for (const auto *entry :
             list(fields, kind == "recordDeclaration" ? "recordField"
                                                      : "entityField")) {
          const auto &annotation = field(*entry, "type");
          const auto *value =
              kind == "recordDeclaration" ? optional(annotation) : &annotation;
          if (value)
            type.fields.emplace(token(field(*entry, "name")),
                                parseType(kind == "recordDeclaration"
                                              ? field(*value, "value")
                                              : *value));
          else
            error("sema.missing_type_contract", "record field requires a type",
                  location(*entry));
        }
      } else {
        type.kind = TypeContract::Kind::Enum;
        const auto collect = [&](auto &&self, const Value &value) -> void {
          if (value.kind == Kind::Token)
            type.variants.emplace(value.tokenText, std::vector<TypeRef>{});
          else
            for (const auto &child : value.elements)
              self(self, child);
        };
        collect(collect, field(declaration, "values"));
      }
      symbol.type = std::move(type);
      add(std::move(symbol), location(declaration));
    } else if (kind == "returnPolicy" || kind == "conditionPolicy" ||
               kind == "assignmentPolicy") {
      if (kind == "returnPolicy")
        synthesize(token(*nameField),
                   {{"Option", {{"ExprId", {}}}}, {"SourceRange", {}}},
                   {"Result", {{"OpId", {}}}}, true, location(declaration));
      if (kind == "conditionPolicy")
        synthesize(token(*nameField), {{"ExprId", {}}},
                   {"Result", {{"ExprId", {}}}}, true, location(declaration));
      if (kind == "assignmentPolicy") {
        synthesize(token(*nameField),
                   {{"PlaceId", {}},
                    {"AssignmentOp", {}},
                    {"ExprId", {}},
                    {"SourceRange", {}}},
                   {"Result", {{"OpId", {}}}}, true, location(declaration));
        for (const auto &entry : field(declaration, "entries").elements) {
          const auto key = token(field(entry, "name"));
          if (key == "check")
            synthesize(token(field(entry, "value")),
                       {{"PlaceId", {}}, {"AssignmentOp", {}}},
                       {"Result", {{"Unit", {}}}}, false, location(entry));
          if (key == "increment")
            synthesize(token(field(entry, "value")),
                       {{"PlaceId", {}}, {"Bool", {}}, {"SourceRange", {}}},
                       {"Result", {{"OpId", {}}}}, true, location(entry));
        }
      }
    } else if (kind == "statementIrPolicy" || kind == "flowActions" ||
               kind == "selectionPolicy") {
      const std::map<std::string, std::vector<TypeRef>> parameters{
          {"append", {{"ScopeId", {}}, {"OpId", {}}}},
          {"call", {{"ExprId", {}}, {"SourceRange", {}}}},
          {"if_without_else",
           {{"ExprId", {}}, {"OpId", {}}, {"SourceRange", {}}}},
          {"if_with_else",
           {{"ExprId", {}}, {"OpId", {}}, {"OpId", {}}, {"SourceRange", {}}}},
          {"loop_while", {{"ExprId", {}}, {"OpId", {}}, {"SourceRange", {}}}},
          {"for_initialization", {{"ScopeId", {}}, {"SourceRange", {}}}},
          {"loop_for",
           {{"ScopeId", {}},
            {"OpId", {}},
            {"ExprId", {}},
            {"OpId", {}},
            {"OpId", {}},
            {"SourceRange", {}}}},
          {"snapshot", {}},
          {"restore", {{"FlowId", {}}}},
          {"merge_snapshot", {{"FlowId", {}}}},
          {"recover", {{"FlowId", {}}, {"Text", {}}, {"SourceRange", {}}}}};
      std::string candidate;
      if (kind == "selectionPolicy")
        for (const auto &entry : field(declaration, "entries").elements)
          if (token(field(entry, "name")) == "candidate")
            candidate = token(field(entry, "value"));
      for (const auto &entry : field(declaration, "entries").elements) {
        const auto key = token(field(entry, "name"));
        const auto name = token(field(entry, "value"));
        if (const auto found = parameters.find(key); found != parameters.end())
          synthesize(
              name, found->second,
              {key == "append" || key == "restore" || key == "merge_snapshot"
                   ? "Unit"
               : key == "snapshot" ? "FlowId"
                                   : "OpId",
               {}},
              true, location(entry));
        else if (key == "choose")
          synthesize(name,
                     {{"List", {{candidate, {}}}}, {"Bool", {}}, {"Bool", {}}},
                     {"Result", {{"Option", {{candidate, {}}}}}}, false,
                     location(entry));
        else if (key == "collect")
          synthesize(name, {{"StructId", {}}, {"List", {{"ExprId", {}}}}},
                     {"List", {{candidate, {}}}}, false, location(entry));
        else if (key == "complete")
          synthesize(name,
                     {{"ScopeId", {}},
                      {"SymbolId", {}},
                      {"List", {{"ExprId", {}}}},
                      {"SourceRange", {}}},
                     {"Result", {{"Unit", {}}}}, true, location(entry));
      }
    }
  }
  void synthesize(std::string name, std::vector<TypeRef> parameters,
                  TypeRef result, bool mutates, SourceLocation source) {
    ContractSymbol symbol{std::move(name), SymbolKind::Function,
                          SymbolOwner::Semantic};
    symbol.function =
        FunctionContract{std::move(parameters), std::move(result), mutates, {}};
    add(std::move(symbol), source);
  }
  void checkAliases() {
    std::map<std::string, int> states;
    const auto visit = [&](auto &&self, const std::string &name) -> void {
      if (states[name] == 2)
        return;
      if (states[name] == 1) {
        error("sema.contract_mismatch", "alias cycle: " + name, {}, name);
        return;
      }
      states[name] = 1;
      if (const auto found = names_.find({SymbolKind::Type, name});
          found != names_.end()) {
        const auto &type = symbols[found->second].contract.type;
        if (type && type->alias) {
          const auto dependencies = [&](auto &&walk,
                                        const TypeRef &value) -> void {
            self(self, value.name);
            for (const auto &argument : value.arguments)
              walk(walk, argument);
          };
          dependencies(dependencies, *type->alias);
        }
      }
      states[name] = 2;
    };
    for (const auto &symbol : symbols)
      if (symbol.contract.type && symbol.contract.type->alias)
        visit(visit, symbol.contract.name);
  }

  void checkDeclaration(const Value &declaration) {
    const auto nameField = find(declaration, "name");
    const auto kind = declaration.typeName;
    if (kind == "rustContextDeclaration") {
      checkType({token(*nameField), {}}, location(declaration));
      return;
    }
    if (kind == "inheritedDeclaration") {
      checkType(parseType(field(declaration, "type")), location(declaration));
      return;
    }
    if (kind == "typeAlias" || kind == "recordDeclaration" ||
        kind == "entityDeclaration" || kind == "enumDeclaration") {
      const auto *symbol =
          resolve(SymbolKind::Type, token(*nameField), location(declaration));
      if (symbol) {
        if (symbol->type->alias)
          checkType(*symbol->type->alias, location(declaration));
        for (const auto &[name, type] : symbol->type->fields)
          checkType(type, location(declaration));
      }
      for (const auto *entry :
           list(find(declaration, "fields"),
                kind == "recordDeclaration" ? "recordField" : "entityField")) {
        const auto *initializer =
            find(*entry,
                 kind == "recordDeclaration" ? "defaultValue" : "initializer");
        if (initializer && optional(*initializer))
          expression(*optional(*initializer), {{"ctx", {context_, {}}}});
      }
      return;
    }
    if (kind == "analyzerSignature" || kind == "functionDeclaration" ||
        kind == "intrinsicDeclaration") {
      // Validate this declaration even when registration reported a collision.
      // A conflicting symbol can have a different number of parameters.
      const auto signature = function(declaration);
      for (const auto &parameter : signature.parameters)
        if (!parameter.name.empty())
          checkType(parameter, location(declaration));
      if (!signature.result.name.empty())
        checkType(signature.result, location(declaration));
      Environment locals{{"ctx", {context_, {}}}};
      auto parameters =
          list(optional(field(declaration, "parameters")), "functionParameter");
      for (std::size_t i = 0; i < parameters.size(); ++i)
        locals.emplace(token(field(*parameters[i], "name")),
                       signature.parameters[i]);
      if (kind == "functionDeclaration")
        block(field(declaration, "body"), locals);
      if (kind == "intrinsicDeclaration" &&
          field(declaration, "tail").variantName != "External")
        error("document.unsupported_construct",
              "defined intrinsic is not supported by semantic checker",
              location(declaration));
      return;
    }
    if (kind == "queryDeclaration") {
      error("document.unsupported_construct",
            "query is not supported by typed semantic checker",
            location(declaration));
      return;
    }
    if (kind == "modulesDeclaration") {
      for (const auto &module : field(declaration, "modules").elements)
        for (const auto &entry : field(module, "fields").elements)
          if (token(field(entry, "name")) == "functions") {
            const auto visit = [&](auto &&self, const Value &value) -> void {
              if (value.kind == Kind::Token)
                resolve(SymbolKind::Function, value.tokenText, location(value));
              else
                for (const auto &part : value.elements)
                  self(self, part);
            };
            visit(visit, field(entry, "values"));
          }
      return;
    }
    if (kind == "modelBindings" || kind.ends_with("Bindings"))
      return; // The shared resolver checks these after registry construction.
    if (kind.ends_with("Policy") || kind == "flowActions") {
      const std::set<std::string> generated{
          "append",         "call",       "if_without_else",
          "if_with_else",   "loop_while", "for_initialization",
          "loop_for",       "snapshot",   "restore",
          "merge_snapshot", "recover",    "choose",
          "collect",        "complete",   "check",
          "increment"};
      for (const auto &entry : field(declaration, "entries").elements) {
        const auto key = token(field(entry, "name"));
        const auto &target = field(entry, "value");
        const auto name = token(target);
          if (generated.contains(key)) {
            const auto *function =
                resolve(SymbolKind::Function, name, location(target));
            if (function && function->function) {
              for (const auto &type : function->function->parameters)
                checkType(type, location(target));
              checkType(function->function->result, location(target));
            }
          } else if (key == "candidate")
            checkType({name, {}}, location(target));
          else if (key.ends_with("_ir"))
            checkVariant("Operation", name, location(target));
      }
    }
  }
  auto checkField(const std::string &owner, const std::string &name,
                  SourceLocation source) -> TypeRef {
    if (owner == context_ && names_.contains({SymbolKind::Role, name})) {
      const auto *role = resolve(SymbolKind::Role, name, source);
      if (role && role->roleType) {
        checkType(*role->roleType, source);
        return *role->roleType;
      }
      return {};
    }
    const auto *symbol = resolve(SymbolKind::Type, owner, source);
    if (symbol && symbol->type) {
      if (const auto member = symbol->type->fields.find(name);
          member != symbol->type->fields.end()) {
        checkType(member->second, source);
        return member->second;
      }
      error("sema.missing_type_contract",
            "field has no contract: " + owner + "." + name, source);
    }
    return {};
  }
  void checkVariant(const std::string &owner, const std::string &name,
                    SourceLocation source) {
    const auto *symbol = resolve(SymbolKind::Type, owner, source);
    if (symbol && symbol->type && !symbol->type->variants.contains(name))
      error("sema.missing_type_contract",
            "variant has no contract: " + owner + "." + name, source);
  }

  auto expression(const Value &value, Environment locals) -> TypeRef {
    if (value.typeName == "actionExpression") {
      auto result = expression(field(value, "first"), locals);
      for (const auto &part : field(value, "rest").elements)
        expression(field(part, "value"), locals);
      if (const auto *choice = optional(field(value, "choice"))) {
        expression(field(*choice, "yes"), locals);
        expression(field(*choice, "no"), locals);
      }
      return result;
    }
    if (value.typeName == "actionUnary") {
      const auto &atom = field(value, "atom");
      const auto &suffixes = field(value, "suffixes").elements;
      if (!locals.contains("List")) {
        try {
          if (const auto operation = collectionOperation(value)) {
            if (operation->name == "empty") {
              TypeRef element{collectionElementName(*operation->arguments[0]), {}};
              checkType(element, location(*operation->arguments[0]));
              return {"List", {element}};
            }
            auto element = expression(*operation->arguments[0], locals);
            if (operation->name == "single")
              return {"List", {element}};
            expression(*operation->arguments[1], locals);
            return element;
          }
        } catch (const std::exception &exception) {
          error("sema.contract_mismatch", exception.what(), location(value));
          return {};
        }
      }
      TypeRef type;
      const ContractSymbol *callable = nullptr;
      if (atom.kind == Kind::Token) {
        const auto name = atom.tokenText;
        if (const auto local = locals.find(name); local != locals.end())
          type = local->second;
        else if (name == "true" || name == "false")
          type = {"Bool", {}};
        else if (name == "unit")
          type = {"Unit", {}};
        else if (name == "none")
          type = {"Option", {{}}};
        else if (!name.empty() && (name.front() == '\'' || name.front() == '"'))
          type = {"Text", {}};
        else if (!name.empty() &&
                 std::isdigit(static_cast<unsigned char>(name.front())))
          type = {"Int", {}};
        else if (!suffixes.empty() && suffixes.front().variantName == "Call")
          callable = resolve(SymbolKind::Function, name, location(atom));
        else if (names_.contains({SymbolKind::Type, name})) {
          resolve(SymbolKind::Type, name, location(atom));
          type = {name, {}};
        } else
          resolve(SymbolKind::Function, name, location(atom));
      } else
        type = expression(atom, locals);
      for (const auto &suffix : suffixes) {
        if (suffix.variantName == "Call") {
          for (const auto *argument : list(&suffix, "actionArgument"))
            expression(field(*argument, "value"), locals);
          if (callable && callable->function) {
            type = callable->function->result;
            if (type.name == "Result" && type.arguments.size() == 1)
              type = type.arguments.front();
          }
        } else if (suffix.variantName == "Field" && !type.name.empty()) {
          const auto name = token(field(suffix, "name"));
          if (name == "present" && type.name == "Option") {
            type = {"Bool", {}};
            continue;
          }
          if (type.name == "Token") {
            if (name == "text")
              type = {"Text", {}};
            else if (name == "source")
              type = {"SourceRange", {}};
            else
              error("sema.missing_type_contract",
                    "unknown Token attribute: " + name, location(suffix));
            continue;
          }
          if (type.name == "Node") {
            if (name == "source")
              type = {"SourceRange", {}};
            else {
              error("document.unsupported_construct",
                    "unsupported node attribute: " + name, location(suffix));
              type = {};
            }
            continue;
          }
          const auto builtin = builtinTypeArity(type.name);
          const auto *symbol =
              builtin ? nullptr
                      : resolve(SymbolKind::Type, type.name, location(suffix));
          if (builtin)
            error("sema.missing_type_contract",
                  "field has no builtin contract: " + type.name + "." + name,
                  location(suffix));
          if (symbol && symbol->type) {
            if (const auto f = symbol->type->fields.find(name);
                f != symbol->type->fields.end())
              type = f->second;
            else if (symbol->type->variants.contains(
                         name)) { /* The type is the enum constructor result. */
            } else {
              error("sema.missing_type_contract",
                    "field or variant has no contract: " + type.name + "." +
                        name,
                    location(suffix));
              type = {};
            }
          }
        } else if (suffix.variantName == "Field")
          error("sema.missing_type_contract",
                "cannot prove the type of field access", location(suffix));
        else if (suffix.variantName == "Index") {
          expression(field(suffix, "index"), locals);
          if (type.name == "List" && type.arguments.size() == 1)
            type = type.arguments.front();
          else
            type = {};
        }
      }
      return type;
    }
    if (value.typeName == "listComprehension") {
      expression(field(value, "collection"), locals);
      locals[token(field(value, "item"))] = {};
      for (const auto &child : value.elements)
        if (child.kind != Kind::Token)
          expression(child, locals);
      return {"List", {{}}};
    }
    for (const auto &child : value.elements)
      expression(child, locals);
    return {};
  }
  void block(const Value &value, Environment locals) {
    for (const auto &statement : field(value, "statements").elements) {
      if (statement.typeName == "analyzeStatement") {
        const auto child = token(field(statement, "child"));
        if (!locals.contains(child))
          error("sema.unresolved_symbol", "unresolved analysis child: " + child,
                location(statement));
        const auto visit = [&](auto &&self, const Value &item) -> void {
          if (item.typeName == "actionExpression") {
            expression(item, locals);
            return;
          }
          for (const auto &part : item.elements)
            self(self, part);
        };
        auto context = locals;
        context["failure_message"] = {"Text", {}};
        if (const auto *arguments = optional(field(statement, "context")))
          visit(visit, *arguments);
        if (const auto *fallback = optional(field(statement, "fallback")))
          expression(field(*fallback, "value"), context);
        if (const auto *destination = optional(field(statement, "destination")))
          locals[token(field(*destination, "name"))] = {};
      } else if (statement.typeName == "letStatement") {
        auto type = expression(field(statement, "value"), locals);
        locals[token(field(statement, "name"))] = std::move(type);
      } else if (statement.typeName == "expressionOrAssignment" &&
                 optional(field(statement, "assignment"))) {
        auto type = expression(
            field(*optional(field(statement, "assignment")), "value"), locals);
        const auto target = token(field(statement, "target"));
        if (!target.empty())
          locals[target] = type;
        else
          expression(field(statement, "target"), locals);
      } else if (statement.typeName == "foreachStatement") {
        auto type = expression(field(statement, "collection"), locals);
        auto loop = locals;
        loop[token(field(statement, "item"))] =
            type.name == "List" && type.arguments.size() == 1
                ? type.arguments.front()
                : TypeRef{};
        if (const auto *filter = optional(field(statement, "filter")))
          expression(*filter, loop);
        block(field(statement, "body"), loop);
      } else {
        const auto visit = [&](auto &&self, const Value &item) -> void {
          if (item.typeName == "actionBlock") {
            block(item, locals);
            return;
          }
          if (item.typeName == "actionExpression" ||
              item.typeName == "actionUnary") {
            expression(item, locals);
            return;
          }
          for (const auto &child : item.elements)
            self(self, child);
        };
        visit(visit, statement);
      }
    }
  }
};
} // namespace
auto bindSemantics(const SemaDocument &document,
                   const ContractEnvironment &contracts)
    -> Outcome<BoundSemantics> {
  Outcome<BoundSemantics> result;
  try {
    Binder binder{document, contracts};
    binder.run();
    result.diagnostics = std::move(binder.errors);
    if (result.diagnostics.empty())
      result.value = BindingAccess::make(
          document, contracts, std::move(binder.symbols),
          std::move(binder.uses), std::move(binder.modelBindings));
  } catch (const std::exception &error) {
    result.diagnostics.push_back(
        {Severity::Error, "sema.contract_mismatch", error.what(), {}, {}});
  }
  return result;
}
auto boundSemanticInput(const BoundSemantics &document) -> SemanticInput {
  return SemanticInputAccess::withModelBindings(
      SemanticInputAccess::withContracts(document.document().semanticInput(),
                                         document.contracts()),
      BindingAccess::modelBindings(document));
}
auto prepareSemanticModel(const BoundSemantics &document)
    -> Outcome<std::shared_ptr<const CheckedSemantics>> {
  return tryPrepareSemanticModel(boundSemanticInput(document));
}
auto checkSemantics(const BoundSemantics &document)
    -> Outcome<SemanticValidation> {
  Outcome<SemanticValidation> result;
  const auto &root =
      SemanticInputAccess::root(document.document().semanticInput());
  result.diagnostics = incompleteAnalysisDiagnostics(
      root, document.document().grammar().grammar);
  if (!result.diagnostics.empty())
    return result;
  if (!optional(ast::field(root, "model"))) {
    result.value = SemanticValidation{};
    return result;
  }
  auto checked = prepareSemanticModel(document);
  result.diagnostics = std::move(checked.diagnostics);
  if (checked.value)
    result.value = SemanticValidation{};
  return result;
}
} // namespace agsem
