#include "cli/Help.h"

#include <ostream>

namespace agas::cli {

void printAgasHelp(std::ostream &output) {
  output << R"(Agas - validate an Agas grammar and generate LR parser data

Usage:
  agas [grammar.ag]
  ag, agas --check grammar.ag
  agas --table [grammar.ag]
  agas --diagnose-parse grammar.ag input-file
  agas --diagnose-conflict grammar.ag input-file
  agas --dump-table [grammar.ag]
  agas --dump-rust-parser [grammar.ag]
  agas --emit-rust-parser OUTPUT [grammar.ag] [artifact_dir]
  agas --emit-package OUTPUT grammar.ag
  agas --parse-package ARTIFACT INPUT
  agas --ast-stats ARTIFACT INPUT
  agas --help

Commands:
  (no option)           Parse and validate the grammar, then print a summary.
  --check               Parse and validate a grammar without generating files.
  --table               Build the requested LR(k) or LALR(k) table and print
                        automaton, conflict and storage statistics.
  --diagnose-parse      Show the parse tree, or the partial tree at a syntax
                        error. Return exit status 1 on rejection.
  --diagnose-conflict   Show both parse branches at the first shift/reduce
                        conflict, including incomplete trees on failure.
  --dump-table          Write the compressed ACTION/GOTO table DSL to stdout.
  --dump-rust-parser    Write a static Rust lexer, reductions and parser to
                        stdout.
  --emit-rust-parser    Write that static Rust module to OUTPUT. An optional
                        artifact_dir selects a reproduced bootstrap package.
  --emit-package        Write a complete versioned parser package to OUTPUT.
  --parse-package       Parse INPUT with an artifact and print neutral AST JSON.
  --ast-stats           Measure AST values, nodes and depth before wire export.
  -h, --help            Show this help and exit.

If grammar.ag is omitted, grammars/Ag.ag from the source tree is used. The
normal agas executable parses .ag files with the pinned artifact package and
does not invoke ANTLR. The options block may select parser=LR or parser=LALR
and a positive lookahead value; omitted parser options default to LR(1).
Grammars with lexerClasses support --table, --diagnose-parse and --emit-package.
Contextual packages run in C++ (--parse-package) and Rust with AST/coverage.
Static contextual Rust source and standalone table DSL export remain unsupported.
)";
}

void printBootstrapHelp(std::ostream &output) {
  output << R"(Agas ANTLR bootstrap - reproduce and inspect parser artifacts

Usage:
  agas-bootstrap [grammar.ag]
  agas-bootstrap --table [grammar.ag]
  agas-bootstrap --dump-table [grammar.ag]
  agas-bootstrap --emit-package OUTPUT [grammar.ag]
  agas-bootstrap --help

Commands:
  (no option)           Parse and validate the grammar, then print a summary.
  --check               Parse and validate a grammar without generating files.
  --table               Build the requested parser table and print statistics.
  --dump-table          Write the compressed table DSL to stdout.
  --emit-package        Write the complete versioned parser package to OUTPUT.
  -h, --help            Show this help and exit.

This is the optional reproducible bootstrap and oracle. Normal use should go
through the agas executable, which loads the pinned package without ANTLR.
)";
}

void printMergeReportHelp(std::ostream &output) {
  output << R"(Agas merge report - compare experimental LR(k) state merging

Usage:
  agas-merge-report [grammar.ag]
  agas-merge-report --help

Builds canonical LR(k), LALR(k), exact-action and compatible-union variants
for the algorithm and lookahead selected by the grammar. This is a diagnostic
experiment built with the ANTLR bootstrap, not the normal Agas frontend.
)";
}

} // namespace agas::cli
