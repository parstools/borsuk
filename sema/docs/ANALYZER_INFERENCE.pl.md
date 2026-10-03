# Wnioskowanie sygnatur analizatorów

[English](ANALYZER_INFERENCE.md) | [Polski](ANALYZER_INFERENCE.pl.md)

Dokument uzupełnia [model wykonania](../../coge/docs/EXECUTION_MODEL.pl.md).
Podstawowe wnioskowanie jest zaimplementowane w generatorze Rust: deklaracje
`inherited` uruchamiają wyprowadzanie brakujących sygnatur `analyzer`.
Typy atrybutów dziedziczonych pozostają jawne; częściowe adnotacje i polecenie
wydruku sygnatur opisane niżej są dalszymi rozszerzeniami.

## Co wynika z obecnych plików

Przed uproszczeniem `toycp_typed.sema` deklarował 57 analizatorów. Ich parametry wykorzystują
tylko 10 różnych nazw; każda z tych nazw ma jeden, zgodny typ we wszystkich
sygnaturach. Duża część deklaracji powtarza zatem informacje dostępne już
w akcjach i sygnaturach funkcji pomocniczych.

Sama gramatyka określa węzły, etykiety dzieci i krotność (`?`, `*`, `+`).
Nie określa semantycznego wyniku: z samego zapisu `expression` nie wiadomo,
czy analizator zwraca `ExprId`, typ, wartość czy inną strukturę. Bloki `analysis`
i kontrakty wywoływanych funkcji dostarczają brakujących ograniczeń.

## Zalecany krótki zapis

Zamiast powtarzać całe sygnatury, deklarujemy dozwolone atrybuty dziedziczone:

```text
semantic_model {
    rust_context Context;

    inherited scope: ScopeId;
    inherited target: DeclarationTarget;
    inherited owner: StructId;
    inherited visibility: Visibility;
    inherited bodies: Bool;
    inherited parameters: List<ParameterId>;
    inherited enter: Bool;
    inherited left: ExprId;
    inherited base: PlaceId;
    inherited arguments: List<ExprId>;

    // Intrinsic signatures and semantic helper functions follow here.
}
```

To dziesięć wspólnych deklaracji zamiast 57 sygnatur `analyzer`.
`inherited` jest nową deklaracją w `semantic_model`.
Nie zmienia bloków `analysis`, nie wymaga `.agx` i nie oznacza globalnej
zmiennej ani ukrytego, automatycznego przekazywania kontekstu.

Każda reguła otrzymuje atrybuty używane przez jej kod oraz atrybuty jawnie
przekazywane jej przez nazwane argumenty `with`. Zachowuje to również
dotychczasowe parametry celowo nieużywane przez ciało, np. `scope` w
`destructorDeclaration`. Nazwa argumentu musi należeć do deklaracji `inherited`
lub jawnej sygnatury reguły; literówka nie rozszerza samoczynnie interfejsu.
Wywołanie `analyze child with scope: local_scope` nadal jawnie wskazuje
pochodzenie argumentu. Zachowana jest dotychczasowa konwencja generatora:
pominięty argument może pochodzić z dostępnej zmiennej o tej samej nazwie
i typie. Wnioskowanie propaguje takie wymaganie do reguły wywołującej.

Typów przy `inherited` można docelowo nie podawać, jeśli wszystkie ograniczenia
wyznaczają je jednoznacznie. Zalecany pierwszy wariant zachowuje te dziesięć
typów: to mały, czytelny kontrakt, który chroni także przed przypadkową zmianą
znaczenia atrybutu. Nie ma potrzeby zgadywania typu na podstawie jego nazwy.

## Przykład rzeczywistej dedukcji

Obecne akcje `compoundStatement` zawierają:

```text
let block_scope = nested_scope(scope, enter, self.source);
// Child statements are analyzed with block_scope here.
result = finish_block(block_scope, enter, self.source);
```

Z kontraktów funkcji wiadomo:

```text
intrinsic nested_scope(parent: ScopeId, enter: Bool, source: SourceRange)
    -> ScopeId mutates;
intrinsic finish_block(scope: ScopeId, entered: Bool, source: SourceRange)
    -> OpId mutates;
```

Stąd automatycznie powstaje:

```text
analyzer compoundStatement(scope: ScopeId, enter: Bool) -> OpId;
```

Analogicznie `result = copy_text(name.text)` wyznacza wynik `declarator` jako
`OwnedText`, a `result = create_module(scope)` wyznacza wynik `program` jako
`ModuleId`. `if bodies` wymaga `Bool`. Wywołanie
`analyze value ... -> analyzed; result = analyzed;` przenosi typ wyniku
analizowanego dziecka do reguły nadrzędnej.

To wnioskowanie typów przy jawnych kontraktach; nie odgadywanie znaczenia kodu.

## Reguły wnioskowania i diagnostyka

1. Z gramatyki ustalamy regułę każdego dziecka, etykiety AST oraz typy pól
   technicznych, takich jak `.text`, `.source` i `.present`.
2. Każda reguła ma nieznany początkowo typ wyniku i zbiór wymaganych wejść.
   Lokalne `let`, zmienne pętli, dzieci AST i nazwy wbudowane tworzą własne
   zakresy. Nazwa etykiety po `with` nie jest odczytem zmiennej.
3. Wolna nazwa jest wejściem wyłącznie wtedy, gdy została zadeklarowana jako
   `inherited`. Nieznane `scpoe` jest błędem, nie nowym, dorozumianym parametrem.
   Dziecko AST lub zmienna lokalna o tej samej nazwie zasłania deklarację
   atrybutu w danym bloku.
4. Wywołania funkcji, użycia operatorów, przypisania do `result` i wywołania
   `analyze` tworzą ograniczenia typów. `with` łączy parametr dziecka z typem
   konkretnego wyrażenia argumentu, a nie z tak samo nazwaną zmienną rodzica.
5. Wszystkie alternatywy i ścieżki zakończenia jednej reguły muszą uzgodnić typ
   wyniku. Wejścia reguły są sumą wymagań jej alternatyw i nazwanych argumentów
   miejsc wywołania. Niezgodność nie tworzy
   automatycznie `Any`, wspólnego enum ani przeciążenia.
6. Rozwiązujemy ograniczenia również dla wzajemnie odwołujących się reguł.
   Kolejność reguł w pliku nie decyduje o wyniku. Cykle gramatyki nie są cyklami
   bibliotecznych funkcji akcji i nie podlegają zakazowi tych drugich.
7. Obsługa błędów i dzieci opcjonalnych jest częścią ograniczeń: `default` musi
   pasować do wyniku dziecka. Przy dotychczasowej propagacji `Result<T>` typem
   wyniku analizatora jest `T`; nie dokładamy kolejnego `Result` do `analyzer`.
8. Po rozwiązaniu sprawdzamy wszystkie wywołania, brakujące i nadmiarowe
   argumenty oraz brak wyniku na ścieżce. Nie przemilczamy odłączonej reguły
   o nierozstrzygniętym typie tylko dlatego, że nie jest aktualnie wywoływana.

Deklaracje `inherited` ograniczają przypadkowe tworzenie parametrów, ale nie
wykrywają każdego błędu intencji: pomylenie dwóch poprawnych nazw tego samego
typu nadal może być błędem programu. Dla ważnych interfejsów można zachować
jawną sygnaturę jako dodatkowe wymaganie.

## Co nadal musi być zadeklarowane

- Kontrakty zewnętrznych `intrinsic`, ponieważ bez ciała nie można ustalić
  ich rzeczywistej implementacji i ABI na podstawie samych miejsc użycia.
- Definicje nominalnych typów i pól danych: `ScopeId` oraz `StructId` nie są
  wymienne tylko dlatego, że oba mogą być liczbą w Rust.
- Powiązanie z zewnętrznym `Context` (obecnie `rust_context`) lub odpowiednia
  konfiguracja backendu. Akcje same nie ustalą nazwy ręcznej struktury Rust.
- Adnotacje dla przypadków nierozstrzygalnych z istniejących ograniczeń,
  np. pustej listy bez informacji o elemencie lub przekazywania wartości w
  zamkniętym cyklu bez żadnego określonego typu.

Zachowujemy istniejące deklaracje `analyzer` jako opcjonalne ograniczenia.
Docelowo dopuszczamy też częściowe sygnatury, np. tylko wskazanie wyniku albo
typu jednego parametru; obecna gramatyka i generator wymagają jeszcze pełnych
informacji. Jawny typ musi zgadzać się z wywnioskowanym, nie zastępuje błędu
niejawną konwersją.

Typy wejść reguł startowych zwykle wynikają z tych samych ograniczeń.
Nie trzeba osobnej listy wszystkich analizatorów dla generatora: ich nazwy
wynikają z reguł. API hosta nadal musi dostarczyć wymagane argumenty wejściowe
oraz ustalić sposób ich utworzenia, np. początkowy ScopeId.

## Wdrożenie bez zmiany znaczenia istniejącego modelu

Faza wnioskowania działa przed dotychczasową kontrolą i generowaniem Rusta.
Jej wynikiem jest taki sam wewnętrzny zestaw `AnalyzerSignature`, jaki
`codegenContract` buduje z jawnych deklaracji. Następnie działa pełny istniejący
checker akcji, w tym kontroli typów, efektów i poprawności AST.

Z ToyC usunięto 41 deklaracji `analyzer`, pozostawiając 7 `inherited`;
z ToyCP usunięto 57 deklaracji, pozostawiając 10 `inherited`. Wynik generowania
`sema_gen.rs`, `sema_lib_gen.rs` i `interpreter_gen.rs` dla obu plików porównano
bajt po bajcie z wersją sprzed uproszczenia: jest identyczny.

Proponowane dalsze wyjście diagnostyczne to `sema --dump-analyzers`: odtworzy pełne
sygnatury do przeglądu bez wymagania ich przechowywania w pliku źródłowym.
Dla inferowanych sygnatur kolejność parametrów wynika z kolejności `inherited`;
jawne sygnatury zachowują podaną kolejność dla zgodności publicznego API Rust.
Nazwane argumenty `with` zachowują znaczenie niezależnie od kolejności.

Test `sema_analyzer_inference` porównuje kod dla jawnych i wyprowadzonych
sygnatur, uwzględnia rekurencyjne reguły, niejawne przekazywanie wejść,
lokalne zmienne, nieużywane parametry i błędne adnotacje, nazwy oraz typy.
Dotychczasowe testy generowania ToyC/ToyCP sprawdzają także opcjonalne dzieci
i obsługę błędów. Rozszerzenie jest niezależne od projektu wykonawcy IR.
