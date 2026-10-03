# Contracts for action calls

[English](ACTION_CONTRACTS.md) | [Polski](ACTION_CONTRACTS.pl.md)

Scope: `toyc.sema.txt` and three `toyscope_*.sema.txt` design sketches. Of 93
call names, 40 construct types or IR. The other 53 include 47 direct-call and
eight method names, with contains_local and lookup_lexical appearing in both
forms. These are logical contracts, not a request for 53 manual runtime functions.

**A** is an authored Action function; **I** a primitive or collection operation;
**G** a generated execution dispatcher; **E** a definition already sketched.
Result<T> is a value or sourced error; Option<T> is absence without error.
Scope/Symbol/Place/Flow/Type/Expr/Stmt/Frame/Cell are logical types. Expr and Stmt
describe IR, not program values. Flow maps SymbolId or place parts to initialization
state. Analysis never reads runtime Cell values.

The proposed split needs 41 new authored function names, two existing sketches
(apply_integer/direct_declarations), nine primitive names and generated evaluate.
This counts names, not overloads or all required lower-level primitives.
The scope_codegen example implements contains_local, lookup_lexical, contains_name,
unique_names, bind, declare_variable, declare_function, declare_incomplete_struct,
lookup_tag_lexical and bind_parameters. It has int objects and string-form function
signatures; full types/places, structure completion and richer diagnostics remain
broader design requirements. Initialization is separate from Symbol in copyable
Flow, passed by explicit FlowId. merge_flow is now authored using map primitives;
see [SEMA_FORMAT.md](SEMA_FORMAT.md) for current branch integration.

## Names, scopes and declarations

| Name | Contract | Kind |
|---|---|---|
| contains_local | (scope,name,namespace=ordinary) → Bool; current scope only. Method form uses the default namespace. | A |
| lookup_lexical | (scope,name,namespace=ordinary) → Option<Symbol>; nearest enclosing binding, including uninitialized symbols. Method form uses default namespace. | A |
| lookup_tag_lexical | (scope,name) → Option<StructTag>; lexical lookup in the tag namespace. | A |
| contains_name | (parameters,name) → Bool; compare collected parameter names. | A |
| unique_names | (parameters) → Bool; detect duplicates without modifying the list. | A |
| bind | scope.bind(name,symbol) → Unit; insert into local map/scope.symbols, rejecting duplicates in that namespace. | A |
| declare_variable | (scope,id,type,target) → Result<Symbol>; fresh SymbolId/place or field, local conflict check; uninitialized local, zero global without initializer. | A |
| declare_function | (scope,header,kind) → Result<FunctionSymbol>; merge compatible prototypes, reject signature mismatch/second definition. | A |
| declare_incomplete_struct | (scope,name) → Result<StructTag>; reserve tag with complete=false before fields. | A |
| bind_parameters | (scope,parameters,state) → Unit; bind parameters and initial state in function scope. | A |
| lookup_field | (struct_type,field_name) → Option<Field>; search only the selected complete structure. | A |
| layout_struct | (fields) → Result<Layout>; selected ABI offsets/alignment/size, requiring complete field types. | A |
| complete | (type) → Bool; whether definition is complete. | A |
| complete_object_type | (type) → Bool; non-void object with complete element/field types. | A |

declare_* may use fresh_symbol_id(), absent from the initial call list.
layout_struct needs ABI scalar sizes/alignment. These are compiler-model
operations without filesystem access.

## Initialization flow and program control

| Name | Contract | Kind |
|---|---|---|
| copy | (flow) → Flow; independent branch snapshot, including persistent copy-on-write implementations. | I |
| parameter_flow | (scope) → Flow; Initialized parameters at function entry. | A |
| hide_locals | (flow,scope) → Flow; remove states owned by the exited scope. | A |
| merge_flow | (a,b) → Flow; ignore unreachable paths; retain Initialized only when both reachable paths guarantee it. | A |
| mark_initialized | (flow,place) → Unit; update exactly the written field/place, or entire structure after full write. | A |
| read | (place,flow) → Result<Expr>; reject Uninitialized/Initializing in analysis, otherwise build SymbolId-bound read IR; never read Cell. | A |
| always_returns | (stmt) → Bool; all reachable paths return; may use bottom-up rewriting instead of helper recursion. | A |
| contains_error | (module_or_program) → Bool; diagnostics/ErrorOperation markers. | A |
| direct_declarations | (items) → List<DeclarationNode>; only direct scope declarations, without descending into blocks; ToyScope3 query. | E |
| remove_all | flow.remove_all(symbols) → Unit; remove current-branch states. | A |

read and mark_initialized are analysis operations. ToyScope3 instead checks
uninitialized reads at runtime with read_initialized; these are different contracts.

## Types and IR construction

| Name | Contract | Kind |
|---|---|---|
| integer | (type) → Bool; ToyC integer classification. | A |
| numeric | (type) → Bool; numeric operations, including float. | A |
| assignable | (place) → Bool; writable lvalue, excluding arrays/nonmodifiable objects. | A |
| promote_numeric | (expr) → Result<Expr>; explicit IR promotion/result type. | A |
| convert | (expr,target_type) → Result<Expr>; permitted ToyC conversion recorded in IR, without evaluating. | A |
| convert_arguments | (arguments,parameters) → Result<List<Expr>>; count/order check and pairwise conversion. | A |
| comparable | (left,right,operator) → Result<ComparisonPlan>; common type and operand conversions. | A |
| type_binary | (operator,left,right) → Result<Expr>; type checks, promotions, IR and runtime checks such as divisor checks. | A |
| compound_value | (operator,old,rhs,target_type) → Result<Expr>; compound-assignment typing through type_binary/convert. | A |
| to_bool | (expr) → Result<Expr>; condition-to-bool IR. | A |
| parse_integer | (text,radix) → Result<MathematicalInteger>; locale-independent parsing, with ToyC int range checked separately. | I |
| parse_float | (text) → Result<Float>; locale-independent format/range checks. | I |
| decode_char | (literal_text) → Result<Char>; escapes and exactly one character. | I |
| decode_string | (literal_text) → Result<Text>; string escapes. | I |

complete and always_returns may traverse nested trees using bottom-up rewrite
or a finite node collection without helper-function cycles.

## ToyScope cell execution

| Name | Contract | Kind |
|---|---|---|
| create_frame | (scope,symbols,initial_state,parent) → Frame; cell per SymbolId, scope instance and current frame; initial state is policy-defined. | A |
| release_frame | (frame) → Unit; remove active instance and restore parent, also after errors. | A |
| lexical_frame | runtime.lexical_frame(scope_id) → Option<Frame>; active instance of the lexical scope. | A |
| place_of | runtime.place_of(symbol_id) → Result<Cell>; SymbolId/scope-frame binding, never name-string lookup. | A |
| leave_uninitialized | (cell) → Unit; Uninitialized without a value. | A |
| initialize | (cell,value) → Unit; initializer write, allowing Reserved in policy 3 and setting Initialized after success. | A |
| read_initialized | (cell) → Result<Value>; read only Initialized, otherwise sourced runtime error. | A |
| write | (cell,value) → Result<Unit>; permitted ordinary write then Initialized; policy 3 distinguishes initialize. | A |
| execute_sequence | (items) → Result<Unit>; ordered IR execution through generated dispatch, stop on error. | A |
| evaluate | (expr) → Result<Value>; generated execution_contract/execution result dispatcher, recursively traversing expression data. | G |
| apply_integer | (operator,a[,b]) → Result<MathematicalInteger>; arithmetic including division/remainder zero checks; ToyScope overload sketches. | E |
| truncate_toward_zero | (quotient) → MathematicalInteger; truncation, requiring explicit negative-division semantics; div_trunc(a,b) may be a safer primitive. | I |

The helper call-graph cycle prohibition does not prohibit a generated IR
dispatcher from recursively traversing data. Execute only programs valid under
the selected analysis policy.

## Collections and structure operations

| Name | Contract | Kind |
|---|---|---|
| append | list.append(value) → Unit; append in source order. | I |
| remove | list.remove(value) → Bool; remove by identity/structural equality specified by the element type. | I |
| length | (list) → Int; element count without analyzing child semantics. | I |

Primitives operate on containers, IDs, cells, scalars and literal text, without
file reads. Actions can update model fields and traverse finite collections;
the compiler must type/check effects. Semantic A helpers form an acyclic graph:
alternative action → higher helper → lower helper → intrinsic/constructor/G
dispatcher. The historical checker detected named function/query cycles but
not all signatures, overloads and effects.

## Generated Rust ownership model

SemaContext owns Vec tables for scopes/symbols/expressions and diagnostics.
Relationships use Copy ScopeId/SymbolId/ExprId/NodeId. Functions receive context
and IDs by value, returning values/IDs rather than internal references;
lookup_lexical returns Option<SymbolId>. Separate reads from writes and finish
short borrows before calling another mutable-context operation. Branch Flow is
owned (clone/persistent map), merged explicitly; indexed frames need reliable
cleanup on errors. Emit only resolved types/effects, with declared runtime
intrinsic; compile and behavior-check the output.

Illustrative interface, not literal generator output:

```rust
trait SemaOps {
    fn lookup_lexical(
        ctx: &SemaContext,
        start: ScopeId,
        name: NameId,
    ) -> Option<SymbolId>;

    fn declare_variable(
        ctx: &mut SemaContext,
        scope: ScopeId,
        name: NameId,
        ty: TypeId,
    ) -> Result<SymbolId, Diagnostic>;
}
```

Do not default to Rc<RefCell<_>>, which hides aliasing and defers checks to runtime.
Shared ownership should follow a particular type's contract. The Rust book
explains [borrowing](https://doc.rust-lang.org/book/ch04-02-references-and-borrowing.html)
and [interior mutability](https://doc.rust-lang.org/book/ch15-05-interior-mutability.html).
