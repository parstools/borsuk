grammar cminus;

program
    : declaration+ EOF
    ;

declaration
    : var_declaration
    | fun_declaration
    ;

declarator
    :   ID
    ;

var_declaration
    : type_specifier declarator SEMI
    ;

fun_declaration
    : (type_specifier | VOID) ID LPAREN formal_parameters? RPAREN compound_statement
    ;

type_specifier
    :   INT
    |   STRING
    ;

formal_parameters
    : formal_parameter ( COMMA formal_parameter )*
    ;

formal_parameter
    :   type_specifier declarator
    ;

compound_statement
    :  LBRACE (local_declaration | statement)* RBRACE
    ;

local_declaration
    : var_declaration
    | var_initialization
    ;

var_initialization
    : type_specifier declarator ASSIGN additive_expression SEMI
    ;

relop: LE | LT | GT | GE | EQ | NEQ;

boolean_expression
    : additive_expression relop additive_expression
	;

addop: PLUS | MINUS;

additive_expression
    : term (addop term)*
    ;

mulop: MUL | DIV;

term
    : factor (mulop factor)*
    ;

factor
    : LPAREN additive_expression RPAREN
    | var_usage
    | call
    | NUMBER
    | STRING_LITERAL
    | MINUS factor
    ;

var_usage
    : ID
    ;

call
    : ID LPAREN arg_list? RPAREN
    ;

arg_list
    : argument (COMMA argument)*
    ;

argument
    : additive_expression
    ;

statement
    : while_statement
    | for_statement
    | if_statement
    | assign_statement SEMI
    | call SEMI
    | compound_statement
    | return_statement
    ;

while_statement
    : WHILE LPAREN boolean_expression RPAREN statement
    ;

for_statement
    : FOR LPAREN var_initialization boolean_expression SEMI assign_statement RPAREN statement
    ;

if_statement
    : IF LPAREN boolean_expression RPAREN statement
	| IF LPAREN boolean_expression RPAREN statement ELSE statement
	;

assignop: ASSIGN | ASSIGN_PLUS | ASSIGN_MINUS | ASSIGN_MUL | ASSIGN_DIV;

assign_statement
    : var_usage assignop additive_expression
    | var_usage INC
    | var_usage DEC
    ;

return_statement
    : RETURN additive_expression? SEMI
    ;

PLUS
    :  '+'
    ;

MINUS
    :  '-'
    ;

MUL
    :  '*'
    ;

DIV
    :  '/'
    ;

COMMA
    : ','
    ;

LPAREN
    : '('
    ;

RPAREN
    : ')'
    ;

LBRACE
    : '{'
    ;

RBRACE
    : '}'
    ;

SEMI
    : ';'
    ;

LT
    : '<'
    ;

LE
    : '<='
    ;

GT
    : '>'
    ;

GE
    : '>='
    ;

ASSIGN
    : '='
    ;

ASSIGN_PLUS
    : '+='
    ;

ASSIGN_MINUS
    : '-='
    ;

ASSIGN_MUL
    : '*='
    ;

ASSIGN_DIV
    : '/='
    ;

INC
    : '++'
    ;

DEC
    : '--'
    ;

EQ
    : '=='
    ;

NEQ
    : '!='
    ;

INT
    : 'int'
    ;

STRING
    : 'string'
    ;

VOID
    : 'void'
    ;

IF
    : 'if'
    ;

ELSE
    : 'else'
    ;

WHILE
    : 'while'
    ;

FOR
    : 'for'
    ;

RETURN
    : 'return'
    ;


BLOCK_COMMENT
    : '/*' .*? '*/' -> channel(HIDDEN)
    ;

LINE_COMMENT
    : '//' ~[\r\n]*    -> channel(HIDDEN)
    ;

ID
    : [a-zA-Z_][a-zA-Z_0-9]*
    ;

NUMBER
    : '0' | [1-9][0-9]*
    ;

STRING_LITERAL
    : '"' (~["\\\r\n] | EscapeSequence)* '"'
    ;

fragment EscapeSequence
    : '\\' [btnfr"'\\]
    ;

WS
    : [ \t\r\n]+ -> channel(HIDDEN)
    ;

