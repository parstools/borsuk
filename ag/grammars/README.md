# Agas grammar collection

Grammars are grouped by purpose, not by source format. An Agas `.ag` grammar
and its ANTLR4 `.g4` source or counterpart belong next to each other so that
their structure, conversion and test results can be compared directly.

The directory layout is intentionally shallow:

- `Ag.ag` and `Ag.g4` define the Agas language and bootstrap path;
- `CompressedTable.ag` and `CompressedTable.g4` define the serialized parse
  table DSL;
- `examples/` contains small C-like grammars used to exercise the frontend and
  LR generator; original `.g4` files stay beside their `.ag` conversions;
- `c/` contains the related C grammar family, including historical variants,
  the declarator subset and its input samples.

Sample source files stay beside the grammar that consumes them when they are
specific to that grammar. For example, the three `CDeclaratorSamples*.c` files
belong next to `CDeclaratorSubset.ag` and `CDeclaratorSubset.g4`. A separate
corpus directory should be introduced only when samples are shared by several
grammars or become numerous enough to need their own hierarchy.

Project documentation and conversion tools remain in the parent `Borsuk`
directory because they describe or operate on more than one grammar family.
