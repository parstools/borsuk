/*
 [The "BSD licence"]
 Copyright (c) 2023 Andrzej Borucki
 All rights reserved.
 Parts from C grammar Copyright (c) 2013 Sam Harwell

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:
 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
 3. The name of the author may not be used to endorse or promote products
    derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

grammar C90;

compilationUnit
    :   external_declaration* EOF
    ;

external_declaration
    :   functionDefinition
    |   varFuncDeclaration
    |   declaration
    ;

declaration
    :   varDeclaration
    |   typeDeclaration ';'
    |   typeDefinition
    |   typeWillBeDeclared
    |   ';'
    ;

storageFuncSpecifier
    :   'static'
    |   '__inline__'
    |  '__inline'
    |   'extern'
    ;

typeWillBeDeclared
    : ('struct'|'union') Identifier ';'
    ;

//if type not spedified : default return int
type
    :   '__extension__'? (storageFuncSpecifier | typeQualifier)*
        (typeName | typeQualifier | storageFuncSpecifier)
        (storageFuncSpecifier | typeQualifier)*
    ;

typeOrDecl
    : type
    | typeQualifier* typeDeclaration
    ;

functionDefinition
    :   type? functionDeclarator parametersKandRlist? compoundStatement
    ;

typeQualifier
    :   'const'
    |   '__const'
    |   volatileRule
    ;

typeDeclaration
    : structDeclaration
    | enumDeclaration
    ;

signedUnsigned
    : 'unsigned'
    | 'signed'
    ;

longShort
    :  'long'
    |  'short'
    ;

intTypeName
    : ('int'| signedUnsigned | longShort)+
    ;

charTypeName
    : signedUnsigned 'char'
    | 'char' signedUnsigned?
    ;

floatTypeName
    : 'float'
    | 'double'
    | 'long' 'double'
    | 'double' 'long'
    | '__float128'
    | '__float80'
    ;


typeName
    : intTypeName
    | charTypeName
    | floatTypeName
    | 'void'
    | ('struct'|'union') Identifier
    | 'enum' Identifier
    | '__builtin_va_list'
    | Identifier
    ;

typeModifier
    : '*'
    ;

fixedParameterOrTypeList
    : parameterOrType (',' parameterOrType)*
    ;

parameterOrTypeList
    : fixedParameterOrTypeList (',' '...')?
    ;

parametersKandRlist
    : (parametersKandR ';')+
    ;

parametersKandR
    : typeOrDecl (variableDeclarator | functionDeclarator) (',' (variableDeclarator | functionDeclarator) )*
    ;

//functionDeclarator as parameter = pointer to function
parameterOrType
    :   typeOrDecl
        (variableDeclarator | variableDeclaratorPlace | functionDeclarator | functionDeclaratorPlace)
    ;

compoundStatement
    :   '{' declaration* labeledStatement* '}'
    ;

varFuncDeclaration
    :   type varFuncList ';'
    ;

varDeclaration
    :   (type | typeDeclaration) varList ';'
    ;

varFuncList
    : commonDeclarator (',' commonDeclarator)*
    ;

varList
    : variableDeclaratorWithInit (',' variableDeclaratorWithInit)*
    ;

variableDeclaratorWithInit
    :   variableDeclarator ('=' initializer)?
    ;

commonDeclarator
    :   variableDeclaratorWithInit
    |   functionDeclarator
    ;

/*** declarator ***/
variableDeclarator
    : typeQualifier* name
    | variablePtrDeclarator
    ;

variablePtrDeclarator
    :   ptrname
    |   typeModifier* variableSubDeclarator
    ;

variableSubDeclarator
    :   '(' variablePtrDeclarator ')' functionParameters
    ;

variableDeclaratorPlace
    : typeQualifier* namePlace
    | variablePtrDeclaratorPlace
    ;

variablePtrDeclaratorPlace
    :   ptrnamePlace
    |   typeModifier* variableSubDeclaratorPlace
    ;

variableSubDeclaratorPlace
    :   '(' variablePtrDeclaratorPlace ')' functionParameters
    ;

functionDeclarator
    :   functionSubDeclarator
    |   typeModifier+ functionDeclarator
    |   '(' functionDeclarator ')' (functionParameters | array )
    ;

functionSubDeclarator
    :   '(' functionDeclarator ')'
    |   name functionParameters
    ;

functionDeclaratorPlace
    :   functionSubDeclaratorPlace
    |   typeModifier+ functionDeclaratorPlace
    |   '(' functionDeclaratorPlace ')' (functionParameters | array )
    ;

functionSubDeclaratorPlace
    :   '(' functionDeclarator ')'
    |   namePlace functionParameters
    ;

name
    :   '(' name ')'
    |   Identifier
    ;

arrname
    :   name array
    |   '(' ptrname ')' array
    ;

ptrname
    :  typeModifier (typeModifier | typeQualifier)* name
    |   (typeModifier | typeQualifier)* arrname
    |   (typeModifier | typeQualifier)* '(' ptrname ')'
    ;

namePlace
    :   '(' namePlace ')'
    |   /*empty*/
    ;

arrnamePlace
    :   namePlace array
    |   '(' ptrnamePlace ')' array
    ;

ptrnamePlace
    :   typeModifier (typeModifier | typeQualifier)* namePlace
    |   (typeModifier | typeQualifier)* arrnamePlace
    |   (typeModifier | typeQualifier)* '(' ptrnamePlace ')'
    ;
/*** end declarator ***/

initializer
    :  assignmentExpression
    |  arrayStructInitializer
    ;

fieldInitializer
    : initializer
    | '.' Identifier '=' initializer
    | Identifier ':' initializer
    ;

arrayStructInitializer
    : '{' (fieldInitializer (',' fieldInitializer)*)? '}'
    | '{' arrayCellInitializer (',' arrayCellInitializer)* '}' //for incomplete initialization
    ;

arrayCellInitializer
    : '[' conditionalExpression']' ('.' Identifier)? '=' commaExpression
    | '[' conditionalExpression '...' conditionalExpression ']' '=' commaExpression
    ;

surroundedVariableName
    : '(' surroundedVariableName ')'
    | typeModifier surroundedVariableName
    |  '(' variableName ')'
    ;

variableName
    : typeModifier variableName
    | variableName arrayOneDim
    | Identifier
    ;


fieldDeclaration //typeName can't be void without modifiers ; bitField: anonymous field
    : typeOrDecl (fieldList | bitField)? ';'
    ;

fieldList
    :   fieldDeclarator (',' fieldDeclarator)*
    ;

fieldDeclarator
    :    commonDeclarator bitField?
    ;

functionParameters
    : '(' parameterOrTypeList? ')'
    ;

array
    : arrayOneDim+
    ;

arrayOneDim // [*] can be in function prototype declaration
    : '[' typeQualifier* (conditionalExpression | '*')? ']'
    ;

bitField
    : ':' conditionalExpression
    ;

structDeclaration
        : '__extension__'? ('struct'|'union') Identifier? '{' fieldDeclarations? '}'
        ;

fieldDeclarations
        : fieldDeclaration+
        ;

labeledStatement
    : label* statement
    ;

statement
    :   compoundStatement
    |   expressionStatement
    |   loopStatement
    |   ifStatement
    |   switchStatement
    |   asmStatement
    |   'goto' Identifier ';'
    |   'return' commaExpression? ';'
    |   'continue' ';'
    |   'break' ';'
    |   ';'
    ;

label
    :   caseLabel ';'
    |   defaulLabel ';'
    ;

asmRule
    :  '__asm__' |  '__asm'
    ;

volatileRule
    :   'volatile' | '__volatile__'
    ;

asmStatement
    :   asmRule volatileRule? '(' StringLiteral ')' ';'
    ;

ifStatement
    : 'if' '(' commaExpression ')' labeledStatement ('else' labeledStatement )?
    ;

/* Relation 'case' to 'switch' is like relation contionue/break to for/while loops:
   must be inside these statement, but it can't be checked with non context gramamr,
   if we want to avoid duplicate rules and is left to be checked by semantics */
switchStatement
    : 'switch' '(' commaExpression ')' labeledStatement
    ;

caseLabel
    : 'case' conditionalExpression ('...' conditionalExpression)? ':'
    ;

defaulLabel
    : 'default' ':'
    ;


loopStatement
    :   'while' '(' commaExpression ')' labeledStatement
    |   'do' labeledStatement 'while' '(' commaExpression ')' ';'
    |   'for' '(' commaExpression? ';' commaExpression? ';' commaExpression? ')' labeledStatement
    ;

expressionStatement
    :   commaExpression ';'
    ;

commaExpression
    :   assignmentExpression (',' assignmentExpression)*
    ;

assignmentOperator
    :   '=' | '*=' | '/=' | '%=' | '+=' | '-=' | '<<=' | '>>=' | '&=' | '^=' | '|='
    ;

assignmentExpression
    :   conditionalExpression
    |   unaryExpression assignmentOperator assignmentExpression
    ;

conditionalExpression
    :   logicalOrExpression ('?' commaExpression? ':' conditionalExpression)?
    ;

logicalOrExpression
    :   logicalAndExpression ('||' logicalAndExpression)*
    ;

logicalAndExpression
    :   inclusiveOrExpression ('&&' inclusiveOrExpression)*
    ;

inclusiveOrExpression
    :   exclusiveOrExpression ('|' exclusiveOrExpression)*
    ;

exclusiveOrExpression
    :   andExpression ('^' andExpression)*
    ;

andExpression
    :   equalityExpression ('&' equalityExpression)*
    ;

eqop
    :   '==' | '!='
    ;

equalityExpression
    :   relationalExpression (eqop relationalExpression)*
    ;

relop
    : '<' | '>' | '<=' | '>='
    ;

relationalExpression
    :   shiftExpression (relop shiftExpression)*
    ;


shiftop
    :   '<<' | '>>'
    ;

shiftExpression
    :   additiveExpression (shiftop additiveExpression)*
    ;


addop
    : '+' | '-'
    ;

additiveExpression
    :   multiplicativeExpression (addop multiplicativeExpression)*
    ;


mulop
    :   '*' | '/' | '%'
    ;

multiplicativeExpression
    :   castExpression (mulop castExpression)*
    ;

castExpression
    :   unaryExpression
    |   '__extension__'? '(' typeSpecifier ')' (castExpression | arrayStructInitializer)
    ;

unaryOperator
    :   '&' | '*' | '+' | '-' | '~' | '!'
    ;

unaryExpression
    :   postfixExpression
    |   '++' castExpression
    |   '--' castExpression
    |   '&&' Identifier
    |   unaryOperator castExpression
    |   sizeofOrAlignof '(' typeSpecifier ')'
    |   sizeofOrAlignof '(' conditionalExpression ')'
    |   sizeofOrAlignof typeSpecifier
    |   sizeofOrAlignof conditionalExpression
    |   '__builtin_offsetof' '(' typeOrDecl ',' postfixExpression ')'
    |  '__builtin_va_arg' '(' postfixExpression ',' typeSpecifier ')'
    |  ('__real__'|'__imag__') unaryExpression
    ;

alignofRule
    :   '_Alignof'
    |   '__alignof__'
    ;

sizeofOrAlignof
    :   'sizeof'
    |   alignofRule
    ;

typeSpecifier
    :   typeOrDecl variableDeclaratorPlace
    ;

postfixExpressionLeft
    :   atom
    |   postfixExpressionLeft '(' argumentExpressionList? ')'
    |   postfixExpressionLeft '[' assignmentExpression ']'
    |   postfixExpressionLeft '.' Identifier
    |   postfixExpressionLeft '->' Identifier
    ;


postfixExpression
    :   primaryExpression
    |   postfixExpressionLeft
    |   postfixExpression '.' Identifier
    |   postfixExpression '->' Identifier
    |   postfixExpression '[' assignmentExpression ']'
    |   postfixExpression '++'
    |   postfixExpression '--'
    ;


argumentExpressionList
    :   assignmentExpression (',' assignmentExpression)* (',' '__extension__' '__PRETTY_FUNCTION__' )?
    ;

atom
    :   Identifier
    |   literal
    |   StringLiteral+
    ;

primaryExpression
    :   '(' commaExpression ')'
    |   '__extension__'? '(' compoundStatement ')'
    ;


literal
    :   integerConstant
    |   FloatingConstant
    |   CharacterConstant
    ;


typeDefinition
    : '__extension__'? 'typedef' typeQualifier*
        typeOrDecl commonDeclarator (',' commonDeclarator)* ';'
    ;

enumDeclaration
    : 'enum' Identifier? '{' enumList '}'
    ;

enumList
    : enumItem (',' enumItem)*
    ;

enumItem
    : Identifier ('=' inclusiveOrExpression)?
    ;

Identifier
    :   Nondigit
        (   Nondigit
        |   Digit
        )*
    ;

fragment
Digit
    :   [0-9]
    ;

fragment
NonzeroDigit
    :   [1-9]
    ;

fragment
Nondigit
    :   [a-zA-Z_]
    ;


integerConstant
    :   sign? DecimalConstant
    |   BinaryConstant
    |   OctalConstant
    |   HexadecimalConstant //to do integer suffix?
    ;

sign
    :   '+' | '-'
    ;

DecimalConstant
    :   (NonzeroDigit Digit* | '0') IntegerSuffix?
    ;

fragment
IntegerSuffix
    :   UnsignedSuffix (LongSuffix | LongLongSuffix)?
    |   (LongSuffix | LongLongSuffix) UnsignedSuffix?
    ;

fragment
UnsignedSuffix
    :   [uU]
    ;

fragment
LongSuffix
    :   [lL]
    ;

fragment
LongLongSuffix
    :   'll' | 'LL'
    ;

fragment
FloatingSuffix
    :  'f' | 'l' | 'F' | 'L'
    |  'fi'| 'fj'| 'i'  /* imag part of complex */
    ;

fragment
Sign
    :   '+' | '-'
    ;

fragment
OctalDigit
    :   [0-7]
    ;


fragment
BinaryDigit
    :   [01]
    ;


fragment
HexadecimalDigit
    :   [0-9a-fA-F]
    ;

OctalConstant
    :   '0' OctalDigit* IntegerSuffix?
    ;

BinaryConstant
    :   '0'[Bb] BinaryDigit* IntegerSuffix?
    ;

HexadecimalConstant
    :   HexadecimalPrefix HexadecimalDigitSequence IntegerSuffix?
    ;

fragment
HexadecimalPrefix
    :   '0' [xX]
    ;

FloatingConstant
    :   DecimalFloatingConstant
    |   HexadecimalFloatingConstant
    ;

fragment
DecimalFloatingConstant
    :   (FractionalConstant ExponentPart? | DigitSequence ExponentPart) FloatingSuffix?
    ;


fragment
HexadecimalDigitSequence
    :   HexadecimalDigit+
    ;

fragment
HexadecimalFractionalConstant
    :   HexadecimalDigitSequence? '.' HexadecimalDigitSequence
    |   HexadecimalDigitSequence '.'
    ;

fragment
BinaryExponentPart
    :   [pP] Sign? DigitSequence
    ;

fragment
HexadecimalFloatingConstant
    :   HexadecimalPrefix (HexadecimalFractionalConstant | HexadecimalDigitSequence) BinaryExponentPart FloatingSuffix?
    ;

fragment
DigitSequence
    :   Digit+
    ;

fragment
ExponentPart
    :   [eE] Sign? DigitSequence
    ;

fragment
FractionalConstant
    :   DigitSequence? '.' DigitSequence
    |   DigitSequence '.'
    ;


fragment
StringLiteralOne
    :   '"' SChar* '"'
    ;

fragment
StringLiteralMulti
    : '"' SChar* '\\'Newline (SChar* '\\'Newline)* SChar* '"'
    ;

StringLiteral
    :   'L'? (StringLiteralOne | StringLiteralMulti) //L"abc" - elements are ints
    ;

fragment
SChar
    :   ~["\\\r\n]
    |   EscapeSequence
    ;

CharacterConstant
    :  'L'? '\'' CChar '\''
    ;

fragment
CChar
    :   ~['\\\r\n]
    |   EscapeSequence
    ;

fragment
EscapeSequence
    :   SimpleEscapeSequence
    |   OctalEscapeSequence
    |   HexadecimalEscapeSequence
    |   UnicodeEscapeSequence
    ;


fragment
SimpleEscapeSequence
    :   '\\' ['"?abfnrtvhe\\]
    ;

fragment
OctalEscapeSequence
    :   '\\' OctalDigit (OctalDigit OctalDigit?)?
    ;

fragment
HexadecimalEscapeSequence
    :   '\\x' HexadecimalDigit HexadecimalDigit?
    ;

fragment
UnicodeEscapeSequence
    :   '\\u' HexQuad
    |   '\\U' HexQuad HexQuad
    ;

fragment
HexQuad
    :   HexadecimalDigit HexadecimalDigit HexadecimalDigit HexadecimalDigit
    ;

Whitespace
    :   [ \t]+
        -> channel(HIDDEN)
    ;

Newline
    :   (   '\r' '\n'?
        |   '\n'
        )
        -> channel(HIDDEN)
    ;

BlockComment
    :   '/*' .*? '*/'
        -> channel(HIDDEN)
    ;

Preprocessor
    :   '#' ~[\r\n]*
        -> channel(HIDDEN)
    ;
