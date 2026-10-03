# Generated interpreter execution model

[English](EXECUTION_MODEL.md) | [Polski](EXECUTION_MODEL.pl.md)

This design uses commit `8b66f6b` as its historical reference. It specifies a
target contract, including syntax beyond the generator available at that time.
The legacy examples use `.sema`; execution sections now belong to `.coge`.
See [the current document CLI](../../docs/DOCUMENT_CLI.md) for the implemented
standalone format and [ToyCP](../examples/toycp/README.md) for its current interpreter.

## Decision and measurable goal

`execution_model` defines typed runtime data and algorithms in the same action
language as analysis. `execution_contract` maps IR variants to those functions.
Generate types, memory, frames, calls, arithmetic, conversions, errors and
execution control in `interpreter_gen.rs`. Moving only dispatch calls is
insufficient: the bodies of load, store, call, convert and binary must also be
authored in the model, rather than hidden in language-specific Rust templates.

At the reference point, interpreter.rs had 656 lines (tests began at line 541),
against 128 generated lines. Acceptance requires at least 80% of the duties
listed below to be model-defined and more generated code than all remaining
manual runtime code, including the old-IR adapter and shared intrinsics. Exclude
tests, comments, blank lines, parser, semantic analysis and generated tables.
The estimated budget was 600–900 generated and 100–180 manual lines.

A later ToyC migration measured 886 generated nonblank lines, 135 adapter lines
and 150 shared-runtime lines, including collection helpers. value_for_type,
value_matches_type, declare, load, store, call, evaluate_call, errors and
conversions were generated. Public entry points, module validation, Context
adaptation and ValueTree implementation remained manual. `capture` and explicit
pop release frames on success/error; a nested-call failure followed by a valid
call was tested. General with/finally and the full lifetime/address design
remained future work at that milestone. Owned IR snapshots simplify ownership
at the cost of additional copying. These measurements are historical.

## Placement and responsibilities

The legacy layout below separates analysis/IR construction from execution.
The interpreter owns mutable runtime state and reads immutable IR; it does
not resolve runtime variables through the analyzer's name dictionary.

New constructs are execution_model, input and runtime_state; existing records,
enums, aliases, functions, intrinsic, mutates and control constructs are reused.
Explicit execute/evaluate signatures define dispatch_execute/dispatch_evaluate:
the first parameter selects the variant and remaining parameters are handler
context. Wrappers read an IR node, enforce limits and dispatch. Every typed
handler returns the declared result; no implicit Control.Continue is inserted.
Expression evaluation may mutate through function calls. Legacy unsigned
contracts retain their compatibility behavior.

The eight proposed input methods return owned snapshots: module, operation,
expression, place, variable, function, field and aggregate. Invalid IDs return
None, converted by model code into a localized InvalidIr rather than a host
panic. The temporary IrInput implementation for Context only reads data: no
arithmetic, execution, allocation or dynamic lookup. Input enum/record schemas
describe external IR without generating duplicate definitions;
execution_input_module = "crate::model" is the proposed migration binding.
Tuple versus named-field variants must be represented explicitly and checked
when compiling the adapter. Eventually one IR declaration can generate both.

Required view contents:

| View | Required data |
|---|---|
| ModuleView | Validity, first diagnostic source, source span, ordered globals, exports |
| OperationNode / Expression | Full IR variant and span; expression type/runtime checks |
| Place | Root SymbolId, ordered projections, final type |
| VariableView | Type, owner/global status, PlaceId, span |
| FunctionView | Result, explicit ParameterBinding list, optional body, span |
| FieldView | Owner, ordinal, type |
| AggregateView | Type identity, ordered FieldIds, ToyCP base subobject |

New View types are ordinary model records assembled by the adapter. IDs remain
opaque, comparable map keys; indexing old Context vectors stays inside the
eight adapter methods. StorageId is a separate generated runtime record.


```text
sema ToyCTyped for "toyc.ag";
semantic_model { ... }
execution_model { ... }
execution_contract { ... }
options { generate_interpreter = true; }
node program : ... analysis { ... } ;
```

```text
execution_contract {
    execute(kind: Operation, source: SourceRange)
        -> Result<Control, RuntimeError> mutates;
    evaluate(kind: ExpressionKind, expression: ExprId,
             checks: List<RuntimeCheck>, source: SourceRange)
        -> Result<Value, RuntimeError> mutates;

    execute Declare(symbol) {
        declare(symbol, source);
        return Control.Continue;
    }
    evaluate Load(place) { return load(place, source); }
    // The remaining variants must be covered as well.
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
    // Record, enum, intrinsic and function declarations follow here.
}
```

## Types and values

Required type features include enum payloads, records, List/Map/Option/Result,
I32/F32/U8/U64/Index, tuple/record/variant patterns, fields, assignments,
typed collection operations and qualified constructors. Action Int maps to
i64, whereas ToyC int is I32. Analysis Result<T> keeps its default error;
execution uses Result<T, RuntimeError>.

TypedValue preserves a structure's nominal type. Stable IDs identify fields
and parameters. FunctionView must carry ParameterBinding(symbol, type, mode);
the adapter may temporarily reconstruct and check the first-N-symbol convention,
but the model must not rely on it. By-value parameters/results deep-copy
aggregates into independent storage; ByReference(self) copies an Address handle,
not a long-lived Rust borrow. ToyC's string pointer is Option<LiteralId>, retaining
ExprId identity; it is unrelated to object Address and adds no pointer arithmetic.
Generation counters must never wrap so stale addresses cannot become valid.


```text
type LiteralId = Index;

enum Value {
    Int(I32), Float(F32), Bool(Bool), Char(U8),
    Pointer(Option<LiteralId>), Struct(List<Value>), Array(List<Value>),
    Void, Uninitialized
}
record TypedValue(type: Type, value: Value);
enum Control { Continue, Return(TypedValue) }

record StorageId(index: Index, generation: U64);
enum Projection { Field(Index), Element(Index), Base(Index) }
record Address(storage: StorageId, path: List<Projection>);
enum Argument { ByValue(TypedValue), ByReference(Address) }

enum Lifetime { Allocated, Constructing, Live, Destroying, Dead }
record Allocation(generation: U64, type: Type, value: Value,
                  lifetime: Lifetime, writable: Bool,
                  subobjects: Map<List<Projection>, Lifetime>);
record Frame(function: FunctionId, bindings: Map<SymbolId, Address>,
             scopes: List<ExecutionScope>);
record Cleanup(function: FunctionId, receiver: Address, source: SourceRange);
record ExecutionScope(scope: ScopeId, owned: List<StorageId>,
                      cleanups: List<Cleanup>);
record Limits(steps_remaining: U64, call_depth: Index, evaluation_depth: Index);
record Runtime(globals: Map<SymbolId, Address>, frames: List<Frame>,
               allocations: List<Allocation>, free_slots: List<Index>,
               global_cleanups: List<Cleanup>, evaluation_depth: Index,
               limits: Limits);
```

## Generated memory algorithms

Allocation, free-slot reuse, lifetime/generation checks, symbol resolution,
projection, initialization, reads and writes are model functions. Release
invalidates old addresses. Access resolves locals only in the current frame
or globals; searching callers is forbidden. Recursion with identical SymbolIds
must select the current invocation, and missing local bindings are InvalidIr.
Caller objects are accessed only through explicit addresses such as self.

Whole-aggregate reads require every leaf initialized; field reads check only
that field, and field writes do not require initialized siblings. load may be
mutating because computing a ToyCP index can have effects; read_address is pure.
checked_allocation, projected_type, fully_initialized, replace_path and
value_matches_type are also ordinary model code. Mutating a local field-list
copy does not mutate runtime. Deep projection traversal is subject to depth
limits and can be generated iteratively.

ToyCP base subobjects remain nested Value.Struct entries, with AggregateView
providing base/own-field indices and explicit Base projection. Fields are not
flattened. ToyC stores arrays but its grammar has no indexing; ToyCP Element
checks bounds and evaluates an index once. Compound assignment must reuse the
same resolved address for read and write, never repeat an effectful index.


```text
function load(place: PlaceId, source: SourceRange)
    -> Result<Value, RuntimeError> mutates {
    let address = resolve_place(place, source);
    let value = read_address(address, source);
    require fully_initialized(value)
        else runtime_error ErrorKind.UninitializedRead at source;
    return value;
}

function store(place: PlaceId, value: Value, source: SourceRange)
    -> Result<Unit, RuntimeError> mutates {
    let address = resolve_place(place, source);
    write_address(address, value, source);
    return unit;
}

function write_address(address: Address, value: Value, source: SourceRange)
    -> Result<Unit, RuntimeError> mutates {
    let slot = checked_allocation(address.storage, source);
    require slot.writable else runtime_error ErrorKind.ReadOnly at source;
    let target = projected_type(slot.type, address.path, source);
    require value_matches_type(value, target)
        else runtime_error ErrorKind.InvalidStoreType at source;
    let updated = replace_path(slot.value, address.path, value, source);
    runtime.allocations[address.storage.index].value = updated;
    return unit;
}
```

```text
function replace_path(root: Value, path: List<Projection>, replacement: Value,
                      source: SourceRange) -> Result<Value, RuntimeError> {
    if list_empty(path) { return replacement; }
    let head = list_first(path);
    let tail = list_tail(path);
    match (root, head) {
        (Value.Struct(fields), Projection.Field(index)) => {
            require index < list_len(fields)
                else runtime_error ErrorKind.InvalidAddress at source;
            let changed = replace_path(fields[index], tail, replacement, source);
            fields[index] = changed;
            return Value.Struct(fields);
        }
        (Value.Array(elements), Projection.Element(index)) => {
            require index < list_len(elements)
                else runtime_error ErrorKind.IndexOutOfBounds at source;
            let changed = replace_path(elements[index], tail, replacement, source);
            elements[index] = changed;
            return Value.Array(elements);
        }
        _ => runtime_error ErrorKind.InvalidAddress at source;
    }
}
```

## Frames, errors and unconditional cleanup

RuntimeError is generated from kind, source and diagnostic data. Model-defined
error_message preserves public ToyC message/source fields without a duplicate
manual message table. Ordinary Result-returning calls propagate errors and
yield T. Compiler construct capture(call(...)) executes once and returns the
whole Result; it neither catches Rust panics nor requires a runtime intrinsic.

The proposed with construct saves/restores its target; finally runs before
restoration, while the outgoing frame is active, including on return/error.
The generator may implement stack replacement with push/pop. This cleanup is
infallible storage/state cleanup, not user code. Materialize arguments/results
before releasing owned storage. finish_call preserves existing errors, checks
return types, and handles missing return/void behavior through generated branches.

Runtime v1 aborts on the first execution error; semantic analysis can still
collect multiple diagnostics. After a runtime or destructor error, free memory
and frames without invoking further language destructors, preserving the first
error. ToyCP promises no C++ exception unwinding.

Step, call-depth and IR/evaluation-depth limits are independent. Execution
recursion execute → evaluate → invoke → execute is valid; the semantic helper
cycle prohibition does not apply here. ToyC defaults remain 1,000,000 steps and
256 call frames, with a separate configurable evaluation-depth limit. Counters
are restored on errors. Frame construction atomically allocates parameters in
its root ExecutionScope: failure frees partial storage and publishes no frame.
Release frees all active owned scopes, not borrowed self addresses. A later
call_named after failure may use the remaining global state; program writes
are not transactionally rolled back.


```text
enum ErrorKind {
    InvalidIr, UninitializedRead, DivisionByZero, IntegerOverflow,
    WrongArgumentCount, WrongArgumentType, UndefinedFunction,
    MissingReturn, StepLimit, CallDepthLimit, EvaluationDepthLimit,
    InvalidAddress, DeadObject, ReadOnly, InvalidStoreType, IndexOutOfBounds
}
record RuntimeError(kind: ErrorKind, source: SourceRange);
```

```text
function invoke(function: FunctionId, arguments: List<Argument>, source: SourceRange)
    -> Result<TypedValue, RuntimeError> mutates {
    let definition = require_function(function, source);
    check_arguments(definition.parameters, arguments, source);
    check_call_depth(source);
    let frame = make_frame(definition, arguments, source);
    with runtime.frames = list_appended(runtime.frames, frame) {
        let outcome = capture(execute(definition.body));
        return finish_call(outcome, definition.result, source);
    } finally {
        release_frame_storage();
    }
}
```

## ToyCP: explicit self and IR lifetimes

Analysis/lowering selects methods, visibility, constructors, types and base
projections. A method call becomes Invoke with a leading ByReference receiver;
the interpreter uses FunctionId and never searches by name or guesses self.
Binding the parameter to an existing address lets writes update the receiver.
Base access uses subobject projection rather than host pointer casting; no
virtual dispatch is required.

Object declaration allocates storage when executed, invokes the selected
constructor, and registers Cleanup only after success. Normal scope exit and
return execute cleanup in reverse order, then release storage. Materialize the
return expression before cleanup. Skipped declarations create no object; each
loop iteration has fresh lifetimes, and for owns a separate initializer scope.
Proposed IR primitives Scope, Invoke and RegisterCleanup complement allocation,
load/store and control. RegisterCleanup appends a model record; it is not a
class-aware intrinsic. Transitional Construct/Destroy/MethodCall handlers must
be modeled or lowered. Never both register automatic cleanup and append the
same Destroy to Block/Return: each exited scope consumes its list exactly once.

Construction order is base, fields in declaration order, constructor body.
Destruction is destructor body, reversed fields, base. Arrays construct in
ascending order and destroy descending; globals construct in declaration order
before entry and destroy in reverse on normal shutdown.

self may target Constructing/Destroying objects, while field initialization
and lifetime checks remain active. Returned objects have independent storage.
The subobjects map tracks class lifetime by projection path; check all prefixes
so a live root cannot expose a dead subobject. Ordinary ToyC scalar fields need
no entries. Proposed v1 class copies synthesize field copying for by-value
arguments/results without implicit copy-constructor selection. Temporary/result
lifetimes must be explicit in IR and tested; Rust Clone does not define them.
This is a ToyCP subset design, not complete C++ semantics.


```text
object.bump(2)
    -> Invoke(bump_id, [ByReference(object_address), ByValue(Int(2))])

value = member
    -> Load(Project(parameter_0_address, Field(member_id)))
```

## Minimal practical manual layer

Memory, frame and class algorithms must be execution_model functions, with
no ToyC/ToyCP-specific intrinsic hiding them. Seven shared scalar families are
proposed, implemented once per backend:

| Family | Result and checks |
|---|---|
| checked_add<I>, checked_sub<I>, checked_mul<I> | Result<I, ArithmeticFault>, overflow |
| checked_div<I> | Zero divisor and MIN / -1 overflow |
| checked_neg<I> | Minimum-value overflow |
| float_eval<F> | IEEE scalar operations; optional second operand for negation |
| numeric_cast<S,T> | Explicit conversion policy |

These are finite monomorphized library schemas, not arbitrary user generics.
ArithmeticFault knows no ToyC text, Value, SymbolId or Context. Model binary
selects scalars and translates faults into sourced RuntimeErrors. F32 comparisons
retain IEEE NaN behavior. Match every possible fault; the example maps unexpected
faults to InvalidIr. C/LLVM may later emit these scalar operations directly.

The action backend also needs ordinary generated records/enums/patterns,
checked Vec operations (empty, len, first, tail, get, set, append, push, pop,
reverse), maps (empty, get, contains, insert, remove), Clone/Option/Result,
capture, with/finally and propagation. Map iteration order cannot define language
semantics. Mutating collection operations borrow checked targets only for the
call; no borrow escapes. Iterating a collection modified by the body requires
an explicit snapshot or checker rejection. These shared facilities are not
per-interpreter handwritten helpers. Count the eight temporary IR adapter
methods in the manual budget. Optional files, console, clock and external
functions belong to a separate host interface; source reading stays in the CLI.


```text
function add_int(left: I32, right: I32, source: SourceRange)
    -> Result<Value, RuntimeError> {
    let outcome = capture(checked_add(left, right));
    match outcome {
        Ok(number) => return Value.Int(number);
        Err(ArithmeticFault.Overflow) =>
            runtime_error ErrorKind.IntegerOverflow at source;
        Err(_) => runtime_error ErrorKind.InvalidIr at source;
    }
}
```

## ToyC numeric and behavioral compatibility

Preserve the existing ToyC subset, without broadening analyzer conversions:

| Area | Required rule |
|---|---|
| Scalars | int I32, float F32, char U8; ToyScope BigInt does not change ToyC |
| Integers | Checked overflow; division truncates toward zero; zero divisor errors; MIN / -1 overflows |
| Float | Existing F32 operations including infinity/NaN; no new float / 0 error |
| int → char | Low eight bits, matching Rust as u8 |
| float → int/char | Truncation/saturation; NaN → 0, matching existing Rust casts |
| char → int/float; int → float | Widening; F32 rounding respectively |
| Bool | Existing truthy conversion, no automatic numeric interpretation |
| Globals/locals | Zero globals including aggregate leaves; locals uninitialized until written |
| Arguments | Left to right; by value except explicit self |
| String literals | ExprId identity, no new deduplication |
| Invalid program | Refuse execution when analysis diagnostics exist |

Convert must still be explicitly inserted into IR. These are compatibility
rules for ToyC, not all C/C++ rules.

## Removing handwritten interpreter code

Generate Value, RuntimeError, Control and Interpreter state from declarations;
new and set_step_limit from model/public entry functions; global and call_named
from exports; error/tick from model messages and limits. Generate value_for_type
and value_matches_type, replace root_value/root_value_mut with checked addresses,
and generate field_value/load/store, call/evaluate_call, execute/evaluate,
declare/write_symbol/write, condition_truthy/execute_optional/return_value,
invalid_operation/invalid_value, truthy/value_from_constant/negate and
binary_checked/binary/compare/convert. Only final scalar primitives are intrinsic.
control_return/no_op wrappers become constructors/unit. Independent behavior
tests remain manual and outside the generated module.

No manual whole-language operator or class-method executor remains. The public
adapter may preserve global() → Option<&Value> using a short storage borrow;
the action language need not expose long-lived Rust references. call_named
can run repeatedly. ToyCP needs explicit run/shutdown for global lifetimes;
language destructors cannot live in Rust Drop, which cannot return a controlled
runtime error.

## Validation and implementation plan

Validate complete IR coverage, parameter/result types, disjoint patterns,
fields, mutates effects, Result propagation, finally, parameter ABI and infinite
by-value types. Input writes are forbidden; invalid models should fail before
rustc. First introduce execution_model, payload enums, fields, collections and
typed match using one typed action AST; migrate values/errors/operators/casts.
Then add capture, exact with/finally and a separate recursion domain; migrate
frames/calls/declarations/memory, retain ToyC API and enforce generated-code size.
Finally generate ToyCP IR/adaptation, lower self/calls and add Scope/cleanup.
A compilable generated file alone is not evidence of a working interpreter.

Acceptance covers recursion and independent storage for repeated SymbolIds,
valid calls after errors, I32 boundaries, division/negation/casts/NaN/signed zero,
independent field initialization and aggregate copying, no caller-local guessing,
dead/stale addresses and invalid fields/indices without host panics, self mutation,
nested methods/base access, declaration-time construction, skipped branches,
LIFO/early return/loop/for lifetimes, base-field-body ordering, object arrays,
globals/shutdown, return-before-destruction, constructor/destructor abort policy
without double destruction, explicit class-copy policy, generated behavior
changes after editing the model, Rust compilation/Clippy and scoped code-size
measurements. These are design acceptance requirements, not checks run for
this documentation update. The design needed no `.agx`; the later standalone
format split is documented in the current CLI guide.
