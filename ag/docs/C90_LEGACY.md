# Three legacy C90 grammars

[English](C90_LEGACY.md) | [Polski](C90_LEGACY.pl.md)

`ag/grammars/c` contains three mechanically converted ANTLR4 grammars. Their
`Legacy` names distinguish them from the hand-developed `C90.ag` and avoid
presenting them as current language specifications.

| Agas file | Historical source | Source lines | Parser rules | Lexer rules | Parser rules after group extraction |
| --- | --- | ---: | ---: | ---: | ---: |
| `C90SmallLegacy.ag` | `grammars-v4-moje-C/c/C90.g4` | 647 | 80 | 33 | 110 |
| `C90LargeBranchLegacy.ag` | `grammars-v4.branch_c/c/C90/C90.g4` | 918 | 123 | 43 | 187 |
| `C90LargestExprLegacy.ag` | `proj/grammars-v4/c/C90/C90.g4` | 998 | 108 | 43 | 160 |

Line count is not active-rule count. The largest source replaces much of the
classic precedence ladder with a large left-recursive `expr` rule; parts of
the old ladder remain commented out.

## Development direction

A larger grammar is not necessarily better or more faithful to C90.
`LargeBranchLegacy` and `LargestExprLegacy` include GCC attributes, `asm`,
extra keywords, and other GNU extensions. Start pure-C90 work from
`C90SmallLegacy.ag`, confirm ISO C90 behavior against a corpus, and add missing
constructs individually with tests. Use larger variants as sources of selected
rules/examples. GNU extensions should remain a separate, explicitly enabled
variant.

## Variants and names

`C90SmallLegacy` is compact, with separate function declarations, prototype/K&R
definitions, simpler declarators, and a classic expression ladder.
`C90LargeBranchLegacy` comes from `branch_c`, adding richer declarators, GCC
attributes, type extensions, and `asm`, while retaining the ladder.
`C90LargestExprLegacy` has the large `expr` rule and originally ANTLR-style
alternative labels. Each file declares the correspondingly named grammar.

Repeated labels such as `#postfixExpression` were disambiguated as
`#PostfixExpression1`, `#PostfixExpression2`, and so on. Public AST variant
names must be unique within a rule. This changes neither `RuleId` nor the
language recognized by the productions.

## Historical ANTLR 4.9.2 checks

The small source generated Java and C++ unchanged. Both larger sources parsed
successfully, but Java generation encountered the reserved rule name
`volatile`. Mechanical name mangling allowed generation with `-Werror`.
Other targets have different reserved names, including `asm`/`alignof` in C++
and `type`/`complex` in Python.

Agas should retain valid grammar names and use a separate generated-code name
table, for example `volatile -> rule_volatile`, `asm -> rule_asm`, and
`alignof -> rule_alignof`. Diagnostics and dumps retain the original names.

## Mechanical conversion and AST

These grammars primarily test the frontend, EBNF-to-BNF conversion, and LR
generators. Every original parser rule is initially `node`; anonymous groups
become deterministically named `inline` helpers:

```text
node parameterList
    : parameter parameterListGroup1*
    ;
inline parameterListGroup1
    : ',' parameter
    ;
```

`...Group1`/`...Group2` names are stable for unchanged input; a hand-designed
grammar may later replace them with domain names such as `parameterTail`.
Lexer regex groups remain nested: only parser grouping is restricted.

`ast = concrete;` preserves the source-rule structure, producing a starting
point closer to CST than a compiler AST. A later explicit AST design can change
selected rules to `inline`, label fields, name variants, remove punctuation
and precedence scaffolding, and fold left-recursive expressions into binary
nodes. Do not make such changes automatically without choosing the tree API.

## Reproducibility and validation

`ag/tools/antlr4_to_agas.py` writes the original license, source path, source
SHA-256, and a note about mechanical group extraction. It supports the
action-free combined-grammar subset used here, not every `.g4` construct.
Other grammars may require actions, predicates, modes, imports, and `tokens`
blocks.

Historically, all three outputs parsed completely with the current `Ag.g4`
frontend. All original rules were retained: 80/33, 123/43, and 108/43
parser/lexer rules respectively. Extra parser rules represent anonymous EBNF
groups only.

Equal rule counts and valid `.ag` syntax do not prove language equivalence.
That requires differential acceptance checks against shared corpora and short
generated words.
