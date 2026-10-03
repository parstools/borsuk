# Ag, sema and coge documents: format 1

[English](DOCUMENT_CLI.md) | [Polski](DOCUMENT_CLI.pl.md)

ag and agas share Ag handling. sema links only agsem_core/agas_core; coge adds
coge_core. The header determines document kind, not the filename extension.
Standalone v1 embeds its grammar and does not read a neighboring Ag through for.

No operation means --check. Checking accepts multiple inputs; emission accepts
one. Operations are mutually exclusive; checking writes no Rust/output files.
Execution, properties and declared backends are checked even without interpreter
generation. Document declarations determine generation selection.
--emit-ag requires valid syntax/section ownership, without preparing analysis.
--emit-sema requires resolved contracts for all retained analysis, but need not
semantically validate discarded execution bodies. Library projectSema(document,
bound, frontend) takes an explicit frontend to reparse/rebind the result using
the selected parser resources.


```sh
cmake --build build --target ag sema coge -j4
build/bin/ag --check language.ag
build/bin/sema --check --contracts runtime.json language.sema
build/bin/coge --check --contracts runtime.json language.coge
build/bin/coge --emit-sema language.sema --contracts runtime.json language.coge
build/bin/coge --emit-ag language.ag language.coge
build/bin/sema --emit-ag language.ag language.sema
build/bin/sema --emit-rust-dir generated --contracts runtime.json language.sema
build/bin/coge --emit-rust-dir generated --contracts runtime.json language.coge
```

## Explicit contracts

ContractEnvironment is typed API input. Repeated --contracts loads explicit
JSON; the CLI does not scan Rust or treat FooId-like names as type declarations.
TypeRef is an object with name and recursive arguments. Type exports are opaque
(optional arity), alias (target), record (field-name/type map) or enum (variant/
ordered-payload list). Opaque values can be passed but undeclared fields cannot
be accessed. Structural generic contracts without explicit parameters are unsupported.

Functions declare parameters/result, optional pure|mutates effect, context and a
Rust path of identifier segments. Methods require context and form a separate
binding category from global DSL functions, so Context.convert and execution
convert do not collide. Role exports declare type and may identify the context
role for binding. Every reference must be closed over explicitly supplied
manifests. semantic→execution dependencies, version conflicts and intrinsic/
effect mismatches are errors. Analysis Result<T> differs from execution Result<T,E>.

Manually authored [ToyC](../contracts/toyc-runtime-v1.json) and
[ToyCP](../contracts/toycp-runtime-v1.json) contracts describe the public runtime;
they do not replace Rust compilation and behavioral compatibility tests.


```json
{
  "format": 1,
  "id": "example-runtime",
  "version": "1",
  "semantic": [
    {"name": "Context", "kind": "opaque"},
    {"name": "Handle", "kind": "opaque"},
    {
      "name": "lookup", "kind": "function",
      "parameters": [{"name": "Int"}],
      "result": {"name": "Handle"},
      "effect": "pure",
      "rust": ["crate", "lookup"]
    }
  ],
  "execution": []
}
```

## Execution profile and typed inspection (5.5)

execution_model can explicitly select shared_value_v1 version 1. The catalog
provides 46 functions, nine execute handlers and seven evaluate handlers as
structural execution trees. Selection neither reads files nor identifies a
language by name. It supplies no types, state or intrinsic implementations.
Compatible runtime interfaces and local initialize_global/value_matches_type/
load bodies remain required. load may be pure or mutating; callers check its
effective effect. The original internal plan is not included in this repository;
the public contract is described below and in the runtime manifests.


```text
execution_model {
    execution_profile shared_value_v1;
    // Local types, state, intrinsics, functions and properties follow.
}
```

### Requirements and generation selection

Select the profile once, exclusively in execution_model. Fully local models
remain supported without it; unbound legacy mode rejects profile selection.
Keep local types and runtime_state runtime: Runtime, including Runtime.store:
Store<SymbolId,Value> and Runtime.steps_remaining: Index. Value/Control payloads
and RuntimeError/PlaceView/FunctionView/OperationNode/Expression fields must
match explicit logical contracts. Extra fields/variants are allowed when the
effective bodies handle them.

| Required local function body | Parameters → result | Effect |
|---|---|---|
| initialize_global | OpId → Result<Unit,RuntimeError> | mutates |
| value_matches_type | Value,Type → Result<Bool,RuntimeError> | pure |
| load | PlaceId,SourceRange → Result<Value,RuntimeError> | pure or mutates |

An intrinsic declaration alone does not satisfy these requirements. Required
interfaces also include operation_node, expression_node, literal_identity,
module_operations, variable_type, variable_place, field_types, function_view and
place_view with declared signatures. place_view may mutate, requiring compatible
caller effects. Other dependencies (Store/AgSemRuntime/AccessError etc.) come
from effective bodies after overrides and fixed profile interfaces.

Profile selection does not select generation. generation's generate_interpreter
enables interpreter/properties emission; lowering/C/LLVM require their own
declarations. Checking validates execution and properties even without emission.

### Local overrides

A same-name local function replaces its catalog entry with the same ordered
parameter types, result and effect; parameter names may change. A local handler
replaces the same variant with a matching payload pattern. Duplicate keys/profile
choices, unknown profiles and function/intrinsic collisions fail. Check dependencies
of the selected body. With a profile, require every Operation/ExpressionKind handler
and exhaustive internal enum/Option/Result matches without wildcard, regardless
of generation selection.

No override keyword is used. Handlers are declared in execution_contract; source
is supplied as handler context. execute Call and evaluate Call are distinct keys.
Overrides retain catalog position; additional locals follow in source order.
Changing parameter/binding names is allowed, changing arity/order/types/result/
effect is not. Entries cannot be removed or replaced by intrinsic, collide with
types or reserved execute_generated/evaluate_generated, or silently fall back to
defaults after invalid overrides.

Properties use effective signatures: pure I32 inputs with optional SourceRange
and Result<I32,RuntimeError>. Discarded default-body dependencies disappear, but
fixed interfaces and the three local implementation requirements still apply.


```text
execution_model {
    execution_profile shared_value_v1;
    // Keep the required local declarations and implementations here.
    function invalid_value(source: SourceRange) -> Result<Value, RuntimeError> {
        runtime_error "custom invalid value" at source;
    }
}
```

```text
execution_contract {
    // Keep the other required local handlers here.
    evaluate Load(place) { return load(place, source); }
}
```

### Inspection, API and provenance

coge --inspect-model adds execution_model in coge-checked-execution-v1, with or
without a profile. It includes effective signatures/bodies/patterns, state/local
types/intrinsic/properties, coverage, bound dependencies and explicit/
profile_default/profile_override origins. Dependencies show checked types/effects
and local declarations or manifest ID/version/hash/JSON path. Default origins
refer to the profile declaration and catalog item ID.

Inspection records source/contracts/catalog/effective-model hashes. Catalog hash
covers bodies/requirements; overrides change effective hash. --calls includes
expanded profile calls; Rust metadata records execution_profiles. Without a
profile, execution_model.profile is null; the model also records
interpreter_requested, effective_sha256, required_implementations, coverage and
representation_obligations. Pure sema and profile-free execution metadata use
execution_profiles: []. Logical proof covers signatures, pattern bindings and
resolved dependencies; JSON retains bodies but not a type on every literal.

Owned CheckedExecutionModel is available through CogeValidation/CheckedGeneration.
model()/dependencies()/effectiveHash(), inspectExecutionModel and executionCalls
share checked data. prepareGeneration(..., const CogeValidation&) reuses checking
and CheckedGeneration::execution() exposes it. Data outlives document/contracts;
source, contract or generation-identity mismatches fail before emission.


```sh
build/bin/coge --inspect-model /tmp/toyc-profile-model.json \
  --contracts contracts/toyc-runtime-v1.json coge/examples/toyc/toyc.coge
build/bin/coge --calls /tmp/toyc-profile-calls.txt \
  --contracts contracts/toyc-runtime-v1.json coge/examples/toyc/toyc.coge
```

### Proof boundary and ABI

With profiles, functions/outer dispatcher arms follow catalog order plus
additional locals in source order; without profiles, preserve existing order.
Rust still checks tuple/record variant representation, field names, ID layout,
Box, Clone/Copy, visibility and external intrinsic implementations. Logical
checking covers names, fields/payloads/signatures/effects, calls/returns/mutations/
capture/pattern bindings/dependencies and profile enum coverage. It does not
prove general termination, numeric pattern exhaustiveness, algorithm correctness
or agreement between manifests and actual Rust. Properties and independent
program tests validate results, evaluation order, faults, frames and cleanup.
Projection removes the profile with execution, without checking discarded bodies.

ToyCP supports interpretation, references, construction/destruction and cleanup;
the profile adds no ToyCP C/LLVM lowering. Subsequent completeness features are
described below. Historical stage 5.5 acceptance recorded Rust 126/126 and CTest
102/104 after a focused projection rerun; two known XML failures remained. Eight
examples regenerated byte-identically. The historical independent full-Rust hash
comparison allowed only whole-function and outer-dispatch-arm reordering.
These are past measurements, not validation run for this documentation edit.

## Projections and output ownership

See [PROJECTIONS.md](PROJECTIONS.md) for ToyC/ToyCP projection regeneration and
ownership. Canonical Ag examples are maintained independently from exported copies.
Sema projection changes the coge header token and removes execution constructs;
Ag removes all extensions/specification header. Parser-span edits preserve
remaining bytes/comments/order/regex/classes/lookahead. The byte law is
Ag(Sema(Coge)) == Ag(Coge).

Existing output needs --force. Input and explicit contract manifests remain
protected through symlink/hardlink aliases. Writes use temporary files in the
destination directory and individual replacement. OUT.provenance.json is last:
schema version, names, source/output/binary hashes, contracts and origin map,
without timestamps. Repeat output is deterministic. Consumers must check its
output hash because two replacements are not one transaction. source is
informational and causes no implicit read. Rust emission protects foreign modules
and deletes only previously owned outputs. Sema's manifest grants no ownership
of interpreters/backends. --calls reports accepted-document calls, not layer
dependency validation.

## Legacy and limitations

Legacy mixed sources need the explicit coge compatibility adapter. Standalone
v1 has stricter layer ownership and explicit contracts; compatibility does not
prove those boundaries.

### Command migration

| Previous use | Current command |
|---|---|
| sema tests/fixtures/legacy/toyc_full.sema | coge --check --contracts contracts/toyc-runtime-v1.json coge/examples/toyc/toyc.coge |
| sema --emit-rust-dir DIR tests/fixtures/legacy/toycp_full.sema | coge --emit-rust-dir DIR --contracts contracts/toycp-runtime-v1.json coge/examples/toycp/toycp.coge |
| sema --legacy FILE.sema | coge --legacy FILE.sema |
| sema sema/examples/toyscope_1/toyscope_1.sema | sema --check --contracts contracts/toyscope-runtime-v1.json sema/examples/toyscope_1/toyscope_1.sema |
| check_*.py --sema SEMA --source legacy/toyc_full.sema | check_*.py --coge COGE --source coge/examples/toyc/toyc.coge --contracts contracts/toyc-runtime-v1.json |
| regenerate_examples.py --sema SEMA | regenerate_examples.py --coge COGE --sema-cli SEMA |

COGE/SEMA mean executable paths. Script --sema remains an alias for --coge,
so that historical flag now expects coge. --sema-cli selects analysis for
ToyScope regeneration. ToyCP checks require its runtime contract. Document
migration includes v1 header, full grammar/lexer, section ownership and explicit
runtime contracts. Header determines kind. CLI errors explain legacy mode,
updated paths, the correct tool and analysis export when given the wrong layer.

### Example status

Authoritative ToyC/ToyCP .coge sources embed grammar, lexer and analysis, choose
the shared execution profile and keep local differences. They read no external
.ag; runtime contracts are explicit input. Legacy toyc_typed.sema/toycp_typed.sema
sources remain for comparisons/private adapter tests and are not projections of
the new documents. Regeneration uses standalone source/contracts. Older examples
still use coge --legacy; sema --legacy explains the command change.

Typed ToyScope has pure .sema projections and explicit contracts for generated
analysis; full .coge retains historical execution contracts needing conversion.
See [TOYSCOPE.md](../sema/docs/TOYSCOPE.md). execution result has ownership and
is correctly removed from projections, but checking/emission rejects it as
coge.unsupported_execution_result. Unsupported execution calls into semantic
functions give coge.unsupported_semantic_call. Other unsupported checker forms
fail explicitly; completeness is described below.


```sh
build/bin/coge --check --contracts contracts/toyc-runtime-v1.json coge/examples/toyc/toyc.coge
build/bin/coge --check --contracts contracts/toycp-runtime-v1.json coge/examples/toycp/toycp.coge
build/bin/coge --emit-rust-dir /tmp/toyc-v1 --contracts contracts/toyc-runtime-v1.json coge/examples/toyc/toyc.coge
```

```sh
python3 tools/regenerate_examples.py toyc toycp --coge build/bin/coge
python3 tools/check_return_policy.py --coge build/bin/coge --source coge/examples/toyc/toyc.coge --contracts contracts/toyc-runtime-v1.json
```

## Enum values and expansion inspection (5.1)

Operator.Add denotes a payload-free variant of an enum explicitly declared
in a contract. It must exist; this step does not add payload constructors or
generic enum values. A local Operator shadows the type namespace. The result
retains type Operator and ordinary call/return checks apply. Emission uses the
contract's rust path or crate::Operator::Add convention, without scanning Rust.
The emitter consumes an immutable checked enum-value plan.

--inspect-model is a separate one-input operation protecting source/contracts
and requiring --force for existing output. agsem-checked-semantic-expansion-v3
contains function/analyzer signatures, enum schemas and typed values with
variant, emission path, source byte span and input identities. Coge adds checked
execution_configuration and execution_model. This is not full serialization of
all policies. ToyScope limitations still apply. checkedEnumConstants and
inspectSemanticModel expose prepared data without emitter AST reanalysis.


```sh
build/bin/coge --inspect-model /tmp/toyc-model.json \
  --contracts contracts/toyc-runtime-v1.json coge/examples/toyc/toyc.coge
build/bin/sema --inspect-model /tmp/toyc-sema-model.json \
  --contracts contracts/toyc-runtime-v1.json coge/examples/toyc/generated/projections/toyc.sema
```

## Typed collections (5.2)

Analysis/actions share the builtin List namespace. empty requires an explicit
non-generic element name checked against builtins/contracts; single infers it.
append requires List<T> and exactly T, borrows input and clones ordered elements
into an owned output, preserving input and context. Rust checks element Clone.
Borrowed AST Node<R>/Token elements, including wrappers, and untyped none are
unsupported. A local List shadows the builtin namespace.

collection_operations records operation, concrete element/result/operand types,
ownership rules, Rust element type, emission expression and source span.
checkedCollectionOperations exposes the same prepared data. The v2 collection
inspection replaced pilot agsem-checked-enum-expansion-v1 while retaining enum
fields; current v3 adds bindings. Emitters do not re-read operation ASTs.


```text
List.empty(ExprId)
List.single(value)
List.append(values, next_value)
```

## Lowering and backend profiles (5.4)

Explicit structured_core_v1 supplies 42 default field/variant bindings for
Structured Core lowering. lower's function name remains required. Selection
is never inferred from language/type names; explicit model contracts and
rust_context are required. Check record/enum kinds, exact fields/ordered payloads
and fixed emitter dependencies for modules/aggregates/places/spans/comments.
Missing fields have no guessed replacement. Local assignments override only
their role, e.g. functions requires the chosen Context field List<Function>.
Duplicates, unknown roles/profiles, wrong types and helper-name collisions fail;
incomplete profile-free configurations still fail.

core_c_v1/core_llvm_v1 default to emit_c/emit_llvm and checked lowering. emit can
be overridden and explicit lower must agree with lowering_model. Backend needs
lowering. Inspection adds coge-checked-configuration-v1, with lowering/backend_c/
backend_llvm null if absent. It records profile ID/spec hash, complete bindings
with explicit/profile_default/checked_lowering origins and declaration spans,
structural TypeRefs, interfaces, pure context reads, dependencies and capabilities.
Profile hash covers canonical defaults/type/interface requirements and is
independent of overrides; input/manifest identities remain in top-level JSON.
CheckedLowering::inspection and CheckedGeneration::lowering/backends expose the
same expanded data without synthesizing DSL or reparsing.

Current lowering type-name/representation limits remain. Rust checks tuple/
record variants, field names, ID representation and Clone/Copy beyond logical
contracts. Profiles add no ToyCP class/cleanup lowering: ToyC uses profiles,
ToyCP interpretation. Sema projection removes execution configuration. These
profiles are independent of the semantic binding schema below.


```text
lowering_model {
    profile = structured_core_v1;
    lower = lower_function_to_structured;
}
backend_c {
    profile = core_c_v1;
}
backend_llvm {
    profile = core_llvm_v1;
}
```

## Model binding schema (5.3)

semantic_model may explicitly select standard_semantic_v1. It fills ports of
existing return/assignment/condition/statement/flow/selection policies without
selecting/creating policies. Require declared fields/context methods; allow one
schema and one policy per family. Unknown/duplicate schemas fail. model_schema
is an ordinary rule name outside MODEL.

With a schema, binding blocks contain only overrides and may be omitted when
all defaults apply. return_ir/cleanup_scopes/error_ir/construction_ir remain
explicit. ToyC chooses tuple/no_scopes; ToyCP record/active_scopes/Construct.
Without schema all bindings remain required. An assignment override changes
only its ports, not return.flow/poison; check actual named targets rather than
similar-type alternatives. selection.fields requires both Context.fields:
List<Field> and Struct.fields: List<FieldId>. condition.emit builds ExpressionKind;
return.emit/statement.emit build Operation.

Check structural TypeRefs including aliases, full method signatures/owner/effect,
and active adapter dependencies such as Function.result/Flow.reachable/Type.Void/
Operation.Return/statement variants. Cleanup/selection add requirements only
when active. Methods belong to the selected context; unsupported rust redirects
fail. pure describes receiver mutation, so mark_initialized may mutate its Flow
argument without mutating context.

Inspection v3 retains enums/collections and adds binding_schema (ID/version/
canonical-port/dependency hash/declaration span), ordered model_bindings (family,
policy, port, target kind, modes, required/actual signatures/effects, reads/writes,
calls and parameter forwarding), binding_dependencies and Rust representation
obligations. Origins refer to explicit binding/schema/policy and real manifest
ID/version/hash/JSON path. External contracts get no fictitious DSL span.
Schema hash excludes overrides/source/manifest hashes. Projection can move spans,
while types/effects/targets/dependencies/schema identity stay unchanged.

checkedModelBindings exposes the immutable binder plan kept in BoundSemantics
and preparation; emitters use complete maps, never expand DSL or guess ports.
Unbound legacy retains its old path and cannot prepare a schema. --emit-sema
preserves schema/overrides verbatim and records binding_schema in provenance.
sema_generation.provenance.json adds source/contracts/schema identity or null;
Rust is byte-identical to full explicit bindings. Protect sources/contracts/
aliases and foreign metadata.

Diagnostics include sema.unknown_model_schema, duplicate_model_schema,
unknown_binding_slot, duplicate_binding, orphan_binding, missing_binding,
binding_target_mismatch, binding_type_mismatch, binding_effect_mismatch and
binding_dependency_mismatch (all with sema. prefix). They report family/policy/
port/expected-versus-actual target and contract identity/JSON path. Rust still
checks tuple/record fields, ID .0, Clone/Copy, borrowing and method/function ABI;
the schema performs no Rust introspection.


```text
semantic_model {
    model_schema standard_semantic_v1;
    rust_context Context;
    // Policy declarations remain explicit.
    model_bindings function_return {
        return_ir = tuple;
        cleanup_scopes = no_scopes;
    }
    flow_bindings statements {
        error_ir = Error;
    }
}
```

```text
assignment_bindings store_value {
    flow = branch_flow;
    poison = branch_poisoned;
}
```

## Templates (6.3)

--from-ag or --from-sema requires exactly one input. Coge requires explicit
repeatable distinct --target interpreter|c|llvm; it never guesses runtime profile
or representation. Ag templates copy grammar/options/lexer/classes/commands/
comments/original bytes and add empty analysis plus status to every authored
alternative, including inline/empty. Status belongs to sema. ID is independent
of position; syntax signature excludes comments/actions. Initially identical
alternatives get deterministic distinct ordinals. Re-templating Ag resets pending
without recovering decisions. Duplicate IDs/status blocks fail; changed syntax
with an old signature gives completeness.stale_alternative during checking/
preparation, though valid Ag export still works. Move status with its alternative;
implemented requires checked body/interface.

From sema, preserve analysis text/metadata, change header and add only execution
work. Bind retained contracts via --contracts without requiring analysis completeness.
execution_obligations belongs to coge: each pending item names kind/target/role/
required work. Known Operation/ExpressionKind schemas enumerate actual variants;
unknown IR leaves an explicit unresolved contract obligation. Selected C/LLVM add
lowering/backend work; unselected targets do not. Metadata is a work list, not
implementation/proof. Remove completed pending entries, while the checker still
checks real declarations/coverage.

Template/provenance are individually atomically replaced, with copied/added
span maps, hashes and explicit targets. --force permits existing output but
protects source/contracts through symlink/hardlink aliases. Ag projection removes
actions/status, possibly retaining inserted whitespace; coge→sema retains analysis
status and removes execution_obligations. Input lacking final LF/CR gains LF before
execution, including after a trailing // comment with spaces/tabs; original bytes
stay preserved and that LF can remain after projection. Templates produce no
CheckedSemantics or pretend working runtime. tools/ag_to_sema_template.py is
legacy; current templates use shared parser/library APIs.


```sh
sema --from-ag language.ag --emit-template language.sema
coge --from-ag language.ag --target interpreter --emit-template language.coge
coge --from-sema language.sema --contracts runtime.json --target interpreter --emit-template language.coge
coge --from-sema language.sema --contracts runtime.json --target c --target llvm --emit-template backends.coge
```

```text
analysis_status {
  id "alt-v1:<64 lowercase hex digits>:0";
  syntax_sha256 "<64 lowercase hex digits>";
  state pending;
}
```

```text
execution_obligations {
  targets "interpreter";
  pending "execution_contract" "ir" "schema" "Select the execution IR explicitly.";
}
```

## Completeness checks and missing-work report (6.4)

--check validates written parts; missing mandatory implementation warns and
alone keeps exit 0. Unknown names, types, bad metadata or forbidden effects
remain errors. --check-complete also requires all selected-target obligations,
returning 1 for missing work. Diagnostics include file/line/column.
--report OUT writes agsem-completeness-v1 for one input, even on validation or
incomplete-body failures if a valid AST exists; syntax failure writes no report.
Existing reports need --force; input/contracts/aliases are protected. Reporting
does not modify source statuses.

Report data includes source/contracts identities, targets, structurally keyed
obligations, expected types/signatures or unresolved conditions, pending/
implemented/no_action state, separate valid/invalid/blocked validation, evidence,
dependencies, origins, byte spans/line/column and diagnostic indices. Ordering
is deterministic. Missing-work diagnostics remain warning in JSON; strict mode
renders errors on terminal and exits 1.

Every authored alternative requires analysis even without status. Unpersisted
IDs are derived deterministically but marked identity_persisted:false, not
proof of historical identity. Deleting pending does not waive work; explicit
pending stays pending even with a valid body. Stale syntax and false implemented
fail. no_action needs a reason, explicit Unit analyzer interface and empty
statements in every block. Checked NoActionUnit emits Ok(()) without visiting
children; it cannot replace other results/handlers. Checked child forwarding is
implemented with checked_forwarding evidence.

Analysis is always selected. Coge targets come from generation, lowering/backend
configuration, execution_obligations.targets and repeatable --target. C/LLVM
also require lowering. Deleting pending metadata cannot hide missing configuration;
unselected backends are optional. Known IR determines all handlers; enum growth
requires new ones. Effective profile/overrides determine obligations and local
requirements. Unknown IR asks for its contract instead of guessing variants.

results separates complete from can_emit per target. Completeness concerns
detectable explicit-model duties, not every language semantic rule. can_emit
adds current Rust emitter restrictions such as empty AST/external-function
binding forms. Emission needs both and full plan preparation; partial reports
never authorize emission. Rust ABI/representation/termination remain outside
proof. Projections/templates need no completeness; sema export still needs
closed retained contracts.


```sh
sema --check draft.sema
sema --check-complete --report completeness.json draft.sema
coge --check-complete --target c --report completeness.json \
  --contracts contracts/toyc-runtime-v1.json program.coge
```

## Generation gate and preparation proof (6.5)

Rust generation refuses unresolved work, valid bodies still marked pending,
missing metadata-hidden execution duties and missing profile-free handlers.
Refusal precedes Rust/manifest/provenance writes; --force cannot bypass it.
checkCoge returns CogeValidation with immutable completeness/configuration.
Both prepareGeneration overloads require complete emitter-supported targets
and matching source/contracts/generation identities. Owned CheckedGeneration
outlives inputs and emission rechecks its binding. CogeValidation is copyable
but cannot be forged/configuration-mutated; execution/lowering/backends/
completeness accessors expose checked data.

Legacy adaptation rejects standalone-v1 ExecutionInput. Passing CheckedExecutionModel
to raw prepareCheckedGeneration also requires document API completeness proof
(document.typed_preparation_required). Actual historical inputs and programmatic
plans without a document model retain prior validation. Templates/projections
can still contain missing work.

## Stage 6 acceptance and checks (6.7)

After templating, implement contracts/actions and change resolved alternatives
to implemented or justified valid no_action. Keep ID/syntax_sha256; syntax edits
need signature validation. New alternatives get their own obligation. Exporting
Ag and templating again resets all pending.

| Operation | Missing implementation | Type/name error | Complete but unsupported emission |
|---|---|---|---|
| --check | Warning, exit 0 | Error, exit 1 | Exit 0 if model valid |
| --check-complete | Error, exit 1 | Error, exit 1 | Exit 0; can_emit may be false |
| --emit-rust-dir | Refusal, exit 1 | Refusal, exit 1 | document.unsupported_construct |

Assume valid arguments/output paths. A node start: empty #Start with explicit
Unit/no_action satisfies completeness but its AST shape is unsupported by the
current Rust emitter. Syntax errors produce no report. Refusal preserves existing
files even with --force.

CTest label agsem_stage6 covers 16 acceptance tests: completeness API, CLI report,
acceptance matrix, templates/projections, independent analysis linking, library
boundaries and ToyC/ToyCP generation. Its build target prepares tools/native tests.
Projection fixtures precede CLI tests under CTest dependency ordering. Historical
full acceptance took about 2–3 minutes. A focused completeness/generation-gate
selection below does not replace template/projection/example acceptance.
These commands document verification options; they were not run for translation.


```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target agsem_stage6_tests -j 4
ctest --test-dir build -L '^agsem_stage6$' --output-on-failure -j 3
```

```sh
ctest --test-dir build -L '^agsem_stage6$' \
  -R 'agsem_(stage6_acceptance|completeness|completeness_cli)_tests' --output-on-failure
```
