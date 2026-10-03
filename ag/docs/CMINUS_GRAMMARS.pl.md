# Gramatyki C Minus dla Agas

[English](CMINUS_GRAMMARS.md) | [Polski](CMINUS_GRAMMARS.pl.md)

W katalogu `grammars/examples` znajdują się trzy przykłady gramatyk zapisanych
w ograniczonym EBNF formatu Agas:

- `cminus.ag` — kompromisowa gramatyka małego języka C Minus;
- `cminus_closed_open.ag` — deterministyczny wariant wiążący `else` z
  najbliższym otwartym `if` przez reguły `closed/open`;
- `cmm.ag` — skrajnie mała gramatyka przeznaczona do testowania generatorów gramatyk i parserów.

Pliki `cminus.g4` i `cmm.g4` są oryginalnymi źródłami ANTLR4 umieszczonymi
obok ich odpowiedników `.ag`. Są bajtowo zgodne z wcześniejszymi kopiami w
`PEG/agbootstrap/g4`.

## `cminus.ag`

`cminus.ag` opisuje mały język proceduralny przypominający C. Jest to gramatyka kompromisowa: większa od minimalnego przykładu testowego, ale nadal pozbawiona wielu konstrukcji pełnego C.

Obsługuje:

- globalne deklaracje zmiennych;
- funkcje z wynikiem `int`, `string` albo `void`;
- formalne parametry funkcji;
- lokalne deklaracje i inicjalizacje;
- bloki instrukcji;
- wywołania funkcji i listy argumentów;
- wyrażenia addytywne i multiplikatywne;
- liczby, napisy, zmienne, wywołania oraz negację arytmetyczną;
- porównania `<=`, `<`, `>`, `>=`, `==` i `!=`;
- instrukcje `while`, `for`, `if`, `if-else` i `return`;
- przypisania proste i złożone;
- postinkrementację i postdekrementację;
- komentarze wierszowe i blokowe.

Zgodnie z opisem źródłowym gramatyka zawiera typ `string` i literały napisowe, ale nie zawiera tablic.

Gramatyka zachowuje klasyczny problem „dangling else”:

```text
node ifStatement
    : IF LPAREN condition=booleanExpression RPAREN thenBranch=statement
    | IF LPAREN condition=booleanExpression RPAREN thenBranch=statement ELSE elseBranch=statement
    ;
```

Dla deterministycznego generatora LR może to dać konflikt shift/reduce przy `ELSE`. Nazwy alternatyw `#IfStatement` i `#IfElseStatement` określają jedynie warianty AST; nie rozstrzygają konfliktu. Generator powinien konflikt zgłosić, a ewentualna reguła „ELSE wiąże się z najbliższym IF” musi być osobną, jawną polityką albo wynikać z przepisanej gramatyki matched/unmatched.

## `cminus_closed_open.ag`

Ten wariant zachowuje oryginalny `cminus.ag` jako przypadek diagnostyczny, a
instrukcje dzieli na `closedStatement` i `openStatement`. Otwarty `if` jest
propagowany także przez ciała `while` i `for`; dzięki temu `else` wiąże się z
najbliższym jeszcze niezamkniętym `if` bez globalnej preferencji shift.

Test Żbika wykazał, że wspólny prefiks deklaracji zmiennej i funkcji nadal
powoduje jeden konflikt canonical LR(1), który znika dla `k=2`. Po usunięciu
*dangling else* gramatyka jest bezkonfliktowa jako canonical LR(2) oraz
LALR(2). Jest to zamierzony przykład praktycznego użycia lookaheadu większego
niż jeden.

## `cmm.ag`

`cmm.ag` jest celowo bardzo mały. Służy jako szybki materiał testowy dla:

- parsera samego formatu `.ag`;
- konwersji ograniczonego EBNF do BNF;
- FIRST i FOLLOW;
- budowy stanów LR;
- generowania tablic ACTION/GOTO;
- generowania kodu parsera w języku docelowym;
- tworzenia prostego AST.

Język zawiera tylko:

- zmienne typu `int`;
- deklaracje i inicjalizacje;
- bloki;
- instrukcję `while`;
- przypisania;
- wywołania funkcji;
- dodawanie i odejmowanie;
- porównania;
- liczby całkowite i identyfikatory;
- komentarze wierszowe.

Nie ma funkcji jako deklaracji, typu `string`, literałów napisowych, mnożenia, dzielenia, `if`, `for` ani `return`.

## Sposób konwersji do ograniczonego EBNF

Anonimowe grupy z gramatyk wejściowych zostały zastąpione nazwanymi regułami. Na przykład konstrukcja:

```text
formal_parameter (COMMA formal_parameter)*
```

ma w Agas postać:

```text
node formalParameters
    : first=formalParameter rest=formalParameterTail*
    ;

inline formalParameterTail
    : COMMA value=formalParameter
    ;
```

Podobnie wyrażenie:

```text
term (addop term)*
```

zostało rozbite na:

```text
node additiveExpression
    : first=term rest=additiveOperation*
    ;

inline additiveOperation
    : operator=addop operand=term
    ;
```

Dzięki temu powtarzana para operator–argument ma własną nazwę i stabilną pozycję w produkcji. Reguła `inline` może zwrócić pomocniczą wartość dla redukcji, ale nie dodaje niepotrzebnego poziomu AST.

## Kształt AST

Reguły `node` oznaczają konstrukcje, które mają być kotwicami drzewa, na przykład:

- `program`;
- `funDeclaration`;
- `varDeclaration` i `varInitialization`;
- `compoundStatement`;
- `booleanExpression` i `additiveExpression`;
- `whileStatement`, `forStatement`, `ifStatement`;
- `assignStatement`, `call` i `returnStatement`.

Reguły `inline` są warstwą techniczną albo przekazują jeden z możliwych elementów:

- `declaration` i `statement`;
- `typeSpecifier` i operatory;
- końcówki list, np. `argumentTail`;
- pomocnicze operacje, np. `additiveOperation`.

Interpunkcja nie ma etykiet pól, więc tokeny `SEMI`, `COMMA`, `LPAREN`, `RPAREN`, `LBRACE` i `RBRACE` nie muszą trafiać do wynikowego AST. Etykiety takie jak `name=ID`, `condition=booleanExpression` i `body=statement` wskazują wartości przeznaczone dla kolejnych etapów kompilatora.

## Zgodność języka

Rozbijanie anonimowych grup na nazwane reguły nie powinno zmieniać rozpoznawanego języka. Reguły pomocnicze są odpowiednikami sekwencji znajdujących się wcześniej wewnątrz `(...)`.

Wyjątkiem nie jest również AST: zmienia się jego projekt i nazwy elementów, lecz nie zbiór akceptowanych ciągów tokenów. W przyszłości zgodność można sprawdzać generatorem krótkich słów: dla ustalonej długości porównać słowa akceptowane przez gramatykę źródłową i gramatykę po konwersji.
