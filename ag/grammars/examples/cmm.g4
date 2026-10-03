grammar cmm;

program
    : declaration+ EOF
    ;

declarator
    :   ID
    ;

declaration
    : var_declaration
    | var_initialization
    | statement
    ;

var_declaration
    : type_specifier declarator SEMI
    ;

type_specifier
    :   INT
    ;

compound_statement
    :  LBRACE declaration* RBRACE
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
    : factor (addop factor)*
    ;

factor
    : var_usage
    | call
    | NUMBER
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
    | assign_statement SEMI
    | call SEMI
    | compound_statement
    ;

while_statement
    : WHILE LPAREN boolean_expression RPAREN statement
    ;

assign_statement
    : var_usage ASSIGN additive_expression
    ;

PLUS
    :  '+'
    ;

MINUS
    :  '-'
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

EQ
    : '=='
    ;

NEQ
    : '!='
    ;

INT
    : 'int'
    ;

WHILE
    : 'while'
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

WS
    : [ \t\r\n]+ -> channel(HIDDEN)
    ;

