# C Minus grammars for Agas

[English](CMINUS_GRAMMARS.md) | [Polski](CMINUS_GRAMMARS.pl.md)

`ag/grammars/examples` contains three restricted-EBNF examples:

- `cminus.ag`: a practical small C Minus grammar;
- `cminus_closed_open.ag`: a deterministic variant binding `else` to the
  nearest open `if` through closed/open rules;
- `cmm.ag`: a minimal grammar for grammar/parser generator tests.

Adjacent `cminus.g4` and `cmm.g4` are original ANTLR4 sources, historically
byte-identical to their earlier copies in `PEG/agbootstrap/g4`.

## cminus.ag

This procedural C-like language is larger than a minimal test but smaller than
full C. It supports global variables; functions returning `int`, `string`, or
`void`; formal parameters; local declarations/initialization; statement blocks;
calls and argument lists; additive/multiplicative expressions; numbers, strings,
variables, calls, and arithmetic negation; `<=`, `<`, `>`, `>=`, `==`, `!=`;
`while`, `for`, `if`, `if-else`, `return`; simple/compound assignment;
post-increment/decrement; and line/block comments. It has strings but no arrays.

It retains the classic dangling-else problem:

```text
node ifStatement
    : IF LPAREN condition=booleanExpression RPAREN thenBranch=statement
    | IF LPAREN condition=booleanExpression RPAREN thenBranch=statement ELSE elseBranch=statement
    ;
```

A deterministic LR generator can report a shift/reduce conflict at `ELSE`.
`#IfStatement`/`#IfElseStatement` labels identify AST variants and do not resolve
it. Binding to the nearest `if` requires an explicit policy or a matched/unmatched
grammar rewrite.

## cminus_closed_open.ag

The original grammar remains a diagnostic case. The variant divides statements
into `closedStatement` and `openStatement`, propagating open `if` through
`while` and `for` bodies. It binds `else` to the nearest unclosed `if` without
a global shift preference.

Historical Zbik tests found one remaining canonical LR(1) conflict caused by
the common variable/function declaration prefix; it disappears at k=2.
After removing dangling else, the grammar is conflict-free in canonical LR(2)
and LALR(2), demonstrating useful lookahead greater than one.

## cmm.ag

This intentionally tiny example tests the `.ag` parser, EBNF-to-BNF conversion,
FIRST/FOLLOW, LR states, ACTION/GOTO tables, target parser generation, and
simple AST construction. It contains only int variables, declarations and
initialization, blocks, `while`, assignment, calls, addition/subtraction,
comparisons, integers/identifiers, and line comments.

It has no function declarations, strings, multiplication/division, `if`,
`for`, or `return`.

## Restricted EBNF conversion

Anonymous groups become named rules. `formal_parameter (COMMA formal_parameter)*`
becomes:

```text
node formalParameters
    : first=formalParameter rest=formalParameterTail*
    ;
inline formalParameterTail
    : COMMA value=formalParameter
    ;
```

Similarly, `term (addop term)*` becomes:

```text
node additiveExpression
    : first=term rest=additiveOperation*
    ;
inline additiveOperation
    : operator=addop operand=term
    ;
```

The repeated operator/operand pair gains a stable name and production position.
An `inline` helper can return a reduction value without adding an AST level.

## AST design and language equivalence

`node` anchors include `program`, `funDeclaration`, `varDeclaration`,
`varInitialization`, `compoundStatement`, `booleanExpression`,
`additiveExpression`, loop/if statements, `assignStatement`, `call`, and
`returnStatement`. Technical/delegating `inline` rules include `declaration`,
`statement`, `typeSpecifier`, operator choices, list tails such as `argumentTail`,
and operations such as `additiveOperation`.

Unlabeled punctuation (`SEMI`, `COMMA`, parentheses, and braces) need not enter
the AST. Labels such as `name=ID`, `condition=booleanExpression`, and
`body=statement` select values for subsequent compiler stages.

Extracting anonymous groups should preserve the accepted token language;
helpers represent the original grouped sequences. AST structure and naming
can change without changing acceptance. Future short-word generation can
compare both grammars up to a selected length.
