# ToyCP typed IR interpreter

[English](README.md) | [Polski](README.pl.md)

`python3 tools/regenerate_examples.py toycp` regenerates the parser,
semantic analyzer, IR interpreter and property checks. Run
`cargo test --offline --manifest-path coge/examples/toycp/Cargo.toml`
to check parsing, analysis and execution of ToyCP source programs. Execute a file
with `cargo run --offline --manifest-path coge/examples/toycp/Cargo.toml --bin toycp_interpret -- PROGRAM.tcp`.

`analyze_source` connects the parser and generated `sema_gen.rs` to intrinsic
implementations in `src/`. `Interpreter::new` takes the resulting `Context`;
`run_main` executes IR and cleans up globals. Tests cover the full source path,
including implicit `self`, out-of-class definitions, inheritance, visibility,
arrays and construction/destruction order. The current model comes from
`toycp.coge`; `tests/fixtures/legacy/toycp_full.sema` retains its legacy reference.

Constructor overloads are supported. Same-name methods still have one signature
and whole arrays cannot be values. Array indexing is supported and checked at
runtime. Run the commands above from the repository root.
