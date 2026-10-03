# Trzy starsze warianty C90 w formacie Agas

[English](C90_LEGACY.md) | [Polski](C90_LEGACY.pl.md)

W katalogu `grammars/c` znajdują się trzy mechanicznie skonwertowane warianty
starszych gramatyk ANTLR4. Otrzymały nazwy `Legacy`, aby nie sugerować, że
zastępują ręcznie opracowywany `C90.ag` albo że są najnowszą specyfikacją
języka.

| Plik Agas | Plik źródłowy | Linie źródła | Reguły parsera | Reguły leksera | Reguły parsera po wydzieleniu grup |
|---|---|---:|---:|---:|---:|
| `C90SmallLegacy.ag` | `grammars-v4-moje-C/c/C90.g4` | 647 | 80 | 33 | 110 |
| `C90LargeBranchLegacy.ag` | `grammars-v4.branch_c/c/C90/C90.g4` | 918 | 123 | 43 | 187 |
| `C90LargestExprLegacy.ag` | `proj/grammars-v4/c/C90/C90.g4` | 998 | 108 | 43 | 160 |

Liczba linii nie jest równoznaczna z liczbą aktywnych reguł. Największy plik źródłowy ma mniej aktywnych reguł parsera niż wariant `LargeBranch`, ponieważ zastępuje znaczną część klasycznej drabiny priorytetów jedną dużą, leworekurencyjną regułą `expr`. Starszy wariant drabiny pozostaje w nim częściowo jako komentarz.

## Kierunek dalszego rozwoju C90

Większy wariant nie oznacza lepszej ani wierniejszej gramatyki C90. Warianty `LargeBranchLegacy` i `LargestExprLegacy` były rozwijane także dla kodu korzystającego z rozszerzeń GNU, dlatego zawierają między innymi atrybuty GCC, `asm`, dodatkowe słowa kluczowe i konstrukcje spoza ISO C90.

Po uruchomieniu parsera Agas właściwym punktem wyjścia powinien być `C90SmallLegacy.ag`. Należy najpierw potwierdzić na korpusie jego zgodność z ISO C90, a następnie dodawać brakujące konstrukcje pojedynczo wraz z testami. Większe warianty są źródłem przykładów i reguł do świadomego przenoszenia, nie bazą przejmowaną w całości. Rozszerzenia GNU powinny pozostać oddzielnym, jawnie włączanym wariantem gramatyki.

## Nazwy plików

### `C90SmallLegacy.ag`

Jest to najmniejszy i najbardziej zwarty wariant. Ma osobne konstrukcje deklaracji funkcji, definicji funkcji prototypowych i K&R, prostszy model deklaratorów oraz klasyczną drabinę wyrażeń.

Nazwa gramatyki wewnątrz pliku:

```text
grammar C90SmallLegacy;
```

### `C90LargeBranchLegacy.ag`

To większy wariant z gałęzi `branch_c`. Zawiera bardziej rozbudowane deklaratory, atrybuty GCC, rozszerzenia typów, instrukcje `asm` oraz klasyczną, rozbitą na poziomy drabinę priorytetów wyrażeń.

Nazwa gramatyki:

```text
grammar C90LargeBranchLegacy;
```

### `C90LargestExprLegacy.ag`

To największy plik pod względem liczby linii. Jego charakterystyczną cechą jest jedna duża reguła `expr` z alternatywami nazwanymi pierwotnie w stylu ANTLR.

Nazwa gramatyki:

```text
grammar C90LargestExprLegacy;
```

Powtarzające się etykiety ANTLR zostały ujednoznacznione. Na przykład kilka alternatyw `#postfixExpression` otrzymało nazwy:

```text
#PostfixExpression1
#PostfixExpression2
#PostfixExpression3
...
```

Agas wymaga, aby publiczne nazwy wariantów AST w obrębie reguły były jednoznaczne. Nie zmienia to `RuleId` ani języka rozpoznawanego przez produkcje.

## Sprawdzenie w ANTLR 4.9.2

Mały plik źródłowy przechodzi generowanie zarówno dla Java, jak i C++ bez zmian.

Dwa większe pliki są poprawnie analizowane przez frontend ANTLR, ale generowanie Java w używanej wersji zatrzymuje się na nazwie reguły `volatile`, która koliduje ze słowem zastrzeżonym języka docelowego. Po mechanicznym manglowaniu tej nazwy oba generują się z `-Werror`.

Dla innych języków docelowych zestaw kolizji może być inny. Przykładowo w C++ problematyczne są również nazwy takie jak `asm` i `alignof`, a w Pythonie `type` i `complex`.

Agas nie powinien odrzucać poprawnej nazwy gramatycznej tylko dlatego, że jest zastrzeżona w jednym z języków generatora. Backend ma prowadzić osobną tablicę nazw kodowych:

```text
nazwa gramatyczna   nazwa w wygenerowanym C++
volatile            rule_volatile
asm                 rule_asm
alignof             rule_alignof
```

Diagnostyka i dump gramatyki nadal powinny używać nazwy oryginalnej.

## Charakter konwersji

Te trzy pliki są konwersją mechaniczną, przeznaczoną przede wszystkim do testowania frontendu Agas, konwersji EBNF do BNF oraz generatorów LR. Nie są ręcznie zaprojektowanym docelowym AST.

Każda oryginalna reguła parsera została oznaczona jako `node`. Każda anonimowa grupa parserowa otrzymała deterministyczną nazwę pomocniczą i modyfikator `inline`.

Przykładowa konstrukcja ANTLR:

```text
parameter (',' parameter)*
```

jest przekształcana do postaci podobnej do:

```text
node parameterList
    : parameter parameterListGroup1*
    ;

inline parameterListGroup1
    : ',' parameter
    ;
```

Nazwy mechaniczne `...Group1`, `...Group2` są stabilne dla niezmienionego źródła. Ręcznie projektowana gramatyka może je później zastąpić nazwami dziedzinowymi, np. `parameterTail`.

Reguły leksera zachowują zagnieżdżone grupy regexowe. Ograniczenie anonimowych nawiasów dotyczy tylko parsera.

## `ast = concrete`

Pliki mają opcję:

```text
ast = concrete;
```

Oznacza ona, że konwersja zachowuje strukturę reguł źródłowych i tworzy punkt wyjścia bliższy CST niż dopracowanemu AST kompilatora. Kolejny etap może:

- zmienić wybrane reguły z `node` na `inline`;
- dodać etykiety `field=Symbol`;
- nazwać warianty `#Alternative`;
- usunąć z AST interpunkcję i techniczne poziomy priorytetów;
- zdefiniować składanie leworekurencyjnych wyrażeń do węzłów binarnych.

Nie należy wykonywać tych zmian automatycznie bez decyzji o docelowym API drzewa.

## Powtarzalność konwersji

Konwersję wykonuje skrypt:

```text
ag/tools/antlr4_to_agas.py
```

Każdy wygenerowany plik zawiera:

- pełną pierwotną informację licencyjną;
- ścieżkę źródła;
- SHA-256 źródła;
- informację o mechanicznym wydzieleniu grup.

Skrypt obsługuje użyty w tych plikach, pozbawiony akcji podzbiór gramatyk łączonych ANTLR4. Nie jest jeszcze ogólnym importerem wszystkich konstrukcji `.g4`. W szczególności przed użyciem na innych gramatykach trzeba dodać obsługę akcji w języku docelowym, predykatów semantycznych, `mode`, importów i bloków `tokens`.

## Walidacja wyników

Wszystkie trzy pliki zostały przeczytane w całości przez parser wygenerowany z
bieżącego `grammars/Ag.g4`, bez błędów składniowych.

Sprawdzono również, że żadna oryginalna reguła nie zniknęła:

- `C90SmallLegacy`: zachowane 80 reguł parsera i 33 leksera;
- `C90LargeBranchLegacy`: zachowane 123 reguły parsera i 43 leksera;
- `C90LargestExprLegacy`: zachowane 108 reguł parsera i 43 leksera.

Dodatkowe reguły parsera są wyłącznie nazwanymi odpowiednikami anonimowych grup EBNF.

Pełna zgodność języków wymaga później testów porównawczych: wygenerowania krótkich słów, uruchomienia obu parserów na tym samym korpusie oraz porównania akceptacji. Sama zgodność liczby reguł i poprawne parsowanie pliku `.ag` nie są jeszcze formalnym dowodem równoważności.
