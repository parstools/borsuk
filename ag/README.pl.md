# Agas C++

[English](README.md) | [Polski](README.pl.md)

Agas czyta gramatyki `.ag`, sprawdza je i generuje deterministyczne tabele
LR(k) albo LALR(k). Definicja obejmuje lexer Unicode, ograniczony EBNF parsera,
kanały tokenów oraz neutralny sposób budowania AST przez reguły `node` i
`inline`.

Zwykły program `agas` parsuje `.ag` za pomocą przypiętego, wersjonowanego
pakietu z `artifacts/ag/v1`. Nie uruchamia ANTLR i nie przebudowuje własnej
gramatyki podczas startu. `agas-bootstrap` pozostaje opcjonalnym narzędziem do
odtworzenia pakietu i porównań z frontendem ANTLR.

Projekt jest w trakcie bootstrapu. Poprawnie analizuje `.ag`, buduje lexer,
tabele i neutralne redukcje AST oraz eksportuje statyczny moduł Rust. Nie jest
jeszcze gotowym instalowanym SDK ani generatorem kompletnej aplikacji parsera.

## Szybki start bez ANTLR

Wymagane są kompilator C++20, CMake co najmniej 3.20, ICU wymagane przez Żbika
oraz `nlohmann_json` co najmniej 3.11. Źródła Żbika muszą być dostępne; w tym
repozytorium są w sąsiednim katalogu `zbik`.

Z katalogu głównego `Borsuk`:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
cargo test --offline --manifest-path rust/Cargo.toml
```

ANTLR bootstrap jest domyślnie wyłączony. Zwykły build C++ i Rust korzysta
wyłącznie z przypiętego pakietu oraz statycznych źródeł Rust; nie wymaga Javy,
ANTLR ani generowania parsera przez `build.rs`.

Pełne odtworzenie bez ANTLR wykonuje się osobno:

```bash
cmake --build build \
  --target agas_check_reproducibility -j2
```

Odtworzenie od bootstrapu ANTLR wymaga Javy, biblioteki ANTLR 4.10 i
przypiętego JAR-a (ścieżkę można podać przez `AGAS_ANTLR_JAR`). Nie modyfikuje
przypiętych plików w repozytorium:

```bash
cmake -S . -B build -DAGAS_BUILD_ANTLR_BOOTSTRAP=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build \
  --target agas_reproduce_from_antlr -j2
```

Oba cele porównują wygenerowane pliki bajt po bajcie. Zwykły `ctest` wykonuje
krótkie sprawdzenie przypiętego pakietu i eksportera; pełne generowanie tabel
jest uruchamiane tylko przez jawny cel.

## Przypięty artefakt v1

`artifacts/ag/v1` zawiera dokładnie siedem plików: `manifest.json`,
`symbols.json`, `lexer.json`, `parser.dsl`, `productions.json`,
`reductions.json` i `ast-schema.json`. Manifest podaje wersję formatu,
ustawienia generatora, identyfikację źródeł i skróty sekcji. Loader sprawdza
manifest, kompletność katalogu, limity, odwołania między sekcjami i skróty
przed wykonaniem parsera. Rust i C++ odczytują te same bajty.

Wersja v1 jest przypiętym kontraktem formatu pakietu, nie obietnicą stabilności
wszystkich API C++/Rust ani składni przyszłych plików `.ag`. Zmiana niezgodna
wstecz wymaga nowego numeru wersji artefaktu i osobnego katalogu; nie należy
po cichu podmieniać interpretacji istniejącego v1. Neutralny protokół wymiany
AST jest opisany osobno w `AST_WIRE_V1.md`.

Sprawdzenie przykładowej gramatyki:

```bash
build/bin/agas \
  ag/grammars/examples/cmm.ag
```

Przykładowy wynik podaje liczbę opcji, kanałów, reguł parsera, reguł leksera i
produkcji BNF:

```text
grammar=Cmm options=3 channels=1 parser_rules=21 lexer_rules=21 bnf_rules=45
```

Wartości mogą zmieniać się wraz z gramatyką i transformacją EBNF.

Pełna pomoc programu:

```bash
build/bin/agas --help
```

## Najmniejsza gramatyka `.ag`

```text
grammar Numbers;

options {
    parser = LR;
    lookahead = 1;
    ast = explicit;
}

node document
    : values=NUMBER+ EOF
    ;

NUMBER
    : [0-9]+
    ;

WS
    : [ \t\r\n]+ -> skip
    ;
```

Pierwsza reguła parsera jest startowa. Końcowe `EOF` oznacza akceptację całego
wejścia i nie staje się polem AST. `node` tworzy nazwany węzeł, a `values=`
zachowuje rozpoznane liczby jako pole listowe. `WS` jest pomijany przez lexer.

Najważniejsze opcje:

```text
options {
    parser = LALR;
    lookahead = 2;
    ast = explicit;
}
```

- `parser = LR` wybiera kanoniczny LR(k) i jest wartością domyślną;
- `parser = LALR` wybiera bezpośrednią konstrukcję LALR(k);
- `lookahead` jest dodatnim `k`, domyślnie `1`; generator nie zwiększa go
  automatycznie;
- `ast = explicit` dokumentuje aktualną, jawną politykę drzewa. Obecnie nie
  przełącza innego trybu: zachowanie wynika bezpośrednio z `node`, `inline`
  i etykiet zachowywanych pól.

Pełny, obowiązujący kontrakt składni, leksera, AST i zakresów znajduje się w
`AG_FORMAT.md`. Dobrym małym przykładem jest `grammars/examples/cmm.ag`, a
gramatyką opisującą sam Agas jest `grammars/Ag.ag`.

## `node`, `inline`, pola i warianty

Te cztery mechanizmy mają osobne role:

```text
node factor
    : value=ID                   #NameFactor
    | value=NUMBER               #NumberFactor
    | LPAREN value=expr RPAREN   #GroupedFactor
    ;

inline argument
    : value=expr
    ;
```

- `node` tworzy nazwany węzeł AST, a przezroczyste łańcuchy i samo grupowanie
  nawiasami przekazują rzeczywisty operand;
- `inline` nie tworzy dodatkowego węzła: przekazuje jedno pole, tworzy rekord
  techniczny z kilku pól albo wartość jednostkową bez pól;
- `value=`, `left=` itp. nazywają konkretne pola i występują przed symbolem;
- `#NameFactor` nazywa wariant całej alternatywy reguły `node`.

Nieetykietowane tokeny i symbole uczestniczą w rozpoznaniu oraz zakresie, ale
nie stają się polami. `inline` nie oznacza automatycznego zachowania wszystkich
dzieci. Przy projektowaniu drzewa praktyczna zasada brzmi: zacząć od `node`, a
reguły będące tylko delegowaniem lub rusztowaniem zmieniać na `inline`.

Generator automatycznie usuwa puste poziomy priorytetu wyrażeń: dla
`first=operand rest=tail*` przekazuje `first`, gdy `rest` jest puste.
Wymagane operandy i obecne operatory pozostają w drzewie. Produkcja
`LPAREN value=expr RPAREN` przekazuje `expr`, zachowując zakres nawiasów
w `recognizedSpan`. Dokładne warunki opisuje `AG_FORMAT.md`.

`agas --ast-stats PAKIET PLIK` mierzy drzewo przed eksportem JSON.
`maximumDepth` liczy wszystkie krawędzie od korzenia, włącznie z listami,
opcjami i tokenami; `maximumNodeDepth` liczy tylko nazwane węzły na ścieżce
(korzeń jako jeden). Odpowiednik Rust:
`agas_ast_wire --stats PAKIET PLIK`. Limit eksportu i odczytu wynosi 200
poziomów technicznych.

## Polecenia `agas`

### Walidacja i podsumowanie

```bash
agas [grammar.ag]
```

Program wykonuje lexer i parser przypiętego Agasa, buduje model składni,
sprawdza odwołania i zasady AST, rozwija EBNF do BNF i wypisuje podsumowanie.
Brak argumentu wybiera źródłowe `grammars/Ag.ag`.

### Statystyki tabeli

```bash
agas --table grammar.ag
```

Buduje algorytm i `k` podane w `options`, a następnie wypisuje liczbę reguł
BNF, stanów, itemów, przejść i konfliktów. Dla tabeli bez konfliktów pokazuje
też oszacowanie bajtów tabeli zwykłej i skompresowanej oraz rozmiar DSL.

Konflikty są raportowane; polecenie nie wybiera produkcji według kolejności i
nie zwiększa `k`. Jawny blok `conflicts` może rozstrzygnąć konkretną komórkę
shift/reduce przez `prefer shift ... over reduce ...` albo odwrotnie;
`resolved=` podaje liczbę rozstrzygniętych komórek. Samo znalezienie
nierozstrzygniętego konfliktu nie jest obecnie błędem procesu w trybie
`--table`, dlatego automatyzacja powinna sprawdzać pole `conflicts=`.

### Eksport skompresowanej tabeli

```bash
agas --dump-table grammar.ag > parser.dsl
```

Wypisuje deterministyczny DSL ACTION/GOTO. DSL zawiera tabelę parsera, nie
cały pakiet: symbole, produkcje, lexer, redukcje i schemat AST są oddzielnymi
sekcjami artefaktu.

### Pełny pakiet parsera

```bash
agas --emit-package /tmp/parser-package grammar.ag
```

Zwykły `agas` używa przypiętego parsera własnej gramatyki, więc generowanie
kompletnego pakietu nie wymaga ANTLR. Pakiet zawiera wersjonowany manifest,
lexer, tabelę, symbole, produkcje, redukcje AST i schemat AST; opcjonalna
sekcja diagnostyczna zapisuje jawnie rozstrzygnięte konflikty. Pakiety
generowane dla przykładów nie muszą trafiać do Git. Przykład pełnej ścieżki
`gramatyka → artefakt → runtime Rust → C i LLVM IR` opisuje
[Structured Core IR](../coge/docs/CORE_IR.pl.md).

### Eksport statycznego modułu Rust

```bash
agas --dump-rust-parser grammar.ag > generated_parser.rs
agas --emit-rust-parser generated_parser.rs grammar.ag
agas --emit-rust-parser generated_parser.rs grammar.ag reproduced_artifact_dir
```

Obie postacie generują statyczny lexer Unicode, neutralne redukcje AST i
tabelę LR. Pierwsza używa standardowego wyjścia, druga zapisuje wskazany plik.
Opcjonalny katalog w trzeciej postaci pozwala użyć właśnie odtworzonego
pakietu bootstrapowego zamiast przypiętego pakietu z repozytorium.
Wygenerowany moduł korzysta z API rozwijanego obecnie w `rust`.

Wszystkie główne tryby poza `--emit-rust-parser` i `--emit-package` mają składnię
`agas OPCJA [grammar.ag]`; opcji nie można obecnie dowolnie łączyć.

Kody zakończenia: `0` — wykonano tryb, `1` — błąd pliku, składni, walidacji
lub generowania, `2` — niepoprawny układ argumentów.

## Lexer

Agas stosuje najdłuższe dopasowanie, a remis rozstrzyga wcześniejsza reguła.
Obsługuje fragmenty, grupy regexowe, klasy i zakresy Unicode, kwantyfikatory
zachłanne oraz `*?`/`+?`, komendy `skip` i `channel(NAME)`. Wejście musi być
poprawnym UTF-8; zakresy tokenów są półotwartymi zakresami bajtów.

Kanał domyślny trafia do parsera. Tokeny kanałów nazwanych pozostają dostępne
dla komentarzy i narzędzi, ale parser ich nie widzi:

```text
channels { HIDDEN }

LINE_COMMENT
    : '//' ~[\r\n]* -> channel(HIDDEN)
    ;

WS
    : [ \t\r\n]+ -> skip
    ;
```

Pierwsza wersja nie obsługuje m.in. trybów leksera ANTLR, `more`, `type`,
`pushMode` ani dowolnego kodu osadzonego w gramatyce.

## Ograniczony EBNF parsera

Alternatywy występują na najwyższym poziomie reguły. `?`, `*` i `+` dotyczą
jednego symbolu lub literału. Pustą alternatywę zapisuje się jako `empty`.
Anonimowe grupy parsera nie są obsługiwane; złożony fragment należy nazwać:

```text
node parameterList
    : first=parameter rest=additionalParameter*
    ;

inline additionalParameter
    : COMMA value=parameter
    ;
```

Reguły leksera mogą używać grup `(...)`, ponieważ nie tworzą one poziomów AST.

## Bootstrap ANTLR

Do zwykłego użycia bootstrap nie jest potrzebny. Pełna konfiguracja wymaga
Javy i dokładnie ANTLR C++ runtime 4.10. CMake pobiera oficjalny, przypięty JAR
4.10 do katalogu budowania albo używa pliku przekazanego przez
`-DAGAS_ANTLR_JAR=/path/to/antlr-4.10-complete.jar`.

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DAGAS_BUILD_ANTLR_BOOTSTRAP=ON
cmake --build build -j2
```

Pomoc i najważniejsze użycia:

```bash
build/bin/agas-bootstrap --help
build/bin/agas-bootstrap --table grammar.ag
build/bin/agas-bootstrap \
  --emit-package /tmp/parser-package grammar.ag
```

`agas-bootstrap` wykorzystuje ANTLR tylko do odczytania definicji `Ag.ag`, a
następnie sprawdza docelowy plik własnym frontendem. `--emit-package` zapisuje
wersjonowany manifest i komplet sekcji. Przypięty pakiet można odtworzyć
poleceniem, ale plików w `artifacts/ag/v1` nie należy zastępować bez wykonania
testu bajtowej reprodukcji.

Build z bootstrapem tworzy także eksperymentalny `agas-merge-report`. Porównuje
on kanoniczny LR(k), LALR(k) i dwa tryby selektywnego scalania dla wskazanej
gramatyki. Jest narzędziem pomiarowym, a nie częścią zwykłego przepływu
generowania:

```bash
build/bin/agas-merge-report --help
build/bin/agas-merge-report grammar.ag
```

## Użycie biblioteki C++

Normalny frontend `.ag` jest dostępny w `agas_core`:

```cpp
#include <string>

#include "agas/runtime/PackagedAgFrontend.h"

agas::runtime::PackagedAgFrontend frontend{"path/to/artifacts/ag/v1"};
const auto result = frontend.parse(source);
if (!result.accepted()) {
    // result.issues contains lexical, syntax or adapter errors.
}
```

Po otrzymaniu `SyntaxDocument` typowy przepływ obejmuje
`validateSyntaxModel()`, `lowerToBnf()` albo `generateParserTable()`.
`agas_core` linkuje `zbik_core`; algorytmy LR, EBNF i kompresji nie są
skopiowane do Agasa.

```cmake
add_subdirectory(path/to/Borsuk)
target_link_libraries(my_tool PRIVATE agas_core)
```

Na obecnym etapie ścieżki artefaktów programu `agas` są osadzane podczas
kompilacji, a projekt nie udostępnia celu `install`. Biblioteka jest najbardziej
przydatna wewnątrz drzewa źródłowego lub projektu, który jawnie zarządza
katalogiem pakietu.

## Dokumentacja projektu

- `AG_FORMAT.md` — normatywny kontrakt aktualnego `.ag`;
- `grammars/README.md` — układ gramatyk i przykładów;
- `WHY_LR.md` — uzasadnienie małego, jawnego lookaheadu LR(k);
- `ROADMAP.md` — stan realizacji i dalsze etapy;
- `AUDYT_ARTEFAKTU_BOOTSTRAP.md` — kontrakt pakietu i bootstrapu;
- `KIERUNEK_SYSTEMU_AGAS.md` — przyszłe wersjonowanie i szerszy system
  atrybutów/semantyki; opisuje kierunki, których obecny format jeszcze nie ma;
- `C_GRAMMARS_V4.md`, `C90_CONVERSION.md`, `CMINUS_GRAMMARS.md` — eksperymenty
  z większymi gramatykami C i CMinus.

## Najważniejsze obecne ograniczenia

- brak gotowego instalatora i stabilnego publicznego API;
- brak precedencji i jawnych polityk rozstrzygania konfliktów;
- brak preprocesora `.agi` oraz konfiguracji wariantów C90/C99 opisanych w
  notatce kierunkowej;
- brak pełnego recovery parsera dla niedokończonego kodu;
- eksport Rust i runtime są rozwijane w ramach etapu 3;
- GLR i IELR pozostają późniejszymi rozszerzeniami.

Do pracy z samymi algorytmami gramatyk i LR bez formatu `.ag` służy Żbik;
jego instrukcja znajduje się w `../zbik/README.md`.
