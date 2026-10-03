# Kontrakty funkcji wywoływanych przez akcje

[English](ACTION_CONTRACTS.md) | [Polski](ACTION_CONTRACTS.pl.md)

Zakres: `toyc.sema.txt` i trzy `toyscope_*.sema.txt`. Z 93 nazw wywołań
40 to konstruktory typów lub IR. Pozostałe 53 nazwy obejmują 47 nazw wywołań
bezpośrednich i 8 nazw metod; `contains_local` oraz `lookup_lexical`
występują w obu postaciach. Tabela poniżej opisuje wszystkie 53 nazwy, a nie
postuluje 53 ręcznych implementacji w runtime.

Oznaczenia: **A** — funkcja możliwa do zapisania w języku `action`, **I** —
operacja pierwotna lub metoda kolekcji dostarczana przez `sema`, **G** —
dyspozytor generowany z `execution_contract`, **E** — definicja już naszkicowana
w plikach `.sema`. `Result<T>` oznacza wartość albo błąd z pozycją źródłową;
`Option<T>` oznacza brak wyniku bez błędu. `Scope`, `Symbol`, `Place`, `Flow`,
`Type`, `Expr`, `Stmt`, `Frame` i `Cell` są typami logicznymi. `Expr` oraz
`Stmt` są opisami IR, nie wartościami wykonywanego programu. `Flow` mapuje
`SymbolId` lub część miejsca na stan inicjalizacji. Operacje analizy nie
odczytują wartości z `Cell`.

Przy przyjętym podziale **41 nazw wymaga nowej definicji `function` w Action**,
2 mają już szkic (`apply_integer`, `direct_declarations`), 9 to nazwy
operacji pierwotnych, a 1 (`evaluate`) powinien wygenerować kompilator.
To rachunek nazw, nie przeciążeń ani liczba wszystkich nowych intrinsic:
implementacja funkcji Action będzie potrzebować również kilku niższych
prymitywów, których żaden z czterech plików jeszcze nie wywołuje wprost.

Przykład `examples/scope_codegen.sema` implementuje już w Action podstawowy
wycinek tej tabeli: `contains_local`, `lookup_lexical`, `contains_name`,
`unique_names`, `bind`, `declare_variable`, `declare_function`,
`declare_incomplete_struct`, `lookup_tag_lexical` i `bind_parameters`.
Przykładowy runtime obsługuje tylko obiekty `int`, a sygnaturę funkcji
reprezentuje kanoniczny napis. Kontrakty poniżej opisują szerszy cel:
parametry typu i miejsca dla `declare_variable`, pełną reprezentację typu
funkcji, dopełnianie struktury i bogatszą diagnostykę trzeba jeszcze dodać.
W tym przykładzie stan inicjalizacji jest już oddzielony od `Symbol` w
kopiowalnym `Flow`; `FlowId` jest przekazywany jawnie do deklaracji,
przypisania i odczytu. `merge_flow` jest już funkcją Action opartą na
intrinsic mapy: łączy dwa osiągalne przepływy, zachowuje tylko wspólne
symbole i wymaga inicjalizacji na obu drogach. Wywołanie jej przez akcję
instrukcji `if` pozostaje kolejnym etapem.

## Nazwy, zakresy i deklaracje

| Nazwa | Kontrakt | Rodzaj |
| --- | --- | --- |
| `contains_local` | `(scope, name, namespace=ordinary) -> Bool`; sprawdza tylko bieżący zakres. Postać `scope.contains_local(name)` ma ten sam kontrakt z domyślną przestrzenią nazw. | A |
| `lookup_lexical` | `(scope, name, namespace=ordinary) -> Option<Symbol>`; idzie po rodzicach od najbliższego, nie pomija symbolu niezainicjalizowanego. Metoda `scope.lookup_lexical(name)` używa domyślnej przestrzeni. | A |
| `lookup_tag_lexical` | `(scope, name) -> Option<StructTag>`; wywołuje `lookup_lexical` dla przestrzeni tagów. | A |
| `contains_name` | `(parameters, name) -> Bool`; porównuje nazwy w już zebranej liście parametrów. | A |
| `unique_names` | `(parameters) -> Bool`; sprawdza brak powtórzeń bez modyfikacji listy. | A |
| `bind` | `scope.bind(name, symbol) -> Unit`; wpisuje symbol do lokalnej mapy i do `scope.symbols`; odrzuca duplikat w tej przestrzeni. | A |
| `declare_variable` | `(scope, id, type, target) -> Result<Symbol>`; tworzy nowe `SymbolId`, miejsce lub pole, sprawdza lokalny konflikt; globalny obiekt bez inicjalizatora dostaje zero, lokalny stan `Uninitialized`. | A |
| `declare_function` | `(scope, header, kind) -> Result<FunctionSymbol>`; scala zgodne prototypy, odrzuca inną sygnaturę i drugą definicję. | A |
| `declare_incomplete_struct` | `(scope, name) -> Result<StructTag>`; rezerwuje tag przed analizą pól, z `complete=false`. | A |
| `bind_parameters` | `(scope, parameters, state) -> Unit`; tworzy symbole parametrów w zakresie funkcji i ustawia ich stan początkowy. | A |
| `lookup_field` | `(struct_type, field_name) -> Option<Field>`; szuka wyłącznie pola zadanej kompletnej struktury. | A |
| `layout_struct` | `(fields) -> Result<Layout>`; oblicza offsety, wyrównanie i rozmiar według wybranej ABI; wymaga kompletnych typów pól. | A |
| `complete` | `(type) -> Bool`; sprawdza, czy definicja typu jest już kompletna. | A |
| `complete_object_type` | `(type) -> Bool`; wymaga typu obiektu innego niż `void` i kompletnego typu elementu/pól. | A |

`declare_*` może korzystać z niewidocznego w 93 nazwach intrinsic
`fresh_symbol_id()`; `layout_struct` z parametrów ABI (rozmiar i wyrównanie
typów bazowych). To operacje na modelu kompilatora, bez dostępu do plików.

## Przepływ inicjalizacji i kontrola programu

| Nazwa | Kontrakt | Rodzaj |
| --- | --- | --- |
| `copy` | `(flow) -> Flow`; niezależna migawka mapy stanów dla gałęzi. Kopia trwała z copy-on-write też spełnia kontrakt. | I |
| `parameter_flow` | `(scope) -> Flow`; stan `Initialized` dla wszystkich parametrów przy wejściu do funkcji. | A |
| `hide_locals` | `(flow, scope) -> Flow`; usuwa stany symboli należących do zakresu przy jego opuszczeniu. | A |
| `merge_flow` | `(flow_a, flow_b) -> Flow`; ignoruje drogi nieosiągalne, a `Initialized` zachowuje tylko przy obu osiągalnych drogach z tym stanem. | A |
| `mark_initialized` | `(flow, place) -> Unit`; aktualizuje dokładnie zapisane miejsce, pole lub całą strukturę przy pełnym zapisie. | A |
| `read` | `(place, flow) -> Result<Expr>`; w fazie analizy odrzuca `Uninitialized` i `Initializing`, inaczej buduje IR odczytu powiązany z `SymbolId`. Nie czyta `Cell`. | A |
| `always_returns` | `(stmt) -> Bool`; dla wszystkich osiągalnych dróg sprawdza zakończenie przez `return`; może działać przez `rewrite bottom_up` zamiast rekurencji funkcji. | A |
| `contains_error` | `(module_or_program) -> Bool`; sprawdza zebrane diagnostyki i znaczniki `ErrorOperation`. | A |
| `direct_declarations` | `(items) -> List<DeclarationNode>`; wybiera wyłącznie bezpośrednie deklaracje zakresu, bez schodzenia do bloków. Istnieje jako `query` w ToyScope3. | E |
| `remove_all` | `flow.remove_all(symbols) -> Unit`; usuwa stany podanych symboli z mapy bieżącej gałęzi. | A |

`read` i `mark_initialized` są operacjami **analizy**. W ToyScope3 kontrola
niezainicjalizowanego odczytu przenosi się do wykonania i używa innej funkcji
`read_initialized`; tych nazw nie należy scalać.

## Typy i budowanie IR

| Nazwa | Kontrakt | Rodzaj |
| --- | --- | --- |
| `integer` | `(type) -> Bool`; czy typ jest całkowity według ToyC. | A |
| `numeric` | `(type) -> Bool`; czy typ dopuszcza operacje liczbowe, w tym `float`. | A |
| `assignable` | `(place) -> Bool`; czy miejsce jest zapisywalnym lvalue, a nie tablicą lub niemodyfikowalnym obiektem. | A |
| `promote_numeric` | `(expr) -> Result<Expr>`; zapisuje promocję liczbową w IR i ustala typ wyniku. | A |
| `convert` | `(expr, target_type) -> Result<Expr>`; stosuje dozwoloną konwersję ToyC i zapisuje ją jawnie w IR; nie oblicza wartości. | A |
| `convert_arguments` | `(arguments, parameters) -> Result<List<Expr>>`; sprawdza liczbę i kolejność, wywołuje `convert` dla każdej pary. | A |
| `comparable` | `(left, right, operator) -> Result<ComparisonPlan>`; wybiera wspólny typ i konwersje operandów. | A |
| `type_binary` | `(operator, left, right) -> Result<Expr>`; sprawdza typy, wykonuje promocje, buduje IR operacji i potrzebne kontrole, np. dzielnika. | A |
| `compound_value` | `(operator, old, rhs, target_type) -> Result<Expr>`; oblicza typ i konwersję IR dla `+=` itd. przez `type_binary` i `convert`. | A |
| `to_bool` | `(expr) -> Result<Expr>`; buduje konwersję warunku do `bool`. | A |
| `parse_integer` | `(text, radix) -> Result<MathematicalInteger>`; czyta literal bez zależności od locale; zakres `int` ToyC sprawdza osobny etap. | I |
| `parse_float` | `(text) -> Result<Float>`; czyta literal z kontrolą formatu i zakresu, niezależnie od locale. | I |
| `decode_char` | `(literal_text) -> Result<Char>`; interpretuje escape i wymaga jednego znaku. | I |
| `decode_string` | `(literal_text) -> Result<Text>`; interpretuje escape w literałach napisowych. | I |

`complete` i `always_returns` mogą wymagać przejścia po zagnieżdżonym
drzewie; istniejący `rewrite bottom_up` albo skończona kolekcja węzłów
pozwalają to zrobić bez cyklu między funkcjami pomocniczymi.

## Wykonanie ToyScope na komórkach

| Nazwa | Kontrakt | Rodzaj |
| --- | --- | --- |
| `create_frame` | `(scope, symbols, initial_state, parent) -> Frame`; tworzy komórkę dla każdego `SymbolId`, wiąże ramkę z instancją zakresu i ustawia bieżącą ramkę. Domyślny stan zależy od polityki. | A |
| `release_frame` | `(frame) -> Unit`; usuwa instancję z aktywnego stosu i przywraca rodzica; musi wykonać się także po błędzie. | A |
| `lexical_frame` | `runtime.lexical_frame(scope_id) -> Option<Frame>`; zwraca aktywną ramkę wskazanego leksykalnego zakresu. | A |
| `place_of` | `runtime.place_of(symbol_id) -> Result<Cell>`; używa wiązania `SymbolId` i aktywnej ramki jego zakresu, nie szuka napisu nazwy. | A |
| `leave_uninitialized` | `(cell) -> Unit`; oznacza komórkę jako `Uninitialized`, bez wpisywania wartości. | A |
| `initialize` | `(cell, value) -> Unit`; zapis inicjalizatora; w polityce 3 dopuszcza stan `Reserved`, po sukcesie ustawia `Initialized`. | A |
| `read_initialized` | `(cell) -> Result<Value>`; zwraca wartość tylko przy `Initialized`, inaczej błąd wykonania z pozycją źródłową. | A |
| `write` | `(cell, value) -> Result<Unit>`; zwykły zapis do dozwolonej komórki, po sukcesie ustawia `Initialized`; polityka 3 rozróżnia go od `initialize`. | A |
| `execute_sequence` | `(items) -> Result<Unit>`; wykonuje instrukcje IR w kolejności, kończy na błędzie. Wywołuje dyspozytor pojedynczej instrukcji dostarczony przez kompilator. | A |
| `evaluate` | `(expr) -> Result<Value>`; dyspozytor wariantów IR wygenerowany z `execution_contract` i lokalnych `execution result`; może schodzić po drzewie wyrażenia. | G |
| `apply_integer` | `(operator, a[, b]) -> Result<MathematicalInteger>`; dodawanie, odejmowanie, mnożenie, dzielenie i reszta z kontrolą zera; szkic przeciążeń już jest w ToyScope. | E |
| `truncate_toward_zero` | `(quotient) -> MathematicalInteger`; obcina iloraz ku zeru. Przy implementacji trzeba ustalić dokładną semantykę `/` dla ujemnych liczb; bezpieczniejszą operacją pierwotną może być `div_trunc(a,b)`. | I |

`evaluate` jest szczególnym punktem: wykonanie drzewa wyrażeń naturalnie
rekuruje po danych. Zakaz cykli dotyczy grafu **funkcji pomocniczych**, a nie
wewnętrznego dyspozytora generowanego z wariantów IR. Przed wykonaniem
program musi być poprawny według wybranej polityki analizy.

## Kolekcje i operacje na strukturach

| Nazwa | Kontrakt | Rodzaj |
| --- | --- | --- |
| `append` | `list.append(value) -> Unit`; dopisuje element na końcu, zachowując kolejność źródłową. | I |
| `remove` | `list.remove(value) -> Bool`; usuwa wskazaną kontrolę lub element według tożsamości/strukturalnej równości ustalonej przez typ listy. | I |
| `length` | `(list) -> Int`; liczba elementów, bez iterowania po semantyce dzieci. | I |

Nie występuje żaden odczyt pliku. Końcowe intrinsic są operacjami na
kontenerach, identyfikatorach, komórkach, liczbach i tekstach literałów.
Akcje mogą modyfikować pola modelu i iterować po skończonych kolekcjach;
kompilator `sema` musi jeszcze nadać tym działaniom typy i skutki. Graf
wywołań funkcji **A** powinien być acykliczny; kierunek warstw to:
akcja alternatywy → pomocnik wyższego poziomu → pomocnik niższego poziomu →
intrinsic, konstruktor lub dyspozytor **G**. Obecny checker wykrywa cykle
między nazwanymi `function`/`query`, ale nie sprawdza jeszcze wszystkich
sygnatur, przeciążeń i skutków.

## Kierunek generowania `sema_gen.rs`

Rust jest naturalnym celem, gdy wygenerowany analizator ma być modułem
kompilatora języka. Generator nie powinien przenosić dowolnego grafu
referencji z Action na `&mut` i lifetimes. Model docelowy jest następujący:

1. Jeden `SemaContext` posiada tabele `Vec<Scope>`, `Vec<Symbol>`, `Vec<Expr>`
   oraz diagnostyki. Powiązania między obiektami mają typowane, kopiowalne
   identyfikatory `ScopeId`, `SymbolId`, `ExprId`, `NodeId`.
2. Funkcje Action otrzymują `&mut SemaContext` i identyfikatory przez wartość;
   zwracają wartość albo identyfikator, nigdy referencję do wnętrza kontekstu.
   `lookup_lexical` zwraca `Option<SymbolId>`, a nie `&Symbol`.
3. Odczyt pola i mutacja są oddzielnymi krokami. Generator kończy krótkie
   pożyczenie przy odczycie, zanim wywoła następną funkcję z `&mut ctx`.
   Jeśli potrzebuje obu operandów, kopiuje `Copy`-ID lub mały opis typu.
4. `Flow` dla gałęzi jest wartością posiadaną przez gałąź (klon/persistent
   map), a wyniki łączy `merge_flow`. Ramki wykonania są indeksowane
   identyfikatorami; `finally` wymusza ich zwolnienie także przy błędzie.
5. Generator emituje kod tylko dla Action o rozstrzygniętych typach i skutkach;
   intrinsic są jawnie zadeklarowanymi metodami warstwy runtime. Wynik
   przechodzi `cargo check` i testy zachowania przed użyciem w kompilatorze.

Przykładowy kształt interfejsu, nie gotowy wynik generatora:

```rust
trait SemaOps {
    fn lookup_lexical(
        ctx: &SemaContext,
        start: ScopeId,
        name: NameId,
    ) -> Option<SymbolId>;

    fn declare_variable(
        ctx: &mut SemaContext,
        scope: ScopeId,
        name: NameId,
        ty: TypeId,
    ) -> Result<SymbolId, Diagnostic>;
}
```

Nie ma potrzeby domyślnie używać `Rc<RefCell<_>>`: ukrywałoby to problem
aliasowania i przesuwało część błędów na czas wykonania. Gdy pojedynczy
przypadek wymaga współdzielonego posiadania, powinien wynikać z kontraktu
konkretnego typu, nie z ogólnej strategii generatora. Podstawy ograniczeń
pożyczania opisuje [The Rust Programming Language: References and Borrowing](https://doc.rust-lang.org/book/ch04-02-references-and-borrowing.html),
a konsekwencje mutowalności wewnętrznej [ten sam podręcznik](https://doc.rust-lang.org/book/ch15-05-interior-mutability.html).
