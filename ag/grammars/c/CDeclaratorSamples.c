/*
 * Basic function declarations.
 * Every name in this section denotes a function, not an object.
 */

void fun1(void);

char *fun2(void);

int add(int left, int right);

double weighted_sum(double value, int count, double weight);

void write_message(char *message, long length);

/* A declaration and a definition with long, nearly identical prefixes. */
void declared_with_five_parameters(
        char character,
        short small_value,
        int value,
        long large_value,
        double real_value);

void defined_with_five_parameters(
        char character,
        short small_value,
        int value,
        long large_value,
        double real_value)
{
}

/*
 * A pre-ANSI/K&R declaration. Empty parentheses mean that parameter types
 * and even the number of parameters are unspecified; this is not (void).
 */
void knr_process();

/*
 * A K&R-style function definition. Names appear in the identifier list,
 * while their types are declared between the header and the function body.
 */
void knr_process_definition(
        character,
        small_value,
        value,
        large_value,
        real_value)
char character;
short small_value;
int value;
long large_value;
double real_value;
{
}

/* K&R parameters with array and pointer-to-function declarators. */
void knr_declarator_parameters(values, operation)
int values[];
int (*operation)();
{
}

/* A structure type and a function returning that structure by value. */
struct OperationResult make_operation_result(int value, int status);

/*
 * One declaration with a shared int specifier and independent declarators.
 * The pointer, array and function modifiers belong to individual names, not
 * to the shared int type specifier.
 */
int shared_value,
    *shared_pointer,
    shared_array[8],
    shared_function(int argument),
    *shared_pointer_function(int argument),
    (*shared_callback)(int argument);

/* One int object, one pointer object, and two functions in one declaration. */
int compact_value, *compact_pointer, compact_function(void), *compact_pointer_function(void);

/*
 * Several objects with the same int specifier and separate initializers.
 * Only first_initialized and second_initialized are scalar int objects;
 */
int first_initialized = 1,
    second_initialized = 2;


/*
 * Object, pointer and array declarations.
 * None of the names in this section denotes a function.
 */

int **var1;

char *message;

/* An array of 100 int objects. */
int table[100];

/* A one-dimensional array containing 20 int objects. */
int matrix[20];

/* An array of 16 pointers to int. */
int *values[16];

/*
 * Named function types.
 * These typedef names denote function types; they declare neither functions
 * nor objects on their own.
 */

typedef int BinaryOperation(int left, int right);

typedef double UnaryTransform(double value);

typedef void Visitor(char *name, int value);

/*
 * Pointers to functions, including an array of function pointers.
 * The declared names below denote objects, not functions.
 */

/* An object containing a pointer to a function returning int. */
int (*binary_operation)(int left, int right);

/* A function returning a pointer to int; this is not a function pointer. */
int *binary_operation_result(int left, int right);

/* An object containing a pointer to a function returning void. */
void (*visitor)(char *name, int value);

/* An object containing a pointer to a named function type. */
BinaryOperation *named_operation;

/* An array object whose elements are pointers to functions. */
BinaryOperation *operation_table[4];

/* The same kind of array written without a function-type typedef. */
int (*raw_operation_table[10])(int left, int right);


/*
 * Functions receiving function pointers.
 * Every declared name in this section denotes a function.
 */

int apply(
        int (*operation)(int left, int right),
        int left,
        int right);

int apply_named(
        BinaryOperation *operation,
        int left,
        int right);

void visit_value(
        Visitor *callback,
        char *name,
        int value);

/* A function-type parameter is adjusted to a pointer-to-function parameter. */
int apply_adjusted(BinaryOperation operation, int left, int right);

/* An array parameter is adjusted to a pointer-to-int parameter. */
void fill_table(int values[100], int value);

/* A multidimensional array parameter preserves all dimensions except the first. */
void fill_matrix(int values[], int rows, int value);


/*
 * Functions returning pointers to functions.
 * All declared names in this section denote functions.
 */

int (*select_operation(int kind))(int left, int right);

void (*make_visitor(char *prefix, int flags))(
        char *name,
        int value);

BinaryOperation *select_named_operation(int kind);

UnaryTransform *select_transform(int mode, double scale);

/* A function returning a pointer to an array of 100 int objects. */
int (*select_table(int kind))[100];


/*
 * Pointers to functions which themselves return pointers to functions.
 * operation_factory and visitor_factory denote pointer objects, not functions.
 */

BinaryOperation *(*operation_factory)(int kind);

Visitor *(*visitor_factory)(char *prefix, int flags);


/*
 * Additional prototypes, without function bodies.
 * Every declared name in this section denotes a function.
 */

int call_samples(int left, int right);

void visitor_call_samples(char *name, int value);

BinaryOperation *return_existing_operation(void);

int (*return_existing_operation_raw(void))(int left, int right);


/*
 * Invalid in C: a function cannot return a function type directly.
 * These forms are useful as negative semantic tests, but stay commented out
 * so that this file remains a valid C translation unit:
 *
 *     BinaryOperation invalid_selector(int kind);
 *     int invalid_selector_raw(int kind)(int left, int right);
 *
 * The valid form returns a pointer to the function type:
 *
 *     BinaryOperation *valid_selector(int kind);
 */
