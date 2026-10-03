#include "agas/bootstrap/AntlrFrontend.h"
#include "agas/generator/ArtifactGeneration.h"
#include "agas/generator/ParserGeneration.h"
#include "agas/generator/TableExport.h"
#include "agas/model/BnfLowering.h"
#include "agas/model/Validation.h"
#include "agas/runtime/SelfHostedAgFrontend.h"
#include "cli/Help.h"
#include "regex/UnicodeProperties.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

enum class Mode { Summary, TableSummary, DumpTable, EmitPackage };

void printIssues(const std::filesystem::path &path,
                 const std::vector<agas::bootstrap::SyntaxIssue> &issues) {
  for (const auto &issue : issues) {
    std::cerr << path.string() << ':' << issue.line << ':' << issue.column
              << ": " << issue.message << '\n';
  }
}

void printIssues(const std::filesystem::path &path,
                 const std::vector<agas::model::ValidationIssue> &issues) {
  for (const auto &issue : issues) {
    const char *severity =
        issue.severity == agas::model::DiagnosticSeverity::Error ? "error"
                                                                 : "warning";
    std::cerr << path.string() << ':' << issue.span.begin.line << ':'
              << issue.span.begin.column << ": " << severity << ": "
              << issue.message << '\n';
  }
}

void printIssues(const std::filesystem::path &path,
                 const std::vector<agas::runtime::AgFrontendIssue> &issues) {
  for (const auto &issue : issues) {
    const char *kind = "adapter";
    if (issue.kind == agas::runtime::AgFrontendIssueKind::Lexical) {
      kind = "lexical";
    } else if (issue.kind == agas::runtime::AgFrontendIssueKind::Syntax) {
      kind = "syntax";
    }
    std::cerr << path.string() << ":byte " << issue.span.beginByte << ':'
              << issue.span.endByte << ": " << kind
              << " error: " << issue.message << '\n';
  }
}

auto readFile(const std::filesystem::path &path) -> std::string {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("cannot open Agas grammar: " + path.string());
  }
  return {std::istreambuf_iterator<char>{stream},
          std::istreambuf_iterator<char>{}};
}

} // namespace

int main(int argc, char **argv) {
  Mode mode = Mode::Summary;
  std::filesystem::path path = AGAS_DEFAULT_GRAMMAR_FILE;
  std::filesystem::path packageOutput;
  if (argc == 2 && (std::string_view{argv[1]} == "--help" ||
                    std::string_view{argv[1]} == "-h")) {
    agas::cli::printBootstrapHelp(std::cout);
    return 0;
  } else if (argc >= 2 && std::string_view{argv[1]} == "--emit-package") {
    mode = Mode::EmitPackage;
    if (argc == 3 || argc == 4) {
      packageOutput = argv[2];
      if (argc == 4)
        path = argv[3];
    } else {
      std::cerr << "usage: agas-bootstrap --emit-package OUTPUT "
                   "[grammar.ag]\n";
      return 2;
    }
  } else if (argc >= 2 && (std::string_view{argv[1]} == "--table" ||
                           std::string_view{argv[1]} == "--dump-table")) {
    mode = std::string_view{argv[1]} == "--table" ? Mode::TableSummary
                                                  : Mode::DumpTable;
    if (argc == 3) {
      path = argv[2];
    } else if (argc != 2) {
      std::cerr << "usage: agas-bootstrap [--table|--dump-table] "
                   "[grammar.ag]\n";
      return 2;
    }
  } else if (argc == 2) {
    path = argv[1];
  } else if (argc != 1) {
    std::cerr << "usage: agas-bootstrap "
                 "[--table|--dump-table] [grammar.ag]\n";
    return 2;
  }

  try {
    const agas::bootstrap::ParseResult definition =
        agas::bootstrap::parseAgasFile(AGAS_DEFAULT_GRAMMAR_FILE);
    if (!definition.accepted()) {
      printIssues(AGAS_DEFAULT_GRAMMAR_FILE, definition.issues);
      return 1;
    }
    const agas::runtime::SelfHostedAgFrontend frontend{*definition.document};
    const std::string source = readFile(path);
    const agas::runtime::AgFrontendResult result = frontend.parse(source);
    if (!result.accepted()) {
      printIssues(path, result.issues);
      return 1;
    }

    const agas::model::SyntaxDocument &document = *result.document;
    const agas::model::ValidationResult validation =
        agas::model::validateSyntaxModel(document);
    printIssues(path, validation.issues);
    if (!validation.valid()) {
      return 1;
    }
    if (mode != Mode::Summary) {
      const agas::generator::GeneratedParserTable generated =
          agas::generator::generateParserTable(document);
      if (mode == Mode::EmitPackage) {
        const auto lexer =
            agas::generator::compileLexerAutomaton(document, generated.bnf());
        const auto package = agas::generator::buildParserArtifactPackage(
            document, generated, lexer, source, source,
            {AGAS_GENERATOR_VERSION, AGAS_ZBIK_REVISION,
             zbik::unicodeDataVersion()});
        agas::artifact::writeArtifactPackageDirectory(package, packageOutput);
        std::cout << "artifact=" << packageOutput.string()
                  << " sections=" << package.manifest.sections.size() << '\n';
        return 0;
      }
      if (mode == Mode::DumpTable) {
        std::cout << agas::generator::exportCompressedTableDsl(generated).text;
        return 0;
      }
      const zbik::LRkDfaStats &stats = generated.dfaStatistics();
      const char *algorithm = generated.configuration().algorithm ==
                                      agas::generator::ParserAlgorithm::Lalr
                                  ? "LALR"
                                  : "LR";
      std::cout << "grammar=" << document.grammarName << " parser=" << algorithm
                << '(' << generated.configuration().lookahead << ')'
                << " bnf_rules=" << generated.bnf().grammar().ruleCount()
                << " states=" << stats.states << " items=" << stats.items
                << " transitions=" << stats.transitions
                << " conflicts=" << generated.table().conflicts().size();
      if (!generated.table().hasConflicts()) {
        const agas::generator::CompressedTableDsl exported =
            agas::generator::exportCompressedTableDsl(generated);
        const zbik::TableStorageStats &storage = exported.storage;
        std::cout << " table_bytes=" << storage.uncompressedBytes
                  << " compressed_bytes=" << storage.compressedBytes
                  << " dsl_bytes=" << exported.text.size();
      }
      std::cout << '\n';
    } else {
      const agas::model::BnfModel bnf = agas::model::lowerToBnf(document);
      std::cout << "grammar=" << document.grammarName
                << " options=" << document.options.size()
                << " channels=" << document.channels.size()
                << " parser_rules=" << document.parserRules.size()
                << " lexer_rules=" << document.lexerRules.size()
                << " bnf_rules=" << bnf.grammar().ruleCount() << '\n';
    }
    return 0;
  } catch (const agas::generator::ParserConfigurationError &error) {
    std::cerr << path.string() << ':' << error.span().begin.line << ':'
              << error.span().begin.column << ": error: " << error.what()
              << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
