grammar CDeclaratorSubset;

compilationUnit
    : externalDeclaration* EOF
    ;

externalDeclaration
    : functionDefinition
    | declaration
    ;

declaration
    : initDeclaration
    | typeDefinition
    | ';'
    ;

functionDefinition
    : type declarator parametersKandRList? compoundStatement
    ;

initDeclaration
    : type initDeclaratorList ';'
    ;

type
    : 'int'
    | 'char'
    | 'double'
    | 'void'
    | 'struct' Identifier
    | Identifier
    ;

initDeclaratorList
    : initDeclarator (',' initDeclarator)*
    ;

initDeclarator
    : declarator ('=' initializer)?
    ;

/*
 * A single syntactic declarator represents objects, functions, arrays and
 * pointers to them. Type construction and C constraints decide which kind a
 * declaration denotes; the parser does not choose a separate branch early.
 */
declarator
    : pointer? directDeclarator
    ;

pointer
    : '*'+
    ;

directDeclarator
    : Identifier
    | '(' declarator ')'
    | directDeclarator functionParameters
    | directDeclarator array
    ;

functionParameters
    : '(' parameterOrTypeList? ')'
    ;

parameterOrTypeList
    : parameterOrType (',' parameterOrType)*
    ;

/* Identifier may denote a typedef name; that distinction is semantic. */
parameterOrType
    : type declarator?
    ;

/* K&R names are accepted as type-only Identifier entries at this layer. */
parametersKandRList
    : parametersKandR+
    ;

parametersKandR
    : type initDeclaratorList ';'
    ;

compoundStatement
    : '{' '}'
    ;

array
    : '[' atom? ']'
    ;

initializer
    : atom
    ;

atom
    : Identifier
    | integerConstant
    ;

integerConstant
    : DecimalConstant
    ;

typeDefinition
    : 'typedef' type initDeclaratorList ';'
    ;

Identifier
    : Nondigit (Nondigit | Digit)*
    ;

fragment Digit
    : [0-9]
    ;

fragment NonzeroDigit
    : [1-9]
    ;

fragment Nondigit
    : [a-zA-Z_]
    ;

DecimalConstant
    : NonzeroDigit Digit* | '0'
    ;

Whitespace
    : [ \t]+ -> channel(HIDDEN)
    ;

Newline
    : ('\r' '\n'? | '\n') -> channel(HIDDEN)
    ;

BlockComment
    : '/*' .*? '*/' -> channel(HIDDEN)
    ;
