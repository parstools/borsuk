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
cargo test --manifest-path rust/Cargo.toml --workspace
cargo build --manifest-path coge/examples/toyc/Cargo.toml
cargo build --manifest-path coge/examples/toycp/Cargo.toml
```

Once dependencies are cached, add `--offline` to avoid network access. To
regenerate examples explicitly, first build `agas`, then run
`python3 tools/regenerate_examples.py`. See the
[regeneration guide](docs/GENERATED.md) for output ownership and verification.

`Cargo.lock` files are generated locally and ignored by Git. The first Cargo
build resolves dependencies and creates a lockfile; later builds reuse it.
After that first resolution, `--locked` can enforce the local dependency snapshot.

## Working with ToyC and ToyCP

The authored specifications are `coge/examples/toyc/toyc.coge` and
`coge/examples/toycp/toycp.coge`. Each contains grammar, lexer, analysis and
execution declarations. The corresponding `contracts/*-runtime-v1.json` file
describes the handwritten runtime interfaces.

Use the three tools in this order:

1. **`coge`** checks the full specification, exports grammar/analysis projections,
   and generates the analyzer, interpreter and any declared backend adapters.
2. **`ag`** builds a parser package or static Rust parser from the `.ag` projection.
3. **`sema`** checks the `.sema` projection and generates analysis without execution.

From the repository root, generate both languages into an ignored build directory:

```sh
for language in toyc toycp; do
    source="coge/examples/$language/$language.coge"
    contracts="contracts/$language-runtime-v1.json"
    out="build/toy-workflow/$language"
    mkdir -p "$out"

    build/bin/coge --check --contracts "$contracts" "$source"
    build/bin/coge --force --emit-ag "$out/$language.ag" "$source"
    build/bin/coge --force --emit-sema "$out/$language.sema" \
        --contracts "$contracts" "$source"
    build/bin/coge --emit-rust-dir "$out/generated" \
        --contracts "$contracts" "$source"

    build/bin/ag --emit-rust-parser "$out/generated/parser_gen.rs" \
        "$out/$language.ag"
    build/bin/ag --emit-package "$out/parser-package" "$out/$language.ag"

    build/bin/sema --check --contracts "$contracts" "$out/$language.sema"
    build/bin/sema --emit-rust-dir "$out/analysis" \
        --contracts "$contracts" "$out/$language.sema"
done
```

The parser-package output directory must not already exist. These commands leave
the checked-in generated baselines unchanged. The analysis modules produced by
`sema` should match the analysis portion of `coge` output; `sema` does not produce
an interpreter or backend adapters. Generated Rust modules need the handwritten
context and runtime code from the example crates.

To regenerate the integrated example crates, use the repository script, which
updates their tracked `generated/` files and projections:

```sh
cmake --build build --target agas -j2
python3 tools/regenerate_examples.py toyc toycp
cargo build --manifest-path coge/examples/toyc/Cargo.toml
cargo build --manifest-path coge/examples/toycp/Cargo.toml
```

Run source programs with the resulting interpreters:

```sh
cargo run --manifest-path coge/examples/toyc/Cargo.toml \
    --bin toyc_interpret -- path/to/program.toyc
cargo run --manifest-path coge/examples/toycp/Cargo.toml \
    --bin toycp_interpret -- path/to/program.toycp
```

ToyC also generates lowering and C/LLVM backend adapters for its supported IR.
ToyCP currently uses interpretation, including classes, methods, inheritance,
arrays and cleanup. Plain `.ag` variants such as `toycm`, `toycmeta` and `toycpp`
describe syntax; a parser package alone does not add semantic analysis or execution.

## Building other grammar variants with ag

`ag/grammars/examples/` includes `toycmeta` (ToyCMeta), `toycpp` (ToyCPP),
`toylang` (ToyLang), and `toylangp` (ToyLangP). These standalone grammars can
be built using only `ag`; neither `sema` nor `coge` is required:

```sh
mkdir -p build/ag-variants
for grammar in toycmeta toycpp toylang toylangp; do
    build/bin/ag --emit-package "build/ag-variants/$grammar" \
        "ag/grammars/examples/$grammar.ag" || exit 1
    build/bin/ag --emit-rust-parser "build/ag-variants/$grammar.rs" \
        "ag/grammars/examples/$grammar.ag" || exit 1
done
```

Each package directory must be new. The package contains the lexer, LR table,
AST schema and reductions; the `.rs` file provides a static Rust parser module.
These four grammars request LALR(2) and explicitly resolve dangling else.
The output stays under the ignored `build/` directory.

For another grammar, substitute its path. `ag --table GRAMMAR.ag` reports table
statistics and conflicts, while `ag --emit-package OUT GRAMMAR.ag` requires a
deterministic table. Some historical C grammars and deliberately ambiguous test
grammars contain unresolved conflicts and cannot be exported as parser packages.
