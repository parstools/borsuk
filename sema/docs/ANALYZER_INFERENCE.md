# Analyzer signature inference

[English](ANALYZER_INFERENCE.md) | [Polski](ANALYZER_INFERENCE.pl.md)

This complements the [execution model](../../coge/docs/EXECUTION_MODEL.md).
Basic inference is implemented in the Rust generator: `inherited` declarations
enable inference of missing `analyzer` signatures. Inherited-attribute types
remain explicit. Partial annotations and signature-dump commands discussed
below are further extensions.

## Motivation

Before simplification, typed ToyCP declared 57 analyzers whose parameters used
only ten distinct names, each consistently typed. Many signatures repeated
information already available from actions and helper-function contracts.

The grammar defines nodes, child labels, and cardinalities (`?`, `*`, `+`),
but not semantic results: an `expression` analyzer might return an `ExprId`,
type, value, or another structure. `analysis` blocks and called-function
contracts supply the missing constraints.

## Compact declarations

Declare permitted inherited attributes instead of repeating full signatures:

```text
semantic_model {
    rust_context Context;
    inherited scope: ScopeId;
    inherited target: DeclarationTarget;
    inherited owner: StructId;
    inherited visibility: Visibility;
    inherited bodies: Bool;
    inherited parameters: List<ParameterId>;
    inherited enter: Bool;
    inherited left: ExprId;
    inherited base: PlaceId;
    inherited arguments: List<ExprId>;
    // Intrinsic signatures and semantic helper functions follow here.
}
```

These ten declarations replace 57 signatures. They neither change `analysis`
blocks nor require `.agx`, global variables, or hidden automatic context passing.

Each rule receives attributes used by its code and those explicitly supplied
through named `with` arguments. This also preserves deliberately unused
parameters, such as `scope` in `destructorDeclaration`. Argument names must
belong to `inherited` declarations or an explicit signature; a typo does not
extend an interface. `analyze child with scope: local_scope` explicitly selects
the argument source. Existing generator behavior permits an omitted argument
to use an available variable with the same name and type; inference propagates
the requirement to the caller.

Future inference could omit an inherited type when constraints determine it
uniquely. The initial design retains explicit types as a small, readable
contract protecting attribute meaning. Types are not guessed from names.

## A concrete deduction

The actions of `compoundStatement` contain:

```text
let block_scope = nested_scope(scope, enter, self.source);
// Child statements are analyzed with block_scope here.
result = finish_block(block_scope, enter, self.source);
```

With the contracts:

```text
intrinsic nested_scope(parent: ScopeId, enter: Bool, source: SourceRange)
    -> ScopeId mutates;
intrinsic finish_block(scope: ScopeId, entered: Bool, source: SourceRange)
    -> OpId mutates;
```

inference produces:

```text
analyzer compoundStatement(scope: ScopeId, enter: Bool) -> OpId;
```

Likewise, `result = copy_text(name.text)` determines `declarator -> OwnedText`,
`result = create_module(scope)` determines `program -> ModuleId`, and
`if bodies` requires `Bool`. `analyze value ... -> analyzed; result = analyzed;`
propagates the child's result type to its parent. This is type inference under
explicit contracts, not a guess at program intent.

## Inference and diagnostics

1. Resolve each child's grammar rule, AST labels, and technical-field types
   such as `.text`, `.source`, and `.present`.
2. Each rule initially has an unknown result type and required-input set.
   Local `let` bindings, loop variables, AST children, and builtins have their
   own scopes. A label after `with` is not a variable read.
3. A free name is an input only if declared `inherited`. Unknown `scpoe` is an
   error, not an implicit parameter. Children/locals shadow inherited names.
4. Calls, operators, `result` assignments, and `analyze` produce constraints.
   `with` connects the child parameter to the actual argument expression's
   type, not a same-named parent variable.
5. Every alternative and exit path of a rule must agree on its result type.
   Inputs are the union of alternative requirements and named arguments at
   call sites. Mismatches do not implicitly create `Any`, an enum, or overload.
6. Solve mutually referring rules independently of source order. Grammar
   recursion is distinct from prohibited cycles among action helpers.
7. Error handling and optional children contribute constraints. `default` must
   fit the child result. Under existing `Result<T>` propagation, an analyzer's
   result is `T`; it does not receive another `Result` wrapper.
8. Check all calls, missing/extra arguments, and paths without results. An
   unreachable rule with unresolved types is still diagnosed.

Inherited declarations prevent accidental parameter creation but cannot detect
every intent error: confusing two valid same-typed names can still be wrong.
Important interfaces may retain explicit signatures as additional constraints.

## Required declarations and integration

External intrinsic contracts remain required: use sites alone cannot determine
implementation or ABI. Nominal types and fields remain explicit; `ScopeId`
and `StructId` are not interchangeable because both happen to be integers in
Rust. `rust_context` or equivalent backend configuration binds the external
context. Ambiguous cases such as an untyped empty list or a closed unconstrained
cycle require annotations.

Existing full `analyzer` signatures remain optional constraints. Partial
signatures are a future extension; explicit types must agree with inferred
types and do not permit hidden conversion. Start-rule inputs follow the same
constraints. Host APIs still supply and construct required inputs, such as an
initial `ScopeId`.

Inference runs before existing checking and Rust generation, producing the
same `AnalyzerSignature` set that `codegenContract` constructs from explicit
declarations. Full type, effect, and AST checks still run afterward.

Historically, ToyC replaced 41 analyzer declarations with seven inherited
attributes; ToyCP replaced 57 with ten. `sema_gen.rs`, `sema_lib_gen.rs`, and
`interpreter_gen.rs` remained byte-identical to pre-simplification output.

The proposed `sema --dump-analyzers` command would print complete signatures
for review. Inferred parameter order follows `inherited` order; explicit
signatures retain source order for Rust API compatibility. Named `with`
arguments preserve their meaning independently of order.

`sema_analyzer_inference` compares explicit/inferred generation and covers
recursive rules, omitted input propagation, local variables, unused parameters,
and invalid annotations/names/types. ToyC/ToyCP generation checks also cover
optional children and errors. Inference is independent of IR executor design.
