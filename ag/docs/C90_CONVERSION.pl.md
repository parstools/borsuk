# Konwersja rozpoczętej gramatyki C90 do Agas

[English](C90_CONVERSION.md) | [Polski](C90_CONVERSION.pl.md)

Plik `C90.ag` jest konwersją rozpoczętej i niedokończonej gramatyki `c_grammar/c90_parser/src/C90.g4`. Celem konwersji jest zachowanie aktualnego zakresu gramatyki, a nie uzupełnienie jej do pełnego standardu ISO C90.

## Stan pliku źródłowego

Źródłowy `C90.g4` ma 818 linii i zawiera rozbudowaną część deklaratorów, typów, instrukcji, wyrażeń oraz literałów. Nadal ma cechy szkicu, między innymi:

- komentarz `to do integer suffix?` przy stałych szesnastkowych;
- rozszerzenia GNU i konstrukcje wykraczające poza czyste C90;
- rozróżnianie nazw typów od zwykłych identyfikatorów pozostawione dalszym etapom;
- reguły zastępcze `namePlace`, `variableDeclaratorPlace` i `functionDeclaratorPlace`;
- obsługę części deklaratorów i inicjalizatorów bez kompletnej walidacji semantycznej;
- brak dowodu, że całość jest deterministyczną gramatyką LR(1).

Sprawdzenie dostępnym ANTLR 4.9.2 pokazało, że plik nie generuje obecnie kodu bez błędów nazw:

- dla celu Java reguła `volatile` koliduje ze słowem języka docelowego;
- dla celu C++ dodatkowo kolidują `asm` i `alignof`.

Nie jest to błąd opisywanego języka C. Generator wielojęzykowy Agas powinien zachowywać nazwę gramatyczną, a identyfikator funkcji w kodzie docelowym bezpiecznie manglować, na przykład:

```text
volatile -> rule_volatile
asm      -> rule_asm
alignof  -> rule_alignof
```

## Zakres zachowany w `C90.ag`

Konwersja zachowuje między innymi:

- deklaracje zmiennych, funkcji, struktur, unii, enumów i `typedef`;
- złożone deklaratory wskaźników, tablic i funkcji;
- listy parametrów prototypowych oraz K&R;
- inicjalizatory agregatów i rozszerzone inicjalizatory pól;
- pola bitowe;
- instrukcje z etykietami, `if`, `switch`, pętle, `goto`, `return`, `break` i `continue`;
- GNU `asm`, `__extension__`, `__real__`, `__imag__`, `__builtin_offsetof` i `__builtin_va_arg`;
- pełną drabinę priorytetów wyrażeń obecną w źródle;
- literały całkowite, zmiennoprzecinkowe, znakowe i napisowe;
- komentarze blokowe i linie preprocesora kierowane na kanał `HIDDEN`.

Nazwa `C90` jest więc historyczna. Gramatyka zawiera elementy późniejszych standardów lub rozszerzeń kompilatorów, np. `_Alignof`, liczby binarne, wyrażenia GNU oraz zakresy `case`.

## Zamiana pełnego EBNF na ograniczony Agas

Każda anonimowa grupa parserowa została zastąpiona nazwaną regułą. Przykładowo:

```text
parameterOrType (',' parameterOrType)*
```

zostało zapisane jako:

```text
node fixedParameterOrTypeList
    : first=parameterOrType rest=parameterOrTypeTail*
    ;

inline parameterOrTypeTail
    : COMMA value=parameterOrType
    ;
```

Podobnie fragment:

```text
logicalAndExpression ('||' logicalAndExpression)*
```

ma postać:

```text
node logicalOrExpression
    : first=logicalAndExpression rest=logicalOrTail*
    ;

inline logicalOrTail
    : LOGICAL_OR value=logicalAndExpression
    ;
```

Nazwane końcówki pozwalają generatorowi jednoznacznie wskazać operator i prawy argument oraz wygenerować kod redukcji bez analizowania anonimowego drzewa wyrażenia EBNF.

## Literały w regułach parsera

W `C90.ag` słowa kluczowe i operatory otrzymały jawne nazwy tokenów, np. `IF`, `STRUCT`, `ASSIGN_SHL`, `LOGICAL_AND` i `ELLIPSIS`. Ma to kilka zalet:

- diagnostyka używa stabilnej nazwy tokenu;
- polecenia generatora nie zależą od tekstu literału;
- kod docelowy nie musi tworzyć niejawnych tokenów `T__0`, `T__1`;
- łatwiej porównywać automaty i tablice pomiędzy implementacjami.

Dłuższe operatory muszą mieć pierwszeństwo przed ich krótszymi prefiksami albo lexer musi bezwzględnie stosować zasadę najdłuższego dopasowania. Przykładami są `>>=` przed `>>` i `>` oraz `...` przed `.`.

## Reguły leksera

Ograniczenie zagnieżdżeń Agas dotyczy parsera, nie wyrażeń regularnych leksera. Dlatego konstrukcje takie jak:

```text
(NonzeroDigit Digit* | '0') IntegerSuffix?
```

pozostały grupami regexowymi. Nie tworzą węzłów AST programu i są naturalnym wejściem dla budowy NFA/DFA.

## Tożsamość produkcji i AST

Każda alternatywa otrzyma własny `RuleId`, niezależnie od tego, czy reguła jest `node`, czy `inline`.

- `node` oznacza kotwicę AST;
- `inline` oznacza regułę techniczną albo przekazującą wartość;
- `field=Symbol` nazywa wartość potrzebną w redukcji lub AST;
- interpunkcja bez etykiety nie musi trafiać do AST.

Konwersja nie nazywa alternatyw `#...`, ponieważ istniejące nazwy reguł i `RuleId` wystarczają do pierwszej wersji modelu. Warianty AST można nazwać później, ale wtedy dana wieloalternatywna reguła `node` powinna nazwać wszystkie alternatywy albo żadnej.

## Rzeczy wymagające dalszej pracy

1. Zbudować model `Grammar` z `C90.ag` i przeprowadzić pełną walidację odwołań do symboli.
2. Porównać lexer Agas z lexerem wygenerowanym z części leksykalnej źródła.
3. Policzyć FIRST/FOLLOW i wykryć cykle nullable.
4. Zbudować kanoniczne LR(1), a następnie sprawdzić konflikty dla większego `k`.
5. Oddzielić konflikty prawdziwe od problemów wymagających informacji o nazwach `typedef`.
6. Zweryfikować deklaratory funkcji, wskaźników i tablic na korpusie rzeczywistych deklaracji C.
7. Porównać oba języki generatorem krótkich słów tam, gdzie rozmiar fragmentu gramatyki na to pozwala.
8. Dopiero potem uzupełniać brakujące elementy standardu lub rozszerzenia GNU.

`C90.ag` należy więc traktować jako wierną migrację istniejącego szkicu do nowej notacji, nie jako ukończoną specyfikację C90.
