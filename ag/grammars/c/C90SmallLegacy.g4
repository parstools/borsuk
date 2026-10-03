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
    :   externalDeclaration+ EOF
    ;

externalDeclaration
    :   functionDefinition
    |   functionDefinitionKandR
    |   functionDeclaration
    |   'extern'? variableDeclaration
    |   typeDeclaration ';'
    |   typeDefinition
    |   typeWillBeDeclared
    ;

storageFuncSpecifier
    :   'static'
    |   '__inline__'
    |   'extern'
    ;

storageVarSpecifier
    :   'register'
    ;


typeWillBeDeclared
    : ('struct'|'union') Identifier ';'
    ;

functionSpecifier
    :  '__inline'
    ;

//if typeSpecifier not spedified : default return int
functionDeclaration
    :   '__extension__'? gccDeclaratorExtension1* storageFuncSpecifier* functionSpecifier? gccDeclaratorExtension1* typeSpecifier? gccDeclaratorExtension1* Identifier function gccDeclaratorExtension2* ';'
    ;

functionDefinition
    :   '__extension__'? gccDeclaratorExtension1* storageFuncSpecifier* functionSpecifier? gccDeclaratorExtension1* typeSpecifier? gccDeclaratorExtension1* Identifier '(' parameterList ')' compoundStatement
    ;

functionDefinitionKandR
    :   typeSpecifier Identifier '(' varList? ')' (typeSpecifier Identifier ';')* compoundStatement
    ;

varList
    : variableName (',' variableName)*
    ;

typeQualifier
    :   'const'
    |   '__const'
    |   '__restrict'
    |   '__restrict__'
    |   'volatile'
    ;

typeDeclaration
    : structDeclaration
    | enumDeclaration
    ;

typeSpecifier
    :   (storageVarSpecifier | typeQualifier)* typeName typeModifier* typeQualifier*
    ;

type
    : typeName
    | typeQualifier typeName
    | typeDeclaration
    ;

typeName
    : 'int'
    | ('unsigned'|'signed')? 'long' 'int'?
    | '__extension__'? ('unsigned'|'signed')? 'long' 'long' 'int'?
    | 'long'? ('unsigned'|'signed') 'int'?
    | 'short'
    | ('unsigned'|'signed')? 'short' 'int'?
    | 'short'? ('unsigned'|'signed') 'int'?
    | ('unsigned'|'signed')? 'char'
    | 'float'
    | 'long'? 'double'
    | 'float' '_Complex'
    | 'long'? 'double' '_Complex'
    | 'void'
    | ('struct'|'union') Identifier
    | 'enum' Identifier
    | '__builtin_va_list'
    | Identifier
    ;

typeModifier
    : '*'
    ;

fixedParameterList
    : parameter (',' parameter)*
    ;

parameterList
    : fixedParameterList
    | fixedParameterList ',' '...'
    | 'void'
    | /*empty*/
    ;

fixedParameterOrTypeList
    : parameterOrType (',' parameterOrType)*
    ;

parameterOrTypeList
    : fixedParameterOrTypeList
    | fixedParameterOrTypeList ',' '...'
    ;

parameter
    : typeSpecifier variableName (array | function)? gccAttributeSpecifier?
    ;

parameterOrType
    : typeSpecifier variablePlace (array | function)? gccAttributeSpecifier?
    ;


compoundStatement
    :   '{' blockItem* '}'
    ;


blockItem
    :   variableDeclaration
    |   typeDeclaration
    |   typeDefinition
    |   statement
    |   label
    ;

label
    : Identifier ':'
    ;

variableDeclaration
    : type variableList ';' //typeName can't be void without modifiers
    ;

variableList
    : variableDeclarator (',' variableDeclarator)*
    ;

variableDeclarator
    : fieldDeclarator ( '=' assignmentExpression )?
    ;

variableName
    : '(' variableName ')'
    | typeModifier variableName
    | Identifier
    ;

variablePlace
    : '(' variablePlace ')'
    | typeModifier variablePlace
    | typeQualifier variablePlace
    | Identifier
    |
    ;


fieldDeclaration
    : '__extension__'? type fieldList? //typeName can't be void without modifiers
    | type bitField //anonymous field
    ;

fieldList
    : fieldDeclarator (',' fieldDeclarator)*
    ;

fieldDeclarator
    : typeModifier* typeQualifier* variableName (bitField | array | function)? gccAttributeSpecifier?
    ;

function
    : '(' parameterOrTypeList? ')'
    ;

array
    : arrayOneDim+
    ;

arrayOneDim
    : '[' typeQualifier* conditionalExpression? ']'
    ;

bitField
    : ':' integerConstant
    ;

fieldName
    : Identifier
    ;

structDeclaration
        : '__extension__'? ('struct'|'union') Identifier? '{' fieldDeclarations '}' gccAttributeSpecifier?
        ;

fieldDeclarations
        : fieldDeclaration (';' fieldDeclaration)* ';'?
        ;

statement
    :   compoundStatement
    |   expressionStatement
    |   loopStatement
    |   ifStatement
    |   switchStatement
    |   'goto' Identifier ';'
    |   'return' commaExpression? ';'
    |   'continue' ';'
    |   'break' ';'
    ;

ifStatement
    : 'if' '(' commaExpression ')' statement ('else' statement )?
    ;

switchStatement
    : 'switch' '(' commaExpression ')' '{' caseLabel* defaultLabel? '}'
    ;

caseLabel
    : 'case' constant ':' statement*
    ;

defaultLabel
    : 'default' ':' statement*
    ;


loopStatement
    :   'while' '(' commaExpression ')' statement
    |   'do' statement 'while' '(' commaExpression ')' ';'
    |   'for' '(' commaExpression? ';' commaExpression? ';' commaExpression? ')' statement
    ;

expressionStatement
    :   commaExpression ';'
    ;

commaExpression
    :   assignmentExpression
    |   commaExpression ',' assignmentExpression
    ;

assignmentOperator
    :   '=' | '*=' | '/=' | '%=' | '+=' | '-=' | '<<=' | '>>=' | '&=' | '^=' | '|='
    ;

assignmentExpression
    :   conditionalExpression
    |   unaryExpression assignmentOperator assignmentExpression
    ;

conditionalExpression
    :   logicalOrExpression ('?' commaExpression ':' conditionalExpression)?
    ;

logicalOrExpression
    :   logicalAndExpression
    |   logicalOrExpression '||' logicalAndExpression
    ;

logicalAndExpression
    :   inclusiveOrExpression
    |   logicalAndExpression '&&' inclusiveOrExpression
    ;

inclusiveOrExpression
    :   exclusiveOrExpression
    |   inclusiveOrExpression '|' exclusiveOrExpression
    ;

exclusiveOrExpression
    :   andExpression
    |   exclusiveOrExpression '^' andExpression
    ;

andExpression
    :   equalityExpression
    |   andExpression '&' equalityExpression
    ;

equalityExpression
    :   relationalExpression
    |   equalityExpression '==' relationalExpression
    |   equalityExpression '!=' relationalExpression
    ;

relationalExpression
    :   shiftExpression
    |   relationalExpression '<' shiftExpression
    |   relationalExpression '>' shiftExpression
    |   relationalExpression '<=' shiftExpression
    |   relationalExpression '>=' shiftExpression
    ;

shiftExpression
    :   additiveExpression
    |   shiftExpression '<<' additiveExpression
    |   shiftExpression '>>' additiveExpression
    ;

additiveExpression
    :   multiplicativeExpression
    |   additiveExpression '+' multiplicativeExpression
    |   additiveExpression '-' multiplicativeExpression
    ;

multiplicativeExpression
    :   castExpression
    |   multiplicativeExpression '*' castExpression
    |   multiplicativeExpression '/' castExpression
    |   multiplicativeExpression '%' castExpression
    ;

castExpression
    :   unaryExpression
    |   '(' typeSpecifier ')' castExpression
    ;

unaryOperator
    :   '&' | '*' | '+' | '-' | '~' | '!'
    ;

unaryExpression
    :   postfixExpression
    |   '++' unaryExpression
    |   '--' unaryExpression
    |   unaryOperator castExpression
    |   'sizeof' '(' typeSpecifier ')'
    ;


postfixExpression
    :   primaryExpression
    |   postfixExpression '[' conditionalExpression ']'
    |   postfixExpression '(' argumentExpressionList? ')'
    |   postfixExpression '.' Identifier
    |   postfixExpression '->' Identifier
    |   postfixExpression '++'
    |   postfixExpression '--'
    ;


argumentExpressionList
    :   assignmentExpression (',' assignmentExpression)* (',' '__extension__' '__PRETTY_FUNCTION__' )?
    ;


primaryExpression
    :   Identifier
    |   constant
    |   StringLiteral+
    |   '('commaExpression ')'
    ;


constant
    :   integerConstant
    |   FloatingConstant
    |   CharacterConstant
    ;


argumentList
    : assignmentExpression (',' assignmentExpression)*
    ;

typeDefinition
    : '__extension__'? 'typedef' typeQualifier* type fieldDeclarator gccAttributeSpecifier? ';'
    ;

gccDeclaratorExtension1  //__attribute__ ((visibility ("default"))) or ((__always_inline__))
    : '__attribute__' '(' '(' Identifier ')' ')'
    | '__attribute__' '(' '(' Identifier '(' integerConstant ')' ')' ')'
    | '__attribute__' '(' '(' Identifier '(' StringLiteral ')' ')' ')'
    ;

gccDeclaratorExtension2
    :   '__asm__' '(' StringLiteral+ ')'
    |   gccAttributeSpecifier
    ;

gccAttributeSpecifier
    :   '__attribute__' '(' '(' gccAttributeList ')' ')'
    ;

gccAttributeList
    :   gccAttribute (',' gccAttribute)*
//    |   // empty
    ;

gccAttribute
    : Identifier
    | 'const'
    | Identifier '(' StringLiteral ')'
    | Identifier '(' Identifier ( ',' integerConstant )* ')'
    | Identifier '(' integerConstant ( ',' integerConstant )* ')'
    ;

enumDeclaration
    : 'enum' Identifier? '{' enumList '}'
    ;

enumList
    : enumItem (',' enumItem)* ','?
    ;

enumItem
    : Identifier
    | Identifier '=' inclusiveOrExpression
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
    |   OctalConstant
    |   HexadecimalConstant //to do integer suffix?
    ;

sign
    :   '+' | '-'
    ;

DecimalConstant
    :   NonzeroDigit Digit* IntegerSuffix?
    |   '0' IntegerSuffix?
    ;

fragment
IntegerSuffix
    :   UnsignedSuffix LongSuffix?
    |   UnsignedSuffix LongLongSuffix
    |   LongSuffix UnsignedSuffix?
    |   LongLongSuffix UnsignedSuffix?
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
HexadecimalDigit
    :   [0-9a-fA-F]
    ;

OctalConstant
    :   '0' OctalDigit* IntegerSuffix?
    ;

HexadecimalConstant
    :   HexadecimalPrefix HexadecimalDigit+ IntegerSuffix?
    ;

fragment
HexadecimalPrefix
    :   '0' [xX]
    ;

FloatingConstant
    :   DecimalFloatingConstant FloatingSuffix?
    ;

fragment
DecimalFloatingConstant
    :   FractionalConstant ExponentPart? FloatingSuffix?
    |   DigitSequence ExponentPart FloatingSuffix?
    ;

fragment
DigitSequence
    :   Digit+
    ;

fragment
ExponentPart
    :   'e' Sign? DigitSequence
    |   'E' Sign? DigitSequence
    ;

fragment
FractionalConstant
    :   DigitSequence? '.' DigitSequence
    |   DigitSequence '.'
    ;


StringLiteral
    :   ('L')? '"' SChar* '"' //L"abc" - elements are ints
    ;

fragment
SChar
    :   ~["\\\r\n]
    |   EscapeSequence
    ;

CharacterConstant
    :   '\'' CChar '\''
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
    ;


fragment
SimpleEscapeSequence
    :   '\\' ['"?abfnrtv\\]
    ;

fragment
OctalEscapeSequence
    :   '\\' OctalDigit
    |   '\\' OctalDigit OctalDigit
    |   '\\' OctalDigit OctalDigit OctalDigit
    ;

fragment
HexadecimalEscapeSequence
    :   '\\x' HexadecimalDigit
    |   '\\x' HexadecimalDigit HexadecimalDigit
    ;


Whitespace
    :   [ \t]+
        -> skip
    ;


Newline
    :   (   '\r' '\n'?
        |   '\n'
        )
        -> skip
    ;

BlockComment
    :   '/*' .*? '*/'
        -> skip
    ;

Preprocessor
    :   '#' ~[\r\n]*
        -> skip
    ;
