# Lexer classes in Ag grammars

[English](README.md) | [Polski](README.pl.md)

See [AG_FORMAT.md](../../docs/AG_FORMAT.md#parser-rule-scoped-lexer-classes)
for syntax.

| File | Case |
|---|---|
| Shift.ag | a>>b produces SHR; c>>d produces two GT tokens |
| GatedBrackets.ag | Neither SHR nor GT active initially; branches enable separate classes |
| Keywords.ag | @read;/@write; are keywords; #read; uses IDENT |
| Combined.ag | 0read>>;/1read>>;/2read>>;/3read>>; select four masks |
| BadContext.ag | Deliberately contradictory SHR/GT choice after the same X |
| Nested.ag | read outer<read<int>>>>write; nests class disabling/restoration |
| [xml.ag](../examples/XML.md) | Rule-scoped text/tags and both quote styles |

Nested.ag also demonstrates LALR's limitation: merging can mix lexer contexts
without ACTION conflicts. Change parser=LALR to parser=LR for that grammar;
LR(1)/LR(2) work. The automated check covers both success and LALR rejection.

Run from the repository root:

```sh
cmake --build build --target agas agas_contextual_generation_tests -j4
build/bin/tests/agas_contextual_generation_tests
build/bin/agas --table ag/grammars/contextual-lexer/Combined.ag
printf '3read>>;' > /tmp/combined.txt
build/bin/agas --diagnose-parse ag/grammars/contextual-lexer/Combined.ag /tmp/combined.txt
```

The check compares both frontends, LR/LALR with k=1/2, tokens, recursion and
context restoration. It covers invalid declarations, the 64-class limit and
bit 63 without generating every combination.

--emit-package retains context plans in lexer section v2. C++/Rust both build
ASTs from it:

```sh
cargo build --manifest-path rust/Cargo.toml --bin agas_ast_wire
python3 ag/tools/check_contextual_artifacts.py
```

The reference check covers 182 cases including LR/LALR k=1/2, hidden channels,
lazy comments, Unicode, bit 63 and four malformed packages. See
[LEXER_CONTEXT.md](../../docs/LEXER_CONTEXT.md).

## Historical implementation verification

Class checks and 36 CTest tests passed, Rust package runtime 5/5, and eight
artifact files reproduced byte-identically. That 36/36 selection omitted three
independent blockers: incomplete XML broke adapter/merging checks, and ToyCM
export reused existing output. Stage 7.4 fixed XML and isolated export directories;
those checks plus XML/frontend/context regressions passed 6/6. XML had 31 agreeing
C++/Rust cases; ToyCM passed three consecutive runs while checking overwrite
refusal. Vist grammar regression retained alternative/quantifier coverage. These
are historical results, not checks run for this documentation change.
