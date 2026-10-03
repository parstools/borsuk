# Converting the unfinished C90 grammar to Agas

[English](C90_CONVERSION.md) | [Polski](C90_CONVERSION.pl.md)

`ag/grammars/c/C90.ag` converts the unfinished historical
`c_grammar/c90_parser/src/C90.g4`. Its purpose is to preserve the source
grammar's existing scope, rather than complete an ISO C90 specification.

## Source status

The source has 818 lines, covering declarators, types, statements, expressions,
and literals. It still has draft characteristics: a `to do integer suffix?`
comment for hexadecimal constants; GNU and post-C90 extensions; deferred
distinction between type names and ordinary identifiers; placeholder rules
`namePlace`, `variableDeclaratorPlace`, and `functionDeclaratorPlace`; incomplete
semantic validation of declarators/initializers; and no proof of LR(1)
determinism.

Historical ANTLR 4.9.2 checks found target-language name collisions: `volatile`
for Java, and additionally `asm` and `alignof` for C++. These are code-generation
issues, not errors in the described C language. A multilingual generator should
preserve grammar names and safely mangle generated identifiers:

```text
volatile -> rule_volatile
asm      -> rule_asm
alignof  -> rule_alignof
```

## Preserved scope

The conversion retains variable/function/struct/union/enum/typedef declarations;
pointer, array, and function declarators; prototype and K&R parameter lists;
aggregate and designated initializers; bit fields; labels, `if`, `switch`, loops,
`goto`, `return`, `break`, and `continue`; GNU `asm`, `__extension__`, `__real__`,
`__imag__`, `__builtin_offsetof`, and `__builtin_va_arg`; the full source
expression-precedence ladder; integer, floating-point, character, and string
literals; and block comments/preprocessor lines on `HIDDEN`.

The name `C90` is historical. `_Alignof`, binary numbers, GNU expressions, and
case ranges go beyond ISO C90.

## Restricted EBNF conversion

Every anonymous parser group becomes a named rule. For example,
`parameterOrType (',' parameterOrType)*` becomes:

```text
node fixedParameterOrTypeList
    : first=parameterOrType rest=parameterOrTypeTail*
    ;
inline parameterOrTypeTail
    : COMMA value=parameterOrType
    ;
```

Similarly, `logicalAndExpression ('||' logicalAndExpression)*` becomes:

```text
node logicalOrExpression
    : first=logicalAndExpression rest=logicalOrTail*
    ;
inline logicalOrTail
    : LOGICAL_OR value=logicalAndExpression
    ;
```

Named tails give operators and right operands stable identities, allowing
reduction generation without inspecting an anonymous EBNF expression tree.

## Parser literals and lexer rules

Keywords/operators receive explicit token names such as `IF`, `STRUCT`,
`ASSIGN_SHL`, `LOGICAL_AND`, and `ELLIPSIS`. Diagnostics use stable names;
generator commands need not depend on literal text or implicit `T__0`/`T__1`
tokens; automata and tables are easier to compare across implementations.

Longer operators must take priority over shorter prefixes, or the lexer must
strictly apply longest match: `>>=` before `>>` and `>`, and `...` before `.`.

The restriction on grouping applies to parser rules, not lexer regexes.
`(NonzeroDigit Digit* | '0') IntegerSuffix?` therefore retains its regex groups.
They create no program AST nodes and are natural NFA/DFA inputs.

## Production identity and AST

Every alternative has a `RuleId`, whether its rule is `node` or `inline`.
`node` anchors an AST object; `inline` is technical scaffolding or forwards a
value. `field=Symbol` names data used in reductions or the AST. Unlabeled
punctuation need not appear in the tree.

The conversion does not name alternatives with `#...`: rule names and `RuleId`
suffice for the initial model. Later AST variant names must follow the rule
that all alternatives of a `node` rule are named, or none are.

## Further work

1. Build a `Grammar` model and validate every symbol reference.
2. Compare the Agas lexer with the lexer generated from the source lexical rules.
3. Compute FIRST/FOLLOW and detect nullable cycles.
4. Build canonical LR(1), then inspect conflicts for larger k.
5. Distinguish genuine conflicts from decisions requiring typedef-name information.
6. Verify function, pointer, and array declarators against real C declarations.
7. Compare short generated words where grammar-fragment size permits.
8. Only then complete missing standard features or GNU extensions.

Treat `C90.ag` as a faithful notation migration of an existing draft, not a
finished C90 specification.
