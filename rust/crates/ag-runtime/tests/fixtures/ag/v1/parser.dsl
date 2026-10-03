compressed-table "LALR(2)" {
  start-state 0;

  action-row 0 {
    ["GRAMMAR", "TOKEN_REF"] => shift 1;
    any => error;
  }
  action-row 1 {
    ["TOKEN_REF", "SEMI"] => shift 4;
    any => error;
  }
  action-row 2 {
    [EOF] => accept;
    any => error;
  }
  action-row 3 {
    any => reduce 67;
  }
  action-row 4 {
    ["SEMI", "OPTIONS"] => shift 6;
    ["SEMI", "CHANNELS"] => shift 6;
    ["SEMI", "LEXER_CLASSES"] => shift 6;
    ["SEMI", "CONFLICTS"] => shift 6;
    ["SEMI", "NODE"] => shift 6;
    ["SEMI", "INLINE"] => shift 6;
    ["SEMI", "FRAGMENT"] => shift 6;
    ["SEMI", "TOKEN_REF"] => shift 6;
    ["SEMI", EOF] => shift 6;
    any => error;
  }
  action-row 5 {
    ["OPTIONS", "LBRACE"] => shift 7;
    ["CHANNELS", "LBRACE"] => shift 8;
    ["LEXER_CLASSES", "LBRACE"] => shift 9;
    ["CONFLICTS", "LBRACE"] => shift 10;
    ["NODE", "RULE_REF"] => shift 11;
    ["INLINE", "RULE_REF"] => shift 12;
    ["FRAGMENT", "TOKEN_REF"] => shift 13;
    [EOF] => reduce 0;
    any => reduce 95;
  }
  action-row 6 {
    any => reduce 1;
  }
  action-row 7 {
    ["LBRACE", "RULE_REF"] => shift 23;
    ["LBRACE", "TOKEN_REF"] => shift 23;
    ["LBRACE", "RBRACE"] => shift 23;
    any => error;
  }
  action-row 8 {
    ["LBRACE", "TOKEN_REF"] => shift 24;
    ["LBRACE", "RBRACE"] => shift 24;
    any => error;
  }
  action-row 9 {
    ["LBRACE", "RULE_REF"] => shift 25;
    ["LBRACE", "TOKEN_REF"] => shift 25;
    ["LBRACE", "RBRACE"] => shift 25;
    any => error;
  }
  action-row 10 {
    ["LBRACE", "PREFER"] => shift 26;
    ["LBRACE", "RBRACE"] => shift 26;
    any => error;
  }
  action-row 11 {
    any => reduce 23;
  }
  action-row 12 {
    any => reduce 24;
  }
  action-row 13 {
    any => reduce 94;
  }
  action-row 14 {
    any => reduce 66;
  }
  action-row 15 {
    any => reduce 2;
  }
  action-row 16 {
    any => reduce 4;
  }
  action-row 17 {
    any => reduce 3;
  }
  action-row 18 {
    any => reduce 5;
  }
  action-row 19 {
    any => reduce 6;
  }
  action-row 20 {
    ["RULE_REF", "COLON"] => shift 27;
    ["RULE_REF", "RARROW"] => shift 27;
    any => error;
  }
  action-row 21 {
    any => reduce 7;
  }
  action-row 22 {
    ["TOKEN_REF", "COLON"] => shift 28;
    any => error;
  }
  action-row 23 {
    any => reduce 69;
  }
  action-row 24 {
    ["TOKEN_REF", "COMMA"] => shift 30;
    ["TOKEN_REF", "RBRACE"] => shift 30;
    any => reduce 73;
  }
  action-row 25 {
    any => reduce 71;
  }
  action-row 26 {
    any => reduce 79;
  }
  action-row 27 {
    ["RARROW", "RULE_REF"] => shift 35;
    ["RARROW", "TOKEN_REF"] => shift 35;
    any => reduce 81;
  }
  action-row 28 {
    ["COLON", "EMPTY"] => shift 38;
    ["COLON", "STRING_LITERAL"] => shift 38;
    ["COLON", "LEXER_CHAR_SET"] => shift 38;
    ["COLON", "TOKEN_REF"] => shift 38;
    ["COLON", "NOT"] => shift 38;
    ["COLON", "LPAREN"] => shift 38;
    ["COLON", "DOT"] => shift 38;
    any => error;
  }
  action-row 29 {
    ["RULE_REF", "ASSIGN"] => shift 39;
    ["TOKEN_REF", "ASSIGN"] => shift 40;
    ["RBRACE", "OPTIONS"] => shift 41;
    ["RBRACE", "CHANNELS"] => shift 41;
    ["RBRACE", "LEXER_CLASSES"] => shift 41;
    ["RBRACE", "CONFLICTS"] => shift 41;
    ["RBRACE", "NODE"] => shift 41;
    ["RBRACE", "INLINE"] => shift 41;
    ["RBRACE", "FRAGMENT"] => shift 41;
    ["RBRACE", "TOKEN_REF"] => shift 41;
    ["RBRACE", EOF] => shift 41;
    any => error;
  }
  action-row 30 {
    any => reduce 75;
  }
  action-row 31 {
    any => reduce 72;
  }
  action-row 32 {
    ["RBRACE", "OPTIONS"] => shift 45;
    ["RBRACE", "CHANNELS"] => shift 45;
    ["RBRACE", "LEXER_CLASSES"] => shift 45;
    ["RBRACE", "CONFLICTS"] => shift 45;
    ["RBRACE", "NODE"] => shift 45;
    ["RBRACE", "INLINE"] => shift 45;
    ["RBRACE", "FRAGMENT"] => shift 45;
    ["RBRACE", "TOKEN_REF"] => shift 45;
    ["RBRACE", EOF] => shift 45;
    any => error;
  }
  action-row 33 {
    ["RULE_REF", "ASSIGN"] => shift 39;
    ["TOKEN_REF", "ASSIGN"] => shift 40;
    ["RBRACE", "OPTIONS"] => shift 46;
    ["RBRACE", "CHANNELS"] => shift 46;
    ["RBRACE", "LEXER_CLASSES"] => shift 46;
    ["RBRACE", "CONFLICTS"] => shift 46;
    ["RBRACE", "NODE"] => shift 46;
    ["RBRACE", "INLINE"] => shift 46;
    ["RBRACE", "FRAGMENT"] => shift 46;
    ["RBRACE", "TOKEN_REF"] => shift 46;
    ["RBRACE", EOF] => shift 46;
    any => error;
  }
  action-row 34 {
    ["PREFER", "SHIFT"] => shift 48;
    ["PREFER", "REDUCE"] => shift 48;
    ["RBRACE", "OPTIONS"] => shift 49;
    ["RBRACE", "CHANNELS"] => shift 49;
    ["RBRACE", "LEXER_CLASSES"] => shift 49;
    ["RBRACE", "CONFLICTS"] => shift 49;
    ["RBRACE", "NODE"] => shift 49;
    ["RBRACE", "INLINE"] => shift 49;
    ["RBRACE", "FRAGMENT"] => shift 49;
    ["RBRACE", "TOKEN_REF"] => shift 49;
    ["RBRACE", EOF] => shift 49;
    any => error;
  }
  action-row 35 {
    ["RULE_REF", "LPAREN"] => shift 39;
    ["RULE_REF", "SEMI"] => shift 39;
    ["RULE_REF", "COLON"] => shift 39;
    ["RULE_REF", "COMMA"] => shift 39;
    ["TOKEN_REF", "LPAREN"] => shift 40;
    ["TOKEN_REF", "SEMI"] => shift 40;
    ["TOKEN_REF", "COLON"] => shift 40;
    ["TOKEN_REF", "COMMA"] => shift 40;
    any => error;
  }
  action-row 36 {
    any => reduce 80;
  }
  action-row 37 {
    ["COLON", "EMPTY"] => shift 53;
    ["COLON", "STRING_LITERAL"] => shift 53;
    ["COLON", "RULE_REF"] => shift 53;
    ["COLON", "TOKEN_REF"] => shift 53;
    any => error;
  }
  action-row 38 {
    ["EMPTY", "OR"] => shift 54;
    ["EMPTY", "SEMI"] => shift 54;
    ["EMPTY", "RARROW"] => shift 54;
    ["STRING_LITERAL", "STRING_LITERAL"] => shift 55;
    ["STRING_LITERAL", "LEXER_CHAR_SET"] => shift 55;
    ["STRING_LITERAL", "TOKEN_REF"] => shift 55;
    ["STRING_LITERAL", "OR"] => shift 55;
    ["STRING_LITERAL", "QUESTION"] => shift 55;
    ["STRING_LITERAL", "STAR"] => shift 55;
    ["STRING_LITERAL", "PLUS"] => shift 55;
    ["STRING_LITERAL", "NOT"] => shift 55;
    ["STRING_LITERAL", "LPAREN"] => shift 55;
    ["STRING_LITERAL", "SEMI"] => shift 55;
    ["STRING_LITERAL", "RARROW"] => shift 55;
    ["STRING_LITERAL", "DOT"] => shift 55;
    ["LEXER_CHAR_SET", "STRING_LITERAL"] => shift 56;
    ["LEXER_CHAR_SET", "LEXER_CHAR_SET"] => shift 56;
    ["LEXER_CHAR_SET", "TOKEN_REF"] => shift 56;
    ["LEXER_CHAR_SET", "OR"] => shift 56;
    ["LEXER_CHAR_SET", "QUESTION"] => shift 56;
    ["LEXER_CHAR_SET", "STAR"] => shift 56;
    ["LEXER_CHAR_SET", "PLUS"] => shift 56;
    ["LEXER_CHAR_SET", "NOT"] => shift 56;
    ["LEXER_CHAR_SET", "LPAREN"] => shift 56;
    ["LEXER_CHAR_SET", "SEMI"] => shift 56;
    ["LEXER_CHAR_SET", "RARROW"] => shift 56;
    ["LEXER_CHAR_SET", "DOT"] => shift 56;
    ["TOKEN_REF", "STRING_LITERAL"] => shift 57;
    ["TOKEN_REF", "LEXER_CHAR_SET"] => shift 57;
    ["TOKEN_REF", "TOKEN_REF"] => shift 57;
    ["TOKEN_REF", "OR"] => shift 57;
    ["TOKEN_REF", "QUESTION"] => shift 57;
    ["TOKEN_REF", "STAR"] => shift 57;
    ["TOKEN_REF", "PLUS"] => shift 57;
    ["TOKEN_REF", "NOT"] => shift 57;
    ["TOKEN_REF", "LPAREN"] => shift 57;
    ["TOKEN_REF", "SEMI"] => shift 57;
    ["TOKEN_REF", "RARROW"] => shift 57;
    ["TOKEN_REF", "DOT"] => shift 57;
    ["NOT", "STRING_LITERAL"] => shift 58;
    ["NOT", "LEXER_CHAR_SET"] => shift 58;
    ["LPAREN", "EMPTY"] => shift 59;
    ["LPAREN", "STRING_LITERAL"] => shift 59;
    ["LPAREN", "LEXER_CHAR_SET"] => shift 59;
    ["LPAREN", "TOKEN_REF"] => shift 59;
    ["LPAREN", "NOT"] => shift 59;
    ["LPAREN", "LPAREN"] => shift 59;
    ["LPAREN", "DOT"] => shift 59;
    ["DOT", "STRING_LITERAL"] => shift 60;
    ["DOT", "LEXER_CHAR_SET"] => shift 60;
    ["DOT", "TOKEN_REF"] => shift 60;
    ["DOT", "OR"] => shift 60;
    ["DOT", "QUESTION"] => shift 60;
    ["DOT", "STAR"] => shift 60;
    ["DOT", "PLUS"] => shift 60;
    ["DOT", "NOT"] => shift 60;
    ["DOT", "LPAREN"] => shift 60;
    ["DOT", "SEMI"] => shift 60;
    ["DOT", "RARROW"] => shift 60;
    ["DOT", "DOT"] => shift 60;
    any => error;
  }
  action-row 39 {
    any => reduce 64;
  }
  action-row 40 {
    any => reduce 65;
  }
  action-row 41 {
    any => reduce 8;
  }
  action-row 42 {
    any => reduce 68;
  }
  action-row 43 {
    ["ASSIGN", "TRUE"] => shift 68;
    ["ASSIGN", "FALSE"] => shift 68;
    ["ASSIGN", "STRING_LITERAL"] => shift 68;
    ["ASSIGN", "RULE_REF"] => shift 68;
    ["ASSIGN", "TOKEN_REF"] => shift 68;
    ["ASSIGN", "INTEGER"] => shift 68;
    any => error;
  }
  action-row 44 {
    ["COMMA", "TOKEN_REF"] => shift 69;
    ["COMMA", "RBRACE"] => shift 69;
    any => reduce 77;
  }
  action-row 45 {
    any => reduce 16;
  }
  action-row 46 {
    any => reduce 15;
  }
  action-row 47 {
    any => reduce 70;
  }
  action-row 48 {
    ["SHIFT", "TOKEN_REF"] => shift 72;
    ["REDUCE", "RULE_REF"] => shift 73;
    any => error;
  }
  action-row 49 {
    any => reduce 19;
  }
  action-row 50 {
    any => reduce 78;
  }
  action-row 51 {
    any => reduce 109;
  }
  action-row 52 {
    ["LPAREN", "RULE_REF"] => shift 75;
    ["LPAREN", "TOKEN_REF"] => shift 75;
    any => reduce 111;
  }
  action-row 53 {
    ["EMPTY", "OR"] => shift 78;
    ["EMPTY", "SEMI"] => shift 78;
    ["EMPTY", "HASH"] => shift 78;
    ["RULE_REF", "ASSIGN"] => shift 79;
    any => reduce 91;
  }
  action-row 54 {
    any => reduce 42;
  }
  action-row 55 {
    any => reduce 46;
  }
  action-row 56 {
    any => reduce 47;
  }
  action-row 57 {
    any => reduce 45;
  }
  action-row 58 {
    ["STRING_LITERAL", "STRING_LITERAL"] => shift 85;
    ["STRING_LITERAL", "LEXER_CHAR_SET"] => shift 85;
    ["STRING_LITERAL", "TOKEN_REF"] => shift 85;
    ["STRING_LITERAL", "OR"] => shift 85;
    ["STRING_LITERAL", "QUESTION"] => shift 85;
    ["STRING_LITERAL", "STAR"] => shift 85;
    ["STRING_LITERAL", "PLUS"] => shift 85;
    ["STRING_LITERAL", "NOT"] => shift 85;
    ["STRING_LITERAL", "LPAREN"] => shift 85;
    ["STRING_LITERAL", "RPAREN"] => shift 85;
    ["STRING_LITERAL", "SEMI"] => shift 85;
    ["STRING_LITERAL", "RARROW"] => shift 85;
    ["STRING_LITERAL", "DOT"] => shift 85;
    ["LEXER_CHAR_SET", "STRING_LITERAL"] => shift 86;
    ["LEXER_CHAR_SET", "LEXER_CHAR_SET"] => shift 86;
    ["LEXER_CHAR_SET", "TOKEN_REF"] => shift 86;
    ["LEXER_CHAR_SET", "OR"] => shift 86;
    ["LEXER_CHAR_SET", "QUESTION"] => shift 86;
    ["LEXER_CHAR_SET", "STAR"] => shift 86;
    ["LEXER_CHAR_SET", "PLUS"] => shift 86;
    ["LEXER_CHAR_SET", "NOT"] => shift 86;
    ["LEXER_CHAR_SET", "LPAREN"] => shift 86;
    ["LEXER_CHAR_SET", "RPAREN"] => shift 86;
    ["LEXER_CHAR_SET", "SEMI"] => shift 86;
    ["LEXER_CHAR_SET", "RARROW"] => shift 86;
    ["LEXER_CHAR_SET", "DOT"] => shift 86;
    any => error;
  }
  action-row 59 {
    ["EMPTY", "OR"] => shift 54;
    ["EMPTY", "RPAREN"] => shift 54;
    ["STRING_LITERAL", "STRING_LITERAL"] => shift 55;
    ["STRING_LITERAL", "LEXER_CHAR_SET"] => shift 55;
    ["STRING_LITERAL", "TOKEN_REF"] => shift 55;
    ["STRING_LITERAL", "OR"] => shift 55;
    ["STRING_LITERAL", "QUESTION"] => shift 55;
    ["STRING_LITERAL", "STAR"] => shift 55;
    ["STRING_LITERAL", "PLUS"] => shift 55;
    ["STRING_LITERAL", "NOT"] => shift 55;
    ["STRING_LITERAL", "LPAREN"] => shift 55;
    ["STRING_LITERAL", "RPAREN"] => shift 55;
    ["STRING_LITERAL", "DOT"] => shift 55;
    ["LEXER_CHAR_SET", "STRING_LITERAL"] => shift 56;
    ["LEXER_CHAR_SET", "LEXER_CHAR_SET"] => shift 56;
    ["LEXER_CHAR_SET", "TOKEN_REF"] => shift 56;
    ["LEXER_CHAR_SET", "OR"] => shift 56;
    ["LEXER_CHAR_SET", "QUESTION"] => shift 56;
    ["LEXER_CHAR_SET", "STAR"] => shift 56;
    ["LEXER_CHAR_SET", "PLUS"] => shift 56;
    ["LEXER_CHAR_SET", "NOT"] => shift 56;
    ["LEXER_CHAR_SET", "LPAREN"] => shift 56;
    ["LEXER_CHAR_SET", "RPAREN"] => shift 56;
    ["LEXER_CHAR_SET", "DOT"] => shift 56;
    ["TOKEN_REF", "STRING_LITERAL"] => shift 57;
    ["TOKEN_REF", "LEXER_CHAR_SET"] => shift 57;
    ["TOKEN_REF", "TOKEN_REF"] => shift 57;
    ["TOKEN_REF", "OR"] => shift 57;
    ["TOKEN_REF", "QUESTION"] => shift 57;
    ["TOKEN_REF", "STAR"] => shift 57;
    ["TOKEN_REF", "PLUS"] => shift 57;
    ["TOKEN_REF", "NOT"] => shift 57;
    ["TOKEN_REF", "LPAREN"] => shift 57;
    ["TOKEN_REF", "RPAREN"] => shift 57;
    ["TOKEN_REF", "DOT"] => shift 57;
    ["NOT", "STRING_LITERAL"] => shift 58;
    ["NOT", "LEXER_CHAR_SET"] => shift 58;
    ["LPAREN", "EMPTY"] => shift 59;
    ["LPAREN", "STRING_LITERAL"] => shift 59;
    ["LPAREN", "LEXER_CHAR_SET"] => shift 59;
    ["LPAREN", "TOKEN_REF"] => shift 59;
    ["LPAREN", "NOT"] => shift 59;
    ["LPAREN", "LPAREN"] => shift 59;
    ["LPAREN", "DOT"] => shift 59;
    ["DOT", "STRING_LITERAL"] => shift 60;
    ["DOT", "LEXER_CHAR_SET"] => shift 60;
    ["DOT", "TOKEN_REF"] => shift 60;
    ["DOT", "OR"] => shift 60;
    ["DOT", "QUESTION"] => shift 60;
    ["DOT", "STAR"] => shift 60;
    ["DOT", "PLUS"] => shift 60;
    ["DOT", "NOT"] => shift 60;
    ["DOT", "LPAREN"] => shift 60;
    ["DOT", "RPAREN"] => shift 60;
    ["DOT", "DOT"] => shift 60;
    any => error;
  }
  action-row 60 {
    any => reduce 48;
  }
  action-row 61 {
    ["RARROW", "RULE_REF"] => shift 35;
    ["RARROW", "TOKEN_REF"] => shift 35;
    any => reduce 97;
  }
  action-row 62 {
    any => reduce 99;
  }
  action-row 63 {
    any => reduce 101;
  }
  action-row 64 {
    ["QUESTION", "STRING_LITERAL"] => shift 92;
    ["QUESTION", "LEXER_CHAR_SET"] => shift 92;
    ["QUESTION", "TOKEN_REF"] => shift 92;
    ["QUESTION", "OR"] => shift 92;
    ["QUESTION", "NOT"] => shift 92;
    ["QUESTION", "LPAREN"] => shift 92;
    ["QUESTION", "RPAREN"] => shift 92;
    ["QUESTION", "SEMI"] => shift 92;
    ["QUESTION", "RARROW"] => shift 92;
    ["QUESTION", "DOT"] => shift 92;
    ["STAR", "STRING_LITERAL"] => shift 93;
    ["STAR", "LEXER_CHAR_SET"] => shift 93;
    ["STAR", "TOKEN_REF"] => shift 93;
    ["STAR", "OR"] => shift 93;
    ["STAR", "QUESTION"] => shift 93;
    ["STAR", "NOT"] => shift 93;
    ["STAR", "LPAREN"] => shift 93;
    ["STAR", "RPAREN"] => shift 93;
    ["STAR", "SEMI"] => shift 93;
    ["STAR", "RARROW"] => shift 93;
    ["STAR", "DOT"] => shift 93;
    ["PLUS", "STRING_LITERAL"] => shift 94;
    ["PLUS", "LEXER_CHAR_SET"] => shift 94;
    ["PLUS", "TOKEN_REF"] => shift 94;
    ["PLUS", "OR"] => shift 94;
    ["PLUS", "QUESTION"] => shift 94;
    ["PLUS", "NOT"] => shift 94;
    ["PLUS", "LPAREN"] => shift 94;
    ["PLUS", "RPAREN"] => shift 94;
    ["PLUS", "SEMI"] => shift 94;
    ["PLUS", "RARROW"] => shift 94;
    ["PLUS", "DOT"] => shift 94;
    any => reduce 103;
  }
  action-row 65 {
    any => reduce 49;
  }
  action-row 66 {
    any => reduce 50;
  }
  action-row 67 {
    ["STRING_LITERAL", "STRING_LITERAL"] => shift 55;
    ["STRING_LITERAL", "LEXER_CHAR_SET"] => shift 55;
    ["STRING_LITERAL", "TOKEN_REF"] => shift 55;
    ["STRING_LITERAL", "OR"] => shift 55;
    ["STRING_LITERAL", "QUESTION"] => shift 55;
    ["STRING_LITERAL", "STAR"] => shift 55;
    ["STRING_LITERAL", "PLUS"] => shift 55;
    ["STRING_LITERAL", "NOT"] => shift 55;
    ["STRING_LITERAL", "LPAREN"] => shift 55;
    ["STRING_LITERAL", "RPAREN"] => shift 55;
    ["STRING_LITERAL", "SEMI"] => shift 55;
    ["STRING_LITERAL", "RARROW"] => shift 55;
    ["STRING_LITERAL", "DOT"] => shift 55;
    ["LEXER_CHAR_SET", "STRING_LITERAL"] => shift 56;
    ["LEXER_CHAR_SET", "LEXER_CHAR_SET"] => shift 56;
    ["LEXER_CHAR_SET", "TOKEN_REF"] => shift 56;
    ["LEXER_CHAR_SET", "OR"] => shift 56;
    ["LEXER_CHAR_SET", "QUESTION"] => shift 56;
    ["LEXER_CHAR_SET", "STAR"] => shift 56;
    ["LEXER_CHAR_SET", "PLUS"] => shift 56;
    ["LEXER_CHAR_SET", "NOT"] => shift 56;
    ["LEXER_CHAR_SET", "LPAREN"] => shift 56;
    ["LEXER_CHAR_SET", "RPAREN"] => shift 56;
    ["LEXER_CHAR_SET", "SEMI"] => shift 56;
    ["LEXER_CHAR_SET", "RARROW"] => shift 56;
    ["LEXER_CHAR_SET", "DOT"] => shift 56;
    ["TOKEN_REF", "STRING_LITERAL"] => shift 57;
    ["TOKEN_REF", "LEXER_CHAR_SET"] => shift 57;
    ["TOKEN_REF", "TOKEN_REF"] => shift 57;
    ["TOKEN_REF", "OR"] => shift 57;
    ["TOKEN_REF", "QUESTION"] => shift 57;
    ["TOKEN_REF", "STAR"] => shift 57;
    ["TOKEN_REF", "PLUS"] => shift 57;
    ["TOKEN_REF", "NOT"] => shift 57;
    ["TOKEN_REF", "LPAREN"] => shift 57;
    ["TOKEN_REF", "RPAREN"] => shift 57;
    ["TOKEN_REF", "SEMI"] => shift 57;
    ["TOKEN_REF", "RARROW"] => shift 57;
    ["TOKEN_REF", "DOT"] => shift 57;
    ["NOT", "STRING_LITERAL"] => shift 58;
    ["NOT", "LEXER_CHAR_SET"] => shift 58;
    ["LPAREN", "EMPTY"] => shift 59;
    ["LPAREN", "STRING_LITERAL"] => shift 59;
    ["LPAREN", "LEXER_CHAR_SET"] => shift 59;
    ["LPAREN", "TOKEN_REF"] => shift 59;
    ["LPAREN", "NOT"] => shift 59;
    ["LPAREN", "LPAREN"] => shift 59;
    ["LPAREN", "DOT"] => shift 59;
    ["DOT", "STRING_LITERAL"] => shift 60;
    ["DOT", "LEXER_CHAR_SET"] => shift 60;
    ["DOT", "TOKEN_REF"] => shift 60;
    ["DOT", "OR"] => shift 60;
    ["DOT", "QUESTION"] => shift 60;
    ["DOT", "STAR"] => shift 60;
    ["DOT", "PLUS"] => shift 60;
    ["DOT", "NOT"] => shift 60;
    ["DOT", "LPAREN"] => shift 60;
    ["DOT", "RPAREN"] => shift 60;
    ["DOT", "SEMI"] => shift 60;
    ["DOT", "RARROW"] => shift 60;
    ["DOT", "DOT"] => shift 60;
    any => reduce 43;
  }
  action-row 68 {
    ["TRUE", "SEMI"] => shift 100;
    ["FALSE", "SEMI"] => shift 101;
    ["STRING_LITERAL", "SEMI"] => shift 102;
    ["RULE_REF", "SEMI"] => shift 39;
    ["TOKEN_REF", "SEMI"] => shift 40;
    ["INTEGER", "SEMI"] => shift 103;
    any => error;
  }
  action-row 69 {
    ["TOKEN_REF", "COMMA"] => shift 106;
    ["TOKEN_REF", "RBRACE"] => shift 106;
    any => reduce 76;
  }
  action-row 70 {
    any => reduce 74;
  }
  action-row 71 {
    any => reduce 17;
  }
  action-row 72 {
    ["TOKEN_REF", "OVER"] => shift 107;
    any => error;
  }
  action-row 73 {
    ["RULE_REF", "HASH"] => shift 108;
    any => error;
  }
  action-row 74 {
    ["COMMA", "RULE_REF"] => shift 109;
    ["COMMA", "TOKEN_REF"] => shift 109;
    any => reduce 60;
  }
  action-row 75 {
    ["RULE_REF", "RPAREN"] => shift 39;
    ["TOKEN_REF", "RPAREN"] => shift 40;
    any => error;
  }
  action-row 76 {
    any => reduce 110;
  }
  action-row 77 {
    any => reduce 62;
  }
  action-row 78 {
    ["HASH", "TOKEN_REF"] => shift 112;
    any => reduce 85;
  }
  action-row 79 {
    ["ASSIGN", "STRING_LITERAL"] => shift 115;
    ["ASSIGN", "RULE_REF"] => shift 115;
    ["ASSIGN", "TOKEN_REF"] => shift 115;
    any => error;
  }
  action-row 80 {
    any => reduce 83;
  }
  action-row 81 {
    any => reduce 87;
  }
  action-row 82 {
    any => reduce 90;
  }
  action-row 83 {
    ["RULE_REF", "ASSIGN"] => shift 79;
    ["OR", "EMPTY"] => reduce 89;
    ["OR", "STRING_LITERAL"] => reduce 89;
    ["OR", "RULE_REF"] => reduce 89;
    ["OR", "TOKEN_REF"] => reduce 89;
    ["SEMI", "OPTIONS"] => reduce 89;
    ["SEMI", "CHANNELS"] => reduce 89;
    ["SEMI", "LEXER_CLASSES"] => reduce 89;
    ["SEMI", "CONFLICTS"] => reduce 89;
    ["SEMI", "NODE"] => reduce 89;
    ["SEMI", "INLINE"] => reduce 89;
    ["SEMI", "FRAGMENT"] => reduce 89;
    ["SEMI", "TOKEN_REF"] => reduce 89;
    ["SEMI", EOF] => reduce 89;
    ["HASH", "TOKEN_REF"] => shift 112;
    any => reduce 91;
  }
  action-row 84 {
    ["STRING_LITERAL", "STRING_LITERAL"] => shift 120;
    ["STRING_LITERAL", "RULE_REF"] => shift 120;
    ["STRING_LITERAL", "TOKEN_REF"] => shift 120;
    ["STRING_LITERAL", "OR"] => shift 120;
    ["STRING_LITERAL", "QUESTION"] => shift 120;
    ["STRING_LITERAL", "STAR"] => shift 120;
    ["STRING_LITERAL", "PLUS"] => shift 120;
    ["STRING_LITERAL", "SEMI"] => shift 120;
    ["STRING_LITERAL", "HASH"] => shift 120;
    ["RULE_REF", "STRING_LITERAL"] => shift 121;
    ["RULE_REF", "RULE_REF"] => shift 121;
    ["RULE_REF", "TOKEN_REF"] => shift 121;
    ["RULE_REF", "OR"] => shift 121;
    ["RULE_REF", "QUESTION"] => shift 121;
    ["RULE_REF", "STAR"] => shift 121;
    ["RULE_REF", "PLUS"] => shift 121;
    ["RULE_REF", "SEMI"] => shift 121;
    ["RULE_REF", "HASH"] => shift 121;
    ["TOKEN_REF", "STRING_LITERAL"] => shift 122;
    ["TOKEN_REF", "RULE_REF"] => shift 122;
    ["TOKEN_REF", "TOKEN_REF"] => shift 122;
    ["TOKEN_REF", "OR"] => shift 122;
    ["TOKEN_REF", "QUESTION"] => shift 122;
    ["TOKEN_REF", "STAR"] => shift 122;
    ["TOKEN_REF", "PLUS"] => shift 122;
    ["TOKEN_REF", "SEMI"] => shift 122;
    ["TOKEN_REF", "DOT"] => shift 122;
    ["TOKEN_REF", "HASH"] => shift 122;
    any => error;
  }
  action-row 85 {
    any => reduce 52;
  }
  action-row 86 {
    any => reduce 53;
  }
  action-row 87 {
    any => reduce 51;
  }
  action-row 88 {
    ["RPAREN", "STRING_LITERAL"] => shift 125;
    ["RPAREN", "LEXER_CHAR_SET"] => shift 125;
    ["RPAREN", "TOKEN_REF"] => shift 125;
    ["RPAREN", "OR"] => shift 125;
    ["RPAREN", "QUESTION"] => shift 125;
    ["RPAREN", "STAR"] => shift 125;
    ["RPAREN", "PLUS"] => shift 125;
    ["RPAREN", "NOT"] => shift 125;
    ["RPAREN", "LPAREN"] => shift 125;
    ["RPAREN", "RPAREN"] => shift 125;
    ["RPAREN", "SEMI"] => shift 125;
    ["RPAREN", "RARROW"] => shift 125;
    ["RPAREN", "DOT"] => shift 125;
    any => error;
  }
  action-row 89 {
    any => reduce 96;
  }
  action-row 90 {
    ["SEMI", "OPTIONS"] => shift 126;
    ["SEMI", "CHANNELS"] => shift 126;
    ["SEMI", "LEXER_CLASSES"] => shift 126;
    ["SEMI", "CONFLICTS"] => shift 126;
    ["SEMI", "NODE"] => shift 126;
    ["SEMI", "INLINE"] => shift 126;
    ["SEMI", "FRAGMENT"] => shift 126;
    ["SEMI", "TOKEN_REF"] => shift 126;
    ["SEMI", EOF] => shift 126;
    any => error;
  }
  action-row 91 {
    ["OR", "EMPTY"] => shift 127;
    ["OR", "STRING_LITERAL"] => shift 127;
    ["OR", "LEXER_CHAR_SET"] => shift 127;
    ["OR", "TOKEN_REF"] => shift 127;
    ["OR", "NOT"] => shift 127;
    ["OR", "LPAREN"] => shift 127;
    ["OR", "DOT"] => shift 127;
    any => reduce 40;
  }
  action-row 92 {
    any => reduce 55;
  }
  action-row 93 {
    ["QUESTION", "STRING_LITERAL"] => shift 129;
    ["QUESTION", "LEXER_CHAR_SET"] => shift 129;
    ["QUESTION", "TOKEN_REF"] => shift 129;
    ["QUESTION", "OR"] => shift 129;
    ["QUESTION", "NOT"] => shift 129;
    ["QUESTION", "LPAREN"] => shift 129;
    ["QUESTION", "RPAREN"] => shift 129;
    ["QUESTION", "SEMI"] => shift 129;
    ["QUESTION", "RARROW"] => shift 129;
    ["QUESTION", "DOT"] => shift 129;
    any => reduce 105;
  }
  action-row 94 {
    ["QUESTION", "STRING_LITERAL"] => shift 131;
    ["QUESTION", "LEXER_CHAR_SET"] => shift 131;
    ["QUESTION", "TOKEN_REF"] => shift 131;
    ["QUESTION", "OR"] => shift 131;
    ["QUESTION", "NOT"] => shift 131;
    ["QUESTION", "LPAREN"] => shift 131;
    ["QUESTION", "RPAREN"] => shift 131;
    ["QUESTION", "SEMI"] => shift 131;
    ["QUESTION", "RARROW"] => shift 131;
    ["QUESTION", "DOT"] => shift 131;
    any => reduce 107;
  }
  action-row 95 {
    any => reduce 102;
  }
  action-row 96 {
    any => reduce 56;
  }
  action-row 97 {
    any => reduce 57;
  }
  action-row 98 {
    any => reduce 44;
  }
  action-row 99 {
    any => reduce 100;
  }
  action-row 100 {
    any => reduce 13;
  }
  action-row 101 {
    any => reduce 14;
  }
  action-row 102 {
    any => reduce 12;
  }
  action-row 103 {
    any => reduce 11;
  }
  action-row 104 {
    ["SEMI", "RULE_REF"] => shift 133;
    ["SEMI", "TOKEN_REF"] => shift 133;
    ["SEMI", "RBRACE"] => shift 133;
    any => error;
  }
  action-row 105 {
    any => reduce 10;
  }
  action-row 106 {
    any => reduce 18;
  }
  action-row 107 {
    ["OVER", "REDUCE"] => shift 134;
    any => error;
  }
  action-row 108 {
    ["HASH", "TOKEN_REF"] => shift 135;
    any => error;
  }
  action-row 109 {
    any => reduce 108;
  }
  action-row 110 {
    ["RPAREN", "SEMI"] => shift 137;
    ["RPAREN", "COLON"] => shift 137;
    ["RPAREN", "COMMA"] => shift 137;
    any => error;
  }
  action-row 111 {
    ["TOKEN_REF", "OR"] => shift 138;
    ["TOKEN_REF", "SEMI"] => shift 138;
    any => error;
  }
  action-row 112 {
    any => reduce 84;
  }
  action-row 113 {
    any => reduce 26;
  }
  action-row 114 {
    any => reduce 30;
  }
  action-row 115 {
    ["OR", "EMPTY"] => shift 139;
    ["OR", "STRING_LITERAL"] => shift 139;
    ["OR", "RULE_REF"] => shift 139;
    ["OR", "TOKEN_REF"] => shift 139;
    ["SEMI", "OPTIONS"] => shift 140;
    ["SEMI", "CHANNELS"] => shift 140;
    ["SEMI", "LEXER_CLASSES"] => shift 140;
    ["SEMI", "CONFLICTS"] => shift 140;
    ["SEMI", "NODE"] => shift 140;
    ["SEMI", "INLINE"] => shift 140;
    ["SEMI", "FRAGMENT"] => shift 140;
    ["SEMI", "TOKEN_REF"] => shift 140;
    ["SEMI", EOF] => shift 140;
    any => error;
  }
  action-row 116 {
    any => reduce 88;
  }
  action-row 117 {
    any => reduce 86;
  }
  action-row 118 {
    any => reduce 27;
  }
  action-row 119 {
    any => reduce 33;
  }
  action-row 120 {
    any => reduce 31;
  }
  action-row 121 {
    ["DOT", "RULE_REF"] => shift 142;
    ["DOT", "TOKEN_REF"] => shift 142;
    any => reduce 32;
  }
  action-row 122 {
    ["QUESTION", "STRING_LITERAL"] => shift 143;
    ["QUESTION", "RULE_REF"] => shift 143;
    ["QUESTION", "TOKEN_REF"] => shift 143;
    ["QUESTION", "OR"] => shift 143;
    ["QUESTION", "SEMI"] => shift 143;
    ["QUESTION", "HASH"] => shift 143;
    ["STAR", "STRING_LITERAL"] => shift 144;
    ["STAR", "RULE_REF"] => shift 144;
    ["STAR", "TOKEN_REF"] => shift 144;
    ["STAR", "OR"] => shift 144;
    ["STAR", "SEMI"] => shift 144;
    ["STAR", "HASH"] => shift 144;
    ["PLUS", "STRING_LITERAL"] => shift 145;
    ["PLUS", "RULE_REF"] => shift 145;
    ["PLUS", "TOKEN_REF"] => shift 145;
    ["PLUS", "OR"] => shift 145;
    ["PLUS", "SEMI"] => shift 145;
    ["PLUS", "HASH"] => shift 145;
    any => reduce 93;
  }
  action-row 123 {
    any => reduce 34;
  }
  action-row 124 {
    any => reduce 54;
  }
  action-row 125 {
    any => reduce 39;
  }
  action-row 126 {
    ["EMPTY", "OR"] => shift 54;
    ["EMPTY", "RPAREN"] => shift 54;
    ["EMPTY", "SEMI"] => shift 54;
    ["EMPTY", "RARROW"] => shift 54;
    ["STRING_LITERAL", "STRING_LITERAL"] => shift 55;
    ["STRING_LITERAL", "LEXER_CHAR_SET"] => shift 55;
    ["STRING_LITERAL", "TOKEN_REF"] => shift 55;
    ["STRING_LITERAL", "OR"] => shift 55;
    ["STRING_LITERAL", "QUESTION"] => shift 55;
    ["STRING_LITERAL", "STAR"] => shift 55;
    ["STRING_LITERAL", "PLUS"] => shift 55;
    ["STRING_LITERAL", "NOT"] => shift 55;
    ["STRING_LITERAL", "LPAREN"] => shift 55;
    ["STRING_LITERAL", "RPAREN"] => shift 55;
    ["STRING_LITERAL", "SEMI"] => shift 55;
    ["STRING_LITERAL", "RARROW"] => shift 55;
    ["STRING_LITERAL", "DOT"] => shift 55;
    ["LEXER_CHAR_SET", "STRING_LITERAL"] => shift 56;
    ["LEXER_CHAR_SET", "LEXER_CHAR_SET"] => shift 56;
    ["LEXER_CHAR_SET", "TOKEN_REF"] => shift 56;
    ["LEXER_CHAR_SET", "OR"] => shift 56;
    ["LEXER_CHAR_SET", "QUESTION"] => shift 56;
    ["LEXER_CHAR_SET", "STAR"] => shift 56;
    ["LEXER_CHAR_SET", "PLUS"] => shift 56;
    ["LEXER_CHAR_SET", "NOT"] => shift 56;
    ["LEXER_CHAR_SET", "LPAREN"] => shift 56;
    ["LEXER_CHAR_SET", "RPAREN"] => shift 56;
    ["LEXER_CHAR_SET", "SEMI"] => shift 56;
    ["LEXER_CHAR_SET", "RARROW"] => shift 56;
    ["LEXER_CHAR_SET", "DOT"] => shift 56;
    ["TOKEN_REF", "STRING_LITERAL"] => shift 57;
    ["TOKEN_REF", "LEXER_CHAR_SET"] => shift 57;
    ["TOKEN_REF", "TOKEN_REF"] => shift 57;
    ["TOKEN_REF", "OR"] => shift 57;
    ["TOKEN_REF", "QUESTION"] => shift 57;
    ["TOKEN_REF", "STAR"] => shift 57;
    ["TOKEN_REF", "PLUS"] => shift 57;
    ["TOKEN_REF", "NOT"] => shift 57;
    ["TOKEN_REF", "LPAREN"] => shift 57;
    ["TOKEN_REF", "RPAREN"] => shift 57;
    ["TOKEN_REF", "SEMI"] => shift 57;
    ["TOKEN_REF", "RARROW"] => shift 57;
    ["TOKEN_REF", "DOT"] => shift 57;
    ["NOT", "STRING_LITERAL"] => shift 58;
    ["NOT", "LEXER_CHAR_SET"] => shift 58;
    ["LPAREN", "EMPTY"] => shift 59;
    ["LPAREN", "STRING_LITERAL"] => shift 59;
    ["LPAREN", "LEXER_CHAR_SET"] => shift 59;
    ["LPAREN", "TOKEN_REF"] => shift 59;
    ["LPAREN", "NOT"] => shift 59;
    ["LPAREN", "LPAREN"] => shift 59;
    ["LPAREN", "DOT"] => shift 59;
    ["DOT", "STRING_LITERAL"] => shift 60;
    ["DOT", "LEXER_CHAR_SET"] => shift 60;
    ["DOT", "TOKEN_REF"] => shift 60;
    ["DOT", "OR"] => shift 60;
    ["DOT", "QUESTION"] => shift 60;
    ["DOT", "STAR"] => shift 60;
    ["DOT", "PLUS"] => shift 60;
    ["DOT", "NOT"] => shift 60;
    ["DOT", "LPAREN"] => shift 60;
    ["DOT", "RPAREN"] => shift 60;
    ["DOT", "SEMI"] => shift 60;
    ["DOT", "RARROW"] => shift 60;
    ["DOT", "DOT"] => shift 60;
    any => error;
  }
  action-row 127 {
    any => reduce 98;
  }
  action-row 128 {
    any => reduce 104;
  }
  action-row 129 {
    any => reduce 58;
  }
  action-row 130 {
    any => reduce 106;
  }
  action-row 131 {
    any => reduce 59;
  }
  action-row 132 {
    any => reduce 9;
  }
  action-row 133 {
    ["REDUCE", "RULE_REF"] => shift 149;
    any => error;
  }
  action-row 134 {
    ["TOKEN_REF", "OVER"] => shift 150;
    any => error;
  }
  action-row 135 {
    any => reduce 61;
  }
  action-row 136 {
    any => reduce 63;
  }
  action-row 137 {
    any => reduce 28;
  }
  action-row 138 {
    any => reduce 22;
  }
  action-row 139 {
    any => reduce 82;
  }
  action-row 140 {
    ["RULE_REF", "STRING_LITERAL"] => shift 39;
    ["RULE_REF", "RULE_REF"] => shift 39;
    ["RULE_REF", "TOKEN_REF"] => shift 39;
    ["RULE_REF", "OR"] => shift 39;
    ["RULE_REF", "QUESTION"] => shift 39;
    ["RULE_REF", "STAR"] => shift 39;
    ["RULE_REF", "PLUS"] => shift 39;
    ["RULE_REF", "SEMI"] => shift 39;
    ["RULE_REF", "HASH"] => shift 39;
    ["TOKEN_REF", "STRING_LITERAL"] => shift 40;
    ["TOKEN_REF", "RULE_REF"] => shift 40;
    ["TOKEN_REF", "TOKEN_REF"] => shift 40;
    ["TOKEN_REF", "OR"] => shift 40;
    ["TOKEN_REF", "QUESTION"] => shift 40;
    ["TOKEN_REF", "STAR"] => shift 40;
    ["TOKEN_REF", "PLUS"] => shift 40;
    ["TOKEN_REF", "SEMI"] => shift 40;
    ["TOKEN_REF", "HASH"] => shift 40;
    any => error;
  }
  action-row 141 {
    any => reduce 36;
  }
  action-row 142 {
    any => reduce 37;
  }
  action-row 143 {
    any => reduce 38;
  }
  action-row 144 {
    any => reduce 92;
  }
  action-row 145 {
    any => reduce 29;
  }
  action-row 146 {
    any => reduce 41;
  }
  action-row 147 {
    ["RULE_REF", "HASH"] => shift 153;
    any => error;
  }
  action-row 148 {
    ["OVER", "SHIFT"] => shift 154;
    any => error;
  }
  action-row 149 {
    any => reduce 25;
  }
  action-row 150 {
    any => reduce 35;
  }
  action-row 151 {
    ["HASH", "TOKEN_REF"] => shift 155;
    any => error;
  }
  action-row 152 {
    ["SHIFT", "TOKEN_REF"] => shift 156;
    any => error;
  }
  action-row 153 {
    ["TOKEN_REF", "SEMI"] => shift 157;
    any => error;
  }
  action-row 154 {
    ["TOKEN_REF", "SEMI"] => shift 158;
    any => error;
  }
  action-row 155 {
    ["SEMI", "PREFER"] => shift 159;
    ["SEMI", "RBRACE"] => shift 159;
    any => error;
  }
  action-row 156 {
    ["SEMI", "PREFER"] => shift 160;
    ["SEMI", "RBRACE"] => shift 160;
    any => error;
  }
  action-row 157 {
    any => reduce 20;
  }
  action-row 158 {
    any => reduce 21;
  }

  action-state-rows [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 35, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 53, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158];

  goto-row 0 {
    "document" => 2;
    "grammarDeclaration" => 3;
  }
  goto-row 1 {
  }
  goto-row 2 {
    "document__ebnf_0_1" => 5;
  }
  goto-row 3 {
    "topLevelItem" => 14;
    "optionsBlock" => 15;
    "lexerClassesBlock" => 16;
    "channelsBlock" => 17;
    "conflictsBlock" => 18;
    "parserRuleSpec" => 19;
    "treeModifier" => 20;
    "lexerRuleSpec" => 21;
    "lexerRuleSpec__ebnf_0_0" => 22;
  }
  goto-row 4 {
    "optionsBlock__ebnf_0_2" => 29;
  }
  goto-row 5 {
    "channelList" => 31;
    "channelsBlock__ebnf_0_2" => 32;
  }
  goto-row 6 {
    "lexerClassesBlock__ebnf_0_2" => 33;
  }
  goto-row 7 {
    "conflictsBlock__ebnf_0_2" => 34;
  }
  goto-row 8 {
    "lexerCommands" => 36;
    "parserRuleSpec__ebnf_0_2" => 37;
  }
  goto-row 9 {
    "optionEntry" => 42;
    "identifier" => 43;
  }
  goto-row 10 {
    "channelList__ebnf_0_1" => 44;
  }
  goto-row 11 {
    "optionEntry" => 47;
    "identifier" => 43;
  }
  goto-row 12 {
    "conflictEntry" => 50;
  }
  goto-row 13 {
    "lexerCommand" => 51;
    "identifier" => 52;
  }
  goto-row 14 {
    "lexerAltList" => 61;
    "lexerAlternative" => 62;
    "lexerElement" => 63;
    "lexerAtom" => 64;
    "lexerNegation" => 65;
    "lexerGroup" => 66;
    "lexerAlternative__ebnf_1_0" => 67;
  }
  goto-row 15 {
    "channelTail" => 70;
    "channelList__ebnf_0_2" => 71;
  }
  goto-row 16 {
    "lexerCommands__ebnf_0_2" => 74;
  }
  goto-row 17 {
    "lexerCommandArgument" => 76;
    "lexerCommand__ebnf_0_1" => 77;
  }
  goto-row 18 {
    "parserAlternative" => 80;
    "parserElement" => 81;
    "elementLabel" => 82;
    "parserAlternative__ebnf_1_0" => 83;
    "parserElement__ebnf_0_0" => 84;
  }
  goto-row 19 {
    "lexerNegatable" => 87;
  }
  goto-row 20 {
    "lexerAltList" => 88;
    "lexerAlternative" => 62;
    "lexerElement" => 63;
    "lexerAtom" => 64;
    "lexerNegation" => 65;
    "lexerGroup" => 66;
    "lexerAlternative__ebnf_1_0" => 67;
  }
  goto-row 21 {
    "lexerCommands" => 89;
    "lexerRuleSpec__ebnf_0_4" => 90;
  }
  goto-row 22 {
    "lexerAltList__ebnf_0_1" => 91;
  }
  goto-row 23 {
    "lexerSuffix" => 95;
    "starSuffix" => 96;
    "plusSuffix" => 97;
    "lexerElement__ebnf_0_1" => 98;
  }
  goto-row 24 {
    "lexerElement" => 99;
    "lexerAtom" => 64;
    "lexerNegation" => 65;
    "lexerGroup" => 66;
  }
  goto-row 25 {
    "optionValue" => 104;
    "identifier" => 105;
  }
  goto-row 26 {
    "lexerCommandTail" => 110;
  }
  goto-row 27 {
    "identifier" => 111;
  }
  goto-row 28 {
    "alternativeLabel" => 113;
    "parserAlternative__ebnf_0_1" => 114;
  }
  goto-row 29 {
    "parserRuleSpec__ebnf_0_5" => 116;
  }
  goto-row 30 {
    "alternativeLabel" => 117;
    "parserElement" => 118;
    "elementLabel" => 82;
    "parserAlternative__ebnf_1_1" => 119;
    "parserElement__ebnf_0_0" => 84;
  }
  goto-row 31 {
    "parserSymbol" => 123;
    "qualifiedReference" => 124;
  }
  goto-row 32 {
    "lexerAlternativeTail" => 128;
  }
  goto-row 33 {
    "starSuffix__ebnf_0_1" => 130;
  }
  goto-row 34 {
    "plusSuffix__ebnf_0_1" => 132;
  }
  goto-row 35 {
    "lexerCommand" => 136;
    "identifier" => 52;
  }
  goto-row 36 {
    "parserAlternativeTail" => 141;
  }
  goto-row 37 {
    "parserSuffix" => 146;
    "parserElement__ebnf_0_2" => 147;
  }
  goto-row 38 {
    "lexerAlternative" => 148;
    "lexerElement" => 63;
    "lexerAtom" => 64;
    "lexerNegation" => 65;
    "lexerGroup" => 66;
    "lexerAlternative__ebnf_1_0" => 67;
  }
  goto-row 39 {
    "parserAlternative" => 151;
    "parserElement" => 81;
    "elementLabel" => 82;
    "parserAlternative__ebnf_1_0" => 83;
    "parserElement__ebnf_0_0" => 84;
  }
  goto-row 40 {
    "identifier" => 152;
  }

  goto-state-rows [0, 1, 1, 2, 1, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 4, 5, 6, 7, 8, 1, 9, 10, 1, 1, 11, 12, 13, 1, 1, 14, 1, 1, 1, 1, 1, 15, 1, 1, 1, 1, 1, 1, 16, 17, 18, 1, 1, 1, 1, 19, 20, 1, 21, 22, 1, 23, 1, 1, 24, 25, 1, 1, 1, 1, 1, 26, 27, 1, 1, 28, 1, 29, 1, 1, 30, 31, 1, 1, 1, 1, 1, 1, 32, 1, 33, 34, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 35, 1, 1, 1, 1, 1, 1, 36, 1, 1, 1, 1, 1, 1, 37, 1, 1, 1, 38, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 39, 1, 1, 40, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1];
}
