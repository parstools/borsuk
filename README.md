![Borsuk logo](docs/images/borsuk_logo.jpeg)

# borsuk

**Borsuk** is the Polish word for **badger**.

Grammar-driven compiler toolkit: deterministic LR(k)/LALR(k) parsing with
Unicode lexers and explicit ASTs (ag), semantic analysis (sema) and code
generation (coge).

## Tools

| Tool | Input | Adds |
|--------|---------|-------------------------------------------------------------------|
| `ag`   | `.ag`   | Grammar: Unicode lexer, parser, token channels, AST construction  |
| `sema` | `.sema` | Everything in `.ag`, plus a semantic contract and analysis        |
| `coge` | `.coge` | Everything in `.sema`, plus an IR executor, lowering and backends |

Each file is self-contained: a `.sema` or `.coge` file carries its own
grammar and lexer, so it needs no separate `.ag` file.

## Deterministic parsing

A grammar is checked statically when its LR(k)/LALR(k) tables are built.
Conflicts are reported for the grammar itself, without sample inputs, and
the generated parser never backtracks. LL(*) and GLR parsers, in contrast,
accept such grammars and reveal ambiguity only on concrete input.

Parsers are built into portable artifacts, so a program loads a pinned,
versioned parser instead of regenerating it at startup.

## Features

- **Grammar-driven parsing:** canonical LR(k) and direct LALR(k), configurable
  lookahead, restricted EBNF, and explicit shift/reduce conflict policies.
- **Unicode lexing:** UTF-8 input, longest-match selection, lexer fragments,
  greedy and lazy quantifiers, token channels, and parser-scoped lexer classes.
- **Explicit syntax trees:** `node` and `inline` rules, named fields and variants,
  transparent expression forwarding, and separate payload and recognition spans.
- **Portable parser artifacts:** versioned packages with integrity checks,
  neutral AST reductions, C++ and Rust runtimes, and static Rust parser export.
- **Typed semantic analysis:** authored actions, inferred analyzer signatures,
  explicit runtime contracts, reusable policies, and generated Rust analyzers.
- **Execution and code generation:** typed IR interpreters, execution profiles,
  boundary properties, and Structured Core IR with C11 and LLVM backends for
  supported constructs. ToyC demonstrates these backends; ToyCP demonstrates
  interpretation with classes, inheritance, construction, and cleanup.
- **Migration and inspection tools:** grammar and analysis projections,
  specification templates, completeness reports, checked-model inspection,
  and output provenance.

The tools reject unsupported constructs explicitly. See the
[Ag format](ag/docs/AG_FORMAT.md), [document CLI](docs/DOCUMENT_CLI.md), and
[Core IR guide](coge/docs/CORE_IR.md) for supported subsets and current limits.

## Building ag, sema and coge

Build requirements are a C++20 compiler, CMake 4.0 or newer, ICU with its `uc`
component, and `nlohmann_json` 3.11 or newer. The build also needs the Zbik
submodule. Run these commands from the repository root:

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ag sema coge -j2
```

The executables are written to `build/bin/ag`, `build/bin/sema`, and
`build/bin/coge`. `agas` provides the same grammar CLI as `ag`; build it with
`cmake --build build --target agas -j2` if needed.

ANTLR bootstrap is disabled by default. Normal builds use the pinned parser
artifacts and require neither Java nor ANTLR. Bootstrap is an optional recovery
and reproducibility path; see the [Agas guide](ag/README.md).

### Checking example specifications

```sh
build/bin/ag --check ag/grammars/examples/cmm.ag
build/bin/sema --check \
  --contracts contracts/toyscope-runtime-v1.json \
  sema/examples/toyscope_1/toyscope_1.sema
build/bin/coge --check \
  --contracts contracts/toyc-runtime-v1.json \
  coge/examples/toyc/toyc.coge
```

Use `--help` on any executable for its available operations. Runtime contracts
are explicit inputs to semantic analysis and execution checking.

### Rust runtimes and generated examples

Compiling generated Rust requires Cargo and a Rust toolchain supporting the
2024 edition. Python 3 is needed for example regeneration. The checked-in
generated sources let you build examples without regenerating them first:

```sh
cargo test --locked --manifest-path rust/Cargo.toml --workspace
cargo build --locked --manifest-path coge/examples/toyc/Cargo.toml
cargo build --locked --manifest-path coge/examples/toycp/Cargo.toml
```

Once dependencies are cached, add `--offline` to avoid network access. To
regenerate examples explicitly, first build `agas`, then run
`python3 tools/regenerate_examples.py`. See the
[regeneration guide](docs/GENERATED.md) for output ownership and verification.
