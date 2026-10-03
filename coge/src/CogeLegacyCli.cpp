#include "PreparedSemanticModel.h"
#include "RustGenerator.h"
#include "SemanticValidation.h"
#include "agas/artifact/ArtifactPackage.h"
#include "agas/runtime/ArtifactAstParser.h"
#include "agas/runtime/ArtifactLexerRuntime.h"
#include "agas/runtime/PackagedAgFrontend.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Position {
  std::size_t line{1};
  std::size_t column{1};
};

auto isGeneratedModuleName(std::string_view file) -> bool {
  if (!file.starts_with("sema_") || !file.ends_with("_gen.rs"))
    return false;
  const auto name = file.substr(5, file.size() - 12);
  if (name.empty() || name == "lib" ||
      !(name.front() == '_' || (name.front() >= 'a' && name.front() <= 'z') ||
        (name.front() >= 'A' && name.front() <= 'Z')))
    return false;
  return std::ranges::all_of(name, [](char ch) {
    return ch == '_' || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9');
  });
}

auto positionAt(std::string_view source, std::size_t offset) -> Position {
  Position result;
  const auto limit = std::min(offset, source.size());
  for (std::size_t index = 0; index < limit; ++index) {
    if (source[index] == '\n') {
      ++result.line;
      result.column = 1;
    } else {
      ++result.column;
    }
  }
  return result;
}

auto readFile(const std::filesystem::path &path) -> std::string {
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    throw std::runtime_error("cannot read input file: " + path.string());
  return {std::istreambuf_iterator<char>{stream},
          std::istreambuf_iterator<char>{}};
}

void collectCalls(const agas::runtime::AstValue &value,
                  std::set<std::string> &names) {
  if (value.typeName == "actionUnary" && value.elements.size() == 3) {
    const auto &atom = value.elements[1];
    const auto &suffixes = value.elements[2];
    std::string callee;
    if (atom.kind == agas::runtime::AstValueKind::Token)
      callee = atom.tokenText;
    for (const auto &suffix : suffixes.elements) {
      if (suffix.typeName != "actionPostfix")
        continue;
      if (suffix.variantName == "Field" && !suffix.elements.empty()) {
        callee = suffix.elements.front().tokenText;
      } else if (suffix.variantName == "Call") {
        if (!callee.empty())
          names.insert(callee);
        callee.clear();
      } else {
        callee.clear();
      }
    }
  }
  for (const auto &element : value.elements)
    collectCalls(element, names);
}

auto field(const agas::runtime::AstValue &value, std::string_view name)
    -> const agas::runtime::AstValue * {
  for (std::size_t index = 0; index < value.fieldNames.size(); ++index)
    if (value.fieldNames[index] == name)
      return &value.elements[index];
  return nullptr;
}

auto checkFile(const std::filesystem::path &path,
               const agas::runtime::ArtifactAstParser &parser,
               const agas::runtime::ArtifactLexerRuntime &lexer,
               std::set<std::string> *calls) -> bool {
  const auto source = readFile(path);
  try {
    const auto result = parser.parse(source, lexer);
    if (result.accepted()) {
      try {
        if (const auto *model = field(*result.root, "model");
            model && !model->elements.empty())
          agsem::checkFunctionCycles(model->elements.front());
      } catch (const std::runtime_error &error) {
        std::cerr << path.string() << ": " << error.what() << '\n';
        return false;
      }
      std::cout << path.string() << ": syntax OK\n";
      if (calls)
        collectCalls(*result.root, *calls);
      return true;
    }
    const auto &error = *result.error;
    const auto position = positionAt(source, error.byteOffset);
    std::cerr << path.string() << ':' << position.line << ':' << position.column
              << ": syntax error: " << error.message << '\n';
  } catch (const agas::runtime::ArtifactLexerError &error) {
    const auto position = positionAt(source, error.errorOffset());
    std::cerr << path.string() << ':' << position.line << ':' << position.column
              << ": lexical error: " << error.what() << '\n';
  }
  return false;
}

} // namespace

int runLegacyCogeCli(int argc, char **argv) {
  std::filesystem::path artifactDirectory;

  std::filesystem::path callsOutput;
  std::filesystem::path rustOutputDirectory;
  std::vector<std::filesystem::path> inputs;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--help" || argument == "-h") {
      std::cout << "usage: coge --legacy [--artifact DIR] [--calls OUTPUT | "
                   "--emit-rust-dir DIR] FILE.sema [FILE.sema ...]\n"
                   "Migration: ToyC/ToyCP use *_typed.coge with coge "
                   "--contracts FILE. See agsem/DOCUMENT_CLI.md.\n";
      return 0;
    }
    if (argument == "--legacy") {
      continue;
    } else if (argument == "--artifact") {
      if (++index == argc) {
        std::cerr << "coge --legacy: --artifact needs a directory\n";
        return 2;
      }
      artifactDirectory = argv[index];
    } else if (argument == "--calls") {
      if (++index == argc) {
        std::cerr << "coge --legacy: --calls needs an output file\n";
        return 2;
      }
      callsOutput = argv[index];
    } else if (argument == "--emit-rust-dir") {
      if (++index == argc) {
        std::cerr
            << "coge --legacy: --emit-rust-dir needs an output directory\n";
        return 2;
      }
      rustOutputDirectory = argv[index];
    } else if (argument.starts_with('-')) {
      std::cerr << "coge --legacy: unknown option " << argument << '\n';
      return 2;
    } else {
      inputs.emplace_back(argv[index]);
    }
  }
  if (inputs.empty()) {
    std::cerr << "usage: coge --legacy [--artifact DIR] [--calls OUTPUT | "
                 "--emit-rust-dir DIR] FILE.sema [FILE.sema ...]\n";
    return 2;
  }
  if ((!callsOutput.empty() && !rustOutputDirectory.empty()) ||
      (!rustOutputDirectory.empty() && inputs.size() != 1)) {
    std::cerr << "coge --legacy: --emit-rust-dir needs exactly one input and "
                 "cannot be "
                 "combined with --calls\n";
    return 2;
  }
  std::cerr << "coge --legacy: compatibility mode for old mixed inputs. "
               "Migrated ToyC/ToyCP use *_typed.coge with --contracts FILE. "
               "See agsem/DOCUMENT_CLI.md.\n";
  for (const auto &destination :
       {callsOutput, rustOutputDirectory / "sema_gen.rs",
        rustOutputDirectory / "sema_lib_gen.rs",
        rustOutputDirectory / "interpreter_gen.rs",
        rustOutputDirectory / "interpreter_properties_gen.rs",
        rustOutputDirectory / "lowering_gen.rs",
        rustOutputDirectory / "backend_c_gen.rs",
        rustOutputDirectory / "backend_llvm_gen.rs",
        rustOutputDirectory / "sema_modules.manifest"}) {
    if (destination.empty())
      continue;
    if (rustOutputDirectory.empty() && destination != callsOutput)
      continue;
    const auto outputPath =
        std::filesystem::absolute(destination).lexically_normal();
    for (const auto &input : inputs) {
      if (outputPath == std::filesystem::absolute(input).lexically_normal()) {
        std::cerr
            << "coge --legacy: output must differ from every input file\n";
        return 2;
      }
    }
  }

  try {
    if (artifactDirectory.empty())
      artifactDirectory = SEMA_DEFAULT_ARTIFACT_DIR;
    const auto package =
        agas::artifact::loadArtifactPackageDirectory(artifactDirectory);
    if (package.manifest.exactSourceSha256 != SEMA_SOURCE_SHA256)
      throw std::runtime_error(
          "artifact does not match this build's grammar sources");
    const agas::runtime::ArtifactLexerRuntime lexer{
        package.lexer, package.symbols.terminals.size(),
        package.symbols.channels.size()};
    const agas::runtime::ArtifactAstParser parser{
        package.parserTable, package.symbols, package.productions,
        package.reductions};
    bool valid = true;
    std::set<std::string> calls;
    for (const auto &path : inputs)
      valid = checkFile(path, parser, lexer,
                        callsOutput.empty() ? nullptr : &calls) &&
              valid;
    if (!valid)
      return 1;
    if (!callsOutput.empty()) {
      std::ofstream output(callsOutput, std::ios::binary);
      if (!output)
        throw std::runtime_error("cannot write calls file: " +
                                 callsOutput.string());
      for (const auto &name : calls)
        output << name << '\n';
      if (!output)
        throw std::runtime_error("cannot finish calls file: " +
                                 callsOutput.string());
      std::cout << callsOutput.string() << ": " << calls.size()
                << " unique call names\n";
    }
    if (!rustOutputDirectory.empty()) {
      const auto prepared = [&] {
        const auto source = readFile(inputs.front());
        const auto parsed = parser.parse(source, lexer);
        const auto *header = field(*parsed.root, "header");
        if (!header || header->elements.empty())
          throw std::runtime_error(
              "Rust generation requires a sema ... for header");
        const auto *sourceField = field(header->elements.front(), "source");
        if (!sourceField || sourceField->tokenText.size() < 2 ||
            sourceField->tokenText.front() != '"' ||
            sourceField->tokenText.back() != '"')
          throw std::runtime_error(
              "invalid source grammar path in sema header");
        const auto pathText =
            sourceField->tokenText.substr(1, sourceField->tokenText.size() - 2);
        if (pathText.find('\\') != std::string::npos)
          throw std::runtime_error(
              "escaped source grammar paths are not supported");
        const auto grammarPath = inputs.front().parent_path() / pathText;
        const agas::runtime::PackagedAgFrontend grammarFrontend{
            AGAS_PINNED_ARTIFACT_DIR};
        const auto grammar = grammarFrontend.parse(readFile(grammarPath));
        if (!grammar.accepted())
          throw std::runtime_error("cannot parse source grammar: " +
                                   grammarPath.string());
        return agsem::prepareSemanticModel(*parsed.root, *grammar.document);
      }();
      const auto generation = agsem::prepareGenerationModel(
          *prepared, *parser.parse(readFile(inputs.front()), lexer).root);
      const auto generated = agsem::emitRust(*prepared, generation);
      for (const auto &[name, content] : generated.modules) {
        static_cast<void>(content);
        const auto outputPath =
            std::filesystem::absolute(rustOutputDirectory / name)
                .lexically_normal();
        if (outputPath ==
            std::filesystem::absolute(inputs.front()).lexically_normal())
          throw std::runtime_error("module output must differ from input: " +
                                   name);
      }
      std::filesystem::create_directories(rustOutputDirectory);
      const auto manifest = rustOutputDirectory / "sema_modules.manifest";
      if (std::filesystem::is_symlink(manifest))
        throw std::runtime_error("module manifest must not be a symlink");
      std::set<std::string> previousModules;
      if (std::filesystem::exists(manifest)) {
        std::ifstream input(manifest, std::ios::binary);
        if (!input)
          throw std::runtime_error("cannot read module manifest: " +
                                   manifest.string());
        std::string name;
        if (!std::getline(input, name) || name != "agsem-modules-v1")
          throw std::runtime_error("invalid generated module manifest");
        while (std::getline(input, name)) {
          if (!isGeneratedModuleName(name) ||
              !previousModules.insert(name).second)
            throw std::runtime_error("invalid generated module manifest");
        }
      }
      for (const auto &[name, content] : generated.modules) {
        static_cast<void>(content);
        if (!isGeneratedModuleName(name))
          throw std::runtime_error("invalid generated module file name: " +
                                   name);
        if (std::filesystem::is_symlink(rustOutputDirectory / name))
          throw std::runtime_error("module output must not be a symlink: " +
                                   name);
        if (!previousModules.contains(name) &&
            std::filesystem::exists(rustOutputDirectory / name))
          throw std::runtime_error("refusing to overwrite unowned module: " +
                                   name);
      }
      const auto writeRust = [&](std::string_view name,
                                 const std::string &content) {
        const auto path = rustOutputDirectory / name;
        std::ofstream output(path, std::ios::binary);
        if (!output)
          throw std::runtime_error("cannot write Rust output: " +
                                   path.string());
        output << content;
        if (!output)
          throw std::runtime_error("cannot finish Rust output: " +
                                   path.string());
        std::cout << path.string() << ": Rust generated\n";
      };
      writeRust("sema_gen.rs", generated.sema);
      writeRust("sema_lib_gen.rs", generated.semaLib);
      if (!generated.interpreter.empty())
        writeRust("interpreter_gen.rs", generated.interpreter);
      if (!generated.properties.empty())
        writeRust("interpreter_properties_gen.rs", generated.properties);
      const auto writeOptional = [&](std::string_view name,
                                     const std::string &content,
                                     std::string_view header) {
        if (!content.empty()) {
          writeRust(name, content);
          return;
        }
        const auto path = rustOutputDirectory / name;
        if (std::filesystem::exists(path) ||
            std::filesystem::is_symlink(path)) {
          if (std::filesystem::is_symlink(path) ||
              !readFile(path).starts_with(header))
            throw std::runtime_error("refusing to remove unowned output: " +
                                     path.string());
          std::filesystem::remove(path);
        }
      };
      writeOptional("lowering_gen.rs", generated.lowering,
                    "// Generated by sema from lowering_model.\n");
      writeOptional("backend_c_gen.rs", generated.backendC,
                    "// Generated by sema from backend_c.\n");
      writeOptional("backend_llvm_gen.rs", generated.backendLlvm,
                    "// Generated by sema from backend_llvm.\n");
      for (const auto &[name, content] : generated.modules)
        writeRust(name, content);
      for (const auto &name : previousModules)
        if (!generated.modules.contains(name))
          std::filesystem::remove(rustOutputDirectory / name);
      std::ofstream manifestOutput(manifest, std::ios::binary);
      if (!manifestOutput)
        throw std::runtime_error("cannot write module manifest");
      manifestOutput << "agsem-modules-v1\n";
      for (const auto &[name, content] : generated.modules) {
        static_cast<void>(content);
        manifestOutput << name << '\n';
      }
      if (!manifestOutput)
        throw std::runtime_error("cannot finish module manifest");
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "coge --legacy: " << error.what() << '\n';
    return 2;
  }
}
