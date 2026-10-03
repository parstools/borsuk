# ANTLR as the Agas lexer oracle

[English](LEXER_ORACLE.md) | [Polski](LEXER_ORACLE.pl.md)

The generated ANTLR 4.10 lexer is used only in differential tests of the
bootstrap frontend. It is neither part of production tokenization nor the
authority for `.ag` semantics; those are defined in [AG_FORMAT.md](AG_FORMAT.md).

## Compared data

For valid input, the test compares every token that is not skipped:

- symbolic terminal name;
- channel;
- token text;
- starting byte offset in the original UTF-8 input.

The comparison covers `Ag.ag`, the `.ag` files in `grammars/`, and focused
cases for keyword priority over identifiers, escapes, character classes,
Unicode preceding another token, and multiple block comments. For invalid
input, both lexers must reject it; their recovery behavior need not match.

The historical pinned result is:

```text
files=11 focused=4 invalid=4 documented_differences=1 mismatches=0
```

## Documented differences

The bootstrap `Ag.g4` permits EOF to terminate `DOC_COMMENT` and
`BLOCK_COMMENT`. `Ag.ag` requires an explicit `*/`, so the native lexer
rejects an unterminated comment. This is intentional: a missing terminator
must produce a user diagnostic, and ANTLR is not the conformance oracle for
that case.

ANTLR may report a lexical error and resume at a later character. The native
lexer stops at the first error. Negative tests therefore compare rejection,
rather than the token stream following an error.

The Agas grammar skips comments and whitespace, so it provides no named-channel
example for this oracle. A separate generated-lexer test checks
`channel(NAME)` using a synthetic grammar.
