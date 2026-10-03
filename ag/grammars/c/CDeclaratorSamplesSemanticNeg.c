/*
 * Samples which violate C type constraints.
 *
 * The deliberately constrained CDeclaratorSubset.g4 may reject structurally
 * impossible type combinations during parsing, even when a full C grammar
 * would leave them to constraint checking. Constructs which still parse,
 * such as an unknown typedef name, require later semantic validation.
 */

/* Functions cannot return function types. */

/* expected-subset-error: function returning a function */
int invalid_function_result(int kind)(int value);

/* expected-semantic-error: function returning a unknown named function type */
BinaryOperation named_function_result(int kind);


/* Functions and arrays cannot be combined into these object types. */

/* expected-subset-error: function returning an array */
int invalid_array_result(void)[4];

/* expected-subset-error: array whose elements are functions */
int invalid_function_array[4](int value);

/* expected-subset-error: function initialized like an object */
int invalid_initialized_function(int value) = 0;
