# Agas: restricted EBNF and AST construction

[English](AG_FORMAT.md) | [Polski](AG_FORMAT.pl.md)

Agas describes grammars for LR parsers in multiple target languages. Its
restricted EBNF gives important fragments stable names, identifies symbols
within productions, supports rule/RuleId/symbol-position diagnostics, avoids
technical AST clutter, and keeps public tree APIs independent of LR state IDs.

## Format purpose

Grammar rules describe recognition and tree construction separately.
Production identities drive LR reductions; field labels select captured data;
optional alternative labels select public AST variants.

## Parser table options

`options` selects table construction and lookahead. `parser = LR` requests
canonical LR(k); `LALR` requests direct LALR(k) construction over LR(0) cores.
`lookahead` must be a positive integer. The generator never increases k,
switches algorithms, or silently resolves conflicts. Conflicting tables can
be inspected but cannot be exported as deterministic parser artifacts.

The first parser rule is the start rule. Rules not reachable from it produce
warnings, even if they refer to one another. `warningsAsErrors = true;`
promotes grammar-validation warnings to errors; by default warnings do not
block generation. Unused lexer tokens are not covered by this warning because
they may still be needed for tokenization.


```text
options {
    parser = LALR;
    lookahead = 2;
}
```

## Explicit conflict resolution

`conflicts` selects an exact shift/reduce pair and a chosen action. Names
after `#` are parser-alternative labels. Only one policy may apply to an ACTION
cell. A declaration must match precisely that shift and reduction; unused or
overlapping declarations are errors. For k > 1, a terminal such as `ELSE` is
the first symbol of a full lookahead word, so a policy can resolve several
words. Other conflicts remain unresolved.

For dangling else, `shift` binds else to the nearest if, whereas `reduce` may
bind it to an outer if. These are explicit semantics for an ambiguous grammar,
not a proof of unambiguity. `agas --table` reports resolved-conflict counts;
packages retain the chosen table and decisions in `diagnostics.json`.


```ag
conflicts {
    prefer shift ELSE over reduce ifStatement#IfStatement;
    // Alternatively: prefer reduce ifStatement#IfStatement over shift ELSE;
}
```

## Restricted parser EBNF

Alternation `|` appears only at the top level of a named rule. `?`, `*`, and
`+` apply to exactly one symbol or literal. Anonymous parser groups `(...)`
are forbidden; use explicit `empty` for an empty production and named helpers
for compound optional/repeated fragments. The first example below is invalid;
the following example is its named-rule equivalent.

The restriction applies to parser rules. Lexer regexes may contain nested
groups, which do not construct AST objects.


```text
node function
    : TYPE IDENT LPAREN (parameter (COMMA parameter)*)? RPAREN block
    ;
```

```text
node function
    : type=TYPE name=IDENT parameters=parameterBlock body=block
    ;

inline parameterBlock
    : LPAREN parameters=parameterList? RPAREN
    ;

node parameterList
    : first=parameter rest=additionalParameter*
    ;

inline additionalParameter
    : COMMA value=parameter
    ;
```

## Lexer contract

Agas owns lexer-rule frontend semantics: fragment expansion, quantifiers,
priorities, channels, and diagnostics. NFA/DFA construction, alphabet
equivalence classes, and minimization are shared through Zbik's public API.
The production lexer does not depend on ANTLR runtime.

### Token selection

At each input position, consider every non-fragment rule. The longest match
wins; source rule order breaks ties. No match is a lexical error identifying
the token start and the first code point where matching cannot continue.
This is maximal munch with rule-order priority. Fragments emit no tokens and
do not compete independently; they are expanded named regexes. Direct or
indirect fragment recursion is an error.

### Greedy and lazy quantifiers

`?`, `*`, and `+` are greedy by default; `*?` and `+?` are lazy. A lazy
quantifier chooses the smallest repetition count permitting the remainder of
its rule to match. Following ANTLR semantics, after a lazy subrule, later
decisions take the first matching alternative rather than maximizing length
again: `.*? ('a' | 'ab')` ends with `'a'`. Multiple lazy decisions resolve
left to right. Only afterward do whole-rule matches compete by length/order.

The block-comment example ends at the first valid `*/`; removing laziness
would use the farthest possible terminator. A plain DFA representing only the
regular language is therefore insufficient: path priorities or a verified
equivalent transformation must preserve matching semantics.


```text
BLOCK_COMMENT : '/*' .*? '*/' -> channel(HIDDEN);
```

### Unicode and source positions

The alphabet consists of Unicode scalar values; input is UTF-8. Dot,
literals, classes, ranges, and negation operate on code points, not bytes.
Invalid UTF-8 is an error, not several independent characters.

Character classes and DFA transitions use canonical disjoint intervals,
avoiding a dense Unicode table. ASCII-only grammars retain roughly byte-lexer
class counts. Zbik provides UTF-8/interval processing while ByteLexer remains
useful for binary data and tests.

Supported design requirements include ordinary escapes, `\xNN`, `\uNNNN`,
`\UNNNNNNNN`, and identifier properties `XID_Start`/`XID_Continue`. Zbik
currently expands these from ICU and exposes the Unicode version. Pin and
record that version before freezing artifacts; executing an exported automaton
should not require a Unicode library.

Token spans are half-open UTF-8 byte ranges. Diagnostics may also hold line
numbers and code-point columns. IDE adapters convert to UTF-16 indices where
needed. Never use an unqualified offset for these different units.

### Channels and commands

`skip` has no argument and removes the token while preserving progress and
positions. `channel(NAME)` emits it on a declared named channel. No command
means the default channel. Parsers consume only default-channel tokens; full
lexer output retains named-channel tokens for comments, formatting, and IDEs.

A rule cannot combine skip/channel, repeat commands, put commands on a
fragment, give arguments to skip, omit the single declared channel argument,
or use unknown commands. Skipped/named-channel tokens cannot be parser
terminals. ANTLR `more`, `type`, `mode`, `pushMode`, and `popMode` are outside
the initial format; importers must report losses instead of ignoring them.

## Production identity

Every alternative of every rule has its own RuleId, stable within the built
grammar. ACTION cells select reductions by RuleId, so shared prefixes do not
require alternative names. Tree construction additionally needs production
metadata, reduction instructions, and the AST schema. Internal functions such
as `reduce_rule_31` need no public `#` label.


```text
node primary
    : value=IDENT
    | value=NUMBER
    ;
```

```text
RuleId 31: primary -> IDENT
RuleId 32: primary -> NUMBER
```

```text
tablica ACTION/GOTO
+ sekwencja wykonanych redukcji
+ RuleId of each reduction
+ metadane node/inline
+ etykiety field=Symbol
+ opcjonalne nazwy #Alternatyw
```

## Node rules

`node` describes a named AST node, subject to the transparent reductions
below. Unlabeled punctuation is recognized but does not become an AST field.
Labels choose stored data. `value=X` holds one X; `X?` is optional; `X*` is a
possibly empty list; `X+` is a nonempty list.


```text
node function
    : type=TYPE name=IDENT parameters=parameterBlock body=block
    ;
```

```text
name=IDENT
body=block
arguments=argument*
resultType=type?
```

### Automatic expression forwarding

An alternative containing exactly one required labeled rule reference and
only additional labeled `?`/`*` references can forward the required child when
all tails are absent. Otherwise it constructs its node with all fields.
`node alias : value=other;` always forwards. A present optional containing an
empty node is still present. `+`, a token, an unlabeled symbol, or a second
required operand prevents this simplification; a return statement retains
its identity.

Exactly an unlabeled single-character opening parenthesis, a required labeled
rule reference, and a closing parenthesis are also recognized as grouping.
Detection uses lexer definitions rather than token names and supports parser
literals. Parentheses add no tree node: `2+3`, `2+((((3))))`, and `((2+3))`
have the same AST structure but different positions. `(2+3)*4` still differs
from `2+3*4`. This generator policy adds no new `.ag` syntax and does not cover
brackets, braces, or commas; explicit per-production AST/grouping/postfix
policies remain future work.

Forwarding preserves sourceSpan; recognizedSpan includes the entire matched
syntax. In `(a+b) << c`, the sum payload spans `a+b` while recognition spans
`(a+b)`. Repeated grouping does not increase AST depth. Production metadata
and coverage reduction traces remain complete. Variant names identify nodes
actually constructed; promotion does not create a wrapper just to retain a
variant label.


```text
node additive : first=multiplicative rest=additiveTail*;
```

## Inline rules

`inline` participates in LR construction and has productions/RuleIds but
creates no independent AST node. With no field labels it returns unit; with
one it forwards that value; with several it creates a technical record.
The outer label binds the entire result: record fields are not injected into
the parent and cannot accidentally collide with parent fields.

Result shape is determined statically, independently of whether an optional
field is present in a particular input. Different alternative shapes form
an explicit neutral sum; target generators cannot invent implicit conversions.
Inline alternatives cannot have `#` labels. In the examples below, unlabeled
inline alternatives are valid; labeled ones are invalid.


```text
inline additionalParameter
    : COMMA value=parameter
    ;
```

```text
inline optionValue
    : value=identifier
    | value=INTEGER
    | value=STRING_LITERAL
    ;
```

```text
inline optionValue
    : value=identifier     #IdentifierOption
    | value=INTEGER        #IntegerOption
    ;
```

## Choosing node or inline

A rule modifier is mandatory; omitting it is a syntax error. Use node for
a fragment requiring identity, a source span, or later compiler visits. Use
inline for named scaffolding needed to avoid nested EBNF. Typical nodes are
declarations, function definitions, statements, expressions, types, parameters,
structure fields, and initializers. Typical inline rules are tails, optional
fragments, operator choices, delegations, and extracted groups.

A one-element node parameter list can still forward under the promotion rule.
Delegating statement rules usually inline their independently meaningful child
nodes, avoiding an extra Statement wrapper. Choose by the intended AST API,
not rule length or alternative count.


```text
node parameterList
    : first=parameter rest=additionalParameter*
    ;

inline additionalParameter
    : COMMA value=parameter
    ;
```

```text
inline statement
    : value=ifStatement
    | value=whileStatement
    | value=returnStatement
    ;
```

## Fields, tokens, and cardinalities

An unlabeled symbol affects parsing and the reduction span but creates no
field and does not implicitly forward a value. Label significant terminals;
token values retain kind, text, and half-open UTF-8 byte range.

The neutral cardinalities are one X, Optional<X>, List<X>, and NonEmptyList<X>.
Nested options/lists are not flattened. Missing values, present empty lists,
and unit differ. `node : empty` constructs a fieldless node; `inline : empty`
returns unit. Repetition of nullable symbols with `*`/`+` is invalid because
it permits infinitely many empty repetitions. Repetition preserves input
order without reversing elements or copying the whole prefix per append.

Unnamed node alternatives expose a merged field set. A field absent in an
alternative is distinct from an empty list or Optional::None within a field.
Different symbols/cardinalities under one field name produce a type sum.
Named alternatives have separate field schemas; full variant identity includes
rule and alternative names.

The schema distinguishes unit, token, named-rule reference, node, technical
record, Optional, List, NonEmptyList, and Choice for conditional promotion.
Type sums are ordered distinct types, independent of target-language syntax.
Each alternative retains source index, variant name, result, and fields;
merged fields record absence separately from optionality. Inline alternatives
form sums of unit, forwarded type, or record. Named references describe
recursive schemas without infinite expansion.

A promoted node's result refers to the child rule. Conditional promotion
has both node and child types in choice/resultTypes. Captures remain described
in alternative metadata, but cannot be read as fields of a forwarded child.

## Parser-value spans

Each stack value separates semantic payload from recognized span. Inline
forwarding preserves child payload/span, while the reduction spans its entire
RHS including unlabeled delimiters; parents use that reduction span.

Spans are half-open UTF-8 byte ranges. Empty reductions are points at the first
unconsumed default-channel token or EOF. Positions come from the parser stack
and token stream, not the lexer's advanced lookahead cursor. Production-definition
spans in `.ag` metadata are separate. If any component is nonempty, point spans
of empty helpers do not extend a production to the next token; an entirely
empty production remains a point.

## Neutral reduction program

Each BNF production has one RuleId-indexed instruction: unit/forward RHS,
construct node/record, OptionalSome/OptionalNone, empty/singleton list, or
append. Operands address actual RHS positions after EBNF expansion. Span
policy selects RHS range or the lookahead point for empty productions. Execution
needs no SyntaxDocument, diagnostic production names, or target-language code.

Validate instruction count, RuleId ordering, RHS indices, operand arity, type
names, and span policies against the grammar before running. Artifact/program
errors differ from input syntax errors. Artifact integers have explicit widths,
not platform-dependent size_t. GeneratedParserTable stores both AST schema and
program: runtime executes instructions; schema validates artifacts and supports
static types/IDEs.

AST execution keeps LR state and neutral-value stacks. Shift pushes a token;
reduce pops exactly the RHS length, runs its RuleId instruction, and applies
GOTO. Accept requires one root and all default-channel tokens consumed. ACTION
uses the full configured-k lookahead word including EOF near input end.
Named channels stay in lexer output only. Empty reductions use the next default
token's start or full input length, including trailing skipped text.

The Agas adapter reconstructs SyntaxDocument, enums, lists, options, and
line/column positions from Ag.ag's neutral tree. Other languages can use
their own adapters or the neutral tree directly. The self-hosted API unifies
successful syntax models with lexical/syntax/adapter errors. Syntax errors
include token span, LR state, received lookahead, and expected words. At k > 1
an error may precede an LL parser's location because invalid continuation is
visible before consuming the whole lookahead.

The first rule's final structural EOF is not a BNF symbol, cannot have a field
label, and represents acceptance rather than a reduction-stack value.
Node/inline selection changes AST assembly only, not recognition, productions,
RuleIds, or the LR machine.

## Alternative names

A # name neither resolves grammar ambiguity nor removes LR conflicts; it
names a public node variant. Without labels, literal alternatives share one
Literal node type and may retain RuleId/index internally. With labels, a
target can expose separate variants. A node rule must name every alternative
or none. The final mixed-label example below is invalid.


```text
node literal
    : value=NUMBER
    | value=STRING
    ;
```

```text
node literal
    : value=NUMBER #NumberLiteral
    | value=STRING #StringLiteral
    ;
```

```cpp
using Literal = std::variant<NumberLiteral, StringLiteral>;
```

```text
node literal
    : value=NUMBER #NumberLiteral
    | value=STRING
    ;
```

## Shared first symbols

Shared prefixes do not require labels: LR uses states, lookahead, and RuleId.
The if/else example still illustrates dangling else when ordinary base
statements are added; RuleIds do not resolve that conflict. It requires an
unambiguous grammar, an explicit policy, or a different parsing strategy such
as GLR. Labels are AST metadata only. Identical productions with different
labels still cause reduce/reduce conflicts; labels are neither priorities nor
selection predicates.


```text
node statement
    : IF condition THEN statement
    | IF condition THEN statement ELSE statement
    ;
```

```text
node statement
    : IF condition THEN statement                #IfStatement
    | IF condition THEN statement ELSE statement #IfElseStatement
    ;
```

```text
node value
    : IDENT #FirstValue
    | IDENT #SecondValue
    ;
```

## Preprocessor and .agi files

The proposed small text preprocessor belongs to source loading, outside Agas
grammar syntax. It composes files and selects configured variants, without C
macros or substitutions inside rules. Frontends receive one expanded text;
directives do not enter Ag.g4/Ag.ag, RuleIds, grammar models, or ASTs.

Main files use .ag; include fragments use .agi, omit the grammar declaration,
and should contain complete top-level items. The initial directive set is
#include with a quoted relative .agi path, #ifdef SYMBOL, #else, and #endif.
The caller/configuration supplies symbols. Conditionals can nest but have at
most one else; each file balances its own conditionals. Inactive includes are
not read. Paths resolve relative to the including file. Include cycles are
errors with the complete chain; repeated includes are not silently suppressed.
Duplicate rules are handled by normal grammar validation.

#define, #undef, #if, #elif, function macros, substitution, arithmetic, and
angle-bracket includes are intentionally excluded from the initial design.


```text
grammar C;

#include "declarations.agi"
#include "expressions.agi"
#include "statements.agi"
```

```text
#include "relative/path.agi"
#ifdef SYMBOL
#else
#endif
```

```text
#ifdef C90
node iterationStatement
    : WHILE LPAREN expression RPAREN statement
    ;
#else
node iterationStatement
    : WHILE LPAREN expression RPAREN statement
    | FOR LPAREN forDeclaration expression? SEMI expression? RPAREN statement
    ;
#endif
```

### Directives versus alternative labels

A # that is the first non-space/tab character on a physical line starts a
directive; after a production symbol it starts an alternative label. Therefore
labels must be on the same physical line as the end of their alternative.
The first example below is valid; the second puts a label on a directive line
and is invalid. Preprocessing removes directive lines before tokenization;
remaining HASH tokens are alternative labels. Expanded lines need source-file
and line mappings so diagnostics point to the correct .ag/.agi file.


```text
#include "expressions.agi"

node literal
    : value=NUMBER #NumberLiteral
    | value=STRING #StringLiteral
    ;
```

```text
node literal
    : value=NUMBER
      #NumberLiteral
    ;
```

## Validation rules

Check that inline rules have no alternative names; node labels are all-or-none
and unique per rule; fields are unique per alternative unless accumulation
is explicitly introduced; quantifiers apply to individual symbols; empty
productions use empty; identical productions retain separate RuleIds and are
reported as potential conflict sources; and labels never affect FIRST/FOLLOW,
LR states, or ACTION/GOTO.

The bootstrap may accept labels syntactically even where semantics later reject
them, allowing a precise diagnostic rather than a generic HASH syntax error.


```text
inline rule `optionValue` cannot name its alternative `IntegerOption`
```

## Identity, fields, and variants

RuleId always identifies an LR production. `field=Symbol` selects captured
data. `#Alternative` optionally names a public node variant. These mechanisms
have independent responsibilities: field labels shape tree contents, while
alternative names are useful only when separate named variants are desired.

## Parser-rule-scoped lexer classes

Declare at most 64 independent class bits in source order; true/false sets
initial activity. A rule without require is always active. Multiple requirements
must all hold. Longest match and source priority apply among active rules.

Parser headers can enable/disable classes. Context is inherited through calls
and EBNF helpers; nested declarations override selected bits, and callers'
context resumes on return. These are not runtime push/pop actions.

Generation specializes reachable rule/mask pairs rather than the entire power
set, with a current safety limit of 4096 reachable contexts. Lexer plans include
LR state and known lookahead prefix, supporting k > 1. Conflicting requirements
or a required terminal without an active lexer rule are errors. LALR merging
can introduce context contradictions without ACTION conflicts; inspect LR then.

C++ --table and --diagnose-parse support this path; diagnostics print tokens
and masks. --emit-package exports context, AST reductions, and coverage;
C++/Rust run packages, and --parse-package PACKAGE FILE prints JSON AST.
See [LEXER_CONTEXT.md](LEXER_CONTEXT.md). Bare DSL/static Rust export for classes
is blocked because the full context plan is required. Unconditional skip/channel
work; combining them with require is unsupported.


```ag
lexerClasses {
    SHIFT = true;
    WORDS = false;
}

node typeName -> disable(SHIFT)
    : IDENT arguments?
    ;
inline arguments : LT typeName GT;
inline expression -> enable(SHIFT) : IDENT SHR IDENT;
inline keyword -> enable(WORDS) : READ | WRITE;

SHR : '>>' -> require(SHIFT);
GT : '>';
LT : '<';
READ : 'read' -> require(WORDS);
WRITE : 'write' -> require(WORDS);
IDENT : [a-z]+;
```
