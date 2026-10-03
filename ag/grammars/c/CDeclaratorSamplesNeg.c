/*
 * Negative samples for CDeclaratorSubset.g4.
 *
 * Every marked case is deliberately malformed syntax and should be rejected
 * by the parser. Syntactically valid C declarations which fail only type
 * constraints live in CDeclaratorSamplesSemanticNeg.c and must parse.
 */

/* Deliberately malformed declarators. */

/* expected-error: unexpected closing parenthesis in a function pointer */
int (*extra_pointer_parenthesis))(int value);

/* expected-error: duplicated comma in a parameter list */
int duplicated_parameter_comma(int left,, int right);

/* expected-error: missing parameter declaration after the comma */
int trailing_parameter_comma(int left,);

/* expected-error: unexpected closing bracket in an array declarator */
int *extra_array_bracket[8]];
