# Core IR after semantic analysis

[English](CORE_IR.md) | [Polski](CORE_IR.pl.md)

Core IR is the shared result of semantic lowering. Runtime sources are in
`rust/crates/sema-runtime/src/`. `structured_ir.rs` preserves blocks,
conditions, loops, and returns. C emits structured code without `goto`;
`structured_ir::lower_to_cfg` expands it into CFG for LLVM. `core_ir.rs`
defines CFG types and verification. `llvm_backend.rs` emits textual LLVM IR;
`backend_llvm` in a coge document generates the language adapter.

Source comments are presentation data, not semantic instructions. ToyC/ToyCP
retain `HIDDEN` comment tokens, text, and byte spans in `Context.comments`.
ToyC lowering transfers function-body comments to the structured module; C
places them near statements by source position, preserving text and relative
order without promising identical whitespace.

Core IR does not replace the parse tree or language-specific ToyC/ToyCP IR.
Generated `lowering_model` translates language IR into Structured Core IR.
ToyC's initial `lowering_gen.rs` supports argument-free `main`, reachable
scalar-signature functions, scalar locals/initializers/assignments, nested
blocks, `if`, `while`, `for`, and `return`. Expressions include scalar constants,
local reads, calls, conversions, arithmetic, and comparisons. Unsupported
constructs are rejected explicitly. There is a C11 emitter and Core IR executor;
the existing ToyC interpreter still executes language IR.

## Structured control flow

A structured module contains functions and blocks. Typed instructions are
shared with CFG, while `Statement` retains nested blocks, `If`, `While`, `For`,
and `Return`. Loop conditions have separate instruction lists executed in
header blocks on every iteration. `For` retains initialization and step blocks;
the step is reached only after normal body completion. Comments are a side list
with source spans. `lower_to_cfg` expands control into jumps and verifies the
CFG; it does not insert a default return for a function requiring a result.

## Data contract

`Module` owns types and functions. `TypeId`, `FunctionId`, `BlockId`, `SlotId`,
and `ValueId` are distinct. A function contains slots, parameter-slot IDs,
an optional result type, blocks, and an entry block. Every block must end with
exactly one jump, branch, return, or error terminator.

Initial types are `i32`, `f32`, `bool`, `u8`, address, array, and aggregate.
No function result is `None`, without a `void` type. `Null` has the concrete
address type selected by its instruction result. `i32`/`f32` match current
ToyC numeric types; this does not introduce BigInt. `SourceSpan` retains file
identity and byte range for later diagnostics.

Temporaries are available from their definition through the end of their
block only. Cross-block data uses slots and explicit `load`/`store`; there are
no SSA phi nodes. Verification rejects cross-block reads and duplicate value
definitions within a block. Parameter slots are initialized on entry; other
slots require runtime initialization. Slot-initialization dataflow is not yet
part of the verifier.

Instructions include constants, slot addresses, load/store, conversions,
arithmetic, scalar comparison producing `bool`, calls, and bounds checks.
`CheckedI32` errors on overflow/division by zero; `IeeeF32` follows IEEE 754.
`BoundsCheck(index, length)` requires `i32` and errors when `index < 0` or
`index >= length`. Every backend must reproduce these effects; verification
checks types and structure only.

## Arrays, structures, and globals

ToyC supports local one-dimensional scalar arrays. Lowering emits
`BoundsCheck`/`IndexAddress`; C creates elements plus initialization flags.
`ResetArray` clears flags every time a declaration executes, including loop
re-entry. Reading an unwritten element reports
`variable used before initialization`. Simple assignment computes the index
before the right side and checks bounds afterward, matching the interpreter.
Arrays of compound types remain outside this slice.

Structures with scalar fields or nested structures use `Aggregate`.
`FieldAddress` takes a structure address and field number; verification checks
the field and result address type. Local aggregates have one initialization
flag per scalar leaf, located through recursive field paths. `ResetAggregate`
clears these flags on declaration and block re-entry; globals start zeroed.
C/LLVM lowering supports scalar-field reads, writes, and updates at arbitrary
depth. `CopyAggregate` copies both data and leaf initialization state, including
nested subobjects and global source/destination. ToyC uses it for structure
initialization/assignment. Arrays of structures and whole-structure function
arguments remain future work.

Globals have their own type list and `GlobalAddress`/`GlobalIndexAddress`,
without occupying function-frame slots. Scalar globals and global array
elements start at zero; writes remain visible across calls. C emits `static`
objects with indexing bounds checks.

Compound element assignment and `a[i]++/--` use language IR `IndexUpdate`.
The index is computed once, then bounds are checked, the old initialized
element is read, the right side is evaluated, arithmetic is performed, and
the result is stored. Numeric narrowing preserves interpreter behavior for
`float -> int/char` and `int -> char`.

## Verification

`verify(&Module)` returns all detected errors. It checks type/slot/function/block
existence, instruction result and operand types, call signatures, boolean
conditions, terminators, and local value availability. Errors retain function,
block, instruction, and optional source location. Backends accept only verified
modules and must not silently skip unknown operations.

The verifier does not yet prove block reachability, slot initialization on all
paths, or address safety. Further work includes checked-error edges, slot
analysis, and lowering contracts for `Error` nodes. Backend results and effect
ordering must be compared with independent ToyC/ToyCP interpreter behavior.

## Lowering and C backend

`coge/examples/toyc/toyc.coge` declares the lowering function and source IR
variants. For `int main() { return 2 + 3; }`, generation produces two constants,
`Binary(Add, CheckedI32)`, and structured `Return`. It verifies CFG expansion
and compares interpreter result `5`; this does not mean the ToyC interpreter
executes Core IR.

`backend_c` binds lowering to `c_backend.rs`, producing `backend_c_gen.rs`.
Tests compile C11 and compare results. Checked helpers enforce i32 arithmetic
faults. Locals map to typed slots and addresses. Lowering includes reachable
functions, scalar `int`/`float`/`bool`/`char`, conversions `char -> int`,
`char -> float`, `int -> float`, scalar-to-bool conditions, float arithmetic
and comparisons. Arguments are evaluated into temporaries in order; parameters
occupy slots. Prototypes precede definitions, supporting recursion and forward
calls. Entry requires argument-free `int main()`. Unsupported slots/instructions
are rejected. Blocks/if/loops remain structured, with repeated loop conditions.

## LLVM backend

LLVM requires explicit target triple and data layout. The slice supports
`int main()`, reachable scalar functions/parameters, multiple CFG blocks,
constants, local slots/arrays, reads/writes, i32/f32 arithmetic, scalar
comparisons, jumps, branches, and returns. Float-to-int/char conversions retain
interpreter rounding/saturation through `llvm.fptosi.sat`/`llvm.fptoui.sat`.
Each block has separate temporary names and may reuse `ValueId`. Calls retain
argument order; parameter values are stored into slots on entry.

Scalar globals and arrays are zeroed LLVM objects. `BoundsCheck` and
`GlobalIndexAddress` check bounds before access; element addresses use
`getelementptr` without `inbounds`. Local arrays have byte initialization
flags: `ResetArray` clears them, reads check them, writes set them after storing.
Uninitialized reads produce the same error as C and the interpreter. Global
scalar-field aggregates start zeroed.

i32 addition/subtraction/multiplication use i64 intermediates followed by
range checks. Division checks zero and `INT_MIN / -1` before `sdiv`. Error
paths currently use x86_64/Linux `write`/`exit` ABI and compatible diagnostics.
Other targets/operations explicitly return `Unsupported`. Tests assemble with
`llvm-as`, verify with `opt -passes=verify`, and execute with `lli`.
