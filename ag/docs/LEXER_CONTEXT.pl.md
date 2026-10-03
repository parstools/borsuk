# Kontekst lexera w artefakcie

[English](LEXER_CONTEXT.md) | [Polski](LEXER_CONTEXT.pl.md)

`agas --emit-package OUTPUT grammar.ag` obsługuje gramatyki z `lexerClasses`.
Manifest pozostaje w wersji 1; deskryptor sekcji `lexer` oraz `lexer.json`
mają **wersję 2**. Pozostałe sekcje zachowują wersję 1. Pakiety bez klas
są nadal eksportowane w dotychczasowej postaci; reprodukcja przypiętego
artefaktu Agas pozostaje identyczna bajtowo. Starszy loader, który zna
wyłącznie lexer v1, musi odrzucić wymaganą sekcję v2.

## Dane

Lexer v2 zawiera dotychczasowe reguły i automaty oraz obowiązkowe `context`:

- `requiredClasses`: maska `uint64` dla każdej reguły lexera; wszystkie
  wskazane bity muszą być aktywne. Reguły bez wymagań mają maskę zero.
- `originalTerminals`: mapowanie każdego ID terminala na jego źródłowy
  odpowiednik w katalogu symboli tego pakietu. Terminale źródłowe mapują
  się na siebie; parser używa osobnych terminali specjalizowanych.
- `rows`: po jednym drzewie prefiksów podglądu na stan LR. Korzeń ma indeks
  zero. Węzeł zawiera `active` (maskę `uint64`) i `edges`. Krawędź zawiera
  `terminal` (ID specjalizowane albo `null` oznaczające EOF) oraz `target`
  (indeks węzła w tym drzewie). Maska węzła służy do odczytu **następnego**
  tokenu po prefiksie prowadzącym do tego węzła.

Drzewa powstają z nieskompresowanej tablicy ACTION: zachowują także prefiksy
usunięte później przez kompresję redukcji domyślnych. Ich głębokość nie
przekracza `lookahead`; EOF kończy ścieżkę. Nie ma tabeli wszystkich `2^64`
kombinacji klas. Eksport obejmuje osiągalne konteksty i ich prefiksy.

`orderedNfas` zawiera automat każdej reguły, także gdy nie występują
operatory z priorytetem. Runtime filtruje reguły według maski, zachowuje
najdłuższe dopasowanie i kolejność reguł, a wewnątrz reguły respektuje
priorytety, m.in. niechciwe powtórzenia. Bezwarunkowe `skip` i kanały
ukryte działają również pomiędzy tokenami podglądu. `require` przy `skip`
lub `channel` pozostaje niedozwolone.

## Wykonanie i AST

Pierwszy przebieg LR wybiera kontekst, tokenizuje źródło i utrwala strumień
terminali specjalizowanych. Redukcje nie zużywają podglądu; przy zmianie
stanu runtime sprawdza, czy nadal otrzymuje te same tokeny źródłowe i zakresy.
Drugi przebieg wykonuje dotychczasowy program redukcji AST i zbiera pokrycie.
Tokeny w końcowym AST wracają do ID źródłowych; zakresy bajtowe i znakowe
nie zmieniają się. Produkcje specjalizowane mają odrębne ID, lecz zachowują
pochodzenie, etykiety i indeksy alternatyw. Coverage agreguje je według
oryginalnych alternatyw i wystąpień kwantyfikatorów.

W C++ używa się `ArtifactAstParser::parse(input, lexer)`; samodzielne
`lexer.tokenize(input)` odrzuca lexer kontekstowy bez tablicy parsera.
W Rust `LoadedPackage::parse` i `tokenize` korzystają z planu automatycznie.
Tokenizacja kontekstowa wymaga poprawnego prefiksu składniowego: przy błędzie
składni może zwrócić tylko prefiks do miejsca błędu. `parse` zgłasza odrzucenie.
Limity kroków i stosu obowiązują w każdym przebiegu osobno.

Oba loadery sprawdzają rozmiary, ID i mapowania, acykliczność drzew,
głębokość, EOF, aktywność wymaganych reguł i zgodność z tablicą parsera.
Pozostają też dotychczasowe kontrole wersji, długości sekcji i SHA-256.

## Odtworzenie testów

Z katalogu głównego repozytorium, po zbudowaniu `agas` i `agas_ast_wire`:

```sh
python3 ag/tools/check_contextual_artifacts.py
python3 vist/grammar/compare_shift_variants.py
python3 vist/grammar/test_examples.py
```

Pierwszy test porównuje pełny AST wire C++ i Rust dla małych gramatyk
LR/LALR z k=1/2, czterech kombinacji klas, zagnieżdżeń, bitu 63, kanału
komentarzy i Unicode. Oba loadery odrzucają cztery celowo uszkodzone plany,
mimo ponownie obliczonych poprawnych długości i SHA-256 sekcji.
