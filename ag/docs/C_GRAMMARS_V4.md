# The grammars-v4 C grammar as a starting point

[English](C_GRAMMARS_V4.md) | [Polski](C_GRAMMARS_V4.pl.md)

`ag/grammars/c/C.ag` mechanically converts the split ANTLR4 grammar:

- `CParser.g4`: 117 parser rules;
- `CLexer.g4`: 181 lexer rules.

All named source rules are retained. Anonymous parser groups become named
`inline` rules. The negated token set in GNU `gnuSingleAttribute` becomes an
explicit `inline` rule because restricted Agas has no parser-side negated-set
operator. Historically, the entire output parsed with the `Ag.g4` bootstrap.

## Standards and extensions

Although its source comment mentions C11, the grammar also contains later C
features and GNU, GCC, Clang, Microsoft/Visual C, and Blocks extensions:
`asm`, `__attribute__`, `__extension__`, `__declspec`, `__m128`, label addresses,
statement expressions, and GCC builtins.

It is not a pure C11, C17, or C23 definition. It is a broad rule catalog and
useful frontend/LR test material. Pure C90 should start from
`C90SmallLegacy.ag`, adding selected features individually with tests.
Future `.agi` files and an external Agas preprocessor could share common rules
while explicitly selecting standards and GNU extensions.

## Removed ANTLR actions and predicates

`CParser.g4` depends on `CParserBase`. Conversion removes six target-language
actions and 17 semantic predicates involving symbol tables, scopes, typedef
names, and declaration/statement/cast/expression distinctions. The resulting
context-free grammar is broader. It may have LR conflicts or accept strings
that ANTLR rejected using symbol-table information.

Do not hide this by arbitrarily selecting a production. Report conflicts and
decide separately which distinctions need grammar rewriting, symbol-table
information, analysis after initial tree construction, or controlled parser
predicates.

## AST and reproducible conversion

The options are `ast = concrete; legacy = true;`. Named source parser rules
are initially `node`, extracted groups are `inline`. This is a CST-like
starting point, not a designed compiler AST API. Use [AG_FORMAT.md](AG_FORMAT.md)
to retain semantically useful `node` constructs and inline delegations/list tails.

From the Borsuk repository root:

```sh
python3 ag/tools/antlr4_split_to_agas.py \
    ag/grammars/c/CParser.g4 ag/grammars/c/CLexer.g4 ag/grammars/c/C.ag \
    --grammar-name C
```

`C.ag` records both source SHA-256 hashes and counts of removed actions and
predicates, allowing the result to be checked against its source copies.
