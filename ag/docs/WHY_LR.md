# Why LR?

Agas is intended primarily for designing new programming languages, including
Vist. Its parser generator should make grammar decisions explicit, inspectable
and reproducible before the generated parser is run. This is the main reason
for choosing deterministic LR(k) as its central parsing model.

## The limitation of adaptive LL prediction

ANTLR uses adaptive LL(*) prediction. It can inspect as much of the remaining
input as a particular decision needs, cache prediction paths and fall back to
full-context prediction when necessary. This is substantially more capable
than a parser with a fixed LL(1) or LL(k) lookahead.

That flexibility is useful, but part of the decision process takes place while
the parser is running. A grammar author normally validates the result with a
large corpus of representative programs. When a new example exposes a slow
prediction path, an ambiguity, a semantic-predicate dependency or an
unexpected interpretation, more tests and possibly a grammar change are
needed. A finite corpus is valuable evidence, but it never proves that every
relevant input shape has already been exercised.

Agas should still use corpus tests, fuzzing and negative examples. The
difference is that a successfully constructed deterministic LR(k) table gives
a global, static result for the whole grammar: every reachable parser state
and every relevant lookahead word has already received an action. Tests then
validate language intent and semantic processing rather than discover basic
prediction requirements one example at a time.

## LR delays a decision until the common prefix has been read

Top-down parsing may have to select a production before consuming a long
common prefix. Bottom-up LR parsing shifts that prefix first and decides only
when the distinguishing suffix is available.

C declarations illustrate the difference. A declaration and a function
definition can begin with the same arbitrarily nested declarator:

```text
externalDeclaration
    : type declarator ';'
    | type declarator compoundStatement
    ;
```

An LL parser sees two alternatives before `declarator`, whose length is not
bounded by any small constant. An LR parser reads the entire declarator and
then distinguishes `;` from `{` with one token of lookahead.

The `CDeclaratorSubset` experiment in Zbik confirms this property. A unified
C-style declarator grammar is canonical LR(1) and LALR(1). Its table recognizes
the structure of variables, functions, arrays and pointers to functions
without trying to classify them in separate grammar branches too early.

This does not move all parsing into semantics. Syntax still records the exact
nesting of `*`, `()`, `[]` and parentheses. A later type-construction pass
interprets that structure and checks C constraints, such as prohibiting a
function from returning an array. Context-sensitive distinctions such as an
ordinary identifier versus a `typedef` name also remain a separate concern;
increasing `k` cannot replace a symbol table.

## LR(1) is useful, but LR(2) can be the natural answer

Even small, realistic grammars are not always LR(1). The CMinus experiment
contains two independent issues:

1. A declaration-related choice has the same first token and becomes
   deterministic with two tokens of lookahead.
2. The traditional dangling-`else` grammar is genuinely ambiguous. Increasing
   `k` cannot repair it; the grammar must be made unambiguous or supplied with
   an explicit resolution policy.

After rewriting dangling `else` with the standard closed/open-statement
construction, the tested CMinus grammar is canonical LR(2) and LALR(2). This
is an important middle case: it does not require generalized parsing, but it
does require more than LR(1).

The size reduction is substantial, not merely theoretical. After EBNF-to-BNF
conversion, the CMinus grammar has 91 productions, 49 nonterminals and 34
terminals. Its conflict-free canonical LR(2) automaton has 765 states, while
the conflict-free LALR(2) automaton has only 139 states: a reduction of about
82 percent. Zbik's experimental selective merger reaches the same 139 states
because full LALR(2) merging is safe for this grammar. The estimated packed
table size falls from 100,460 bytes to 26,452 bytes.

This is encouraging evidence for the practical Agas design. CMinus is still
small, but it has the shape of a real programming language: declarations,
functions, expressions, loops, nested conditionals and the dangling-`else`
problem. It demonstrates both parts of the intended approach at once: a small
fixed lookahead can express a useful grammar that LR(1) rejects, and LALR(k)
merging can remove most of the canonical-state cost without giving up static
determinism.

A future direct LALR(k) construction should exploit this difference instead
of first materializing the canonical automaton. For a grammar already known
to be LALR(2), Agas should be able to request LALR(2) explicitly. During an
automatic search it should try `LALR(k)` and then canonical `LR(k)` for each
successive `k`, stopping at the first conflict-free table. This preserves the
canonical fallback for grammars that are LR(k) but not LALR(k), while avoiding
its state cost when direct LALR construction succeeds.

Agas itself provides an even smaller bootstrap example. In `Ag.ag`, a parser
element may begin either with an optional field label or directly with a rule
reference:

```text
parserElement : elementLabel? parserSymbol parserSuffix?;
elementLabel  : RULE_REF ASSIGN;
parserSymbol  : RULE_REF | TOKEN_REF | STRING_LITERAL | qualifiedReference;
```

With `RULE_REF` as the next token, one-token lookahead cannot decide whether to
shift it as the start of `elementLabel` or reduce `elementLabel?` to empty and
then read it as `parserSymbol`; the following `ASSIGN` makes the decision.
The generated canonical LR(1) table therefore has shift/reduce conflicts,
whereas the canonical LR(2) table is conflict-free. This is why the bootstrap
grammar explicitly declares `parser = LALR` and `lookahead = 2`. Its direct
LALR(2) table is also conflict-free; LALR(1), like canonical LR(1), has three
conflict cells.

This is not an artificial stress grammar: it is the grammar of Agas itself.
Restricting a generator to LR(1) would therefore exclude a compact and useful
grammar whose only additional requirement is one more token of lookahead. Its
current LALR(2) automaton has 130 states instead of 206 canonical LR(2)
states. Its compressed table is modest: 31,619 bytes in the textual table DSL
and 23,576 bytes in the estimated packed representation.

Falling back immediately to GLR would permit runtime branching, while an
adaptive or effectively-unbounded LR(*)-style mechanism would replace the
small explicit bound with a more complex prediction contract. Agas instead
determines during generation that the complete grammar is deterministic for
the declared finite `k`; it does not rely on observing prediction behaviour
only while parsing a selected test corpus.

A conflict-free LR(k) table is consequently a global certificate for the
syntactic grammar: every reachable parser configuration has one action for
each relevant lookahead word. It does not prove that the language semantics
matches the author's intent, but it does prove determinism and excludes
ambiguity in that LR(k) grammar. For a newly designed language, this early and
repeatable guarantee is preferable to making generalized or adaptive parsing
the default.

The first practical target for Agas is therefore not a very large arbitrary
lookahead. It is deterministic LR(k) with a small explicit value, usually:

- `k = 1` for conventional grammars and C-style declarators;
- `k = 2` when one additional token cleanly separates realistic alternatives;
- occasionally `k = 3`, after inspecting the exact conflict and its source
  rules.

Larger values are possible in principle, but FIRST(k), FOLLOW(k), automata and
tables can grow rapidly. Agas should report their sizes and stop searching as
soon as the first conflict-free `k` is found.

## The gap between Bison and ANTLR

Bison offers mature LALR(1), IELR(1) and canonical LR(1) table construction.
It can also generate a GLR parser. GLR handles unresolved conflicts by keeping
multiple parsing branches alive and later discarding or merging them. This is
powerful, but its extra work is dynamic and depends on the input and on how
long competing interpretations survive.

Bison does not provide general deterministic LR(2), LR(3), and so on. Thus a
grammar that is cleanly LR(2) commonly faces an awkward choice:

- rewrite it until LR(1) is sufficient;
- use semantic or precedence-based conflict resolution;
- or select GLR even though a small fixed lookahead would produce a fully
  deterministic parser.

ANTLR approaches the same space from the other direction with adaptive
top-down prediction. Agas is intended to occupy the useful gap between them:
statically verified, deterministic LR(k) tables for small `k > 1`, with clear
diagnostics that map conflicts back to source grammar rules.

## Why this matters for new languages

When designing Vist or another new language, the grammar is still under the
designer's control. A conflict report can guide syntax design before ambiguous
or expensive constructs become compatibility constraints. Agas should make it
possible to ask concrete questions:

- Is this grammar LR(1), LR(2) or LR(3)?
- Which state, rules and lookahead words prevent a smaller `k`?
- Is the problem insufficient lookahead, premature grammar factoring, true
  ambiguity or context-sensitive information?
- How many states and table bytes does the chosen formulation require?
- Does a grammar rewrite preserve the bounded language and the intended AST?

This feedback is more useful for language design than merely observing that a
large test suite currently parses.

## Scope and optional extensions

Deterministic LR(k) is the primary path required by Agas. It is not a claim
that every useful language grammar is LR(k), nor that every conflict should be
removed by increasing `k`.

Zbik's planned GLR stage remains useful for deliberately ambiguous grammars or
cases where several interpretations must survive. Experimental IELR can reduce
LR(1) tables without losing the recognition power of canonical LR(1). Both are
valuable extensions, but neither is required to bootstrap Agas after the
successful LR(1) and LR(2) feasibility experiments.

The intended progression is therefore:

1. bootstrap Agas with deterministic LR(k), concentrating on `k = 1..3`;
2. use real Agas and Vist grammars to measure table size and construction time;
3. add explicit conflict-resolution mechanisms where the language definition
   requires them;
4. implement GLR and IELR later as optional capabilities justified by concrete
   grammars rather than as prerequisites for the first compiler.

In short, LR lets Agas postpone decisions until enough syntax has actually
been read, while fixed small `k` keeps the result static and predictable.
LR(2) and LR(3) cover a useful space that mainstream tools often leave between
LR(1) and fully dynamic generalized or adaptive parsing.
