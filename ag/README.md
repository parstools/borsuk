# Agas C++

[English](README.md) | [Polski](README.pl.md)

Agas reads .ag grammars, validates them and generates deterministic LR(k) or
LALR(k) tables. Definitions include a Unicode lexer, restricted parser EBNF,
token channels and neutral AST construction through node/inline rules.
Normal agas parses using pinned versioned artifacts/ag/v1 without starting
ANTLR or rebuilding its grammar. agas-bootstrap is an optional reconstruction
and ANTLR comparison tool. The project builds lexers/tables/neutral reductions
and static Rust modules but is not yet an installable SDK or complete application
generator.

## Quick start without ANTLR

Requirements: C++20 compiler, CMake ≥3.20, ICU for Zbik and nlohmann_json ≥3.11.
Zbik sources live in the repository's zbik directory. Run the commands below
from the repository root. ANTLR bootstrap is disabled by default; normal C++/
Rust builds use the pinned package/static sources without Java, ANTLR or build.rs
parser generation. Explicit agas_check_reproducibility reconstructs without
ANTLR. Optional agas_reproduce_from_antlr requires Java, ANTLR runtime 4.10 and
the pinned JAR (AGAS_ANTLR_JAR can supply its path). Neither target modifies
pinned source files. Both compare outputs byte-for-byte. Ordinary CTest does
the short pinned-package/exporter check; full table generation is explicit.


```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
cargo test --offline --manifest-path rust/Cargo.toml
```

```bash
cmake --build build \
  --target agas_check_reproducibility -j2
```

```bash
cmake -S . -B build -DAGAS_BUILD_ANTLR_BOOTSTRAP=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build \
  --target agas_reproduce_from_antlr -j2
```

## Pinned v1 artifact

artifacts/ag/v1 contains exactly manifest.json, symbols.json, lexer.json,
parser.dsl, productions.json, reductions.json and ast-schema.json. The manifest
records format version, generator settings, source identities and section hashes.
The loader validates completeness, limits, references and hashes before parsing;
C++/Rust read the same bytes. v1 pins package format, not all public APIs or future
Ag syntax. Incompatible changes need a new version/directory, not reinterpretation
of existing v1. See [AST_WIRE_V1.md](docs/AST_WIRE_V1.md).
The sample summary counts options/channels/parser rules/lexer rules/BNF productions;
counts may evolve with grammar and EBNF transformation. --help lists all modes.


```bash
build/bin/agas \
  ag/grammars/examples/cmm.ag
```

```text
grammar=Cmm options=3 channels=1 parser_rules=21 lexer_rules=21 bnf_rules=45
```

```bash
build/bin/agas --help
```

## Minimal Ag grammar

The first parser rule is the start. Final EOF accepts the complete input without
creating an AST field. node creates a named node, values= retains a list of
numbers and WS is skipped. parser=LR defaults to canonical LR(k); LALR selects
direct LALR(k). lookahead is positive, defaults to 1 and is never auto-increased.
ast=explicit documents current node/inline/field semantics rather than selecting
a second mode. The full contract is [AG_FORMAT.md](docs/AG_FORMAT.md), with
grammars/examples/cmm.ag as a small example and grammars/Ag.ag as the self grammar.


```text
grammar Numbers;

options {
    parser = LR;
    lookahead = 1;
    ast = explicit;
}

node document
    : values=NUMBER+ EOF
    ;

NUMBER
    : [0-9]+
    ;

WS
    : [ \t\r\n]+ -> skip
    ;
```

```text
options {
    parser = LALR;
    lookahead = 2;
    ast = explicit;
}
```

## Nodes, inline rules, fields and variants

node provides named AST identity, with transparent chains/grouping forwarding
their operand. inline forwards one field, builds a technical multi-field record
or returns unit with none. Labels such as value= capture fields; #NameFactor
names an entire node alternative. Unlabeled symbols affect recognition/spans
but create no fields. Begin with node and use inline for delegation/scaffolding.

For first=operand rest=tail*, an empty tail forwards first; real operands/operators
remain. LPAREN value=expr RPAREN forwards expr while recognizedSpan includes
parentheses. Exact conditions are in the format guide. --ast-stats PACKAGE FILE
measures before JSON: maximumDepth includes lists/options/tokens, while
maximumNodeDepth counts named nodes including root as one. Rust equivalent:
agas_ast_wire --stats PACKAGE FILE. Wire read/export limit is 200 technical levels.


```text
node factor
    : value=ID                   #NameFactor
    | value=NUMBER               #NumberFactor
    | LPAREN value=expr RPAREN   #GroupedFactor
    ;

inline argument
    : value=expr
    ;
```

## Agas commands

Commands below show the main validation and export operations.

### Validation and summary

Pinned Agas lexes/parses the grammar, builds a syntax model, validates references/
AST rules, lowers EBNF to BNF and prints counts. No path selects grammars/Ag.ag.


```bash
agas [grammar.ag]
```

### Table statistics

Builds the configured algorithm/k and reports BNF rules/states/items/transitions/
conflicts. Conflict-free tables also report plain/compressed-byte estimates and
DSL size. No source-order conflict choice or automatic k increase occurs.
Explicit conflicts can prefer one shift/reduce pair; resolved counts chosen
cells. --table currently reports unresolved conflicts without failing the process,
so automation must inspect conflicts=.


```bash
agas --table grammar.ag
```

### Compressed table export

Writes deterministic ACTION/GOTO DSL. Symbols, productions, lexer, reductions
and AST schema are separate package sections; this is not a whole artifact.


```bash
agas --dump-table grammar.ag > parser.dsl
```

### Complete parser package

The pinned self parser permits generation without ANTLR. Output contains a
versioned manifest, lexer, table, symbols, productions, reductions and schema;
optional diagnostics records explicit conflict decisions. Example packages need
not be committed. ToyC's complete parser/runtime/C/LLVM route is documented in
[CORE_IR.md](../coge/docs/CORE_IR.md).


```bash
agas --emit-package /tmp/parser-package grammar.ag
```

### Static Rust module export

Both dump-to-stdout and explicit-file forms generate a static Unicode lexer,
neutral reductions and LR table. The optional artifact directory selects a just
reconstructed bootstrap package instead of the pinned one. Output uses the Rust
runtime API under development. Other major modes use agas OPTION [grammar.ag];
arbitrary option combinations are not supported. Exit codes: 0 completed,
1 file/syntax/validation/generation failure, 2 invalid arguments.


```bash
agas --dump-rust-parser grammar.ag > generated_parser.rs
agas --emit-rust-parser generated_parser.rs grammar.ag
agas --emit-rust-parser generated_parser.rs grammar.ag reproduced_artifact_dir
```

## Lexer

Longest match wins, with earlier-rule tie priority. Supported features include
fragments, regex groups, Unicode classes/ranges, greedy and lazy *?/+?, skip and
channel(NAME). UTF-8 must be valid; token spans are half-open byte ranges.
Only default-channel tokens reach the parser; named-channel tokens remain for
comments/tooling. Initial scope excludes ANTLR modes, more, type, pushMode and
arbitrary embedded code.


```text
channels { HIDDEN }

LINE_COMMENT
    : '//' ~[\r\n]* -> channel(HIDDEN)
    ;

WS
    : [ \t\r\n]+ -> skip
    ;
```

## Restricted parser EBNF

Alternation is rule-level. ?/*/+ apply to one symbol/literal. Use empty for
empty alternatives and named rules instead of anonymous parser groups.
Lexer regex groups remain valid because they create no AST levels.


```text
node parameterList
    : first=parameter rest=additionalParameter*
    ;

inline additionalParameter
    : COMMA value=parameter
    ;
```

## Optional ANTLR bootstrap

Normal use needs no bootstrap. Enable it explicitly for Java and exactly ANTLR
C++ runtime 4.10. CMake downloads the pinned official 4.10 JAR into the build
directory or accepts AGAS_ANTLR_JAR. agas-bootstrap uses ANTLR to read Ag.ag,
then validates targets with its own frontend. Package export writes every section;
do not replace artifacts/ag/v1 without byte-reproduction checks.
The optional experimental agas-merge-report measures canonical LR(k), LALR(k)
and two selective merging modes; it is not normal generation workflow.


```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DAGAS_BUILD_ANTLR_BOOTSTRAP=ON
cmake --build build -j2
```

```bash
build/bin/agas-bootstrap --help
build/bin/agas-bootstrap --table grammar.ag
build/bin/agas-bootstrap \
  --emit-package /tmp/parser-package grammar.ag
```

```bash
build/bin/agas-merge-report --help
build/bin/agas-merge-report grammar.ag
```

## C++ library use

agas_core provides the packaged frontend. After SyntaxDocument, use
validateSyntaxModel, lowerToBnf or generateParserTable. agas_core links zbik_core;
LR/EBNF/compression algorithms are not copied. CLI artifact paths are currently
embedded at compile time and no install target exists. Use the library inside
a source tree or explicitly manage the package directory.


```cpp
#include <string>

#include "agas/runtime/PackagedAgFrontend.h"

agas::runtime::PackagedAgFrontend frontend{"path/to/artifacts/ag/v1"};
const auto result = frontend.parse(source);
if (!result.accepted()) {
    // result.issues contains lexical, syntax or adapter errors.
}
```

```cmake
add_subdirectory(path/to/Borsuk)
target_link_libraries(my_tool PRIVATE agas_core)
```

## Project documentation

- [AG_FORMAT.md](docs/AG_FORMAT.md): current grammar/lexer/AST contract.
- [Grammar inventory](grammars/README.md).
- [WHY_LR.md](docs/WHY_LR.md): explicit small lookahead rationale.
- [C_GRAMMARS_V4.md](docs/C_GRAMMARS_V4.md),
  [C90_CONVERSION.md](docs/C90_CONVERSION.md) and
  [CMINUS_GRAMMARS.md](docs/CMINUS_GRAMMARS.md): larger grammar experiments.
- [Regeneration](../docs/GENERATED.md) and [document CLI](../docs/DOCUMENT_CLI.md).

Historical roadmap/bootstrap audit/system-direction notes were not imported.

## Current limitations

No installer/stable public API, general precedence system, .agi preprocessor,
C90/C99 variant configuration or full incomplete-input recovery exists. Explicit
shift/reduce conflict policies are supported as described above. Rust export/
runtime APIs continue evolving; GLR/IELR are future extensions. For grammar/LR
algorithms independently of Ag, see [Zbik](../zbik/README.md).
