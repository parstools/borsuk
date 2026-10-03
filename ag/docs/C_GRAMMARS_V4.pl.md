# Gramatyka C z `grammars-v4` jako punkt wyjścia

[English](C_GRAMMARS_V4.md) | [Polski](C_GRAMMARS_V4.pl.md)

Plik `grammars/c/C.ag` jest mechaniczną konwersją rozdzielonej gramatyki ANTLR4:

- `grammars/c/CParser.g4` — 117 reguł parsera;
- `grammars/c/CLexer.g4` — 181 reguł leksera.

Wynik zawiera wszystkie nazwane reguły źródłowe. Anonimowe grupy parsera zostały wydzielone do nazwanych reguł `inline`. Zanegowany zbiór tokenów z GNU `gnuSingleAttribute` został rozwinięty do jawnej reguły `inline`, ponieważ ograniczony Agas nie ma operatora zanegowanego zbioru po stronie parsera.

`C.ag` przechodzi w całości przez bootstrapowy parser utworzony z
`grammars/Ag.g4`.

## To nie jest czysta gramatyka jednego standardu ISO C

Komentarz źródłowy mówi o C11, ale bieżąca gramatyka zawiera także konstrukcje z późniejszych wersji C oraz rozszerzenia implementacji. Występują w niej między innymi elementy GNU, GCC, Clang, Microsoft/Visual C i Blocks. Są to na przykład `asm`, `__attribute__`, `__extension__`, `__declspec`, typy `__m128`, adres etykiety, wyrażenia instrukcyjne i wbudowane funkcje GCC.

Pliku nie należy więc traktować jako definicji czystego C11, C17 ani C23. Jest szerokim katalogiem istniejących reguł i dobrym materiałem do testowania frontendu Agas oraz generatora LR.

Dla czystego C90 bezpieczniejszym początkiem pozostaje `C90SmallLegacy.ag`. Brakujące elementy powinny być przenoszone świadomie, pojedynczo i z testami. W przyszłości wspólne reguły mogą trafić do plików `.agi`, a różnice standardów i rozszerzenia GNU mogą być wybierane przez zewnętrzny preprocesor Agas.

## Usunięte akcje i predykaty ANTLR

`CParser.g4` zależy od klasy `CParserBase`. Podczas konwersji usunięto:

- 6 akcji w języku docelowym;
- 17 predykatów semantycznych.

Dotyczą one między innymi tablicy symboli, zakresów nazw, rozpoznawania nazw `typedef` oraz rozróżniania deklaracji, instrukcji, rzutowań i wyrażeń. Po ich usunięciu pozostaje szersza gramatyka bezkontekstowa. Może ona mieć konflikty LR albo akceptować ciągi, które parser ANTLR odrzucał na podstawie bieżącej tablicy symboli.

Nie należy ukrywać tego przez przypadkowe wybieranie jednej produkcji. Generator Agas powinien najpierw raportować konflikty. Osobno trzeba zdecydować, które rozróżnienia:

- da się uzyskać przez przepisanie gramatyki;
- wymagają informacji z tablicy symboli;
- powinny być rozstrzygane dopiero po utworzeniu wstępnego drzewa;
- wymagają kontrolowanego mechanizmu predykatów w parserze.

## Charakter AST

Plik ma ustawienia:

```text
ast = concrete;
legacy = true;
```

Każda nazwana reguła parsera źródłowego została na razie zachowana jako `node`. Reguły utworzone mechanicznie z grup są `inline`. Jest to punkt wyjścia bliski CST, a nie zaprojektowane API AST kompilatora.

Po uruchomieniu parsera należy przejrzeć reguły zgodnie z kryteriami opisanymi w `AG_FORMAT.md`: konstrukcje potrzebne analizatorowi semantycznemu pozostawić jako `node`, a delegacje, ogony list i pozostałe rusztowanie zmienić na `inline`.

## Powtarzalna konwersja

Konwersję wykonuje polecenie uruchomione w katalogu `Borsuk`:

```text
python3 tools/antlr4_split_to_agas.py \
    grammars/c/CParser.g4 grammars/c/CLexer.g4 grammars/c/C.ag \
    --grammar-name C
```

Nagłówek `C.ag` zapisuje sumy SHA-256 obu plików źródłowych oraz liczbę usuniętych akcji i predykatów. Pozwala to sprawdzić, czy wynik odpowiada aktualnym kopiom źródeł.
