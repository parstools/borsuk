# borsuk

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
