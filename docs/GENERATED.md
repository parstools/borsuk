# Generated Rust example files

[English](GENERATED.md) | [Polski](GENERATED.pl.md)

Each example keeps handwritten code in `src/` and generator artifacts in the
adjacent `generated/` directory:

| File | Contents |
| --- | --- |
| `parser_gen.rs` | Parser, lexer, and AST reductions from Agas |
| `sema_gen.rs` | Analyzers from `analysis` blocks |
| `sema_lib_gen.rs` | `semantic_model` functions |
| `interpreter_gen.rs` | IR executor types, state, and functions |
| `interpreter_properties_gen.rs` | Boundary tests from `properties` |

Not every example needs every file. Rust includes them through
`#[path = "../generated/..."]` or `include!("../generated/...")`.
Handwritten ToyC/ToyCP tests stay in `src/interpreter_tests.rs`. ToyCP uses
generated `sema_gen.rs` to build IR from the parser tree and executes it through
`interpreter_gen.rs`; tests cover the full path from source text to result.

## Regeneration

From the repository root:

```sh
cmake --build build --target ag agas sema coge -j4
python3 tools/regenerate_examples.py
python3 tools/regenerate_examples.py toyc
```

Use `--coge` and `--agas` to select binaries. `--sema` remains an alias for
`--coge`; `--sema-cli` selects the separate analysis binary.

ToyC/ToyCP use `coge/examples/{toyc,toycp}/*.coge` and explicit
`contracts/*-runtime-v1.json`. Their parser is generated from grammar exported
from the same `.coge`, without reading a separate authored Ag file. The script
also refreshes `.sema`, `.ag`, and manifests in each example's
`generated/projections/`; see [PROJECTIONS.md](PROJECTIONS.md).

Typed ToyScope uses `.coge` as the full description and a generated `.sema`
projection as analysis input. Its executor is handwritten; retained contracts
require the conversion described in [TOYSCOPE.md](../sema/docs/TOYSCOPE.md).
Other examples still use explicit `--legacy`.

Help text explains argument migration. Migrated `check_*` scripts require a
standalone coge source and `--contracts FILE`; missing arguments show concrete
example paths. See [CLI migration](DOCUMENT_CLI.md#command-migration).

Regeneration saves direct generator output. ToyC/ToyCP and typed ToyScope
baseline comparisons normalize both Rust copies using `rustfmt`, required in
`PATH`. Parser output is compared byte for byte. `sema_modules.manifest` owns
generated Rust modules; changing module declarations removes only previously
owned files.

Generated files remain tracked as CTest baselines. Ignoring them would require
tests and builds to regenerate the artifacts first, rather than comparing
with checked-in baselines.

## Migration checks

```sh
ctest --test-dir build --output-on-failure \
    -R 'agsem_migrated_examples_tests|agsem_toyscope_layer_tests'
cargo test --offline --manifest-path coge/examples/toyc/Cargo.toml -- --test-threads=1
cargo test --offline --manifest-path coge/examples/toycp/Cargo.toml
```

`agsem_migrated_examples_tests` copies only authored `.coge` and a runtime
contract to an isolated directory per language. It exports `.sema` and `.ag`,
checks manifest hashes, generates complete ToyC/ToyCP Rust, removes `.coge`,
and generates analysis using `sema` alone. It compares Rust files and module
manifests against baselines, and parsers byte for byte. For ToyScope it compares
parser and analysis; executor limitations are checked separately.

Independent Cargo tests verify program behavior, including all three ToyScope
policies, `scope_codegen`, and both runtime libraries.

## Full Rust and projection acceptance

After regeneration, test all eight examples and both runtime libraries. A
shared target directory reuses compiled dependencies. `--locked --offline`
uses pinned versions and the local cache. One test thread also serializes
interpreter cases that alter process environment.

```sh
for example in coge/examples/toyc coge/examples/toycp \
    sema/examples/scope_codegen sema/examples/typed_codegen \
    sema/examples/typed_mixed_codegen sema/examples/toyscope_1 \
    sema/examples/toyscope_2 sema/examples/toyscope_3; do
    cargo test --offline --locked --manifest-path "$example/Cargo.toml" \
        --target-dir /tmp/borsuk-rust-acceptance -j4 -- --nocapture --test-threads=1 || exit 1
done
cargo test --offline --locked --workspace --manifest-path rust/Cargo.toml \
    --target-dir /tmp/borsuk-rust-acceptance -j4 -- --nocapture --test-threads=1
ctest --test-dir build --output-on-failure -j3 \
    -R '^(agsem_document_projection_tests|agsem_semantic_link_tests|agsem_document_cli_tests|agsem_migrated_examples_tests|agsem_toyscope_layer_tests)$'
```

C backend tests compile and run emitted programs with `cc`. LLVM execution
requires `clang`, `llvm-as`, `opt`, `lli`, and a supported host; the historical
acceptance used x86_64 Linux. Check tools and versions beforehand.
`--nocapture` exposes LLVM skip messages; backend acceptance must confirm
that execution actually occurred.

Projection checks cover bytes and the model law `Ag(Sema(Coge)) == Ag(Coge)`,
origin maps, and analysis equivalence with exported `.sema`. ToyScope boundaries
and independent linking of `sema` have separate checks.

The pre-import historical baseline was CTest 109/109, Rust 256/256 with C/LLVM
execution, and 90/90 reproducible generated artifacts after XML and ToyCM
fixes. These figures do not report a completed acceptance of this checkout.
