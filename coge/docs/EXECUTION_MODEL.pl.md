# Projekt modelu wykonania dla generowanego interpretera

[English](EXECUTION_MODEL.md) | [Polski](EXECUTION_MODEL.pl.md)

Stan odniesienia projektu: commit `8b66f6b`. Dokument opisuje docelowy
kontrakt, nie w całości wdrożoną składnię. `Sema.ag` rozpoznaje już
`execution_model`, a generator tworzy typy, stan `Interpreter` oraz metody ToyC z ciał
`function`. Przykłady pełnego modelu pamięci i ramek poniżej nadal wykraczają
poza obecny podzbiór generatora. Rozszerzenie pliku pozostaje `.sema`.

## Decyzja i mierzalny cel

Dodajemy `execution_model`: typowany model danych wykonawcy i funkcje napisane
w tym samym języku akcji, którego używają `analysis` i `semantic_model`.
`execution_contract` pozostaje mapowaniem wariantów IR na te funkcje.
Generator wytwarza typy, pamięć, ramki, wywołania, arytmetykę wartości,
konwersje, błędy i sterowanie wykonaniem do `interpreter_gen.rs`.

Nie wystarczy przenieść wywołań `load`, `store`, `call`, `convert` i `binary`
do wygenerowanego dispatchera: **treści tych funkcji również muszą pochodzić
z `.sema`**. Nie przenosimy ich jako gotowego, specyficznego dla ToyC szablonu
Rust do generatora C++.

W punkcie odniesienia `interpreter.rs` miał 656 linii, z czego testy zaczynały się w linii 541;
`interpreter_gen.rs` ma 128 linii. Docelowe kryterium odbioru:

- co najmniej 80% wskazanych niżej obowiązków wykonawcy jest zdefiniowane w `.sema`;
- wygenerowany kod wykonawcy jest większy od całego ręcznego kodu wykonawcy,
  wliczając adapter starego IR i używane wspólne intrinsic;
- do tego porównania nie wliczamy testów, komentarzy, pustych linii, parsera,
  analizy semantycznej ani wygenerowanych tablic;
- orientacyjny budżet to 600–900 linii wygenerowanych i 100–180 ręcznych;
  to oszacowanie do sprawdzenia po implementacji, nie obecny wynik.

Po migracji deklaracji, wywołań, kontroli typów wartości i dostępu do pamięci
ToyC ma 886 niepustych linii generowanych, 135 ręcznych linii adaptera
i 150 ręcznych linii wspólnego runtime. Liczby nie obejmują komentarzy i testów;
do porównania wliczono cały runtime, także pomocnicze funkcje kolekcji.

Z `.sema` powstają już `value_for_type`, `value_matches_type`, `declare`,
`load`, `store`, `call`, `evaluate_call`, kontrola błędów i konwersje.
Runtime przechowuje wartości, realizuje projekcje i podstawowe operacje
skalarne. Ręcznie pozostały publiczne wejścia API, kontrola poprawności
modułu, adapter `Context` oraz implementacja `ValueTree`.

Ramka ToyC jest zdejmowana po sukcesie i błędzie przez `capture` i jawny
`pop` zapisane w `.sema`; test obejmuje błąd zagnieżdżonego wywołania oraz
kolejne poprawne wywołanie. Pełne `with/finally`, checker typów wykonawcy,
adresy z kontrolą życia i kompletny wykonawca ToyCP pozostają do wdrożenia.
Obecny adapter zwraca własne kopie widoków IR, więc migracja zachowuje
prostotę modelu własności kosztem dodatkowego kopiowania.

## Umiejscowienie w pliku i odpowiedzialności

```text
sema ToyCTyped for "toyc.ag";
semantic_model { ... }
execution_model { ... }
execution_contract { ... }
options { generate_interpreter = true; }
node program : ... analysis { ... } ;
```

`semantic_model` opisuje analizę i budowę IR; `execution_model` opisuje stan
i algorytmy wykonania gotowego IR. Instancja wykonawcy ma niezmienne wejście `ir`
i zmienny stan `runtime`. Nie używa słownika nazw analizatora do wyszukiwania
zmiennych podczas wykonania.

Trzy nowe konstrukcje to `execution_model`, `input` i `runtime_state`.
Wykorzystujemy istniejące `record`, `enum`, `type`, `function`, `intrinsic`,
`mutates`, `if`, `foreach`, `while`, `match`, `with` i `finally`.
`execute` oraz `evaluate` stają się typowanymi punktami wejścia, których
sygnatury są podane w modelu; nazwy `Operation`, `ExpressionKind`, `Control`
nie powinny być wpisane na stałe w generatorze.

Kontrakt otrzymuje jawne sygnatury dispatchera, odróżnialne składniowo od
handlerów przez brak nazwy wariantu po `execute`/`evaluate`:

```text
execution_contract {
    execute(kind: Operation, source: SourceRange)
        -> Result<Control, RuntimeError> mutates;
    evaluate(kind: ExpressionKind, expression: ExprId,
             checks: List<RuntimeCheck>, source: SourceRange)
        -> Result<Value, RuntimeError> mutates;

    execute Declare(symbol) {
        declare(symbol, source);
        return Control.Continue;
    }
    evaluate Load(place) { return load(place, source); }
    // The remaining variants must be covered as well.
}
```

Sygnatury definiują generowane funkcje `dispatch_execute` i `dispatch_evaluate`,
dostępne w funkcjach modelu. Pierwszy parametr jest dopasowywany do wariantu;
pozostałe są kontekstem handlera. Typowany checker zna te funkcje bez ręcznej
implementacji. Wrapper `execute(OpId)` czyta węzeł przez `ir.operation`,
sprawdza limit i wywołuje dispatcher; analogicznie działa `evaluate(ExprId)`.
Dla starych plików bez sygnatur pozostaje dotychczasowy tryb zgodności.
W nowym trybie każdy handler jawnie zwraca wynik zgodny z sygnaturą;
generator nie dopisuje na stałe `Control::Continue`. Obie sygnatury dopuszczają
`mutates`, ponieważ również ewaluacja wyrażenia może wywołać funkcję zmieniającą
stan programu. Czyste funkcje pomocnicze zachowują osobny, sprawdzany efekt.

Przykładowy szkielet nowego bloku:

```text
execution_model {
    input ir {
        module() -> Option<ModuleView>;
        operation(id: OpId) -> Option<OperationNode>;
        expression(id: ExprId) -> Option<Expression>;
        place(id: PlaceId) -> Option<Place>;
        variable(id: SymbolId) -> Option<VariableView>;
        function(id: FunctionId) -> Option<FunctionView>;
        field(id: FieldId) -> Option<FieldView>;
        aggregate(id: StructId) -> Option<AggregateView>;
    }

    runtime_state runtime: Runtime;
    // Record, enum, intrinsic and function declarations follow here.
}
```

`input` generuje typowany interfejs Rust. Osiem metod to odczyty gotowych
struktur, zwracające posiadane opisy/snapshoty. Brak identyfikatora daje `None`,
a kod z `.sema` zamienia go w zlokalizowany `InvalidIr`. Nie ma indeksowania
niezweryfikowanym ID kończącego się paniką.

Na początku potrzebny jest niewielki `impl IrInput for Context`, ponieważ
obecne rekordy IR są napisane ręcznie w `model.rs`. Nie powinien on zawierać
arytmetyki, wykonywania instrukcji, alokacji ani wyszukiwania dynamicznego.
Gdy deklaracje IR również będą źródłowo opisane w `.sema`, generator może
wytworzyć ten adapter. Nie uzależniamy pierwszego etapu od przeniesienia całego
analizatora i `Context` do nowego modelu.

Wejściowe typy także muszą mieć schemat dostępny checkerowi. Wewnątrz `input`
dopuszczamy deklaracje `enum`/`record` opisujące zewnętrzne typy IR; generator
nie emituje ich ponownie. Dla migracji ToyC opcja
`execution_input_module = "crate::model"` wiąże je z istniejącymi typami Rust.
Przykładowo deklarujemy tam pełne warianty `Type`, `Operation`, `ExpressionKind`
i pola `Place`. Różnicę między polami nazwanymi a wariantem krotkowym zapisujemy
w schemacie. Zmiana hostowego typu powodująca niezgodność musi zostać wykryta
przy kompilacji adaptera. Docelowo jedna deklaracja IR może generować także
hostowy typ, co usuwa dublowanie schematu.

Minimalna zawartość opisów używanych przez wykonawcę:

| Opis | Wymagane dane |
| --- | --- |
| ModuleView | poprawność, źródło pierwszej diagnostyki, zakres źródłowy, uporządkowane deklaracje globalne, mapa eksportów |
| OperationNode / Expression | pełny wariant IR i zakres źródłowy; dodatkowo typ i kontrole runtime wyrażenia |
| Place | SymbolId korzenia, uporządkowane projekcje, typ końcowy |
| VariableView | typ, właściciel/globalność, PlaceId, zakres źródłowy |
| FunctionView | wynik, jawne ParameterBinding, opcjonalne ciało, zakres źródłowy |
| FieldView | właściciel, ordinal, typ |
| AggregateView | tożsamość typu, uporządkowane FieldId; w ToyCP również podobiekt bazy |

Opisy `*View`, które nie istnieją w starym IR, są zwykłymi rekordami generowanymi
z `execution_model`. Adapter składa je z istniejących pól. Typów ID nie trzeba
rozpakowywać w języku akcji: są nieprzezroczyste, porównywalne i mogą być kluczami
map; indeksowanie wektorów starego `Context` pozostaje wewnątrz ośmiu metod
adaptera. `StorageId` wykonawcy jest osobnym, generowanym rekordem.

## Typy i wartości

Potrzebne rozszerzenia systemu typów:

- warianty `enum` z polami, np. `Int(I32)`;
- `List<T>`, `Map<K,V>`, `Option<T>`, `Result<T,E>` i zwykłe rekordy;
- jawne typy skalarne `I32`, `F32`, `U8`, `U64`, `Index`;
- dopasowanie wariantów, krotek i rekordów, z blokiem instrukcji jako ciałem gałęzi;
- dostęp do pól, przypisania i typowane operacje na kolekcjach;
- kwalifikowane nazwy konstruktorów, np. `Value.Int`, `Type.Int`.

Obecne `Int` języka akcji mapuje się na `i64`. Nie używamy go jako milczącego
odpowiednika ToyC `int`, który obecnie jest `i32`. Parametr `Result<T>` używany
przez istniejącą analizę zachowuje dotychczasowy domyślny błąd; wykonawca używa
jawnego `Result<T, RuntimeError>`.

Przykładowe deklaracje (wycinek, nie cały model):

```text
type LiteralId = Index;

enum Value {
    Int(I32), Float(F32), Bool(Bool), Char(U8),
    Pointer(Option<LiteralId>), Struct(List<Value>), Array(List<Value>),
    Void, Uninitialized
}
record TypedValue(type: Type, value: Value);
enum Control { Continue, Return(TypedValue) }

record StorageId(index: Index, generation: U64);
enum Projection { Field(Index), Element(Index), Base(Index) }
record Address(storage: StorageId, path: List<Projection>);
enum Argument { ByValue(TypedValue), ByReference(Address) }

enum Lifetime { Allocated, Constructing, Live, Destroying, Dead }
record Allocation(generation: U64, type: Type, value: Value,
                  lifetime: Lifetime, writable: Bool,
                  subobjects: Map<List<Projection>, Lifetime>);
record Frame(function: FunctionId, bindings: Map<SymbolId, Address>,
             scopes: List<ExecutionScope>);
record Cleanup(function: FunctionId, receiver: Address, source: SourceRange);
record ExecutionScope(scope: ScopeId, owned: List<StorageId>,
                      cleanups: List<Cleanup>);
record Limits(steps_remaining: U64, call_depth: Index, evaluation_depth: Index);
record Runtime(globals: Map<SymbolId, Address>, frames: List<Frame>,
               allocations: List<Allocation>, free_slots: List<Index>,
               global_cleanups: List<Cleanup>, evaluation_depth: Index,
               limits: Limits);
```

`TypedValue.type` zachowuje nominalny typ struktury, którego sama lista pól
`Value.Struct` nie wyraża. Pola i parametry są identyfikowane przez stabilne ID.
`FunctionView` musi zawierać jawną listę `ParameterBinding(symbol, type, mode)`;
nie polegamy docelowo na tym, że pierwsze N symboli zakresu przypadkiem oznacza
parametry. Obecny adapter może odtworzyć i zweryfikować tę konwencję, dopóki IR
nie dostanie jawnej listy.

Zachowujemy kształt publicznego `Value` ToyC: `Pointer(Option<LiteralId>)`
oznacza obecny wskaźnik literału, z tożsamością opartą na ID wyrażenia,
nie adres hosta. Nie dodajemy dereferencji ani arytmetyki wskaźników do ToyC.
`ByReference(Address)` jest osobnym argumentem wewnętrznego ABI dla ToyCP `self`.
Nie udajemy, że `self` jest `char*` lub kopią `Value.Struct`.

Odczyt/skopiowanie `Value` ma semantykę wartości: agregaty kopiują zawartość,
a adresy zachowują tożsamość wskazywanego miejsca. Parametr przez wartość
otrzymuje własną alokację, parametr `self` zachowuje adres odbiorcy.
Uchwyt `Address` nie jest pożyczką Rust i może bezpiecznie przetrwać wywołanie
innej funkcji języka interpretowanego.

## Pamięć: algorytm także jest generowany

Pamięć jest listą rekordów `Allocation` w `Runtime`, a nie ukrytą biblioteką
z funkcjami wysokiego poziomu `load`, `store`, `construct`.
Z `.sema` generujemy:

1. `allocate`: użyj wolnego slotu lub dopisz rekord, zwróć `StorageId`;
2. `release`: oznacz slot jako martwy i unieważnij generację; slot z wyczerpaną
   generacją nie jest ponownie używany (nie zawijamy numeru);
3. `resolve_symbol`: wybierz wiązanie w bieżącej ramce albo w globalach;
4. `resolve_place`: do adresu korzenia dopisz jawne projekcje IR;
5. `read_address`: sprawdź generację, czas życia, projekcje i inicjalizację;
6. `write_address`: sprawdź adres i typ, podmień wskazany fragment wartości;
7. `default_value`: zbuduj strukturę wartości z zerami lub niezainicjalizowanymi
   liśćmi, zależnie od rodzaju deklaracji;
8. wejście/wyjście z zakresu: przechowuj listę jego alokacji i zwolnij je na końcu.

W pierwszej implementacji `write_address` może kopiować korzeń i odtworzyć
ścieżkę pól. To prosty, poprawny wariant bez długotrwałych pożyczek Rust.
Optymalizacja do pojedynczego krótkiego `&mut` jest zadaniem generatora,
bez zmiany semantyki `.sema`.

Przykładowa treść funkcji, a nie deklaracja kolejnego intrinsic:

```text
function load(place: PlaceId, source: SourceRange)
    -> Result<Value, RuntimeError> mutates {
    let address = resolve_place(place, source);
    let value = read_address(address, source);
    require fully_initialized(value)
        else runtime_error ErrorKind.UninitializedRead at source;
    return value;
}

function store(place: PlaceId, value: Value, source: SourceRange)
    -> Result<Unit, RuntimeError> mutates {
    let address = resolve_place(place, source);
    write_address(address, value, source);
    return unit;
}

function write_address(address: Address, value: Value, source: SourceRange)
    -> Result<Unit, RuntimeError> mutates {
    let slot = checked_allocation(address.storage, source);
    require slot.writable else runtime_error ErrorKind.ReadOnly at source;
    let target = projected_type(slot.type, address.path, source);
    require value_matches_type(value, target)
        else runtime_error ErrorKind.InvalidStoreType at source;
    let updated = replace_path(slot.value, address.path, value, source);
    runtime.allocations[address.storage.index].value = updated;
    return unit;
}
```

`checked_allocation`, `projected_type`, `fully_initialized`, `replace_path`
i `value_matches_type` także są zwykłymi funkcjami modelu, z dopasowaniem
wariantów i pętlami. Ich nazwy nie tworzą nowego ręcznego API.
Odczyt całego agregatu sprawdza jego liście; zapis pojedynczego pola nie wymaga
zainicjalizowania sąsiadów. Odczyt pola wymaga inicjalizacji tylko tego pola.
`load` dopuszcza efekt `mutates`, ponieważ ustalanie adresu w ToyCP może
obliczać wyrażenie indeksu; samo `read_address` jest czystym odczytem.

Na przykład samo zejście do wartości i zastąpienie pola jest wyrażalne
bez ręcznej funkcji poruszającej się po pamięci:

```text
function replace_path(root: Value, path: List<Projection>, replacement: Value,
                      source: SourceRange) -> Result<Value, RuntimeError> {
    if list_empty(path) { return replacement; }
    let head = list_first(path);
    let tail = list_tail(path);
    match (root, head) {
        (Value.Struct(fields), Projection.Field(index)) => {
            require index < list_len(fields)
                else runtime_error ErrorKind.InvalidAddress at source;
            let changed = replace_path(fields[index], tail, replacement, source);
            fields[index] = changed;
            return Value.Struct(fields);
        }
        (Value.Array(elements), Projection.Element(index)) => {
            require index < list_len(elements)
                else runtime_error ErrorKind.IndexOutOfBounds at source;
            let changed = replace_path(elements[index], tail, replacement, source);
            elements[index] = changed;
            return Value.Array(elements);
        }
        _ => runtime_error ErrorKind.InvalidAddress at source;
    }
}
```

Ten wycinek dotyczy układu ToyC; ToyCP dodaje jawny wariant/projekcję podobiektu
bazy. W proponowanym układzie ToyCP podobiekt bazy jest oddzielnym zagnieżdżonym
`Value.Struct` w zarezerwowanym elemencie listy. `AggregateView` podaje jego
indeks oraz indeksy własnych pól; dojście do odziedziczonego pola zaczyna się
od projekcji `Base`. Nie spłaszczamy pól i nie gubimy tożsamości podobiektu.
Modyfikacja lokalnej kopii `fields` nie jest efektem `mutates` na stanie
wykonawcy. Przy dużych ścieżkach generator może zastąpić tę rekurencję pętlą;
walidacja IR i limit głębokości obejmują także przechodzenie przez agregaty.

Wyszukiwanie w **ramkach wywołujących jest zabronione** dla ToyC/ToyCP.
Obecne `root_value` przegląda wszystkie ramki od końca; nie należy utrwalać
tego jako modelu języka. Powtarzające się przy rekurencji `SymbolId` wybiera
alokację bieżącego wywołania. Dostęp do obiektu wywołującego odbywa się przez
jawny `Address`, np. przekazany jako `self`. Błędny IR z brakującym lokalnym
wiązaniem ma zgłosić błąd, a nie znaleźć starszą instancję zmiennej.

ToyC nie ma indeksowania w gramatyce, choć potrafi przechowywać tablice.
`Element` jest potrzebny dla ToyCP; wyrażenie indeksu obliczamy raz przy
ustalaniu konkretnego adresu i kontrolujemy granice. Operacja złożonego
przypisania musi użyć tego samego ustalonego adresu do odczytu i zapisu.
Nie wolno obliczać indeksu z efektem ubocznym drugi raz.

## Ramki, błędy i bezwarunkowe sprzątanie

`RuntimeError` to generowany rekord z `kind`, `source` i opcjonalnymi danymi
diagnostycznymi. Teksty błędów są częścią modelu języka. Publiczny adapter
zachowuje dotychczasowe `message` i `source` oraz istniejące teksty ToyC.

```text
enum ErrorKind {
    InvalidIr, UninitializedRead, DivisionByZero, IntegerOverflow,
    WrongArgumentCount, WrongArgumentType, UndefinedFunction,
    MissingReturn, StepLimit, CallDepthLimit, EvaluationDepthLimit,
    InvalidAddress, DeadObject, ReadOnly, InvalidStoreType, IndexOutOfBounds
}
record RuntimeError(kind: ErrorKind, source: SourceRange);
```

To rdzeń błędu wewnętrznego. `error_message(kind)` jest generowaną funkcją
z napisami z modelu. Publiczny `RuntimeError` ToyC może być generowanym widokiem
`(kind, message, source)`, żeby zachować dostęp do dotychczasowego pola
`.message`; nie wymaga ręcznego duplikowania tabeli komunikatów.

Zwykłe wywołanie funkcji zwracającej `Result<T,E>` zachowuje konwencję akcji:
błąd propaguje się, a wyrażenie udostępnia `T`. Potrzebna jest jedna jawna
operacja kompilatora: `capture(call(...))` zatrzymuje automatyczną propagację
i zwraca pełne `Result<T,E>`. Nie wykonuje wywołania ponownie i nie przechwytuje
panik Rust. Konstrukcja nie jest ręczną funkcją runtime.

```text
function invoke(function: FunctionId, arguments: List<Argument>, source: SourceRange)
    -> Result<TypedValue, RuntimeError> mutates {
    let definition = require_function(function, source);
    check_arguments(definition.parameters, arguments, source);
    check_call_depth(source);
    let frame = make_frame(definition, arguments, source);
    with runtime.frames = list_appended(runtime.frames, frame) {
        let outcome = capture(execute(definition.body));
        return finish_call(outcome, definition.result, source);
    } finally {
        release_frame_storage();
    }
}
```

To jest semantyczny zapis stosu: `with` zachowuje poprzedni stan celu i go
przywraca na wyjściu. `finally` wykonuje się **przed** przywróceniem, przy
aktywnym kończącym się frame, także po `return` i błędzie. Generator może
zrealizować ten konkretny przypadek przez `push`/`pop`, zamiast kopiowania stosu.
`finally` w tym zastosowaniu jest nieomylny: zwalnia przechowywanie i przywraca
stan, nie uruchamia kodu użytkownika. Argumenty i wyniki są materializowane
przed zwolnieniem należących do ramki miejsc pamięci.

`finish_call` przy sukcesie rozpoznaje `Return`, sprawdza typ wyniku i obsługuje
brak `return` w funkcji `void`; przy błędzie zachowuje go bez nadpisywania.
Wszystkie te rozgałęzienia generujemy z funkcji modelu.

Polityka błędów wykonania v1: przerwij bieżące wykonanie przy pierwszym błędzie;
nie kontynuuj programu z fikcyjną wartością. To nie zmienia zbierania wielu
błędów analizy semantycznej. Przy błędzie wykonania nie wywołujemy dalszych
destruktorów języka: zwalniamy pamięć i ramki, zachowując pierwotny błąd.
ToyCP nie ma wyjątków języka, więc nie obiecujemy mechanizmu C++ exception
unwinding. Jeśli destruktor sam kończy się błędem, również przerywamy kod
użytkownika i zwalniamy resztę pamięci bez dalszych wywołań.

Niezależne limity obejmują kroki, głębokość wywołań i głębokość przejść po IR.
Rekurencja `fib` jest dozwolona, ale nie może ominąć limitu. Rekurencja
`execute -> evaluate -> invoke -> execute` jest poprawna w domenie wykonania.
Dotychczasowe odrzucanie cykli pomocniczych funkcji analizy pozostaje regułą
domeny `semantic_model`; checker nie może bezwarunkowo zastosować jej do
`execution_model`. Alternatywą w przyszłości jest generowany stos kontynuacji.

W domyślnej konfiguracji zachowujemy 1 000 000 kroków i 256 ramek funkcji
obecnego ToyC; dodajemy oddzielny konfigurowalny limit głębokości ewaluacji.
`make_frame` zapisuje alokacje parametrów w korzeniowym `ExecutionScope` ramki.
Operacja jest atomowa wobec błędów kontrolowanych: przy niepowodzeniu tworzenia
zwalnia dotąd utworzone miejsca i nie publikuje częściowej ramki.
`release_frame_storage` zwalnia je oraz pozostałe aktywne zakresy, także gdy
błąd przerwał normalną ścieżkę opuszczania bloków.
Zwalniamy tylko listy `owned`, a nie wszystkie adresy występujące w `bindings`:
parametr `self` nie przenosi własności obiektu do ramki metody.
`limits.call_depth` i `limits.evaluation_depth` są maksymalnymi głębokościami;
bieżąca głębokość wynika odpowiednio z liczby ramek i `runtime.evaluation_depth`.
Każdy licznik zagnieżdżenia jest przywracany także po błędzie. Ponowne
`call_named` po błędzie ToyC jest dozwolone ze stanem globali pozostałym po
przerwanym wywołaniu; nie obiecujemy transakcyjnego cofania zapisów programu.

## ToyCP: jawne self i czas życia w IR

Po analizie i obniżeniu konstrukcji klas do prostszych operacji wywołanie
metody wygląda jak zwykłe wywołanie z pierwszym argumentem przez referencję:

```text
object.bump(2)
    -> Invoke(bump_id, [ByReference(object_address), ByValue(Int(2))])

value = member
    -> Load(Project(parameter_0_address, Field(member_id)))
```

Wybór metody, sprawdzenie widoczności, dopasowanie przeciążenia konstruktora,
typ i ewentualna projekcja do bazy następują podczas analizy/obniżania IR.
Interpreter nie szuka metody po nazwie, nie rozstrzyga `private` i nie zgaduje
ukrytego argumentu. `ByReference` wiąże symbol parametru z istniejącym adresem;
zapis przez ten symbol zmienia obiekt odbiorcy. Dla baz używamy projekcji do
podobiektu, bez rzutowania adresów hosta. Brak wirtualności pozwala zachować
bezpośrednie `FunctionId`.

Potrzebny jest jeden, jednoznaczny mechanizm zakończenia życia obiektów:

1. deklaracja obiektu rezerwuje miejsce w momencie wykonania deklaracji;
2. wywołuje jawnie wybrany konstruktor z adresem tego miejsca;
3. dopiero po sukcesie dodaje `Cleanup(destructor_id, address, source)` do
   aktualnego zakresu wykonania;
4. opuszczenie zakresu normalnie lub przez `return` wykonuje aktywne cleanup
   w kolejności odwrotnej, a potem zwalnia miejsca pamięci;
5. wartość `return expression` jest obliczona i utrwalona przed cleanup.

Konstruktor nie uruchamia się z góry na początku bloku. Deklaracja w pominiętej
gałęzi nie tworzy obiektu i nie rejestruje destruktora. Każde wejście w ciało
pętli tworzy nowe czasy życia; `for` osobno posiada zakres inicjalizatora.

Proponowane prymitywne operacje IR to `Scope(body)`, `Invoke(function, args)`
i `RegisterCleanup(function, address)`, obok alokacji, load/store i sterowania.
`RegisterCleanup` jest instrukcją IR, której implementacja w `.sema` dopisuje
rekord do listy. Nie jest intrinsic ani ukrytą wiedzą interpretera o klasach.
Obecne `Construct`, `Destroy` i `MethodCall` mogą pozostać przejściowo w IR,
ale ich handlery muszą zostać opisane w modelu lub obniżone do tego zestawu.

Nie można równocześnie rejestrować automatycznego cleanup i doklejać tych samych
`Destroy` do `Block` oraz `Return`. Obecne komentarze `finish_block`/`make_return`
w `toycp_typed.sema` zapowiadają doklejanie wywołań; przy implementacji tego
projektu trzeba zmienić kontrakt tych funkcji. Przed zwróceniem `Control.Return`
każdy opuszczany `Scope` konsumuje własne cleanup dokładnie raz.

Kolejność jest zapisana w wygenerowanym IR konstruktorów/destruktorów:

- konstrukcja: baza, pola w kolejności deklaracji, ciało konstruktora;
- destrukcja: ciało destruktora, pola w kolejności odwrotnej, baza;
- tablica: konstrukcja elementów rosnąco, destrukcja malejąco;
- obiekty globalne: konstrukcja w kolejności deklaracji przed punktem wejścia,
  destrukcja w kolejności odwrotnej przy normalnym zakończeniu programu.

`self` może wskazywać obiekt w stanie `Constructing` lub `Destroying` podczas
wykonywania odpowiedniego kodu. Każdy odczyt pola nadal kontroluje jego
inicjalizację i czas życia. Zwrócona wartość obiektu ma oddzielne przechowywanie;
nie zwracamy aliasu do właśnie niszczonego lokalnego obiektu.
Mapa `subobjects` rejestruje czasy życia podobiektów klasowych po ścieżce
projekcji. Rozwiązanie adresu sprawdza także prefiksy tej ścieżki: żywy korzeń
nie pozwala odczytać podobiektu, którego destruktor już zakończył działanie.
Dla zwykłych pól skalarnych ToyC mapa może być pusta.

Ponieważ ToyCP nie ma jeszcze wykonawcy, regułę kopiowania klas trzeba zapisać
jawnie przy implementacji. Propozycja v1: syntezowane kopiowanie pól dla
przekazania/zwrócenia przez wartość, bez niejawnego wybierania specjalnego
konstruktora kopiującego. Żywotność obiektów wynikowych i tymczasowych musi
być widoczna w IR i mieć osobne testy; nie dziedziczymy jej przypadkiem z `Clone`
Rusta. Nie obiecujemy pełnej semantyki C++.

## Najmniejsza praktyczna warstwa ręczna

**Zero intrinsic specyficznych dla pamięci, ramek i klas ToyC/ToyCP.**
Ich algorytmy są funkcjami `execution_model`.

Proponuję siedem wspólnych rodzin intrinsic skalarnych, implementowanych raz
dla backendu Rust, a nie osobno dla każdego języka:

| Intrinsic | Wynik i obowiązek |
| --- | --- |
| `checked_add<I>(a,b)` | `Result<I, ArithmeticFault>`; przepełnienie |
| `checked_sub<I>(a,b)` | jak wyżej |
| `checked_mul<I>(a,b)` | jak wyżej |
| `checked_div<I>(a,b)` | dzielenie przez zero oraz `MIN / -1` |
| `checked_neg<I>(a)` | przepełnienie dla minimalnej liczby |
| `float_eval<F>(op,a,b)` | działania IEEE na skalarach, `b` opcjonalne dla negacji |
| `numeric_cast<S,T>(value,policy)` | jawnie wybrana reguła konwersji |

To rodziny monomorfizowane dla skończonego zestawu typów użytych w modelu.
Zapis `<I>` w tabeli opisuje schemat sygnatury wbudowanej biblioteki; nie wymaga
w pierwszym etapie dodania dowolnych funkcji generycznych autora języka.
`ArithmeticFault` nie zna tekstów ToyC, `Value`, `SymbolId` ani `Context`.
Funkcja `binary` napisana w `.sema` wybiera właściwy skalar i zamienia awarię
na `RuntimeError` ze źródłem. Porównania skalarów są zwykłymi operacjami akcji,
z semantyką IEEE dla `F32` (bez udawania całkowitego porządku przy NaN).
Te siedem rodzin można później emitować bezpośrednio jako operacje Rust;
wtedy osobny ręczny moduł liczbowy nie jest technicznie konieczny.

Na przykład wybór operatora i interpretacja błędu pozostają w akcjach:

```text
function add_int(left: I32, right: I32, source: SourceRange)
    -> Result<Value, RuntimeError> {
    let outcome = capture(checked_add(left, right));
    match outcome {
        Ok(number) => return Value.Int(number);
        Err(ArithmeticFault.Overflow) =>
            runtime_error ErrorKind.IntegerOverflow at source;
        Err(_) => runtime_error ErrorKind.InvalidIr at source;
    }
}
```

W pełnym modelu `binary` dopasowuje typy obu wartości i `Operator`, po czym
wywołuje takie funkcje. `checked_add<I32>` ma tylko awarię przepełnienia;
`checked_div<I32>` dodatkowo awarię zera dzielnika. Schematy wyników muszą to
wyrażać albo handler musi pokrywać pełny wspólny enum błędów. Powyższy przykład
korzysta ze wspólnego enum i traktuje nieoczekiwaną awarię jako naruszenie
kontraktu wykonawcy.

Nie ukrywamy reszty wymaganej infrastruktury pod liczbą „7”. Kompilator akcji
musi obsługiwać następujące zwykłe operacje danych:

| Operacje języka akcji / biblioteki standardowej | Realizacja Rust |
| --- | --- |
| konstrukcja, pola, przypisanie, warianty, match | struktury i enum generowane z deklaracji |
| listy: empty, len, first, tail, get, set, append, push, pop, reverse | `Vec`; sprawdzane indeksy |
| mapy: empty, get, contains, insert, remove | `HashMap`; brak zależności semantyki od kolejności iteracji |
| kopiowanie wartości, Option, Result | kod generowany, `Clone` i standardowe typy |
| capture, with/finally, propagacja błędu | sterowanie generowane przez kompilator |

To część backendu języka akcji, wspólna także z analizą. Nie wymagamy ręcznego
pisania `list_push` dla każdego nowego interpretera. Operacje modyfikujące
kolekcję otrzymują sprawdzane miejsce docelowe; pożyczka trwa wyłącznie podczas
wywołania i nie może przejść do wyniku. Przejście po kolekcji, którą ciało zmienia,
wymaga jawnego snapshotu albo jest odrzucane przez checker.

Pierwsza migracja istniejącego ToyC dodatkowo ma osiem opisanych wcześniej
metod adaptera `input`. Są jawnie liczone do ręcznego budżetu. Opcjonalny host
(pliki, konsola, zegar, funkcje zewnętrzne) jest oddzielnym interfejsem i nie
jest potrzebny do obecnych testów. Odczyt pliku źródłowego pozostaje w CLI.

## Zgodność liczb i zachowań ToyC

| Obszar | Reguła zachowywana w profilu ToyC |
| --- | --- |
| liczby | `int = I32`, `float = F32`, `char = U8`; BigInt ToyScope nie zmienia ToyC |
| arytmetyka int | checked; błąd `integer overflow`; zero dzielnika: `division by zero` |
| dzielenie int | obcięcie do zera; `MIN / -1` jest przepełnieniem |
| float | dotychczasowe operacje F32, także inf/NaN; brak nowego błędu dla float / 0 |
| int → char | dolne 8 bitów, zgodnie z obecnym `as u8` |
| float → int/char | obcięcie i nasycenie, NaN → 0, zgodnie z obecnymi rzutowaniami Rust |
| char → int/float | rozszerzenie; int → float: zaokrąglenie do F32 |
| bool | dotychczasowa konwersja przez `truthy`; bool nie staje się automatycznie liczbą |
| globalne | zerowanie także pól i elementów tablic |
| lokalne | niezainicjalizowane liście do czasu zapisu; brak zerowania |
| argumenty | kolejność od lewej do prawej; parametry przez wartość poza jawnym self |
| literał string | dotychczasowa tożsamość po ExprId; brak nowej deduplikacji |
| błędny program | odmowa wykonania przy diagnostykach analizy |

Tabela opisuje zgodność z obecną implementacją ToyC, nie twierdzenie, że są to
wszystkie reguły C/C++. Nie rozszerza konwersji dopuszczanych przez analizator.
Operacja `Convert` nadal musi zostać świadomie wstawiona do IR.

## Co dokładnie znika z ręcznego interpreter.rs

| Obecny element | Docelowe źródło |
| --- | --- |
| `Value`, `RuntimeError`, `Control`, stan `Interpreter` | deklaracje execution_model; typy Rust generowane |
| `new`, `set_step_limit` | funkcje modelu i generowane punkty publiczne |
| `global`, `call_named` | generowana obsługa eksportów; cienkie zachowanie API Rust |
| `error`, `tick` | funkcje modelu, także teksty i limity |
| `value_for_type`, `value_matches_type` | rekursja/match nad typem i wartością w akcjach |
| `root_value`, `root_value_mut` | zastępuje generowane rozwiązywanie Address i dostęp do alokacji |
| `field_value`, `load`, `store` | generowane operacje ścieżek i inicjalizacji |
| `call`, `evaluate_call` | generowane ramki, wiązanie parametrów i zabezpieczone wyjście |
| `execute`, `evaluate` | generowane odczyty IR, limit oraz wywołanie kontraktu |
| `declare`, `write_symbol`, `write` | generowane deklaracje i zapisy |
| `condition_truthy`, `execute_optional`, `return_value` | zwykłe funkcje akcji |
| `control_return`, `no_op` | zbędne wrappery; konstruktory i `unit` w akcjach |
| `invalid_operation`, `invalid_value` | generowany błąd z modelem ErrorKind |
| `truthy`, `value_from_constant`, `negate` | match zapisany w modelu |
| `binary_checked`, `binary`, `compare`, `convert` | akcje; tylko końcowe operacje skalarne są intrinsic |
| testy interpretera | ręczne testy niezależnych zachowań, poza generowanym modułem |

Nie zostaje ręczna funkcja „wykonaj cały operator języka” ani „wywołaj metodę
klasy”. Publiczny adapter może zachować `global() -> Option<&Value>` przez
krótką pożyczkę do wygenerowanego magazynu. W języku akcji nie trzeba z tego
powodu wprowadzać długotrwałych referencji Rust. `call_named` nadal może działać
wielokrotnie; dla ToyCP dodajemy jawne `run`/`shutdown` kończące życie globali.
Nie wykonujemy destruktorów języka z `Drop` Rusta, który nie potrafi zwrócić
kontrolowanego błędu wykonania.

## Walidacja modelu i plan wdrożenia

Checker musi sprawdzać pełne pokrycie wariantów IR, typy argumentów/wyników,
rozłączność wzorców, poprawność pól, efekty `mutates`, propagację `Result`,
obecność `finally`, poprawność ABI parametrów i nieskończone typy przez wartość.
Zapisy przez `input` są zabronione. Generator nie powinien polegać wyłącznie
na tym, że błędny model odrzuci później `rustc`.

Kolejność wdrożenia dająca szybko przewagę kodu generowanego:

1. Dodać `execution_model`, enum z danymi, pola/kolekcje i ogólny typowany match.
   Użyć wspólnego typowanego AST akcji zamiast rozbudowywać niezależny,
   nietypowany `executionExpression`. Przenieść Value, błędy, stałe, truthy,
   operatory i konwersje. To usuwa dużą, samodzielną część ręcznego pliku.
2. Dodać `capture`, precyzyjne with/finally i osobną domenę rekurencji.
   Przenieść ramki, wywołania, deklaracje i pamięć. Zachować publiczne API
   i testy ToyC. W tym kroku egzekwować przewagę rozmiaru części generowanej.
3. Wytworzyć model IR i adapter ToyCP; obniżyć self/wywołania i dodać Scope oraz
   cleanup. Dopiero wtedy stwierdzić, że ToyCP ma działający interpreter,
   a nie jedynie poprawnie wygenerowany plik Rust.

Testy odbioru oprócz obecnych testów ToyC:

- rekurencja i odrębne miejsca dla tych samych SymbolId, także ponowne wywołanie
  po błędzie bez pozostawionej ramki;
- granice I32, dzielenie, negacja, konwersje float/char, NaN i signed zero;
- niezależna inicjalizacja pól, kopia struktury, brak aliasowania parametrów
  przez wartość i brak zgadywania lokalnego symbolu z ramki wywołującej;
- martwy/stary adres, niepoprawny FieldId i indeks bez paniki hosta;
- metoda zmienia obiekt przez self, zagnieżdżone wywołanie metody i dostęp do bazy;
- konstrukcja przy deklaracji, brak destruktora dla pominiętej gałęzi, LIFO,
  wczesny return, każda iteracja pętli i zakres inicjalizatora for;
- kolejność baza/pola/ciało, tablice obiektów, globalna inicjalizacja i shutdown;
- wynik return utrwalony przed destruktorem; błąd konstruktora/destruktora
  zgodny z polityką przerwania, bez podwójnej destrukcji;
- parametry i wyniki klas przez wartość z jawną polityką kopiowania;
- zmiana reguły wykonania w testowym `.sema` zmienia zachowanie wygenerowanego
  programu, bez edycji pliku ręcznego;
- kompilacja Rust i Clippy oraz pomiar kodu generowanego/ręcznego z wyżej
  określonym zakresem pomiaru.

Projekt nie wymaga `.agx`: pojawienie się modelu wykonania nie zmienia sposobu
czytania gramatyki. Powrót do nazwy formatu ma sens przy rzeczywistym połączeniu
leksera, parsera, analizy i obniżania IR w jeden plik języka.
