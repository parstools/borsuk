# Sema format and design

[English](SEMA_FORMAT.md) | [Polski](SEMA_FORMAT.pl.md)

This document covers the standalone format and its earlier action-language
design. Historical examples and milestones are retained below; current CLI
ownership and checking rules are described in [DOCUMENT_CLI.md](../../docs/DOCUMENT_CLI.md).

## Format separation

`.ag` owns grammar/lexer, standalone `.sema` adds semantic analysis, and `.coge`
adds execution/lowering/backends. The standalone document was introduced in
stage 2, followed by section ownership and example migration. Statements below
about a lexer-free `.sema` containing execution describe the legacy format.
Actions and policy contracts remain the starting point of the new format.
The original internal implementation plans were not imported into this repository.

## Standalone v1 document

The default sema frontend accepts a complete grammar and lexer in one file.
coge uses the same document frontend. Specification and grammar names are
independent. Omitted format means version 1; other versions are rejected.
There is no for path or implicit neighboring .ag read. The frontend uses its
packaged tool grammar.

options, channels, lexerClasses, conflicts, rule enable/disable and lexer rules
use Ag syntax. Actions follow symbols and the optional alternative label;
empty alternatives require empty. Generation has its own generation block.
The early frontend accepted both layers; current sema/coge enforce ownership.

composeDocumentGrammar combines typed productions from ag/grammars/Ag.ag,
Sema.ag actions/models and Document.ag headers, excluding duplicated Ag rules
and the old Sema header. Explicit extension points are the header, topLevelItem
and alternative termination. A right-recursive item sequence handles shared
prefixes; actions terminate symbols. The document parser is LALR(2) without
conflict-hiding policies; its k is independent of the described language's k.

ACTION/MODEL classes activate extension keywords in their scopes; AG enables
lexer character sets outside actions, where brackets retain list/index meaning.
User lexerClasses describe a separate lexer. Extension names remain available
as Ag rule and field names outside their keyword scopes.


```text
sema ExampleAnalysis;
format 1;
grammar Example;
options { parser = LALR; lookahead = 2; ast = explicit; }
node start : value=ID EOF analysis {};
ID : [a-z]+;
WS : [ \t\r\n]+ -> skip;
```

### Source and library projection

PackagedAgFrontend::parseDocument returns GrammarDocument with the Ag model
and owned UTF-8 text partitioned into tokens/trivia. Half-open byte spans cover
all bytes, including skipped comments and trailing whitespace.
DocumentFrontend::parse returns immutable ParsedDocument with source, identity,
private AST, section index and grammar projection. makeSemaDocument and
makeCogeDocument check header kind and section ownership; SemanticInput owns
only analysis data.

Projection removes parser-identified specification headers, extension sections
and actions, without regex or brace scanning. Comments inside removed spans
disappear; adjacent bytes, including the header's following newline, remain.
Canonical Ag reparses the result. Extension AST spans refer to full source;
grammar-model spans refer to the projection.

### Legacy compatibility

Legacy input requires explicit coge --legacy. ToyC/ToyCP generation uses
standalone .coge documents and runtime contracts; older examples and adapter
tests retain legacy mode. New documents need no compatibility flag and emit
from their embedded grammar. SemaDocument::semanticInput() owns its data and
can outlive the document. Repeated --contracts FILE supplies external contracts
explicitly, with no directory discovery. See [the CLI guide](../../docs/DOCUMENT_CLI.md).


```sh
build/bin/coge --check --contracts contracts/toyc-runtime-v1.json coge/examples/toyc/toyc.coge
```

## Legacy format

Historically .ag defined syntax and .sema attached semantic actions and could
describe execution of the resulting IR. Legacy .sema has no user lexer rules.
Its format parser still needs a lexer, reusing Ag tokens plus action tokens.
Neither island grammar nor opaque analysis strings are necessary.

## Declarative specification instead of copying Rust

The parser/generator supports return_policy, assignment_policy, condition_policy,
statement_ir and flow_actions for ToyC/ToyCP, plus a ToyCP selection_policy pilot.
Other proposed policies remain design work. Migrating statements.rs,
expressions.rs, declarations.rs and classes.rs should shorten the complete
specification, not move arena loops and Result plumbing into equivalent actions
or a language-specific intrinsic. Policies describe conditions, conversions,
IR construction and flow/lifetime effects; the generator expands them into
typed actions. Ordinary functions cover unusual cases. Policy field names need
no separate lexer keywords.

One return_policy is accepted per legacy file. ToyC uses no_cleanup/no_scopes;
ToyCP uses active scopes and record-form Return. A rule analyzes its optional
expression and applies the policy, without searching arenas or constructing
operation IDs manually. Return must have an explicit matching IR contract.
Missing enclosing function is an error; void requires no value and other result
types require a convertible value. Poisoned input suppresses secondary conversion
errors. Failure emits error IR and preserves incoming flow; only a valid return
makes the path unreachable. exited_automatic_lifetimes cleans owned objects in
reverse successful-construction order, excluding borrowed self and globals.
Analysis records cleanup in IR; execution materializes the result before cleanup.

Constructor selection emits choose_constructor, collect_constructor_candidates
and complete_construction. Candidates retain ID, conversion plan, rank and access;
receiver excludes implicit self from matching. Select the unique lowest rank,
resolve ties before checking access, and never skip a private best candidate.
Apply its saved conversion plan, emit Construct and mark initialized on success.
classes.rs remains a thin adapter. This is a ToyCP pilot, not full C++ overload
resolution. Member access/subobject construction/Construct insertion remain
explicit obligations, not a hidden finish_construction intrinsic.

Assignment specifies valid target, conversion, initialized-old-value requirement,
successful-write state and target/RHS evaluation order. ToyC static_place pairs
with Store; ToyCP target_once_before_rhs pairs with StoreAndCompoundStore.
CompoundStore resolves the target once before RHS, including effectful indices.
assignment_bindings names arenas/flow/builders; missing/unknown fields and
inconsistent evaluation/IR pairs fail. increment_value allows int/float/char,
creates integer 1 and uses compound add/sub, preserving invalid increment operand.
Current adapters use ToyC/ToyCP AssignmentOp/Type/ExpressionKind/Operation;
broader models need explicit mapping or a schema.

condition_policy preserves bool, converts numbers/pointers to bool, propagates
dependent error for poisoned input, and rejects other types as condition is not
convertible to bool. Bindings name expression storage, poison checks and conversion
construction. statement_ir builds Call/If/While/For/Block and statement insertion
through declared scope/operation storage. Early variant-shape checks relied on
Rust; standalone contracts now provide logical schema checks.

flow_actions names snapshot, restore, merge and error recovery. Shared runtime
stores snapshots, while language model owns the merge of place initialization.
Both if branches start from the same snapshot; loops include the zero-iteration
path. Recovery records diagnostics, restores input flow and emits error IR.

Compare diagnostics, IR and execution against independent source tests while
shortening the total model/policy text. The statements group is generated.
Optional modules organizes sema_<name>_gen.rs without changing semantics;
sema_gen.rs/sema_lib_gen.rs facades preserve calls. sema_modules.manifest permits
deleting only previously owned generated modules. ToyC/ToyCP split control rules
and IR builders into modules.

lowering_model names a function and mappings for declarations, expressions,
blocks, branches, loops and returns. lowering_gen.rs creates Structured Core IR
for the entry function and reachable callees, scalar parameter slots, ordered
argument evaluation and source comments; it verifies CFG expansion and rejects
unsupported operations. See [CORE_IR.md](../../coge/docs/CORE_IR.md).
backend_c binds the checked lower function to the shared C11 emitter. The pilot
supports parameterless main, reachable scalar functions/local slots, int/float/
bool/char, char→int/float and int→float, scalar→bool, constants/load/store,
checked integer and float arithmetic, blocks/if/while/for/return/comments.
C prototypes permit recursion and later definitions. Indexed local scalar arrays
have distinct load/store operations, with runtime bounds and element-initialization
checks. Future common lowering makes self, aggregate conversions, construction
and cleanup explicit for both C and LLVM.


```text
semantic_model {
    return_policy function_return {
        target = enclosing_function;
        value = absent_for_void_otherwise_required;
        conversion = implicit_conversion;
        cleanup = exited_automatic_lifetimes;
        evaluation = capture_value_before_cleanup;
        flow = unreachable_after_success;
        ir = Return;
        errors {
            missing_target = "return outside function";
            wrong_presence = "invalid return value";
            failed_conversion = "incompatible return type";
        }
    }
}
```

```text
model_bindings function_return {
    active_function = active_function;
    functions = functions;
    flow = current_flow;
    poison = poisoned_expression;
    convert = convert;
    emit = add_operation;
    return_ir = record;
    cleanup_scopes = active_scopes;
    scopes = scopes;
    variables = variables;
    destructible = type_needs_destruction;
}
```

```text
selection_policy constructor_choice {
    choose = choose_constructor;
    collect = collect_constructor_candidates;
    complete = complete_construction;
    candidate = ConstructorCandidate;
    receiver = first_parameter;
    ranking = fewest_conversions;
    access = after_ranking;
    fallback = no_declared_and_no_arguments;
    missing = "constructor not declared";
    ambiguous = "ambiguous constructor call";
    inaccessible = "constructor is not accessible";
    nonclass = "constructor requires class type";
    missing_default = "default constructor not declared";
    incompatible = "incompatible argument type";
}
selection_bindings constructor_choice {
    structs = structs;
    constructors = constructors;
    functions = functions;
    parameters = parameters;
    expressions = expressions;
    expression_type = ty;
    visibility = visibility;
    accessible = accessible;
    convertible = conversion_allowed;
    variables = variables;
    variable_type = ty;
    variable_place = place;
    fields = fields;
    field_type = ty;
    default_constructible = default_constructible;
    convert = convert;
    construction_ir = Construct;
    add_operation = add_operation;
    scopes = scopes;
    scope_operations = operations;
    current_flow = current_flow;
    mark_initialized = mark_initialized;
    active_initializer = active_initializer;
}
```

## Legacy outer syntax

Legacy .sema reuses node/inline, rule names, fields, quantifiers, alternative
labels and separators from Ag. Lexer rules/channels/conflicts remain in the for
source; its own options controls generation rather than copying parser options.
The header identifies a semantic variant separately from its grammar.
Validation compares rule kinds/names, fields, ordered symbols, quantifiers and
labels against .ag. Unlabeled alternatives are position-keyed, so reordering
also requires verification. Multiple analysis blocks form one ordered sequence;
earlier let bindings remain visible and result assignment does not terminate it.
execution result belongs to a different phase. A formatter may merge analysis
blocks without changing meaning.


```text
sema ToyScope1 for "toyscope.ag";

node declaration
    : INT name=ID initializer=initializer SEMI #InitializedDeclaration
      analysis {
          let id = text(name);
          require not(containsLocal(scope, id)) else error "identifier already declared";
          let symbol = declare(scope, id, Int, Uninitialized);
          setState(flow, symbol, Initializing);
          analyze initializer with scope: scope -> value;
          require canConvert(typeOf(value), Int) else error "incompatible initializer type";
          setState(flow, symbol, Initialized);
          result = makeDeclaration(symbol, value);
      }
    ;
```

## Small action language

The examples describe the proposed early core, not the complete current parser.
That core used named functions for computations rather than arithmetic precedence;
later execution support includes scalar operators. Control words plus analyze
and require are keywords; result/scope/flow/declare/makeInitialization are
identifiers whose meanings come from context/library. result is the predefined
alternative output. let creates a binding; assignment requires an existing
mutable place. foreach visits a finite collection in source order. return
exits an action helper, not the user program. Builtins need explicit types/effects;
f(...) alone cannot distinguish builtin from authored function.


```text
block       := "{" statement* "}"
statement   := "let" ID "=" expression ";"
             | place "=" expression ";"
             | expression ";"
             | "if" expression block ("else" block)?
             | "foreach" ID "in" expression block
             | "return" expression ";"
             | "analyze" ID ("with" arguments)? "->" ID ";"
             | "require" expression "else" "error" STRING ";"
expression  := atom postfix*
atom        := ID | NUMBER | STRING | "true" | "false" | "none"
             | "(" expression ")"
postfix     := "." ID | "[" expression "]"
             | "(" (expression ("," expression)*)? ")"
place       := ID ("." ID | "[" expression "]")*
```

```text
analysis {
    let operations = list();
    foreach item in items {
        analyze item with scope: scope, flow: flow -> operation;
        append(operations, operation);
    }
    result = makeBlock(scope, operations);
}
```

## Helpers and primitives

The 93 names in sema_call_names.txt include constructors, methods, helpers,
queries and primitives, not 93 manual runtime implementations. Model functions
use the same statements as analysis. intrinsic f(...); declares a provided
operation without a body; legacy expression-bodied intrinsic remains a call-graph
leaf. Records/entities provide data constructors and query has an expression
body. The old checker treated intrinsic as a contract, not proof of implementation.

Typed functions specify parameters/result. Rust generation requires one
rust_context; analyzer signatures can be explicit or inferred from actions,
analyze arguments, function contracts and typed inherited declarations.
Inherited inputs are passed by analyze and the arrow result is synthesized.
Context/name types are not hardcoded. Typed intrinsic binds context methods.
See [ANALYZER_INFERENCE.md](ANALYZER_INFERENCE.md).

The scope_codegen vertical example emits sema_lib_gen.rs and analyzers for
program/statement/block. Program supports name read, declaration-assignment-read,
standalone declaration and statement-then-read. Statement supports if/else,
assignment, read, declaration and nested block; block is statement*. LALR(2)
distinguishes alternatives following int x;. Handwritten sema_intr.rs provides
SemaContext, IDs and intrinsic implementations. Production validation checks
names/kinds/labels/symbol order; Ag separately generates parser_gen.rs.

declare_variable binds before later reads, duplicates report identifier already
declared, missing names undeclared identifier. read_symbol checks local
uninitialized variable; globals start at zero and parameters initialized.
Initialization lives in a separate Flow map, not Symbol. Explicit FlowId is
passed through declaration/assignment/read. flow_copy creates independent state;
merge_flow retains common symbols initialized on both reachable paths, preserving
ZeroInitialized only when both do, otherwise Initialized. statement returns the
new FlowId; block passes it sequentially. Each block creates a child scope and
leave_scope removes its locals, preserving outer state and shadowing correctly.
Both if branches are reachable and analyzed from separate snapshots. Empty
analysis on statement's block delegation forwards to analyze_block with checked
result compatibility.

IR contains Declare/Assign/Read/Block/If with checked symbol IDs and ordered child
operations. record_if replaces the shared branch prefix with one If. program_ir
keeps one list per accepted source, including empty blocks. analyze_source
snapshots SemaContext and restores declarations/scopes/Flow/partial IR on failure;
a change journal or persistent structures can replace full copying at scale.
execute_ir runs this small IR with i64 values and explicit initial SymbolId/value
pairs. Declare handles zero/uninitialized state; Initialized external symbols need
an input number. Locals disappear on block exit. Missing runtime values and
function-as-number are errors. Assignment analysis builds IR, never executes it;
unknown targets/functions/out-of-i64 literals fail before insertion.

The example generator uses production/action data rather than hardcoded Lookup
or DeclareAssignRead sequences. It supports labeled node alternatives, single
token/rule children and statement* lists; top-level let/require/analyze,
Unit calls, foreach with FlowId accumulation and final result assignment.
The historical analysis subset has limits on nested control. Helper support
includes ScopeId/FlowId/SymbolId/StructTagId/ParameterId/Text/OwnedText/Bool/Int/
Unit, selected Option/Result/list types, let/if/foreach/return/error, calls,
comparisons and and. It checks argument count/types/effects for declared intrinsic.
mutates permits context writes; pure helpers call only read operations. Analysis
Result currently emits Result<T, &'static str> with propagation.

parameter_name uses declaration handles occurring once in a list; unique_names
compares names without modifying it. bind_parameters validates the whole list
before binding initialized parameters. Ordinary symbols and structure tags have
separate namespaces. Compatible function prototypes may repeat but only one
definition is allowed. This small example uses int objects and canonical-string
function signatures, without full structural function types/structure completion.
The parser accepts more Action forms than this generator. Rust stores objects
in context and passes Copy IDs and borrowed input text; results never borrow
inside context. Broader contracts are in [ACTION_CONTRACTS.md](ACTION_CONTRACTS.md).


```text
semantic_model {
    intrinsic lookup_builtin(scope, name);

    function find_symbol(scope, name) {
        return lookup_builtin(scope, name);
    }

    function require_symbol(scope, name) {
        let symbol = find_symbol(scope, name);
        require symbol != none else error "undeclared identifier";
        return symbol;
    }
}
```

```text
build/bin/coge --legacy --emit-rust-dir \
  sema/examples/scope_codegen/generated \
  sema/examples/scope_codegen/scope_codegen.sema
```

```text
build/bin/agas --emit-rust-parser \
  sema/examples/scope_codegen/generated/parser_gen.rs \
  sema/examples/scope_codegen/scope_codegen.ag
cargo test --manifest-path sema/examples/scope_codegen/Cargo.toml
```

## Target interpreter generation contract

analysis builds typed IR; runtime data/state/functions belong to execution_model
and variant mappings to execution_contract. Historical .sema examples below now
belong in .coge. Sema.ag parses input/runtime_state/payload enums/records/type/
intrinsic/function. The initial generator emitted Value/state, records and ToyC
methods; input/type generation was explicitly unsupported at that milestone.
runtime_state requires a record. input/runtime_state are MODEL keywords, while
state is an ordinary identifier; grammar productions disable MODEL/ACTION.

The implemented execution subset includes typed parameters and Result<T,
RuntimeError>, let/return, propagating calls, if/foreach/pattern match, scalar
operators and sourced runtime_error. It reads record fields and reads/writes
declared runtime_state, generating counters and execute/evaluate. Value.Int
emits Value::Int. AgSemRuntime checked arithmetic returns Option; model code
translates none to a sourced error. foreach consumes its collection; clone()
permits reuse. Local collection/store methods return raw Rust results without
automatic ?. Recognized mutating methods make locals mutable; runtime writes
require mutates. Rust validates host collection method types.

capture(action_call) evaluates once and retains Result. ToyC captures execution,
pops the frame and matches Ok/Err; re-emitting fault.message/source preserves
the original error. capture catches no panic. General with/finally is proposed;
the implemented model uses explicit push_bindings/capture/pop/finish_call.
Execution recursion is permitted independently of semantic helper cycles.
Early checking resolved locals/declaration structure; standalone documents now
add typed logical contracts as described in the CLI guide, while Rust still
checks physical representation.

rust/crates/sema-runtime provides typed global/frame storage, nested path
projection, checked I32 and scalar casts. Missing storage versus invalid path
remain distinct; model code owns messages/source and evaluation rules. ToyC
uses SymbolId/Value. Future BigInt operations can reuse num-bigint, as ToyScope
already does; choosing I32 does not require BigInt. Context adaptation remains
project-specific. The complete address/lifetime scheme below is a target design.

All example names/types, including Address/Frame/Allocation/resolve_place/
read_address/fully_initialized, need declarations; none is a hidden ToyC operation
in the C++ generator. Explicit input schemas cover Operation/ExpressionKind,
node fields and read signatures. Eight temporary read adapters can be generated
later from IR declarations. Typed dispatcher signatures specify input/result/
effect and require one compatible handler per variant with explicit results;
legacy statement handlers retain default Continue until converted.

default_value/value_matches_type/truthy/value_from_constant/binary/convert/
resolve_symbol/resolve_place/load/store/execute/evaluate/call/scope algorithms
must have generated bodies. Required action features include payload enums,
records, List/Map/Option/Result, typed assignment, matching, loops, scalars,
capture and reliable cleanup. Reject unknown variants, invalid projections,
missing branches and immutable-input writes. Runtime recursion needs limits.

Addresses combine allocation ID/generation with field/element path; recycled
slots cannot revive stale handles. Current frames resolve locals without searching
callers. Aggregates copy by value; ToyCP self is an explicit object address,
unrelated to ToyC char pointers. ToyCP lowering selects FunctionId/FieldId and
base projection; execution neither chooses methods nor checks visibility.
Construct at declaration and register cleanup only on success. Scope exit/return
destroys in reverse order after capturing the result; do not duplicate Destroy.

The manual boundary is shared scalar add/sub/mul/div/neg/F32/casts plus a
temporary eight-method immutable IR adapter. No memory/frame/class algorithm
may hide behind an intrinsic. Host files/printing/console remain CLI or a separate
interface. Generate Value/RuntimeError/Control/state, memory/frames/calls/value
functions, complete dispatch and public new/global/call_named/limits. Generated
runtime code must exceed all manual runtime+adapter+scalar code excluding tests,
parser and analyzer. Implementation milestones are values/scalars, memory/
initialization, frames/error cleanup and explicit ToyCP calls/lifetimes, each
compared with ToyC. Re-measure after memory migration; ToyCP needs separate
behavior tests. See [EXECUTION_MODEL.md](../../coge/docs/EXECUTION_MODEL.md).

Semantic function/query graphs reject direct/indirect recursion and unresolved
calls; constructors/intrinsic are leaves and parameters/constants are valid
leaf returns. The historical name-based checker did not fully resolve method
signatures/overloads/effects; legacy functions.sema illustrates that stage.


```text
semantic_model { ... }   // Analysis contracts and IR construction.
execution_model { ... }  // Runtime data, state and executable functions.
execution_contract { ... } // Handler for each IR operation and expression.
```

### Execution properties and boundary tests

One properties block emits interpreter_properties_gen.rs with
check_execution_properties. A manual test initializes the interpreter and calls
it; generated code supplies domains/loops/expectations/error source/argument
reports. An optional trailing SourceRange parameter is supplied automatically.
Properties do not change production behavior.

The initial subset supports one or two I32 variables, pure functions returning
Result<I32, RuntimeError>, numeric where comparisons, fits/not fits I32,
expected-result equality and fails with message. mathematical_sum/difference/
product/negation compute independently in i128, without calling tested runtime;
nested mathematical expressions are unsupported to keep expectation evaluation
overflow-free. forall tests a finite set, not all values. The base has 18 boundary
values including I32 extremes/neighbors, zero, ±1/±2 and multiplication/cast
boundaries, extended by property constants, their negatives and neighbors.
Two variables use the Cartesian product. A property admitting no cases fails.
The historical nine ToyC properties produced 1008 checked calls. properties/
forall/expect/fits/fails are MODEL keywords and do not change grammar rules.


```text
properties {
    forall a: I32, b: I32
        where mathematical_sum(a, b) fits I32
        expect checked_add_i32(a, b) == mathematical_sum(a, b);
    forall a: I32, b: I32
        where mathematical_sum(a, b) not fits I32
        expect checked_add_i32(a, b) fails "integer overflow";
}
```

```text
execution_model {
    input ir {
        module() -> Option<ModuleView>;
        operation(id: OpId) -> Option<OperationNode>;
        expression(id: ExprId) -> Option<Expression>;
        place(id: PlaceId) -> Option<Place>;
        variable(id: SymbolId) -> Option<VariableView>;
        function(id: FunctionId) -> Option<FunctionView>;
        field(id: FieldId) -> Option<FieldView>;
        aggregate(id: StructId) -> Option<AggregateView>;
    }
    runtime_state runtime: Runtime;

    enum Value { Int(I32), Float(F32), Bool(Bool), Char(U8),
                 Pointer(Option<LiteralId>), Struct(List<Value>),
                 Array(List<Value>), Void, Uninitialized }
    enum Control { Continue, Return(Value) }
    record Runtime(globals: Map<SymbolId, Address>, frames: List<Frame>,
                   allocations: List<Allocation>, limits: Limits);

    function load(place: PlaceId, source: SourceRange)
        -> Result<Value, RuntimeError> mutates {
        let address = resolve_place(place, source);
        let value = read_address(address, source);
        require fully_initialized(value)
            else runtime_error ErrorKind.UninitializedRead at source;
        return value;
    }
}
```

```text
execution_contract {
    execute(kind: Operation, source: SourceRange)
        -> Result<Control, RuntimeError> mutates;
    evaluate(kind: ExpressionKind, expression: ExprId,
             checks: List<RuntimeCheck>, source: SourceRange)
        -> Result<Value, RuntimeError> mutates;

    execute Store(place: place, value: value) {
        let computed = evaluate(value);
        store(place, computed, source);
        return Control.Continue;
    }
    evaluate Load(place) { return load(place, source); }
}
```

## Execution and limits

Child analysis is explicit: parsing never automatically executes descendant
analysis. A declaration can bind before analyzing its initializer. analyze
returns a result or sourced error. Semantic actions build checked symbols,
types, AST/IR and diagnostics; they do not run the user program.

Turing completeness is not required for semantic analysis. Finite foreach and
acyclic helpers aid termination; execution must support user loops/recursion
with step/call-depth limits and a separate recursion domain. Early *.sema.txt
files are normalization inputs, not executable programs in the typed core.
Their full value/result types, builtin signatures, inherited context, error
propagation, spans and production compatibility must be established first.

## Initial parser

Sema.ag implements the early core as Agas LALR(1). MODEL and ACTION use the
same contextual lexer machinery as >> versus > >. Model bodies enable MODEL;
actions there inherit it, while grammar-attached actions do not. Grammar rules
restore grammar context. function can be a model keyword and an ordinary rule
name. Older type/present/otherwise field words remain allowed identifiers.
Disabling MODEL for every actionBlock caused contradictory contexts in a merged
LALR state; removing that disable yielded zero conflicts. A stricter function-body
action mode would need narrower MODEL scope or separate entry rules.

status=accepted confirms syntax only. The initial parser accepted the four
legacy sketches including semantic_model/execution_contract/execution result/
rewrite/analyze/require/match/with/for and their expressions. That historical
milestone did not yet validate action types or copied productions. Its operator
sequences required later precedence/semantic interpretation before Rust emission.


```sh
build/bin/agas --diagnose-parse sema/grammars/Sema.ag tests/fixtures/legacy/minimal.sema
```

## Comparing with Ag

tools/compare_sema.py matches named node/inline rules and alternatives by
symbol/field/suffix/label, ignoring comments/actions. Missing/additional items
are errors; order differences are warnings with move positions. Historical
ToyC matched 41 rules/82 alternatives; ToyScope matched 13/26 after explicit
empty/repeated/optional alternatives. Compare policy 1 against its semadesc
reference, not ToyC. RustGenerator.cpp/.h contains generation code.

The generator supports unlabeled inline forwarding with surrounding unlabeled
tokens, a single unlabeled node variant, nonempty analysis on promoted labeled
nodes, inline multi-field records and forwarded tokens. Small examples compile
in Rust. Legacy ToyScope1 stopped at missing rust_context and untyped
apply_integer parameters. typed_codegen demonstrates CalcContext, Int result,
two inherited inputs and parse_decimal intrinsic.

Typed ToyScope1 covers the whole grammar with stable IDs for scopes/symbols/
places/expressions/operations and mathematical BigInt from num-bigint. It keeps
source ranges, independent diagnostics and operations after erroneous statements.
analyze_source_partial returns an invalid program for inspection; analyze_source
returns its first error. Execution refuses invalid programs; syntax recovery
is separate. Helpers are generated in sema_lib_gen.rs and primitive operations
implemented by ToyScopeContext. Typed policies 2/3 respectively bind after the
initializer and reserve direct declarations before analysis, with runtime cell
checks; their Rust examples share policy 1's IR/runtime.

The historical generate_interpreter option first generated handler control
flow (let/calls/return/if/foreach/while and Continue/Return), leaving value/memory/
frames manual; execution_model then expanded generation. Full address/lifetime
design remains distinct from implemented subsets. Inference reduces ToyC's
41 signatures to seven inherited declarations and ToyCP's 57 to ten. Older rich
ToyScope execution contracts are design specifications and need conversion to
the standalone checker; current sources/CLI use the paths in the examples below.

Comparison -o creates a reordered copy only for structurally identical input.
It moves complete alternatives with comments/actions, protects input/existing
output and refuses non-whitespace between rules because comment ownership is
ambiguous. This is a textual structural check; syntax/type validation belongs
to the C++ tool.


```sh
python3 tools/compare_sema.py tests/fixtures/legacy/toyc.ag tests/fixtures/legacy/toyc.sema.txt
python3 tools/compare_sema.py tests/fixtures/legacy/toyc.ag tests/fixtures/legacy/toyc.sema.txt -o /tmp/toyc.reordered.sema.txt
```

## Legacy CLI and subsequent work

coge --legacy loads the packaged Sema.ag parser, checks grammar compatibility
and reports syntax OK or a line/column error. The historical syntax-check phase
did not run semantic actions. --calls gathers sorted unique call names; the
four legacy sketches yielded 93, including constructors/methods without resolved
owners/signatures. parse_int was unified with parse_integer using explicit
radix 10. read versus read_initialized remain separate analysis/runtime operations;
Store/CheckedStore and Load/CheckedLoad represent different initialization policies.

The early roadmap required verified-tree structural comparison and action typing.
The legacy grammar header was transitional; sema Name for "file.ag" identified
variant/source, while current standalone v1 embeds grammar. Resolved call
manifests should classify analyze/require compiler operations, data constructors,
context methods, intrinsic and authored helpers, including signature, use and
definition locations. Unresolved names/signature mismatches must fail before
sema_gen.rs. External runtime implementations can remain a separate library;
generation must never emit empty functions pretending to satisfy contracts.


```sh
cmake --build build --target coge
build/bin/coge --legacy tests/fixtures/legacy/toyc.sema.txt tests/fixtures/legacy/toyscope_1.sema.txt
```

```sh
build/bin/coge --legacy --calls /tmp/sema_call_names.txt \
  tests/fixtures/legacy/toyc.sema.txt tests/fixtures/legacy/toyscope_1.sema.txt \
  tests/fixtures/legacy/toyscope_2.sema.txt tests/fixtures/legacy/toyscope_3.sema.txt
```
