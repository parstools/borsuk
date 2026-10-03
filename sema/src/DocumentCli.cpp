#include "DocumentCli.h"
#include "AstAccess.h"
#include "DocumentStorage.h"
#include "SemanticInputStorage.h"
#include "agas/artifact/Sha256.h"
#include "agsem/Template.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unistd.h>

namespace agsem::cli {
namespace {
auto read(const std::filesystem::path &path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read file: " + path.string());
  return {std::istreambuf_iterator<char>{input}, {}};
}
auto samePath(const std::filesystem::path &a, const std::filesystem::path &b)
    -> bool {
  std::error_code error;
  if (std::filesystem::exists(a) && std::filesystem::exists(b) &&
      std::filesystem::equivalent(a, b, error))
    return true;
  return std::filesystem::weakly_canonical(a) ==
         std::filesystem::weakly_canonical(b);
}
void protect(const std::filesystem::path &output,
             const std::vector<std::filesystem::path> &inputs) {
  for (const auto &input : inputs)
    if (samePath(input, output))
      throw std::runtime_error("output.would_overwrite_input: " +
                               output.string());
  if (std::filesystem::is_symlink(output))
    throw std::runtime_error("output must not be a symlink: " +
                             output.string());
}
void atomicWrite(const std::filesystem::path &path, const std::string &text) {
  static std::atomic<unsigned> ordinal{};
  const auto temporary = std::filesystem::path{path.string() + ".tmp-" +
                                               std::to_string(::getpid()) +
                                               "-" + std::to_string(ordinal++)};
  try {
    std::ofstream output(temporary, std::ios::binary);
    if (!output)
      throw std::runtime_error("cannot write temporary output: " +
                               temporary.string());
    output << text;
    output.close();
    if (!output)
      throw std::runtime_error("cannot finish temporary output: " +
                               temporary.string());
    std::filesystem::rename(temporary, path);
  } catch (...) {
    std::error_code error;
    std::filesystem::remove(temporary, error);
    throw;
  }
}
auto moduleName(std::string_view name) -> bool {
  if (!name.starts_with("sema_") || !name.ends_with("_gen.rs") ||
      name == "sema_lib_gen.rs")
    return false;
  const auto middle = name.substr(5, name.size() - 12);
  return !middle.empty() && std::ranges::all_of(middle, [](unsigned char c) {
    return std::isalnum(c) || c == '_';
  });
}
void writeRust(const std::filesystem::path &directory,
               const std::map<std::string, std::string> &files,
               const std::vector<std::filesystem::path> &inputs, bool coge,
               const std::string &provenance) {
  const auto metadata = directory / "sema_generation.provenance.json";
  protect(metadata, inputs);
  if (std::filesystem::exists(metadata)) {
    const auto previous =
        nlohmann::ordered_json::parse(read(metadata), nullptr, false);
    if (previous.is_discarded() || previous.value("format", std::string{}) !=
                                       "agsem-generation-provenance-v1")
      throw std::runtime_error(
          "refusing to overwrite unowned generation provenance");
  }
  const auto manifest = directory / "sema_modules.manifest";
  protect(manifest, inputs);
  std::set<std::string> previous;
  if (std::filesystem::exists(manifest)) {
    std::istringstream text(read(manifest));
    std::string line;
    if (!std::getline(text, line) || line != "agsem-modules-v1")
      throw std::runtime_error("invalid generated module manifest");
    while (std::getline(text, line))
      if (!moduleName(line) || !previous.insert(line).second)
        throw std::runtime_error("invalid generated module manifest");
  }
  const std::set<std::string> fixed{
      "sema_gen.rs",        "sema_lib_gen.rs",
      "interpreter_gen.rs", "interpreter_properties_gen.rs",
      "lowering_gen.rs",    "backend_c_gen.rs",
      "backend_llvm_gen.rs"};
  for (const auto &[name, text] : files) {
    if (!fixed.contains(name) && !moduleName(name))
      throw std::runtime_error("invalid generated filename");
    const auto output = directory / name;
    protect(output, inputs);
    if (std::filesystem::exists(output) && !fixed.contains(name) &&
        !previous.contains(name))
      throw std::runtime_error("refusing to overwrite unowned module: " + name);
  }
  std::set<std::string> stale = previous;
  if (coge)
    for (const auto &name : fixed)
      if (name != "sema_gen.rs" && name != "sema_lib_gen.rs")
        stale.insert(name);
  for (const auto &[name, text] : files)
    stale.erase(name);
  for (const auto &name : stale) {
    const auto output = directory / name;
    protect(output, inputs);
    if (std::filesystem::exists(output) &&
        !read(output).starts_with("// Generated by sema"))
      throw std::runtime_error("refusing to remove unowned output: " + name);
  }
  std::filesystem::create_directories(directory);
  for (const auto &[name, text] : files)
    atomicWrite(directory / name, text);
  for (const auto &name : stale) {
    std::error_code error;
    std::filesystem::remove(directory / name, error);
    if (error)
      throw std::runtime_error(error.message());
  }
  std::string owned = "agsem-modules-v1\n";
  for (const auto &[name, text] : files)
    if (!fixed.contains(name))
      owned += name + '\n';
  atomicWrite(manifest, owned);
  atomicWrite(metadata, provenance);
}
void writeProjection(const std::filesystem::path &output,
                     const ProjectionResult &projection,
                     const std::filesystem::path &input,
                     const std::vector<std::filesystem::path> &inputs,
                     bool force, Operation operation,
                     std::string_view templateKind = {},
                     std::string_view templateInput = {},
                     const std::vector<std::string> &targets = {}) {
  const auto manifest =
      std::filesystem::path{output.string() + ".provenance.json"};
  protect(output, inputs);
  protect(manifest, inputs);
  if (!force &&
      (std::filesystem::exists(output) || std::filesystem::exists(manifest)))
    throw std::runtime_error("output exists; use --force: " + output.string());
  nlohmann::ordered_json root{
      {"format", 1},
      {"document_format", 1},
      {"projection", operation == Operation::EmitTemplate ? "template"
                     : operation == Operation::EmitAg     ? "ag"
                                                          : "sema"},
      {"specification", projection.specificationName},
      {"grammar", projection.grammarName},
      {"source", input.generic_string()},
      {"source_sha256", projection.sourceIdentity},
      {"output_sha256", projection.outputIdentity},
      {"tool_sha256", agas::artifact::sha256Hex(read("/proc/self/exe"))}};
  if (operation == Operation::EmitTemplate) {
    root["template_kind"] = templateKind;
    root["template_input"] = templateInput;
    root["targets"] = targets;
  }
  root["contracts"] = nlohmann::ordered_json::array();
  for (const auto &contract : projection.contracts)
    root["contracts"].push_back({{"id", contract.id},
                                 {"version", contract.version},
                                 {"sha256", contract.sha256}});
  root["binding_schema"] = nullptr;
  if (projection.bindingSchema)
    root["binding_schema"] = {{"id", projection.bindingSchema->id},
                              {"version", projection.bindingSchema->version},
                              {"sha256", projection.bindingSchema->sha256}};
  root["origins"] = nlohmann::ordered_json::array();
  for (const auto &origin : projection.origins)
    root["origins"].push_back({{"output_begin", origin.outputBegin},
                               {"output_end", origin.outputEnd},
                               {"input_begin", origin.inputBegin},
                               {"input_end", origin.inputEnd},
                               {"replacement", origin.replacement}});
  const auto serialized = root.dump(2) + '\n';
  atomicWrite(output, projection.text);
  atomicWrite(manifest, serialized);
}
void print(const std::filesystem::path &path, std::string_view source,
           const std::vector<Diagnostic> &diagnostics) {
  for (const auto &diagnostic : diagnostics) {
    std::size_t line = 1, column = 1;
    const auto offset =
        diagnostic.location ? diagnostic.location->beginByte : 0;
    for (std::size_t i = 0; i < std::min<std::size_t>(offset, source.size());
         ++i)
      if (source[i] == '\n') {
        ++line;
        column = 1;
      } else if ((static_cast<unsigned char>(source[i]) & 0xc0) != 0x80)
        ++column;
    std::cerr << path.string() << ':' << line << ':' << column << ": "
              << (diagnostic.severity == Severity::Error ? "error: "
                                                         : "warning: ")
              << diagnostic.code << ": " << diagnostic.message << '\n';
    for (const auto &related : diagnostic.related)
      std::cerr << "  related declaration at byte " << related.beginByte
                << '\n';
  }
}
void printMissingContractGuidance(const std::vector<Diagnostic> &diagnostics) {
  if (std::ranges::any_of(diagnostics, [](const auto &issue) {
        return issue.code == "sema.missing_type_contract";
      }))
    std::cerr << "External runtime declarations require --contracts FILE. "
                 "Example contracts are in contracts/.\n";
}
} // namespace
void collectCalls(const ast::Value &value, std::set<std::string> &calls) {
  if (value.typeName == "actionUnary") {
    auto name = ast::token(ast::field(value, "atom"));
    for (const auto &suffix : ast::field(value, "suffixes").elements) {
      if (suffix.variantName == "Field")
        name = ast::token(ast::field(suffix, "name"));
      else if (suffix.variantName == "Call") {
        if (!name.empty())
          calls.insert(name);
        name.clear();
      } else
        name.clear();
    }
  }
  for (const auto &child : value.elements)
    collectCalls(child, calls);
}
auto generationProvenance(const CheckedSemantics &model) -> std::string {
  nlohmann::ordered_json provenance{
      {"format", "agsem-generation-provenance-v1"},
      {"source_sha256", SemanticInputAccess::identity(semanticInput(model))},
      {"contracts_sha256",
       SemanticInputAccess::contractsIdentity(semanticInput(model))},
      {"binding_schema", nullptr},
      {"execution_profiles", nlohmann::ordered_json::array()}};
  if (const auto &schema = checkedModelBindings(model).schema())
    provenance["binding_schema"] = {{"id", schema->id},
                                    {"version", schema->version},
                                    {"sha256", schema->sha256}};
  return provenance.dump(2) + '\n';
}
auto processSema(const ParsedDocument &parsed,
                 const ContractEnvironment &contracts, const DocumentFrontend &,
                 Operation operation) -> Outcome<Artifacts> {
  Outcome<Artifacts> result;
  auto document = makeSemaDocument(parsed);
  if (!document.value) {
    result.diagnostics = std::move(document.diagnostics);
    return result;
  }
  if (operation == Operation::EmitAg) {
    auto projection = projectAg(parsed);
    result.diagnostics = std::move(projection.diagnostics);
    if (projection.value)
      result.value = Artifacts{std::move(projection.value)};
    return result;
  }
  if (operation == Operation::EmitSema) {
    result.diagnostics.push_back({Severity::Error,
                                  "document.wrong_kind",
                                  "--emit-sema is available in coge",
                                  {},
                                  {}});
    return result;
  }
  auto bound = bindSemantics(*document.value, contracts);
  if (!bound.value) {
    result.diagnostics = std::move(bound.diagnostics);
    return result;
  }
  Artifacts output;
  if (operation == Operation::InspectModel) {
    auto prepared = prepareSemanticModel(*bound.value);
    if (!prepared.value) {
      result.diagnostics = std::move(prepared.diagnostics);
      return result;
    }
    output.model = inspectSemanticModel(**prepared.value);
  } else if (operation == Operation::EmitRust) {
    auto prepared = prepareSemanticModel(*bound.value);
    if (!prepared.value) {
      result.diagnostics = std::move(prepared.diagnostics);
      return result;
    }
    auto files = emitCheckedSemanticsRust(**prepared.value);
    output.generationProvenance = generationProvenance(**prepared.value);
    output.rust = std::move(files.modules);
    output.rust.emplace("sema_gen.rs", std::move(files.sema));
    output.rust.emplace("sema_lib_gen.rs", std::move(files.semaLib));
  } else {
    auto checked = checkSemantics(*bound.value);
    if (!checked.value) {
      result.diagnostics = std::move(checked.diagnostics);
      return result;
    }
    if (operation == Operation::Calls)
      collectCalls(SemanticInputAccess::root(document.value->semanticInput()),
                   output.calls);
  }
  result.value = std::move(output);
  return result;
}
auto run(int argc, char **argv, std::string_view program,
         const Processor &processor, const TemplateProcessor &templates,
         const CompletenessProcessor &completeness) -> int {
  try {
    Operation operation = Operation::Check;
    bool explicitOperation = false, force = false;
    std::optional<bool> fromAg;
    std::vector<std::string> targets;
    std::filesystem::path reportOutput;
    std::filesystem::path output, artifact = SEMA_DOCUMENT_ARTIFACT_DIR;
    std::vector<std::filesystem::path> inputs, manifests;
    for (int i = 1; i < argc; ++i) {
      const std::string_view argument{argv[i]};
      if (argument == "--help" || argument == "-h") {
        std::cout
            << "usage: " << program
            << " [--check | --check-complete | --calls OUT | --emit-ag OUT | "
            << (program == "coge" ? "--emit-sema OUT | " : "")
            << "--inspect-model OUT | --emit-rust-dir DIR] [--contracts FILE] "
               "[--artifact DIR] "
               "[--report OUT] [--force] FILE...\n"
            << "Format 1 inputs contain their grammar and lexer. External "
               "runtime contracts require --contracts FILE.\n"
            << "Migration: old mixed inputs with 'for' use coge --legacy "
               "FILE.\n"
            << "ToyC/ToyCP: use coge with agsem/*_typed.coge and the matching "
               "contracts/*-runtime-v1.json.\n"
            << "Analysis only: use sema with an exported .sema document.\n"
            << "Templates: --from-ag FILE --emit-template OUT"
            << (program == "coge"
                    ? " --target interpreter|c|llvm (repeatable); --from-sema "
                      "FILE also supported.\n"
                    : "\n")
            << "See agsem/DOCUMENT_CLI.md and agsem/TOYSCOPE_MIGRATION.md.\n";
        return 0;
      }
      if (argument == "--legacy")
        throw std::runtime_error(
            "old mixed input is available as coge --legacy; migrated .coge "
            "inputs use coge --contracts FILE, analysis exports use sema. "
            "See agsem/DOCUMENT_CLI.md");
      if (argument == "--from-ag" || argument == "--from-sema") {
        if (fromAg || ++i == argc)
          throw std::runtime_error("one template input option is required");
        fromAg = argument == "--from-ag";
        inputs.emplace_back(argv[i]);
        continue;
      }
      if (argument == "--report") {
        if (++i == argc || !reportOutput.empty())
          throw std::runtime_error("--report needs one output path");
        reportOutput = argv[i];
        continue;
      }
      if (argument == "--target") {
        if (++i == argc)
          throw std::runtime_error("--target needs interpreter, c or llvm");
        const std::string target = argv[i];
        if (target != "interpreter" && target != "c" && target != "llvm")
          throw std::runtime_error("unknown template target: " + target);
        if (std::ranges::find(targets, target) != targets.end())
          throw std::runtime_error("duplicate template target: " + target);
        targets.push_back(target);
        continue;
      }
      if (argument == "--force") {
        force = true;
        continue;
      }
      if (argument == "--contracts" || argument == "--artifact") {
        if (++i == argc)
          throw std::runtime_error("option needs a file/directory");
        if (argument == "--contracts")
          manifests.emplace_back(argv[i]);
        else
          artifact = argv[i];
        continue;
      }
      if (argument == "--check" || argument == "--check-complete" ||
          argument == "--calls" || argument == "--emit-ag" ||
          argument == "--emit-sema" || argument == "--emit-rust-dir" ||
          argument == "--inspect-model" || argument == "--emit-template") {
        if (explicitOperation)
          throw std::runtime_error("operations are mutually exclusive");
        explicitOperation = true;
        operation = argument == "--check-complete"  ? Operation::CheckComplete
                    : argument == "--check"         ? Operation::Check
                    : argument == "--calls"         ? Operation::Calls
                    : argument == "--emit-ag"       ? Operation::EmitAg
                    : argument == "--emit-sema"     ? Operation::EmitSema
                    : argument == "--emit-template" ? Operation::EmitTemplate
                    : argument == "--inspect-model" ? Operation::InspectModel
                                                    : Operation::EmitRust;
        if (operation != Operation::Check &&
            operation != Operation::CheckComplete) {
          if (++i == argc)
            throw std::runtime_error("operation needs an output");
          output = argv[i];
        }
        continue;
      }
      if (argument.starts_with('-'))
        throw std::runtime_error("unknown option: " + std::string{argument});
      inputs.emplace_back(argv[i]);
    }
    const bool checking =
        operation == Operation::Check || operation == Operation::CheckComplete;
    if (inputs.empty() || (!checking && inputs.size() != 1))
      throw std::runtime_error(
          "check needs inputs; output operations need exactly one input");
    if (operation == Operation::EmitTemplate) {
      if (!fromAg)
        throw std::runtime_error(
            "--emit-template requires --from-ag or --from-sema");
      if (program == "sema" && (!*fromAg || !targets.empty()))
        throw std::runtime_error(
            "sema templates support --from-ag without execution targets");
      if (program == "coge" && targets.empty())
        throw std::runtime_error(
            "coge templates require at least one --target");
    } else if (fromAg || (!targets.empty() && (!checking || program != "coge")))
      throw std::runtime_error("--from-ag/--from-sema require templates; "
                               "--target requires coge checks or templates");
    if (!reportOutput.empty() && (!checking || inputs.size() != 1))
      throw std::runtime_error(
          "--report requires a check of exactly one input");
    std::ranges::sort(targets);
    ContractEnvironment contracts;
    for (const auto &manifest : manifests) {
      const auto text = read(manifest);
      const auto diagnostics = contracts.addManifest(text);
      if (!diagnostics.empty()) {
        print(manifest, text, diagnostics);
        return 1;
      }
    }
    const DocumentFrontend frontend{artifact, AGAS_PINNED_ARTIFACT_DIR};
    if (frontend.package().manifest.exactSourceSha256 !=
        SEMA_DOCUMENT_SOURCE_SHA256)
      throw std::runtime_error(
          "artifact does not match this build's document grammar");
    auto protectedInputs = inputs;
    protectedInputs.insert(protectedInputs.end(), manifests.begin(),
                           manifests.end());
    bool valid = true;
    for (const auto &input : inputs) {
      const auto source = read(input);
      if (operation == Operation::EmitTemplate) {
        auto generated =
            templates ? templates(source, *fromAg, targets, contracts, frontend)
                      : templateFromAg(source, frontend);
        if (!generated.value) {
          print(input, source, generated.diagnostics);
          valid = false;
          continue;
        }
        generated.value->contracts = contracts.identities();
        writeProjection(output, *generated.value, input, protectedInputs, force,
                        operation, program, *fromAg ? "ag" : "sema", targets);
        continue;
      }
      auto parsed = frontend.parse(source);
      if (!parsed.value) {
        print(input, source, parsed.diagnostics);
        std::cerr
            << "Migration: format 1 requires a standalone sema/coge header "
               "and embedded grammar. Old mixed inputs with 'for' use "
               "coge --legacy FILE; migrated ToyC/ToyCP use *_typed.coge "
               "with --contracts FILE. See agsem/DOCUMENT_CLI.md.\n";
        valid = false;
        continue;
      }
      if ((checking || operation == Operation::EmitRust) &&
          ((program == "sema") ==
           (parsed.value->kind() == DocumentKind::Sema))) {
        std::vector<CompletenessTarget> selected;
        for (const auto &target : targets)
          selected.push_back(target == "interpreter"
                                 ? CompletenessTarget::Interpreter
                             : target == "c" ? CompletenessTarget::C
                                             : CompletenessTarget::Llvm);
        auto assessment = completeness
                              ? completeness(*parsed.value, contracts, selected)
                              : assessCompleteness(*parsed.value, contracts);
        auto issues = assessment.diagnostics;
        if (operation != Operation::Check)
          for (auto &issue : issues)
            if (issue.code == "completeness.pending")
              issue.severity = Severity::Error;
        print(input, source, issues);
        if (manifests.empty())
          printMissingContractGuidance(issues);
        if (!assessment.value) {
          valid = false;
          continue;
        }
        if (!reportOutput.empty()) {
          protect(reportOutput, protectedInputs);
          if (!force && std::filesystem::exists(reportOutput))
            throw std::runtime_error("output exists; use --force: " +
                                     reportOutput.string());
          atomicWrite(
              reportOutput,
              serializeCompleteness(*assessment.value, source, input.string()));
        }
        const bool strict = operation != Operation::Check;
        const bool accepted = !assessment.value->hasErrors() &&
                              (!strict || assessment.value->complete());
        if (!accepted) {
          valid = false;
          continue;
        }
        if (operation == Operation::EmitRust &&
            std::ranges::any_of(assessment.value->results,
                                [](const auto &r) { return !r.canEmit; })) {
          print(
              input, source,
              {{Severity::Error,
                "document.unsupported_construct",
                "The complete model has unresolved Rust emitter prerequisites.",
                {},
                {}}});
          valid = false;
          continue;
        }
        if (checking) {
          std::cout << input.string()
                    << (strict ? ": complete OK\n" : ": check OK\n");
          continue;
        }
      }
      auto processed = processor(*parsed.value, contracts, frontend, operation);
      if (!processed.value) {
        print(input, source, processed.diagnostics);
        if ((program == "sema") != (parsed.value->kind() == DocumentKind::Sema))
          std::cerr
              << "Migration: complete .coge documents use coge; analysis "
                 "exports use sema. Export analysis with coge --emit-sema "
                 "OUT --contracts FILE INPUT.coge.\n";
        else if (manifests.empty())
          printMissingContractGuidance(processed.diagnostics);
        valid = false;
        continue;
      }
      if (operation == Operation::EmitAg || operation == Operation::EmitSema)
        writeProjection(output, *processed.value->projection, input,
                        protectedInputs, force, operation);
      else if (operation == Operation::EmitRust)
        writeRust(output, processed.value->rust, protectedInputs,
                  program == "coge", processed.value->generationProvenance);
      else if (operation == Operation::InspectModel) {
        protect(output, protectedInputs);
        if (!force && std::filesystem::exists(output))
          throw std::runtime_error("output exists; use --force: " +
                                   output.string());
        atomicWrite(output, processed.value->model);
      } else if (operation == Operation::Calls) {
        protect(output, protectedInputs);
        std::string text;
        for (const auto &call : processed.value->calls)
          text += call + '\n';
        atomicWrite(output, text);
      } else
        std::cout << input.string() << ": check OK\n";
    }
    return valid ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << program << ": " << error.what() << '\n';
    return 1;
  }
}
} // namespace agsem::cli
