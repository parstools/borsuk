#include "agas/generator/ContextualGeneration.h"
#include "agas/generator/ArtifactGeneration.h"
#include "agas/generator/LexerGeneration.h"
#include "agas/generator/ParserGeneration.h"
#include "agas/generator/StaticRustParserExport.h"
#include "agas/generator/TableExport.h"
#include "agas/model/BnfLowering.h"
#include "agas/model/Validation.h"
#include "agas/runtime/PackagedAgFrontend.h"
#include "agas/runtime/ArtifactAstParser.h"
#include "agas/runtime/AstWireJson.h"
#include "cli/Help.h"
#include "regex/UnicodeProperties.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum class Mode {
  Summary,
  TableSummary,
  DumpTable,
  DumpRustParser,
  EmitRustParser,
  EmitPackage,
  ParsePackage,
  DiagnoseConflict,
  DiagnoseParse
};

struct TraceNode {
  std::string label;
  std::vector<std::unique_ptr<TraceNode>> children;
};

struct ConflictTrace {
  bool accepted{};
  bool visitedConflict{};
  std::size_t tokenIndex{};
  std::string problem;
  std::vector<std::unique_ptr<TraceNode>> forest;
};

auto parserTokens(const agas::generator::AgasLexResult &lexed)
    -> std::vector<const agas::generator::AgasLexedToken *> {
  std::vector<const agas::generator::AgasLexedToken *> result;
  for (const auto &token : lexed.tokens) {
    if (!token.channel)
      result.push_back(&token);
  }
  return result;
}

auto lookaheadAt(const zbik::ParseTable &table,
                 const std::vector<const agas::generator::AgasLexedToken *> &tokens,
                 std::size_t position) -> zbik::LookaheadWord {
  std::vector<zbik::LookaheadSymbol> symbols;
  while (position < tokens.size() && symbols.size() < table.maxLength())
    symbols.emplace_back(tokens[position++]->terminal);
  if (position == tokens.size() && symbols.size() < table.maxLength())
    symbols.emplace_back(zbik::endOfInput);
  return zbik::LookaheadWord{std::move(symbols)};
}

auto reductionLabel(const agas::generator::GeneratedParserTable &generated,
                    zbik::RuleId rule) -> std::string {
  const auto &origin = generated.productions().diagnostic(rule);
  std::string label = origin.sourceRuleName;
  if (origin.role == zbik::EbnfGeneratedRuleRole::SourceAlternative) {
    label += '#';
    label += origin.alternativeLabel.value_or(
        std::to_string(origin.alternativeIndex + 1));
  } else {
    label += origin.role == zbik::EbnfGeneratedRuleRole::OptionalEmpty
                 ? " [empty optional]"
                 : origin.role == zbik::EbnfGeneratedRuleRole::OptionalPresent
                       ? " [present optional]"
                       : origin.role == zbik::EbnfGeneratedRuleRole::RepetitionBase
                             ? " [empty repetition]"
                             : " [repetition]";
  }
  return label;
}

auto traceParse(
    const agas::generator::GeneratedParserTable &generated,
    const agas::generator::AgasLexResult &lexed,
    const zbik::Conflict *conflict = nullptr,
    const zbik::Action *choice = nullptr)
    -> ConflictTrace {
  const auto &table = generated.table();
  const auto &grammar = table.grammar();
  const auto tokens = parserTokens(lexed);
  ConflictTrace trace;
  std::vector<zbik::StateId> states{table.start()};
  for (std::size_t step = 0; step < 100000; ++step) {
    const auto lookahead = lookaheadAt(table, tokens, trace.tokenIndex);
    const auto state = states.back();
    const auto &cell = table.actions(state, lookahead);
    const zbik::Action *action = nullptr;
    if (conflict && state == conflict->state() &&
        lookahead == conflict->lookahead()) {
      action = choice;
      trace.visitedConflict = true;
    } else if (cell.size() == 1) {
      action = &cell.actions().front();
    } else {
      trace.problem = cell.empty() ? "syntax error" : "another unresolved conflict";
      return trace;
    }
    if (const auto *shift = std::get_if<zbik::Shift>(action)) {
      if (trace.tokenIndex >= tokens.size()) {
        trace.problem = "shift past end of input";
        return trace;
      }
      const auto *token = tokens[trace.tokenIndex++];
      auto node = std::make_unique<TraceNode>();
      node->label = grammar.terminalName(token->terminal) + " '" + token->text + "'";
      trace.forest.push_back(std::move(node));
      states.push_back(shift->target);
    } else if (const auto *reduce = std::get_if<zbik::Reduce>(action)) {
      const auto &rule = grammar.rule(reduce->rule);
      if (rule.size() >= states.size() || rule.size() > trace.forest.size()) {
        trace.problem = "invalid reduction stack";
        return trace;
      }
      auto node = std::make_unique<TraceNode>();
      node->label = reductionLabel(generated, reduce->rule);
      const auto first = trace.forest.size() - rule.size();
      for (std::size_t index = first; index < trace.forest.size(); ++index)
        node->children.push_back(std::move(trace.forest[index]));
      trace.forest.resize(first);
      states.resize(states.size() - rule.size());
      const auto target = table.goTo(states.back(), rule.lhs());
      if (!target) {
        trace.problem = "missing GOTO after reduction";
        return trace;
      }
      trace.forest.push_back(std::move(node));
      states.push_back(*target);
    } else {
      trace.accepted = true;
      return trace;
    }
  }
  trace.problem = "trace step limit reached";
  return trace;
}

void printTree(const TraceNode &node, std::size_t depth, std::size_t &remaining) {
  if (remaining == 0)
    return;
  --remaining;
  std::cout << std::string(depth * 2, ' ') << node.label << '\n';
  if (depth == 30) {
    std::cout << std::string((depth + 1) * 2, ' ') << "...\n";
    return;
  }
  for (const auto &child : node.children)
    printTree(*child, depth + 1, remaining);
}

void printTrace(const ConflictTrace &trace,
                const agas::generator::AgasLexResult &lexed,
                bool showConflict = true) {
  const auto tokens = parserTokens(lexed);
  std::cout << "  status="
            << (trace.accepted ? "accepted" : "incomplete-or-rejected");
  if (showConflict)
    std::cout << " conflict_reached=" << (trace.visitedConflict ? "yes" : "no");
  std::cout << " consumed=" << trace.tokenIndex << '/' << tokens.size();
  if (!trace.problem.empty())
    std::cout << " reason=" << trace.problem;
  if (trace.tokenIndex < tokens.size())
    std::cout << " next='" << tokens[trace.tokenIndex]->text << '\'';
  std::cout << '\n';
  std::size_t remaining = 160;
  for (const auto &node : trace.forest)
    printTree(*node, 1, remaining);
  if (remaining == 0)
    std::cout << "  ... tree output truncated\n";
}

auto readFile(const std::filesystem::path &path) -> std::string {
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    throw std::runtime_error("cannot open Agas grammar: " + path.string());
  return {std::istreambuf_iterator<char>{stream},
          std::istreambuf_iterator<char>{}};
}

void writeFile(const std::filesystem::path &path, std::string_view contents) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream || !stream.write(contents.data(), contents.size()))
    throw std::runtime_error("cannot write generated Rust parser: " +
                             path.string());
  stream.close();
  if (!stream)
    throw std::runtime_error("cannot finish generated Rust parser: " +
                             path.string());
}

void printIssues(const std::filesystem::path &path,
                 const std::vector<agas::runtime::AgFrontendIssue> &issues) {
  for (const auto &issue : issues) {
    const char *kind = "adapter";
    if (issue.kind == agas::runtime::AgFrontendIssueKind::Lexical)
      kind = "lexical";
    else if (issue.kind == agas::runtime::AgFrontendIssueKind::Syntax)
      kind = "syntax";
    std::cerr << path.string() << ":byte " << issue.span.beginByte << ':'
              << issue.span.endByte << ": " << kind
              << " error: " << issue.message << '\n';
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

} // namespace

int main(int argc, char **argv) {
  Mode mode = Mode::Summary;
  std::filesystem::path path = AGAS_DEFAULT_GRAMMAR_FILE;
  std::filesystem::path artifactDirectory = AGAS_DEFAULT_ARTIFACT_DIR;
  std::optional<std::filesystem::path> outputPath;
  std::optional<std::filesystem::path> inputPath;
  if (argc == 3 && std::string_view{argv[1]} == "--check") {
    path = argv[2];
  } else if (argc == 2 && (std::string_view{argv[1]} == "--help" ||
                    std::string_view{argv[1]} == "-h")) {
    agas::cli::printAgasHelp(std::cout);
    return 0;
  } else if (argc >= 2 && (std::string_view{argv[1]} == "--diagnose-conflict" ||
              std::string_view{argv[1]} == "--diagnose-parse")) {
    if (argc != 4) {
      std::cerr << "usage: agas --diagnose-parse|--diagnose-conflict "
                   "grammar.ag input-file\n";
      return 2;
    }
    mode = std::string_view{argv[1]} == "--diagnose-conflict"
               ? Mode::DiagnoseConflict
               : Mode::DiagnoseParse;
    path = argv[2];
    inputPath = argv[3];
  } else if (argc >= 2 && (std::string_view{argv[1]} == "--parse-package" ||
                           std::string_view{argv[1]} == "--ast-stats")) {
    if (argc != 4) {
      std::cerr << "usage: agas --parse-package|--ast-stats ARTIFACT INPUT\n";
      return 2;
    }
    mode = Mode::ParsePackage;
    artifactDirectory = argv[2];
    inputPath = argv[3];
  } else if (argc >= 2 && std::string_view{argv[1]} == "--emit-rust-parser") {
    if (argc < 3 || argc > 5) {
      std::cerr << "usage: agas --emit-rust-parser OUTPUT [grammar.ag] "
                   "[artifact_dir]\n";
      return 2;
    }
    mode = Mode::EmitRustParser;
    outputPath = argv[2];
    if (argc >= 4)
      path = argv[3];
    if (argc == 5)
      artifactDirectory = argv[4];
  } else if (argc >= 2 && std::string_view{argv[1]} == "--emit-package") {
    if (argc != 4) {
      std::cerr << "usage: agas --emit-package OUTPUT grammar.ag\n";
      return 2;
    }
    mode = Mode::EmitPackage;
    outputPath = argv[2];
    path = argv[3];
  } else if (argc >= 2 && (std::string_view{argv[1]} == "--table" ||
                           std::string_view{argv[1]} == "--dump-table" ||
                           std::string_view{argv[1]} == "--dump-rust-parser")) {
    if (std::string_view{argv[1]} == "--table")
      mode = Mode::TableSummary;
    else if (std::string_view{argv[1]} == "--dump-table")
      mode = Mode::DumpTable;
    else
      mode = Mode::DumpRustParser;
    if (argc == 3)
      path = argv[2];
    else if (argc != 2) {
      std::cerr << "usage: agas [--table|--dump-table|--dump-rust-parser] "
                   "[grammar.ag]\n";
      return 2;
    }
  } else if (argc == 2) {
    path = argv[1];
  } else if (argc != 1) {
    std::cerr << "usage: agas [--table|--dump-table|--dump-rust-parser] "
                 "[grammar.ag]\n";
    return 2;
  }

  try {
    if (mode == Mode::ParsePackage) {
      const auto package = agas::artifact::loadArtifactPackageDirectory(artifactDirectory);
      const agas::runtime::ArtifactLexerRuntime lexer{package.lexer, package.symbols.terminals.size(), package.symbols.channels.size()};
      const agas::runtime::ArtifactAstParser parser{package.parserTable, package.symbols, package.productions, package.reductions};
      const auto source = readFile(*inputPath);
      const auto result = parser.parse(source, lexer);
      if (!result.accepted()) { std::cerr << "source has a syntax error\n"; return 1; }
      if (std::string_view{argv[1]} == "--ast-stats") {
        const auto stats = agas::runtime::measureAst(*result.root);
        std::cout << "{\"values\":" << stats.values << ",\"nodes\":" << stats.nodes
                  << ",\"maximumDepth\":" << stats.maximumDepth
                  << ",\"maximumNodeDepth\":" << stats.maximumNodeDepth << "}\n";
        return 0;
      }
      const auto hash = [&](agas::artifact::ArtifactSectionKind kind) -> std::string_view {
        for (const auto &section : package.manifest.sections)
          if (section.kind == kind) return section.sha256;
        throw std::logic_error("missing artifact section");
      };
      const auto name = inputPath->filename().string();
      std::cout << agas::runtime::dumpAstWireJson(*result.root, {
          package.astSchema.version, hash(agas::artifact::ArtifactSectionKind::Symbols),
          hash(agas::artifact::ArtifactSectionKind::AstSchema), package.symbols.terminals.size(), name, source});
      return 0;
    }
    const agas::runtime::PackagedAgFrontend frontend{artifactDirectory};
    const std::string source = readFile(path);
    const auto parsed = frontend.parse(source);
    if (!parsed.accepted()) {
      printIssues(path, parsed.issues);
      return 1;
    }
    const auto &document = *parsed.document;
    auto validation = agas::model::validateSyntaxModel(document);
    for (const auto &option : document.options)
      if (option.name == "generate_interpreter")
        validation.issues.push_back(
            {agas::model::DiagnosticSeverity::Error, option.span,
             "generate_interpreter belongs in coge generation"});
    printIssues(path, validation.issues);
    if (!validation.valid())
      return 1;
    if (mode == Mode::Summary) {
      const auto bnf = agas::model::lowerToBnf(document);
      std::cout << "grammar=" << document.grammarName
                << " options=" << document.options.size()
                << " channels=" << document.channels.size()
                << " parser_rules=" << document.parserRules.size()
                << " lexer_rules=" << document.lexerRules.size()
                << " bnf_rules=" << bnf.grammar().ruleCount() << '\n';
      return 0;
    }

    if (!document.lexerClasses.empty()) {
      if (mode != Mode::TableSummary && mode != Mode::DiagnoseParse &&
          mode != Mode::EmitPackage)
        throw std::runtime_error(
            "lexer classes support --table, --diagnose-parse and "
            "--emit-package; static Rust/DSL export is unavailable");
      const auto generated =
          agas::generator::generateContextualParser(document);
      if (mode == Mode::EmitPackage) {
        const auto package = agas::generator::buildContextualArtifactPackage(
            document, generated, source,
            {AGAS_GENERATOR_VERSION, AGAS_ZBIK_REVISION,
             zbik::unicodeDataVersion()});
        agas::artifact::writeArtifactPackageDirectory(package, *outputPath);
        std::cout << "artifact=" << outputPath->string()
                  << " sections=" << package.manifest.sections.size() << '\n';
        return 0;
      }
      const auto configuration = agas::generator::parserConfiguration(document);
      std::cout << "grammar=" << document.grammarName << " parser="
                << (configuration.algorithm ==
                            agas::generator::ParserAlgorithm::Lalr
                        ? "LALR"
                        : "LR")
                << '(' << configuration.lookahead
                << ") states=" << generated.statistics.states
                << " conflicts=" << generated.table.conflicts().size()
                << " resolved=" << generated.resolved << '\n';
      for (const auto &conflict : generated.table.conflicts())
        std::cout << "unresolved " << conflict.dump() << '\n';
      if (generated.table.hasConflicts())
        return 1;
      const auto machine = [&] {
        try {
          return zbik::ContextualLRMachine{generated.table,
                                           generated.scoped.requirements,
                                           generated.scoped.sourceTerminals};
        } catch (const zbik::LexerContextError &error) {
          for (std::size_t i = 0; i < document.lexerClasses.size(); ++i) {
            if (!(error.conflictingBits() & (zbik::LexerClassMask{1} << i)))
              continue;
            const auto &entry = document.lexerClasses[i];
            std::cerr << path.string() << ':' << entry.span.begin.line << ':'
                      << entry.span.begin.column
                      << ": conflicting lexer class `" << entry.name << "`\n";
            for (const auto &rule : document.parserRules)
              for (const auto &command : rule.lexerContext)
                if (command.argument == entry.name)
                  std::cerr << "  " << rule.name << " " << command.name
                            << " at " << command.span.begin.line << ':'
                            << command.span.begin.column << '\n';
          }
          throw;
        }
      }();
      machine.validateLexer(generated.lexer.rules());
      if (mode == Mode::DiagnoseParse) {
        const auto result =
            machine.parse(generated.lexer, readFile(*inputPath), true);
        for (const auto &token : result.tokens)
          std::cout << "token="
                    << generated.source.grammar().terminalName(token.terminal)
                    << " offset=" << token.offset << " text=" << token.text
                    << '\n';
        for (const auto &request : result.requests)
          std::cout << "lex-offset=" << request.byteOffset
                    << " active-mask=" << request.active << '\n';
        std::cout << (result.accepted ? "accepted"
                                      : "rejected: " + result.error)
                  << '\n';
        return result.accepted ? 0 : 1;
      }
      return 0;
    }

    const auto generated = agas::generator::generateParserTable(document);
    if (mode == Mode::DiagnoseParse) {
      const auto lexer = agas::generator::compileLexerAutomaton(
          document, generated.bnf());
      const auto lexed = lexer.tokenize(readFile(*inputPath));
      const auto trace = traceParse(generated, lexed);
      printTrace(trace, lexed, false);
      return trace.accepted ? 0 : 1;
    }
    if (mode == Mode::DiagnoseConflict) {
      const zbik::Conflict *conflict = nullptr;
      if (!generated.resolvedConflicts().empty())
        conflict = &generated.resolvedConflicts().front().original;
      else if (!generated.table().conflicts().empty())
        conflict = &generated.table().conflicts().front();
      if (!conflict) {
        std::cerr << "grammar has no shift/reduce conflict to diagnose\n";
        return 1;
      }
      const auto lexer = agas::generator::compileLexerAutomaton(
          document, generated.bnf());
      const auto lexed = lexer.tokenize(readFile(*inputPath));
      std::cout << conflict->dump() << '\n';
      if (!generated.resolvedConflicts().empty())
        std::cout << "policy-selected "
                  << zbik::dumpAction(generated.resolvedConflicts().front().selected)
                  << '\n';
      for (const auto &action : conflict->actions()) {
        std::cout << "branch " << zbik::dumpAction(action);
        if (const auto *reduce = std::get_if<zbik::Reduce>(&action))
          std::cout << " (" << reductionLabel(generated, reduce->rule) << ')';
        std::cout << '\n';
        printTrace(traceParse(generated, lexed, conflict, &action), lexed);
      }
      return 0;
    }
    if (mode == Mode::EmitPackage) {
      const auto lexer = agas::generator::compileLexerAutomaton(
          document, generated.bnf());
      const auto package = agas::generator::buildParserArtifactPackage(
          document, generated, lexer, source, source,
          {AGAS_GENERATOR_VERSION, AGAS_ZBIK_REVISION,
           zbik::unicodeDataVersion()});
      agas::artifact::writeArtifactPackageDirectory(package, *outputPath);
      std::cout << "artifact=" << outputPath->string()
                << " sections=" << package.manifest.sections.size() << '\n';
      return 0;
    }
    if (mode == Mode::DumpRustParser || mode == Mode::EmitRustParser) {
      auto [symbols, productions] =
          agas::generator::buildGrammarArtifactSections(document, generated);
      const auto table = agas::artifact::parseParserTableDsl(
          agas::generator::exportCompressedTableDsl(generated).text, symbols,
          productions);
      const auto lexer = agas::generator::buildLexerArtifactSection(
          agas::generator::compileLexerAutomaton(document, generated.bnf()),
          symbols);
      const std::string rust =
          agas::generator::exportStaticRustLexer(lexer, symbols) + "\n" +
          agas::generator::exportStaticRustReductions(generated.reductions(),
                                                      productions) +
          "\n" +
          agas::generator::exportStaticRustParser(table, symbols, productions);
      if (mode == Mode::EmitRustParser)
        writeFile(*outputPath, rust);
      else
        std::cout << rust;
      return 0;
    }
    if (mode == Mode::DumpTable) {
      std::cout << agas::generator::exportCompressedTableDsl(generated).text;
      return 0;
    }
    const auto &stats = generated.dfaStatistics();
    const char *algorithm = generated.configuration().algorithm ==
                                    agas::generator::ParserAlgorithm::Lalr
                                ? "LALR"
                                : "LR";
    std::cout << "grammar=" << document.grammarName << " parser=" << algorithm
              << '(' << generated.configuration().lookahead << ')'
              << " bnf_rules=" << generated.bnf().grammar().ruleCount()
              << " states=" << stats.states << " items=" << stats.items
              << " transitions=" << stats.transitions
              << " conflicts=" << generated.table().conflicts().size()
              << " resolved=" << generated.resolvedConflicts().size();
    if (!generated.table().hasConflicts()) {
      const auto exported =
          agas::generator::exportCompressedTableDsl(generated);
      std::cout << " table_bytes=" << exported.storage.uncompressedBytes
                << " compressed_bytes=" << exported.storage.compressedBytes
                << " dsl_bytes=" << exported.text.size();
    }
    std::cout << '\n';
    for (const auto &conflict : generated.table().conflicts()) {
      std::cout << "unresolved " << conflict.dump() << '\n';
      for (const auto &action : conflict.actions()) {
        if (const auto *reduce = std::get_if<zbik::Reduce>(&action)) {
          const auto &origin = generated.productions().diagnostic(reduce->rule);
          std::cout << "  reduce-source=" << origin.sourceRuleName << '#'
                    << origin.alternativeIndex << " role="
                    << static_cast<int>(origin.role) << " at="
                    << origin.sourceSpan.begin.line << ':'
                    << origin.sourceSpan.begin.column << '\n';
        }
      }
    }
    for (const auto &resolution : generated.resolvedConflicts()) {
      const auto &preference =
          document.conflictPreferences.at(resolution.preferenceIndex);
      std::cout << "resolved state=" << resolution.original.state().value
                << " lookahead=" << resolution.original.lookahead().dump()
                << " original=" << resolution.original.dump()
                << " selected=" << zbik::dumpAction(resolution.selected)
                << " source=" << preference.reduceRule << '#'
                << preference.reduceAlternative << " terminal="
                << preference.shiftTerminal << " at="
                << preference.span.begin.line << ':'
                << preference.span.begin.column << '\n';
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
