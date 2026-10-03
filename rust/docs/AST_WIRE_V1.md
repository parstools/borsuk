# Neutral AST wire format, version 1

The Rust frontend can pass an accepted neutral AST to C++ through a UTF-8 JSON
document on stdout or in a file. This is a data boundary, not a Rust–C++ ABI.
The source text is **not** embedded: both processes receive the same exact
source bytes separately.

The top-level object has the following fields, in this canonical order:

```text
wireVersion, astSchemaVersion, symbolsSha256, astSchemaSha256,
sourceName, sourceByteLength, sourceSha256, root
```

`wireVersion` and `astSchemaVersion` are currently `1`. The two catalog hashes
are the lowercase SHA-256 values of the exact `symbols.json` and
`ast-schema.json` section bytes from the validated package manifest.
`sourceName` is a nonempty caller-provided identity, compared exactly; it is
not interpreted as a filesystem path. `sourceByteLength` and `sourceSha256`
refer to the original UTF-8 source bytes. The consumer must supply these
bytes and the matching package catalogs; wire data cannot choose them.

Every AST value has these fields, in canonical order:

```text
kind, sourceSpan, recognizedSpan, typeName, variantName,
tokenKind, tokenText, fieldNames, elements
```

`kind` is `unit`, `token`, `node`, `record`, `optional` or `list`. A span is an
object `{ "beginByte": n, "endByte": n }` using half-open UTF-8 byte offsets.
`sourceSpan` covers the value's payload; `recognizedSpan` covers the full
construct recognized by its reduction. Thus an inline rule can retain a
child's smaller payload span while recording punctuation in the recognized
span. The payload must lie within the recognized span; all boundaries must
fall on UTF-8 scalar boundaries in the supplied source.

For `token`, `tokenKind` is a dense terminal ID in the bound symbol catalog
and `tokenText` must equal the exact source slice at `sourceSpan`. `node` and
`record` have nonempty `typeName`; their `fieldNames` and `elements` arrays
have equal lengths, and names are nonempty and unique. Only `node` may have a
nonempty `variantName`. `optional` has zero or one element, `list` any number.
All unused fields have their empty or zero value. Array order is preserved
exactly; consumers must not reorder fields or children.

The canonical encoding is a two-space-indented JSON object with the stated
field order and one trailing newline. Unknown, missing and duplicate fields,
noncanonical encodings, incorrect types, mismatched versions/hashes/source,
invalid ranges or value shapes are rejected. Maximum wire size is 128 MiB,
maximum AST depth 200 (root depth 0), maximum values 1,000,000. Lists,
optionals and tokens count toward that depth, not just named nodes. The JSON
reader bounds syntactic nesting at 408 before recursive deserialization;
JSON has an object and an elements array per AST level. This version carries only an
accepted AST; lexical and syntax errors remain separate runtime results.

Transparent rule chains and parenthesized grouping may forward their operand
without allocating an AST wrapper. The payload keeps its `sourceSpan`; its
`recognizedSpan` includes discarded grouping. This allows a semantic consumer
with access to the bound source to distinguish `a+b << c` from `(a+b) << c`.
Redundant parentheses change spans, but not the semantic tree shape.

The reduction package uses `forward` for unconditional promotion and
`construct-node-or-forward` for a node with optional/repeated tails. The latter
has one operand index, naming the bound child to forward, and all node fields.
Every other field must be a list or optional: if all are empty the operand is
forwarded, otherwise the complete node is constructed. The AST schema describes
the conditional result as `choice` with an empty name and at least two type
arguments; its rule-level `resultTypes` contains the distinct member types.
Older packages remain readable; older readers reject the new opcode/type kind.
The checked-in Ag package has been regenerated with this reduction policy.

The Rust emitter is `rust/crates/ag-runtime/src/bin/agas_ast_wire.rs`; the C++ loader is
`parseAstWireJson`. An end-to-end check from the repository root is:

```bash
cargo build --offline --manifest-path rust/Cargo.toml --bin agas_ast_wire
rust/target/debug/agas_ast_wire \
  ag/artifacts/ag/v1 ag/grammars/Ag.ag |
  build/bin/agas_artifact_runtime_parity_tests --from-stdin
```

The C++ consumer parses the wire before calling the existing
`adaptAgSyntaxDocument`; the integration test compares the resulting
`SyntaxDocument` with the pure C++ path and checks byte-identical wire output.
