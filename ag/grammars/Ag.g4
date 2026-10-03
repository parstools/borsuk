grammar Ag;

/*
 * Bootstrap parser for the Agas grammar-description language.
 *
 * Deliberate parser-rule restriction:
 *   - alternatives may occur only at the top level of a named rule;
 *   - ?, * and + may modify only one named symbol or one literal;
 *   - anonymous parser groups (...) do not exist.
 *
 * A complex fragment therefore has to become a named rule.  This gives every
 * generated parser target stable rule/alternative/element coordinates and
 * keeps the generated AST shallow and intentional.
 *
 * Lexer rules are different: they describe regular languages and may use
 * nested (...) groups.  Lexer groups never create nodes in the program AST.
 *
 * Proposed AST convention for an Agas backend:
 *   - every parser rule explicitly chooses `node` or `inline`;
 *   - an `inline` rule is a parsing helper and creates no AST node;
 *   - only labelled elements (field=Symbol) become named AST fields;
 *   - unlabelled punctuation is recognized but need not enter the AST;
 *   - ?, * and + determine optional/scalar/vector field cardinality;
 *   - #Alternative labels name AST variants of `node` rules;
 *   - `inline` alternatives must not have #Alternative labels;
 *   - a multi-alternative `node` rule labels either every alternative or none.
 *
 * The last two conditions are semantic validation rules.  The bootstrap
 * grammar accepts the general shape so that the Agas frontend can produce a
 * precise validation diagnostic instead of a generic syntax error.
 */

document
    : grammarDeclaration topLevelItem* EOF
    ;

grammarDeclaration
    : GRAMMAR TOKEN_REF SEMI
    ;

topLevelItem
    : optionsBlock
    | channelsBlock
    | lexerClassesBlock
    | conflictsBlock
    | parserRuleSpec
    | lexerRuleSpec
    ;

optionsBlock
    : OPTIONS LBRACE optionEntry* RBRACE
    ;

optionEntry
    : identifier ASSIGN optionValue SEMI
    ;

optionValue
    : identifier
    | INTEGER
    | STRING_LITERAL
    | TRUE
    | FALSE
    ;

lexerClassesBlock
    : LEXER_CLASSES LBRACE optionEntry* RBRACE
    ;

channelsBlock
    : CHANNELS LBRACE channelList? RBRACE
    ;

channelList
    : TOKEN_REF channelTail* COMMA?
    ;

channelTail
    : COMMA TOKEN_REF
    ;

conflictsBlock
    : CONFLICTS LBRACE conflictEntry* RBRACE
    ;

conflictEntry
    : PREFER SHIFT TOKEN_REF OVER REDUCE RULE_REF HASH TOKEN_REF SEMI
    | PREFER REDUCE RULE_REF HASH TOKEN_REF OVER SHIFT TOKEN_REF SEMI
    ;

// --------------------------------------------------------------------------
// Parser rules: deliberately restricted EBNF.

parserRuleSpec
    : treeModifier RULE_REF lexerCommands? COLON parserAlternative parserAlternativeTail* SEMI
    ;

treeModifier
    : NODE
    | INLINE
    ;

parserAlternativeTail
    : OR parserAlternative
    ;

parserAlternative
    : EMPTY alternativeLabel?
    | parserElement+ alternativeLabel?
    ;

alternativeLabel
    : HASH TOKEN_REF
    ;

parserElement
    : elementLabel? parserSymbol parserSuffix?
    ;

elementLabel
    : RULE_REF ASSIGN
    ;

parserSymbol
    : RULE_REF
    | TOKEN_REF
    | STRING_LITERAL
    | qualifiedReference
    ;

qualifiedReference
    : TOKEN_REF DOT identifier
    ;

parserSuffix
    : QUESTION
    | STAR
    | PLUS
    ;

// --------------------------------------------------------------------------
// Lexer rules: regular expressions may contain nested groups.

lexerRuleSpec
    : FRAGMENT? TOKEN_REF COLON lexerAltList lexerCommands? SEMI
    ;

lexerAltList
    : lexerAlternative lexerAlternativeTail*
    ;

lexerAlternativeTail
    : OR lexerAlternative
    ;

lexerAlternative
    : EMPTY
    | lexerElement+
    ;

lexerElement
    : lexerAtom lexerSuffix?
    ;

lexerAtom
    : TOKEN_REF
    | STRING_LITERAL
    | LEXER_CHAR_SET
    | DOT
    | NOT lexerNegatable
    | lexerGroup
    ;

lexerNegatable
    : STRING_LITERAL
    | LEXER_CHAR_SET
    ;

lexerGroup
    : LPAREN lexerAltList RPAREN
    ;

lexerSuffix
    : QUESTION
    | STAR QUESTION?
    | PLUS QUESTION?
    ;

lexerCommands
    : RARROW lexerCommand lexerCommandTail*
    ;

lexerCommandTail
    : COMMA lexerCommand
    ;

lexerCommand
    : identifier lexerCommandArgument?
    ;

lexerCommandArgument
    : LPAREN identifier RPAREN
    ;

identifier
    : RULE_REF
    | TOKEN_REF
    ;

// --------------------------------------------------------------------------
// Meta-lexer.

GRAMMAR  : 'grammar';
OPTIONS  : 'options';
CHANNELS : 'channels';
LEXER_CLASSES : 'lexerClasses';
CONFLICTS: 'conflicts';
PREFER   : 'prefer';
SHIFT    : 'shift';
OVER     : 'over';
REDUCE   : 'reduce';
NODE     : 'node';
INLINE   : 'inline';
FRAGMENT : 'fragment';
EMPTY    : 'empty';
TRUE     : 'true';
FALSE    : 'false';

STRING_LITERAL
    : '\'' (Escape | ~['\\\r\n])* '\''
    ;

LEXER_CHAR_SET
    : '[' LexerSetCharacter* ']'
    ;

RULE_REF
    : LowerLetter IdentifierCharacter*
    ;

TOKEN_REF
    : UpperLetter IdentifierCharacter*
    ;

INTEGER
    : [0-9]+
    ;

fragment LexerSetCharacter
    : Escape
    | ~[\]\\\r\n]
    ;

fragment Escape
    : '\\' .
    ;

fragment LowerLetter
    : [a-z]
    ;

fragment UpperLetter
    : [A-Z]
    ;

fragment IdentifierCharacter
    : [a-zA-Z_0-9]
    ;

OR       : '|';
QUESTION : '?';
STAR     : '*';
PLUS     : '+';
NOT      : '~';
LPAREN   : '(';
RPAREN   : ')';
SEMI     : ';';
COLON    : ':';
RARROW   : '->';
COMMA    : ',';
LBRACE   : '{';
RBRACE   : '}';
ASSIGN   : '=';
DOT      : '.';
HASH     : '#';

DOC_COMMENT
    : '/**' .*? ('*/' | EOF) -> skip
    ;

BLOCK_COMMENT
    : '/*' .*? ('*/' | EOF) -> skip
    ;

LINE_COMMENT
    : '//' ~[\r\n]* -> skip
    ;

WS
    : [ \t\r\n\f]+ -> skip
    ;
