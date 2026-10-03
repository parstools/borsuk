# XML example

[English](XML.md) | [Polski](XML.pl.md)

[xml.ag](xml.ag) is a standalone Agas grammar for a small XML subset, producing
AST without sema/coge. It parses the checked-in [example.xml](example.xml).

## Lexer contexts

Parser-rule scope selects classes using enable/disable/require, restoring
outer context on return without new lexer modes or implementation changes.

| Class | Purpose |
|---|---|
| ROOT | Whitespace around root and XML declaration start |
| TAG | Names, attributes, spaces and tag endings |
| TEXT | Element text including whitespace/Unicode |
| DOUBLE | Double-quoted attribute value |
| SINGLE | Single-quoted attribute value |

Explicit TAG_SPACE avoids discarding content whitespace. No class-conditioned
skip/hidden-channel tokens are required. </ is one token; LR(1) distinguishes
closing from nested opening tags. The reference table has 94 states, zero conflicts.

## Scope

Supports nesting/empty elements/mixed text, both attribute quote forms,
spaces around =, ASCII names with colon/dot/underscore/hyphen and optional XML
declaration. AST retains opening/closing names, attribute values/text and UTF-8
byte spans. Content whitespace is data.

Name agreement is a post-parse AST check, demonstrated by matching_names in
[check_xml_example.py](../../tools/check_xml_example.py). <a></b> is grammatically
accepted and rejected by that check; lexer classes do not match names.
This is not a full XML implementation: entities, character references, DTD,
comments, CDATA and processing instructions are unsupported. Declaration
attributes use ordinary syntax without validating required order/values.
Full name characters, unique attributes and namespace resolution are separate work.

## Running

Run from repository root. Package export uses lexer section v2; the commands
below check C++/Rust AST agreement. The reference test has 31 cases: valid/invalid
input, nesting, empty values, both quotes, EOF inside values, context boundaries,
text/UTF-8 preservation and separate post-parse name agreement.


```sh
build/bin/agas --table ag/grammars/examples/xml.ag
build/bin/agas --diagnose-parse \
    ag/grammars/examples/xml.ag ag/grammars/examples/example.xml
ctest --test-dir build --output-on-failure \
    -R '^agas_xml_example$'
```

```sh
cargo build --offline --locked --manifest-path rust/Cargo.toml \
    --target-dir /tmp/agas-xml-rust --bin agas_ast_wire -j4
python3 ag/tools/check_xml_example.py \
    --agas build/bin/agas \
    --grammar ag/grammars/examples/xml.ag \
    --example ag/grammars/examples/example.xml \
    --ast-wire /tmp/agas-xml-rust/debug/agas_ast_wire
```
