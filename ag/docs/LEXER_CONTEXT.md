# Lexer context in parser artifacts

[English](LEXER_CONTEXT.md) | [Polski](LEXER_CONTEXT.pl.md)

`agas --emit-package OUTPUT grammar.ag` supports grammars with `lexerClasses`.
The manifest stays at version 1; the `lexer` section descriptor and
`lexer.json` use **version 2**. Other sections remain at version 1. Packages
without classes retain their previous representation, including byte-identical
reproduction of the pinned Agas artifact. A loader supporting only lexer v1
must reject a required v2 section.

## Data

Lexer v2 contains the existing rules and automata, plus mandatory `context`:

- `requiredClasses`: a `uint64` mask for each lexer rule. Every required bit
  must be active. Rules without requirements have mask zero.
- `originalTerminals`: a mapping from each terminal ID to its source terminal
  in the package symbol catalog. Source terminals map to themselves; the parser
  uses separate specialized terminals.
- `rows`: one lookahead-prefix tree per LR state, with root index zero. Each
  node contains `active` (a `uint64` mask) and `edges`. An edge contains a
  specialized `terminal` ID, or `null` for EOF, and a `target` node index in
  that tree. The node mask selects the **next** token after the prefix leading
  to the node.

Trees are built from the uncompressed ACTION table. They retain prefixes
subsequently removed by default-reduction compression. Their depth does not
exceed `lookahead`; EOF ends a path. The export contains reachable contexts
and prefixes rather than a table of all `2^64` class combinations.

`orderedNfas` contains an automaton for every rule, including rules without
priority operators. The runtime filters rules by mask, preserves longest
match and rule order, and respects priorities within each rule, including
non-greedy repetition. Unconditional `skip` and hidden channels also work
between lookahead tokens. Combining `require` with `skip` or `channel`
remains unsupported.

## Execution and AST

The first LR pass chooses context, tokenizes the source, and records specialized
terminals. Reductions do not consume lookahead. After a state change, the
runtime verifies that source tokens and spans remain identical. The second
pass executes the existing AST reduction program and collects coverage.

Final AST tokens use source IDs again, with unchanged byte and character
spans. Specialized productions have distinct IDs but retain origins, labels,
and alternative indices. Coverage aggregates them by original alternatives
and quantifier occurrences.

C++ uses `ArtifactAstParser::parse(input, lexer)`; standalone
`lexer.tokenize(input)` rejects a contextual lexer without a parser table.
Rust `LoadedPackage::parse` and `tokenize` use the plan automatically.
Contextual tokenization requires a valid syntax prefix: on a syntax error it
may return only the prefix up to the error. `parse` reports rejection.
Step and stack limits apply separately to each pass.

Both loaders validate sizes, IDs, mappings, tree acyclicity, depth, EOF,
required-rule activity, and consistency with the parser table. Existing
checks for versions, section lengths, and SHA-256 remain in force.

## Reproducing the checks

From the repository root, after building `agas` and `agas_ast_wire`:

```sh
python3 ag/tools/check_contextual_artifacts.py
```

The check compares complete C++ and Rust AST wire output for small LR/LALR
grammars with k=1/2, four class combinations, nesting, bit 63, a comment
channel, and Unicode. Both loaders reject four deliberately corrupted plans,
even after their section lengths and SHA-256 values have been recomputed.

The historical Vist checks `grammar/compare_shift_variants.py` and
`grammar/test_examples.py` belong to the separate Vist repository.
