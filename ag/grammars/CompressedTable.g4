grammar CompressedTable;

// Matches CompressedParseTable::dumpDsl(); no target-language actions.
// Validate the decoded parser name (SLR, LR(k), LALR(k)) in the importer.
document
    : 'compressed-table' parserName=STRING '{'
      'start-state' startState=UINT ';'
      actionRow+
      'action-state-rows' actionStateRows=indices ';'
      gotoRow+
      'goto-state-rows' gotoStateRows=indices ';'
      '}' EOF
    ;

actionRow
    : 'action-row' id=UINT '{' actionEntry* defaultEntry '}'
    ;

actionEntry
    : lookahead '=>' action ';'
    ;

// The exporter only uses reductions or errors as the default action.
defaultEntry
    : 'any' '=>' ('reduce' ruleId=UINT | 'error') ';'
    ;

action
    : 'shift' stateId=UINT
    | 'reduce' ruleId=UINT
    | 'accept'
    ;

lookahead
    : '[' lookaheadSymbol (',' lookaheadSymbol)* ']'
    ;

// Quoted "EOF" is an ordinary terminal. The literal 'EOF' below is distinct
// from ANTLR's built-in EOF token used to terminate document.
lookaheadSymbol
    : STRING
    | 'EOF'
    ;

gotoRow
    : 'goto-row' id=UINT '{' gotoEntry* '}'
    ;

gotoEntry
    : nonterminal=STRING '=>' stateId=UINT ';'
    ;

indices
    : '[' UINT (',' UINT)* ']'
    ;

UINT : '0' | [1-9] [0-9]*;

// JSON string escaping, shared with the current DSL exporter.
STRING : '"' (ESCAPE | ~["\\\u0000-\u001F])* '"';
fragment ESCAPE : '\\' (["\\/bfnrt] | 'u' HEX HEX HEX HEX);
fragment HEX : [0-9a-fA-F];

WS : [ \t\r\n]+ -> skip;
