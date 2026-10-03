# Roboczy format `.sema`

[English](SEMA_FORMAT.md) | [Polski](SEMA_FORMAT.pl.md)

## Nowy podział formatów — kierunek docelowy

Historyczny plan `IMPLEMENTATION_PLAN_AG_SEMA_COGE.md` (nieprzeniesiony do tego repozytorium)
ustala podział na `.ag` (składnia i lexer), samodzielne `.sema` (dodatkowo analiza)
oraz `.coge` (dodatkowo wykonawca i backendy). Zawiera pomiary obecnych plików,
projekcje, szablony, podział bibliotek i etapy migracji. Etap 2 wprowadza
samodzielny dokument opisany poniżej; ścisły podział sekcji i migracja przykładów
należą do kolejnych etapów. Poniższy opis braku leksera w `.sema` i umieszczania w nim
wykonawcy dotyczy formatu dotychczasowego; po migracji sekcje wykonania należą
do `.coge`. Składnia działań i kontrakty polityk pozostają punktem wyjścia.

## Samodzielny dokument v1

Domyślny frontend CLI `sema` przyjmuje pełną gramatykę i lexer w jednym pliku:

```text
sema ExampleAnalysis;
format 1;
grammar Example;
options { parser = LALR; lookahead = 2; ast = explicit; }
node start : value=ID EOF analysis {};
ID : [a-z]+;
WS : [ \t\r\n]+ -> skip;
```

Nagłówek `coge ExampleAnalysis;` używa tego samego frontendu. Nazwa specyfikacji
jest niezależna od nazwy gramatyki. `format 1;` można pominąć (domyślna wersja 1);
inne wersje są odrzucane. Nie ma ścieżki `for` ani automatycznego odczytu
sąsiedniego `.ag`. Parser korzysta z zainstalowanych pakietów narzędzia.

`options`, `channels`, `lexerClasses`, `conflicts`, reguły parsera z
`enable`/`disable` i reguły leksera mają składnię Ag. Akcje występują po symbolach
i opcjonalnej etykiecie alternatywy. Dla pustej alternatywy wymagane jest `empty`.
Opcje generacji mają osobny blok `generation { generate_interpreter = true; }`.
W etapie 2 frontend rozpoznaje sekcje obu warstw; walidacja ich przynależności
oraz oddzielny CLI `coge` należą do etapu 3.

`composeDocumentGrammar` składa typowane produkcje z kanonicznego
`ag/grammars/Ag.ag`, działań/modeli `Sema.ag` oraz nagłówków `Document.ag`.
Kopie produkcji Ag i stary nagłówek z `Sema.ag` są pomijane. Jawne punkty
rozszerzenia to nagłówek dokumentu, `topLevelItem` i zakończenie alternatywy.
Wspólne prefiksy nazw są rozstrzygane przez prawostronny ciąg elementów; akcja
kończy część symboli. Format jest generowany jako LALR(2), bez polityk ukrywających
konflikty. Opcja lookahead opisywanego języka pozostaje osobnym ustawieniem.

Klasy `ACTION`/`MODEL` aktywują słowa rozszerzeń w działaniach/modelach.
Klasa `AG` włącza zapis zbiorów znaków leksera poza działaniami; nawiasy
kwadratowe w działaniach nadal oznaczają listy i indeksowanie. Niezależne
`lexerClasses` zapisane wewnątrz dokumentu opisują lexer języka użytkownika.
Nazwy rozszerzeń pozostają dostępne jako nazwy reguł i pól Ag.

### Źródło i projekcja biblioteczna

`PackagedAgFrontend::parseDocument` zwraca `GrammarDocument`: model Ag i własny
tekst UTF-8 podzielony na tokeny oraz trivia. Zakresy są bajtowe i pokrywają
całe źródło, łącznie z pomijanymi komentarzami i końcowymi białymi znakami.
`DocumentFrontend::parse` zwraca niemutowalny `ParsedDocument` z pełnym źródłem,
tożsamością dokumentu, prywatnym AST, indeksem sekcji i projekcją `grammar`.
Fabryki `makeSemaDocument` i `makeCogeDocument` sprawdzają rodzaj nagłówka
i właścicieli sekcji. `SemanticInput` przechowuje wyłącznie część analizy.

Projekcja usuwa rozpoznane przez parser zakresy nagłówka specyfikacji,
sekcji rozszerzeń i akcji. Nie używa regexów ani skanowania klamer. Komentarze
wewnątrz usuwanego zakresu znikają z nim; komentarze i białe znaki przed/po nim
zostają bez zmian. Dotyczy to również nowej linii po nagłówku. Pozostały tekst
jest ponownie sprawdzany kanonicznym frontendem Ag. Zakresy AST rozszerzeń
odnoszą się do pełnego źródła, a zakresy modelu `grammar` do jego projekcji.

### Zgodność ze starymi plikami

Dotychczasowy format jest dostępny wyłącznie przez jawną opcję:

```sh
build/bin/coge --check --contracts contracts/toyc-runtime-v1.json coge/examples/toyc/toyc.coge
```

ToyC/ToyCP i ich testy generacji korzystają z samodzielnych `.coge` i jawnych
kontraktów runtime. Starsze przykłady oraz testy adaptera korzystają z `--legacy`.
Nowe pliki sprawdza się bez tej opcji. Emisja `--emit-rust-dir` z nowego dokumentu
korzysta z jego własnej gramatyki. API `SemaDocument::semanticInput()` zwraca czyste dane analizy z własnością,
które mogą przeżyć obiekt dokumentu. Kontrakty zewnętrzne są jawne: powtarzalne
`--contracts FILE`, bez szukania plików obok źródła. Opis nowego CLI i schematu
manifestów znajduje się w [DOCUMENT_CLI.md](../../docs/DOCUMENT_CLI.pl.md).

## Format dotychczasowy

`.ag` definiuje składnię programu, a `.sema` przypisuje działania semantyczne do
alternatyw tej gramatyki i może opisywać wykonanie powstałego IR. Plik `.sema`
nie zawiera reguł leksera. Parser formatu
`.sema` nadal potrzebuje własnego leksera; może użyć tokenów z
`ag/grammars/Ag.ag` i dodać tokeny dla działań. Nie trzeba gramatyki
wyspowej ani traktowania zawartości `analysis` jako nieprzezroczystego napisu.

## Kierunek rozwoju: specyfikacja zamiast przepisywania Rust

Szczegółowy plan implementacji, kontrakty, kolejność etapów i testy opisuje
historyczny plan `IMPLEMENTATION_PLAN_POLICIES_BACKENDS.md` (nieprzeniesiony do tego repozytorium).

Parser i generator obsługują `return_policy`, `assignment_policy`,
`condition_policy`, `statement_ir` i `flow_actions` dla ToyC/ToyCP oraz
pilotażowy `selection_policy` dla ToyCP. Tworzą funkcje analizy i adaptery do
ich `Context`. Pozostałe
polityki poniżej są projektem.
Migracja ręcznych
modułów `statements.rs`, `expressions.rs`, `declarations.rs` i `classes.rs`
ma skracać opis semantyki. Samo przepisanie pętli, indeksowania aren i obsługi
`Result` do języka działań nie spełnia tego celu.

Proponowana warstwa deklaratywna opisuje warunki, konwersje, budowę IR oraz
skutki dla przepływu i czasu życia. Generator rozwija ją do istniejącego,
typowanego modelu działań. Zwykłe funkcje pozostają sposobem opisywania
nietypowych przypadków. Poniższa składnia ilustruje kontrakt; nazwy pól
polityk nie wymagają osobnych słów kluczowych leksera.

Przykład specyfikacji instrukcji zwrotu:

```text
semantic_model {
    return_policy function_return {
        target = enclosing_function;
        value = absent_for_void_otherwise_required;
        conversion = implicit_conversion;
        cleanup = exited_automatic_lifetimes;
        evaluation = capture_value_before_cleanup;
        flow = unreachable_after_success;
        ir = Return;
        errors {
            missing_target = "return outside function";
            wrong_presence = "invalid return value";
            failed_conversion = "incompatible return type";
        }
    }
}
```

Powiązania z modelem danych są krótką deklaracją, np. w ToyCP:

```text
model_bindings function_return {
    active_function = active_function;
    functions = functions;
    flow = current_flow;
    poison = poisoned_expression;
    convert = convert;
    emit = add_operation;
    return_ir = record;
    cleanup_scopes = active_scopes;
    scopes = scopes;
    variables = variables;
    destructible = type_needs_destruction;
}
```

Generator sprawdza kompletność i dopuszczalne pola powiązań. Kompilator Rust
sprawdza na razie istnienie wskazanych pól i zgodność ich typów z wygenerowaną
implementacją `ReturnPolicyContext`; przyszły model danych AgSem ma umożliwić
zgłaszanie tych błędów już przy generowaniu.
Obecny generator przyjmuje jedną politykę `return_policy` na plik `.sema`.
ToyC ustawia `cleanup = no_cleanup` i `cleanup_scopes = no_scopes`; ToyCP używa
listy aktywnych zakresów oraz operacji IR `Return` w postaci rekordu.

Alternatywa gramatyki analizuje opcjonalne wyrażenie i stosuje tę politykę
do jego wyniku oraz miejsca w źródle. Nie wyszukuje funkcji w arenie, nie
przegląda stosu zakresów i nie tworzy ręcznie identyfikatora operacji.
`Return` musi być zadeklarowaną operacją IR z pasującym kontraktem; sama nazwa
nie upoważnia generatora do zgadywania jej znaczenia.

Kontrakt polityki jest ścisły: brak funkcji oznacza błąd; `void` wymaga braku
wartości, pozostałe typy wymagają wartości podlegającej wskazanej konwersji.
Wartość zatruta wcześniejszym błędem nie wywołuje wtórnego błędu konwersji.
Nieudana analiza daje operację błędu i zachowuje wejściowy stan przepływu.
Dopiero poprawny zwrot oznacza dalszą ścieżkę jako nieosiągalną.

`exited_automatic_lifetimes` oznacza zakończenie aktywnych czasów życia
opuszczanych przez zwrot, w odwrotnej kolejności zakończonej konstrukcji.
Dotyczy obiektów własnych; pożyczone `self` i obiekty globalne nie należą
do tej listy. Model czasu życia musi określać własność i rejestrację udanej
konstrukcji, także na ścieżkach warunkowych. Analiza opisuje sprzątanie w IR;
nie wykonuje destruktorów. Wykonawca najpierw oblicza i zachowuje wynik,
potem sprząta i przekazuje wynik wywołującemu. Tej kolejności używają również
przyszłe backendy C i LLVM.

Pilotaż wyboru konstruktora korzysta ze wspólnego mechanizmu wyboru
kandydatów. ToyCP przygotowuje kandydatów z identyfikatorem, planem
konwersji, rangą i dostępnością. Polityka określa jedyne obsługiwane na razie
warianty rankingu, sprawdzenia dostępności i fallbacku oraz komunikaty:

```text
selection_policy constructor_choice {
    choose = choose_constructor;
    collect = collect_constructor_candidates;
    complete = complete_construction;
    candidate = ConstructorCandidate;
    receiver = first_parameter;
    ranking = fewest_conversions;
    access = after_ranking;
    fallback = no_declared_and_no_arguments;
    missing = "constructor not declared";
    ambiguous = "ambiguous constructor call";
    inaccessible = "constructor is not accessible";
    nonclass = "constructor requires class type";
    missing_default = "default constructor not declared";
    incompatible = "incompatible argument type";
}
selection_bindings constructor_choice {
    structs = structs;
    constructors = constructors;
    functions = functions;
    parameters = parameters;
    expressions = expressions;
    expression_type = ty;
    visibility = visibility;
    accessible = accessible;
    convertible = conversion_allowed;
    variables = variables;
    variable_type = ty;
    variable_place = place;
    fields = fields;
    field_type = ty;
    default_constructible = default_constructible;
    convert = convert;
    construction_ir = Construct;
    add_operation = add_operation;
    scopes = scopes;
    scope_operations = operations;
    current_flow = current_flow;
    mark_initialized = mark_initialized;
    active_initializer = active_initializer;
}
```

Generator tworzy `choose_constructor`, `collect_constructor_candidates` i
`complete_construction` z tych
deklaracji. Powiązania wskazują pola modelu i operacje ToyCP, a `receiver`
określa pominięcie niejawnego `self` przy dopasowaniu argumentów. Wspólny runtime
wybiera jedynego kandydata o najniższej randze. Najpierw rozstrzyga remis,
potem dostępność zwycięzcy; prywatny najlepszy kandydat nie jest pomijany.
Kandydat zachowuje plan konwersji wybranej funkcji. Generowana operacja
wykonuje konwersje, zapisuje `Construct` do IR i oznacza obiekt jako
zainicjalizowany po sukcesie. `classes.rs` udostępnia cienki adapter.

To uproszczona polityka ToyCP, nie pełne rozstrzyganie przeciążeń C++.
Wybrany kandydat zawiera plan konwersji argumentów, więc nie trzeba ich
ponownie wyszukiwać. Dostępność składowej, konstrukcja podobiektów i zapis
operacji `Construct` są osobnymi zobowiązaniami otaczającej akcji.
Ogólny mechanizm wyboru nie może mieć ręcznej implementacji przeznaczonej
wyłącznie dla `finish_construction`.

Polityka przypisania określa dopuszczalny cel, konwersję wartości, wymóg
inicjalizacji przy odczycie starej wartości oraz stan po udanym zapisie.
Złożone przypisanie musi zachować pojedyncze obliczenie miejsca docelowego,
np. indeksu tablicy. Kolejność obliczania celu i wartości jest jawną decyzją
języka; generator nie ustala jej na podstawie gramatyki.

ToyC używa `evaluation = static_place` i `ir = Store`, ponieważ jego miejsca
nie zawierają dynamicznych indeksów. ToyCP używa
`evaluation = target_once_before_rhs` oraz `ir = StoreAndCompoundStore`;
wykonawca `CompoundStore` wiąże miejsce przed obliczeniem prawej strony.
`assignment_bindings` wskazuje pola aren, stan przepływu i funkcje budujące IR.
Parser odrzuca brakujące lub obce pola oraz niezgodną parę `evaluation`/`ir`.
Ta sama polityka generuje `increment_value`: dopuszcza `int`, `float` i `char`,
tworzy literał całkowity `1`, a następnie używa przypisania złożonego z
dodawaniem albo odejmowaniem. Zachowuje błąd `invalid increment operand`.
Obecny adapter obsługuje typowany model ToyC/ToyCP: zakłada ich warianty
`AssignmentOp`, `Type`, `ExpressionKind` i `Operation`. Szersze modele będą
wymagały jawnego mapowania tych wariantów albo schematu modelu.

`condition_policy` zachowuje wyrażenie typu `bool`, a typy liczbowe i wskaźniki
konwertuje do `bool`. Wcześniejszy błąd wyrażenia daje `dependent error`;
pozostałe typy zgłaszają `condition is not convertible to bool`.
`condition_bindings` wskazuje arenę wyrażeń, rozpoznawanie wyrażenia zatrutego
i funkcję dopisującą konwersję do IR. Generator tworzy `condition_bool` dla
warunków `if`, `while` i `for`.

`statement_ir` mapuje funkcje budujące `Call`, `If`, `While`, `For` i `Block`
na warianty operacji IR. `statement_bindings` wskazuje arenę zakresów i
funkcję zapisującą operację. Generator tworzy także dopisanie instrukcji do
zakresu. Dzięki temu ToyC i ToyCP nie utrzymują osobnych, identycznych
konstruktorów IR w `statements.rs`. Kształt pól tych wariantów jest na razie
sprawdzany przez kompilator Rust; przyszły schemat modelu pozwoli zgłosić
niezgodność już przy generowaniu.

`flow_actions` nadaje nazwy czterem działaniom analizy: zapamiętaniu stanu,
przywróceniu, połączeniu z zapamiętanym stanem i odzyskaniu po błędzie.
`flow_bindings` wskazuje bieżący stan, listę migawek, operację łączenia,
diagnostykę oraz wariant błędnej instrukcji IR. Generator tworzy funkcje ToyC
i ToyCP; wspólny runtime przechowuje i odtwarza migawki. Operacja łączenia
pozostaje częścią modelu języka, ponieważ zna jego miejsca i stany
inicjalizacji. Dla `if` każda gałąź zaczyna od tej samej migawki. Dla pętli
łączy się także wejście bez wykonania ciała. Odzyskanie po błędzie zapisuje
diagnostykę, przywraca stan wejściowy i emituje instrukcję błędu.

Warunkiem migracji jest skrócenie całej specyfikacji, licząc także deklaracje
polityk i modelu danych, bez ukrycia tego samego algorytmu w nowym intrinsic
specyficznym dla ToyC/ToyCP. Pierwszy pilotaż obejmie `return` i przypisanie,
z porównaniem diagnostyki, IR i wykonania na niezależnych testach źródłowych.
Pełna grupa `statements.rs` jest już generowana. Opcjonalne `modules` dzieli
funkcje i analizatory na pliki `sema_<nazwa>_gen.rs`; fasady w `sema_gen.rs`
i `sema_lib_gen.rs` zachowują dotychczasowe wywołania. Przypisanie do modułu
steruje organizacją plików, nie semantyką. Manifest `sema_modules.manifest`
pozwala usunąć tylko poprzednio wygenerowane moduły przy zmianie mapowania.
ToyC i ToyCP wydzielają reguły `if`, `while`, `for` i ich konstruktory IR.

`lowering_model` w ToyC jest oddzielną sekcją przed `execution_model`.
Wskazuje nazwę generowanej funkcji i warianty IR dla deklaracji, wyrażeń,
bloków, rozgałęzień, pętli i zwrotu. Powstaje `lowering_gen.rs`, który tworzy
Structured Core IR dla funkcji wejściowej oraz funkcji osiągalnych przez
wywołania. Parametry skalarne są przypisywane do slotów, a argumenty obliczane
w kolejności źródłowej. Zachowuje komentarze źródłowe i weryfikuje rozwinięcie
do CFG. Nieobsługiwane instrukcje zgłasza
jawnie; szczegółowy zakres opisuje [CORE_IR.pl.md](../../coge/docs/CORE_IR.pl.md).

Opcjonalne `backend_c { emit = emit_c; lower = lower_function_to_structured; }`
wiąże wygenerowane obniżanie ze wspólnym emiterem C11. AgSem sprawdza zgodność
nazwy `lower` z `lowering_model`, a `backend_c_gen.rs` zawiera typowany adapter.
Obecny emiter obsługuje pilotaż ToyC: bezargumentowe `main`, osiągalne funkcje
z parametrami i lokalne sloty skalarne (`int`, `float`, `bool`, `char`),
niejawne konwersje `char → int`, `char → float`, `int → float` i warunki
skalarne konwertowane do `bool`, stałe, odczyt
i zapis, sprawdzaną arytmetykę `int` oraz arytmetykę `float`,
zagnieżdżone bloki, `if`, `while`, `for`, `return` i komentarze źródłowe.
Prototypy C umożliwiają rekurencję i definicje po miejscu wywołania.
Indeksowanie lokalnej tablicy skalarów `a[i]` ma osobne warianty odczytu i
zapisu. Interpreter oraz backend C sprawdzają granice i stan inicjalizacji
każdego elementu podczas wykonania, ponieważ indeks może być zmienny.

Kolejny etap to wspólne obniżanie IR: jawne argumenty `self`, konwersje złożone,
wywołania konstruktorów i sprzątanie. Backend C, a następnie LLVM, wykorzysta
ten sam kontrakt operacji; nie będzie ponownie rozstrzygał reguł języka.

## Zewnętrzna składnia

Z `Ag.ag` można przejąć `node`/`inline`, nazwy reguł, oznaczone elementy,
kwantyfikatory, etykiety `#...` i separator alternatyw `|`. Usuwa się z
dokumentu `.sema` reguły leksera oraz bloki `channels` i `conflicts`:
ich źródłem pozostaje wskazany plik `.ag`. Własny blok `options` pliku `.sema`
steruje generowaniem, np. `generate_interpreter = true`; nie jest kopią opcji
parsera z `.ag`. Dodaje się nagłówek i działania po
alternatywach:

```text
sema ToyScope1 for "toyscope.ag";

node declaration
    : INT name=ID initializer=initializer SEMI #InitializedDeclaration
      analysis {
          let id = text(name);
          require not(containsLocal(scope, id)) else error "identifier already declared";
          let symbol = declare(scope, id, Int, Uninitialized);
          setState(flow, symbol, Initializing);
          analyze initializer with scope: scope -> value;
          require canConvert(typeOf(value), Int) else error "incompatible initializer type";
          setState(flow, symbol, Initialized);
          result = makeDeclaration(symbol, value);
      }
    ;
```

Nagłówek wskazuje **wariant semantyki** i źródło produkcji. `ToyScope1` nie
udaje nowej gramatyki `ToyScope1`: gramatyką jest nadal `ToyScope` w
`toyscope.ag`. Weryfikator porównuje nazwy, pola, symbole, kwantyfikatory i
etykiety alternatyw ze źródłowym `.ag`. Zmiana produkcji bez uaktualnienia
`.sema` jest błędem specyfikacji. Dla alternatywy bez etykiety kluczem jest
pozycja w regule, dlatego zmiana kolejności również wymaga weryfikacji.

Wiele bloków `analysis` w jednej alternatywie tworzy jeden ciąg instrukcji.
Generator zachowuje ich kolejność; `let` z wcześniejszego bloku pozostaje
widoczne w późniejszym, a przypisanie do `result` nie kończy dalszych działań.
Bloki `execution result` należą do odrębnej fazy. Docelowy formatter może
połączyć bloki `analysis` bez zmiany znaczenia.

## Mały język działań

Poniżej jest proponowany rdzeń składni, nie pełna gramatyka parsera. Wyrażenia
nie mają operatorów arytmetycznych ani złożonej precedencji. Obliczenia takie
jak zgodność typów, wybór symbolu lub budowa IR mają nazwy funkcji.

```text
block       := "{" statement* "}"
statement   := "let" ID "=" expression ";"
             | place "=" expression ";"
             | expression ";"
             | "if" expression block ("else" block)?
             | "foreach" ID "in" expression block
             | "return" expression ";"
             | "analyze" ID ("with" arguments)? "->" ID ";"
             | "require" expression "else" "error" STRING ";"
expression  := atom postfix*
atom        := ID | NUMBER | STRING | "true" | "false" | "none"
             | "(" expression ")"
postfix     := "." ID | "[" expression "]"
             | "(" (expression ("," expression)*)? ")"
place       := ID ("." ID | "[" expression "]")*
```

Słowami kluczowymi są elementy sterujące oraz `analyze` i `require`.
`result`, `scope`, `flow`, `declare` czy `makeInitialization` są zwykłymi
identyfikatorami, których znaczenie nadaje kontekst lub biblioteka. `result`
jest z góry zadanym miejscem wyniku alternatywy. `let` tworzy lokalne
wiązanie; przypisanie wymaga istniejącego, modyfikowalnego miejsca. `foreach`
odwiedza skończoną kolekcję w kolejności źródłowej. `return` kończy funkcję
pomocniczą; nie jest instrukcją zwrotu analizowanego programu.

```text
analysis {
    let operations = list();
    foreach item in items {
        analyze item with scope: scope, flow: flow -> operation;
        append(operations, operation);
    }
    result = makeBlock(scope, operations);
}
```

Wersja pierwsza może udostępniać funkcje wbudowane z jawnie opisanymi
typami i skutkami: `lookup`, `declare`, operacje na listach i mapach oraz
tworzenie zakresu. Sama składnia wywołania `f(...)` nie określa jeszcze, czy
`f` jest funkcją wbudowaną, czy zdefiniowaną przez autora.

## Funkcje pomocnicze i operacje wbudowane

93 nazwy z `sema_call_names.txt` nie oznaczają 93 funkcji runtime do napisania.
Wykaz obejmuje konstruktory rekordów i encji, metody, funkcje pomocnicze,
zapytania oraz operacje pierwotne. Funkcje pomocnicze zapisuje się w
`semantic_model` w tym samym języku instrukcji co `analysis`:

```text
semantic_model {
    intrinsic lookup_builtin(scope, name);

    function find_symbol(scope, name) {
        return lookup_builtin(scope, name);
    }

    function require_symbol(scope, name) {
        let symbol = find_symbol(scope, name);
        require symbol != none else error "undeclared identifier";
        return symbol;
    }
}
```

Deklaracja `intrinsic f(...);` oznacza operację dostarczaną przez kompilator
`sema`, bez ciała w pliku `.sema`. Starsze `intrinsic f(...) = wyrażenie;`
pozostaje składnią zgodności ze szkicami; ma ciało wyrażeniowe i obecny
checker traktuje je jako liść grafu. `record` i `entity` tworzą konstruktory danych, nie funkcje
runtime. `query` jest funkcją o ciele w postaci wyrażenia. Obecna deklaracja
`intrinsic` jest kontraktem; program `sema` nie sprawdza jeszcze, czy ma dla
niej implementację w kompilatorze.

Parametry i wynik funkcji mogą mieć jawne typy, na przykład
`function contains_local(scope: ScopeId, name: Text) -> Bool { ... }`.
Generator Rust wymaga dodatkowo w `semantic_model` jednego
`rust_context SemaContext;`. Sygnatury reguł można podać jawnie, np.
`analyzer program(flow: FlowId, scope: ScopeId) -> SymbolId;`, albo wyprowadzić
z bloków `analysis`, argumentów `analyze`, kontraktów funkcji i krótkiej listy
typowanych atrybutów `inherited`.
Parametry analizatora są atrybutami dziedziczonymi przekazywanymi przez
`analyze`; typ po `->` określa wynik syntetyzowany. Nazwy i typy nie są już
przypisane na stałe do `program`, `flow` i `scope`. Deklarowane w modelu
typowane `intrinsic` są metodami wskazanego kontekstu Rusta; generator nie
wymaga konkretnej listy funkcji wbudowanych.
Pierwszy pionowy przykład znajduje się w `examples/scope_codegen.sema`;
odpowiadają mu gramatyka `examples/scope_codegen.ag` i mała biblioteka Rust
`examples/scope_codegen`. Polecenie:

```text
build/bin/coge --legacy --emit-rust-dir \
  sema/examples/scope_codegen/generated \
  sema/examples/scope_codegen/scope_codegen.sema
```

generuje `sema_lib_gen.rs` z funkcjami odczytu zakresów, wiązania nazw i deklaracji
oraz `sema_gen.rs` z `analyze_program`, `analyze_statement` i `analyze_block`.
Reguła `program` ma cztery alternatywy: odczyt nazwy,
`int declared; target = value; used`, samą deklarację `int declared;` oraz
wywołanie reguły `statement` z późniejszym odczytem. Źródło
`int declared; used` przechodzi przez alternatywę `statement`, która
obejmuje teraz również deklarację. Reguła `statement` ma trzy postacie
`if–else`, przypisanie, odczyt, deklarację i zagnieżdżony `block`.
Reguła `block` zawiera listę
`statement*`. Przykładowa gramatyka używa LALR(2), aby po `int x;`
rozróżnić dalszy odczyt od przypisania w alternatywach `program`.
Akcje `analysis` deklarują symbol, analizują ewentualne przypisanie i
wyszukują później użyty identyfikator.
Ręcznie pisany `sema_intr.rs` zawiera
`SemaContext`, identyfikatory i implementacje zadeklarowanych `intrinsic`.
Dla tych alternatyw `sema` wczytuje wskazaną w nagłówku gramatykę `.ag` i sprawdza
nazwę reguły, jej rodzaj, etykiety, symbole i ich kolejność. Osobno Agas
generuje `examples/scope_codegen/generated/parser_gen.rs` z gramatyki `.ag`.
Biblioteka przykładu wykonuje parser i analizę przez `analyze_source`:
odczyt zadeklarowanej nazwy daje `SymbolId`, a brak nazwy zgłasza
`undeclared identifier` zdefiniowany w `.sema`. Deklaracja w źródle
`int item; item` przechodzi przez wygenerowaną akcję `declare_variable`,
co wiąże `item` przed jego odczytem; duplikat daje `identifier already declared`.
Funkcja Action `read_symbol` sprawdza stan przed odczytem: lokalne
`int item; item` zgłasza `uninitialized variable`, globalny obiekt jest
zainicjalizowany zerem, a parametry są oznaczone jako zainicjalizowane.
Stan inicjalizacji jest w osobnej mapie `Flow`, indeksowanej przez `SymbolId`;
`Symbol` przechowuje tylko informacje o deklaracji. Uchwyt `FlowId` jest jawnie
przekazywany do `analysis` oraz funkcji deklaracji, przypisania i odczytu.
`flow_copy` tworzy niezależną migawkę stanu, więc przypisanie w jednej kopii
nie zmienia drugiej. Funkcja Action `merge_flow` tworzy nowy przepływ dla
dwóch osiągalnych gałęzi: zachowuje tylko symbole obecne w obu, a odczyt
pozostaje bezpieczny wyłącznie wtedy, gdy był bezpieczny w każdej. Stan
`ZeroInitialized` pozostaje tylko wtedy, gdy obie gałęzie go zachowały;
w przeciwnym razie bezpieczny odczyt ma stan `Initialized`. Alternatywy
`if (flag) x = 1; else x = 2; x` i `if (flag) x = 1; else ; x` tworzą
osobne kopie stanu obu gałęzi i łączą je przez `merge_flow` w
`analyze_statement`. Instrukcja Action
`analyze statement_node with flow: flow, scope: scope -> after_statement;`
przekazuje dziecko AST i odziedziczone `FlowId` oraz `ScopeId` do jego
analizy. Wynikiem reguły jest nowy `FlowId`, którego `program` używa do
sprawdzenia odczytu po `if`. `block` wykonuje `foreach item in items`,
przekazując wynikowy `FlowId` kolejnej instrukcji. Przykład
`{ if (flag) x = 1; else ; x = 2; x; } x` przechodzi, a odczyt `x;`
bez drugiego przypisania zgłasza `uninitialized variable`. Warunek musi
wskazywać zadeklarowaną i zainicjalizowaną zmienną. W tym przykładzie
`block` tworzy zakres potomny przed analizą instrukcji. Dzięki temu
`{ int x; x = 7; x; } x` przypisuje wartość wewnętrznemu `x`, a końcowy
odczyt wskazuje zewnętrzny symbol. Przy wyjściu funkcja Action
`leave_scope` usuwa symbole lokalne z wynikowego `Flow`, pozostawiając
stan zewnętrznych zmiennych. Blok wewnętrzny może ponownie przesłonić
`x`; po jego zakończeniu widoczne jest `x` z bloku zewnętrznego, a po
drugim `}` symbol z zakresu wywołującego. Agas przekazuje alternatywę
`statement: nested=block` bezpośrednio jako węzeł `block`. Jej puste
`analysis {}` oznacza przekazanie analizy do `analyze_block`; generator
sprawdza zgodność wyniku obu reguł. Forma
`if (flag) { x = 1; } else { x = 2; } x` analizuje oba bloki z osobnych
kopii `Flow` i łączy ich stany przez `merge_flow`. Każdy blok tworzy własny
zakres, więc deklaracja w gałęzi nie przesłania nazwy po `if`. Przypisanie
do zewnętrznej zmiennej pozostaje widoczne po połączeniu tylko wtedy,
gdy obie gałęzie ją inicjalizują. Obie gałęzie `if` są traktowane jako osiągalne.
Analiza buduje uporządkowane IR: `Declare` zapisuje symbol i jego stan
początkowy, `Assign` zapisuje cel i wartość, `Read` wskazuje sprawdzony
symbol, `Block` przechowuje zakres i operacje wewnętrzne, a `If`
przechowuje symbol warunku oraz osobne listy operacji obu gałęzi.
Każda kopia `Flow` niesie dotychczasowe operacje; przy połączeniu gałęzi
`record_if` zastępuje ich wspólny prefiks jednym węzłem `If`. Wynik
`program_ir()` zawiera osobną listę dla każdego poprawnie przeanalizowanego
źródła. Puste bloki pozostają w IR jako węzły z pustą listą operacji.
Przed wywołaniem wygenerowanego analizatora `analyze_source` kopiuje
`SemaContext`. Gdy analiza zwróci błąd, przywraca migawkę: deklaracje,
zakresy, stany `Flow` i częściowe IR nie przechodzą do następnego źródła.
W tym małym przykładzie pełna kopia jest prosta; dla większych programów
można ją zastąpić dziennikiem zmian lub trwałymi strukturami danych.
Funkcja `execute_ir` interpretuje ten zestaw węzłów dla liczb `i64`.
Przyjmuje `Flow` z dostępnymi symbolami oraz pary
`(SymbolId, wartość)` dla wartości wejściowych symboli zastanych.
Operacja `Declare` ustawia stan symboli tworzonych przez program. Symbole o stanie
`ZeroInitialized` zaczynają od zera; dla symbolu `Initialized` sama analiza
nie przechowuje liczby, więc przed odczytem trzeba podać ją jako wejście.
Wynik udostępnia kolejne odczyty i końcowe wartości widocznych symboli;
zmienne lokalne znikają po wyjściu z `Block`. Odczyt bez wartości i próba
traktowania funkcji jako liczby zwracają błąd wykonania.
`int item; item = 5; item` tworzy opis przypisania do `SymbolId` z wartością
`i64` i zmienia stan na `Initialized`; analizator nie wykonuje programu.
Nieznana nazwa po lewej stronie, funkcja jako cel lub literał poza zakresem
`i64` kończą analizę przed zapisaniem przypisania.

```text
build/bin/agas --emit-rust-parser \
  sema/examples/scope_codegen/generated/parser_gen.rs \
  sema/examples/scope_codegen/scope_codegen.ag
cargo test --manifest-path sema/examples/scope_codegen/Cargo.toml
```

Generator przechodzi po alternatywach `.ag` i odpowiadających im blokach
`analysis`; nie koduje już osobnych sekwencji dla nazw `Lookup`
i `DeclareAssignRead`. Pozostałe alternatywy używają
tego samego mechanizmu. Obecny podzbiór obejmuje trzy reguły `node`,
oznaczone alternatywy oraz pojedyncze referencje do tokenów i reguł,
a także oznaczoną listę `statement*`.
Na najwyższym poziomie `analysis` obsługiwane są `let`, `require`,
`analyze` z nazwanymi argumentami `flow` i `scope`, wywołania zwracające
`Unit`, `foreach` po liście dzieci z aktualizacją akumulatora `FlowId`
oraz końcowe przypisanie `result = ...`. `program` zwraca `SymbolId`,
a `statement` i `block` zwracają `FlowId`. Inne formy zagnieżdżonych
bloków sterowania w `analysis` pozostają do dodania; ten przykład nie jest
jeszcze kompilatorem ToyC.
Generator tego przykładu obsługuje typy `ScopeId`, `FlowId`, `SymbolId`, `StructTagId`,
`ParameterId`, `Text`, `OwnedText`, `Bool`, `Int`, `Unit`, wybrane `Option<T>`,
`Result<T>` i listy; instrukcje `let`, `if`, `foreach`, `return`, `error`;
wywołania funkcji, porównania oraz `and`. Zna sygnatury intrinsic użytych w
`scope_codegen.sema` i sprawdza liczbę, typy argumentów oraz efekt wywołania.
Funkcja i intrinsic z dopiskiem `mutates` mogą zmieniać `SemaContext`; funkcja
bez tego dopisku może wywoływać tylko operacje odczytowe. `Result<T>` propaguje
błąd przez wywołania, a w wygenerowanym Rust ma obecnie postać
`Result<T, &'static str>`.

Pierwsza grupa funkcji nazw parametrów dodaje operację
`parameter_name(ParameterId) -> Text`. `ParameterId` jest uchwytem dla jednej deklaracji parametru;
przekazana lista powinna zawierać każdy taki uchwyt tylko raz. Przy tym
kontrakcie `unique_names` porównuje nazwy odrębnych deklaracji i nie zmienia
listy. `bind_parameters` najpierw sprawdza całą listę, potem tworzy symbole
parametrów i oznacza je jako zainicjalizowane. `bind`, `declare_variable`,
`declare_function` i `declare_incomplete_struct` wprowadzają symbole do
odpowiednich przestrzeni nazw. Funkcja może ponawiać prototyp o tej samej
sygnaturze i otrzymać jedną definicję. Tagi struktur mają osobną przestrzeń.
W tym przykładzie wszystkie obiekty mają typ `int`, sygnatura funkcji jest
kanonicznym tekstem, a nie strukturalnym typem; nie ma jeszcze dopełniania
struktury ani wyliczania jej układu.

Pozostałe konstrukcje Action są obecnie odrzucane przez generator; parser
samego pliku `.sema` nadal je przyjmuje. Pełna weryfikacja zgodności z
gramatyką źródłową i generowanie dowolnych bloków `analysis` pozostają
osobnymi etapami. Biblioteka Rust przechowuje zakresy, symbole i przepływy w `SemaContext`;
funkcje przenoszą kopiowalne identyfikatory `ScopeId` i `SymbolId`, a nazwy
przekazują jako `&str`, więc generowany kod nie zwraca referencji do wnętrza
kontekstu.

## Docelowy kontrakt generowania interpretera

`analysis` tworzy typowane IR. Wykonawca otrzymuje to IR jako wejście i
interpretuje je według trzech części tego samego pliku `.sema`:

```text
semantic_model { ... }   // Analysis contracts and IR construction.
execution_model { ... }  // Runtime data, state and executable functions.
execution_contract { ... } // Handler for each IR operation and expression.
```

`Sema.ag` rozpoznaje obecnie szkielet `execution_model`: `input`, `runtime_state`,
warianty `enum` z typowanymi polami oraz deklaracje `record`, `type`,
`intrinsic` i `function`. Generator tworzy z tej sekcji warianty `enum`,
typowane `record`, stan `Interpreter` wskazany przez `runtime_state` i metody z ciałem `function`; `intrinsic` oznacza metodę
dostarczoną ręcznie. W ToyC emituje typy wartości i stanu, widoki miejsc i funkcji
oraz funkcje inicjalizacji, deklaracji, odczytu i zapisu, wywołań, kontroli
argumentów, obliczania wyrażeń i sterowania wykonaniem. Ręczny
`literal_identity` pozostaje małym adapterem do identyfikatora literału;
pozostałe adaptery odczytują `Context`. Sprawdzone działania `i32` i
rzutowania skalarne wywołują wspólny runtime z ciał funkcji `.sema`.
`runtime_state runtime: Runtime` wymaga rekordu `Runtime` i generuje
pole `runtime` obok referencji do kontekstu IR. `input` i `type` są już
parsowane, ale generowanie z nich kończy się jawnym błędem.
Przykłady w dalszej części rozdziału określają docelowy kontrakt, którego
dzisiejszy generator jeszcze nie spełnia.
Różnica między `semantic_model` a `execution_model` dotyczy domeny i typów
efektów. Funkcje obu korzystają z tego samego parsera akcji; docelowo będą
sprawdzane przez wspólny checker typów. `input` i `runtime_state` należą do
istniejącej klasy leksera `MODEL`; nazwa `state` pozostaje zwykłym
identyfikatorem. Reguły przepisane z `.ag` wyłączają klasy `MODEL` i `ACTION`.
Nowe rozszerzenie pliku nie jest potrzebne.

Obecny podzbiór ciał funkcji obejmuje typowane parametry i wynik
`Result<T, RuntimeError>`, `let`, `return`, wywołania z propagacją błędu,
`if`, `foreach`, dopasowanie wariantów, proste operatory skalarne i
`runtime_error ... at ...`. Funkcje `execution_model` mogą odczytać pole
lokalnego rekordu i odczytać lub przypisać pole zadeklarowanego
`runtime_state`; dzięki temu ToyC generuje licznik kroków oraz wejścia
`execute` i `evaluate`. Konstruktor wariantu
zapisuje się jako `Value.Int(number)`, a generator emituje `Value::Int(number)`.
Wywołanie `AgSemRuntime.checked_add_i32(a, b)` oznacza funkcję wspólnego
crate `agsem_runtime`, zwracającą `Option<I32>`; funkcja `.sema` zamienia
`none` na błąd ze źródłem.
Pętle `foreach` zużywają przekazaną kolekcję; przy ponownym wykorzystaniu
wartości można podać `clone()`. Wywołania metod lokalnych kolekcji i magazynu
zwracają bezpośrednio wynik Rust, bez automatycznego `?`. Generator rozpoznaje
modyfikujące metody kolekcji/magazynu, nadaje lokalnym wiązaniom `mut`,
a dla zapisu stanu wymaga deklaracji `mutates`. Typy metod weryfikuje Rust.

`capture(execute(body))` wykonuje funkcję akcji raz i zachowuje całe
`Result`, zamiast propagować błąd. ToyC zapisuje ten wynik, zdejmuje ramkę,
a następnie dopasowuje `Ok(control)` lub `Err(fault)`. Wyrażenie
`runtime_error fault.message at fault.source` zachowuje oryginalny komunikat
i położenie błędu. `capture` przyjmuje pojedyncze wywołanie funkcji akcji;
nie przechwytuje panik Rust. Ogólne generowanie `with/finally` pozostaje
do wdrożenia; obecna funkcja `call` zapewnia sprzątanie przez jawny zapis
`push_bindings`, `capture`, `pop`, `finish_call` w modelu.

Funkcje `execution_model` nie podlegają zakazowi cykli funkcji analizy;
rekurencja programu użytkownika jest dopuszczalna. Generator sprawdza
wiązanie nazw lokalnych oraz część struktury deklaracji; pełne typowanie ciał
pozostaje do wdrożenia, a wygenerowany Rust jest dodatkowo sprawdzany przez
kompilator Rusta.

Wspólny crate `rust/crates/sema-runtime` udostępnia typowany magazyn globali i ramek,
projekcję ścieżki w zagnieżdżonych wartościach, sprawdzone operacje `i32`
oraz konwersje skalarne. `load_path` i `write_path` rozróżniają brak miejsca
od niepoprawnej ścieżki, a `.sema` określa komunikat i miejsce błędu.
ToyC przekazuje mu `SymbolId` i `Value`, a kolejność oraz reguły wywołań pozostają w modelu
wykonania danego języka. Funkcje `.sema` określają komunikat dla
przepełnienia i dzielenia przez zero; do wspólnego crate trafia tylko działanie
na liczbach. Analogiczne operacje na `BigInt` oparte na używanej
już przez ToyScope bibliotece `num-bigint` mogą zostać dodane do crate.
Przy obsłudze `BigInt` runtime będzie używać tej biblioteki; nie będzie
implementować dużych liczb samodzielnie.
Język wybiera typ liczbowy w `.sema`, więc ToyC nie musi zależeć od `BigInt`.
Adapter odczytujący ręcznie napisane IR `Context` pozostaje zależny od
projektu. W ToyC reguły inicjalizacji, wiązania parametrów i obsługi błędu
wywołania są już generowane z `execution_model`. Pełny model adresów i cyklu
życia obiektów ToyCP opisany poniżej pozostaje do wdrożenia.

### Właściwości wykonawcy i testy graniczne

W `execution_model` można podać jeden blok `properties`:

```text
properties {
    forall a: I32, b: I32
        where mathematical_sum(a, b) fits I32
        expect checked_add_i32(a, b) == mathematical_sum(a, b);
    forall a: I32, b: I32
        where mathematical_sum(a, b) not fits I32
        expect checked_add_i32(a, b) fails "integer overflow";
}
```

Generator tworzy `interpreter_properties_gen.rs`, zawierający funkcję
`check_execution_properties`. Ręczny test przygotowuje wykonawcę i wywołuje
ją; domeny, pętle, oczekiwania, źródło błędu i raportowanie argumentów są
wygenerowane. Końcowy parametr `SourceRange` testowanej funkcji jest
uzupełniany automatycznie. Własności nie zmieniają kodu produkcyjnego.

Pierwszy podzbiór obsługuje jedną lub dwie zmienne `I32`, funkcje bez `mutates`
z wynikiem `Result<I32, RuntimeError>`, porównanie liczb w `where`, warunek
`fits I32`/`not fits I32`, równość oczekiwanego wyniku i `fails` z komunikatem.
Funkcje oczekiwań to `mathematical_sum`, `mathematical_difference`,
`mathematical_product` i `mathematical_negation`. Przyjmują skalary `I32`,
a wynik obliczają niezależnie w `i128`, bez wywoływania testowanych funkcji
runtime. Ten zakres arytmetyki nie wymaga BigInt. Zagnieżdżone wyrażenia
matematyczne nie są jeszcze obsługiwane, żeby zachować gwarancję braku
przepełnienia samego obliczenia oczekiwanego wyniku.

`forall` oznacza tutaj testowanie skończonego zbioru, nie dowód dla całej
dziedziny. Bazowy zbiór ma 18 liczb: krańce `i32` i sąsiadów, zero, `±1`,
`±2` oraz okolice granic istotnych dla mnożenia i konwersji. Dodawane są
stałe występujące we właściwościach, ich przeciwieństwa oraz sąsiedzi.
Dla dwóch zmiennych sprawdzany jest iloczyn kartezjański zbiorów. Własność,
której warunek nie dopuszcza żadnego przypadku, kończy test błędem.
Dziewięć własności ToyC daje obecnie 1008 wywołań podlegających sprawdzeniu.

Nowe słowa `properties`, `forall`, `expect`, `fits`, `fails` należą do klasy
leksera `MODEL`; składnia reguł gramatyki pozostaje bez zmian.

Blok wykonania deklaruje niezmienne wejście IR i zmienny stan:

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

    enum Value { Int(I32), Float(F32), Bool(Bool), Char(U8),
                 Pointer(Option<LiteralId>), Struct(List<Value>),
                 Array(List<Value>), Void, Uninitialized }
    enum Control { Continue, Return(Value) }
    record Runtime(globals: Map<SymbolId, Address>, frames: List<Frame>,
                   allocations: List<Allocation>, limits: Limits);

    function load(place: PlaceId, source: SourceRange)
        -> Result<Value, RuntimeError> mutates {
        let address = resolve_place(place, source);
        let value = read_address(address, source);
        require fully_initialized(value)
            else runtime_error ErrorKind.UninitializedRead at source;
        return value;
    }
}
```

Wszystkie użyte w przykładzie typy i funkcje, w tym `Address`, `Frame`,
`Allocation`, `resolve_place`, `read_address` oraz `fully_initialized`,
muszą mieć deklarację w kompletnym modelu. Nie są ukrytymi operacjami
specyficznymi dla ToyC w generatorze C++. Typy wejściowego IR muszą mieć jawny
schemat: warianty `Operation` i `ExpressionKind`, pola węzłów oraz podpisy
odczytów `input`. Początkowo osiem odczytów może realizować mały adapter do
istniejącego `Context`; docelowo adapter też można wygenerować z deklaracji IR.

`execution_contract` wiąże warianty IR z funkcjami modelu. Nowe sygnatury
dispatchera określają typ wejścia, wynik i efekt; stary zapis handlerów
pozostaje obsługiwany bez zmiany znaczenia:

```text
execution_contract {
    execute(kind: Operation, source: SourceRange)
        -> Result<Control, RuntimeError> mutates;
    evaluate(kind: ExpressionKind, expression: ExprId,
             checks: List<RuntimeCheck>, source: SourceRange)
        -> Result<Value, RuntimeError> mutates;

    execute Store(place: place, value: value) {
        let computed = evaluate(value);
        store(place, computed, source);
        return Control.Continue;
    }
    evaluate Load(place) { return load(place, source); }
}
```

Generator sprawdza dokładnie jeden handler dla każdego wariantu obu enumów,
poprawność pól, typy argumentów i wyniku oraz efekty wywołań. Handler nie może
milcząco zakończyć się bez wartości w nowym, typowanym trybie. Historyczny
`execution_contract` ToyC ma nadal domyślne `Continue` po instrukcjach bez
`return`, dopóki nie zostanie przeniesiony na nową sygnaturę.

Operacje na stanie są **zwykłymi funkcjami `execution_model`**. Dotyczy to
`default_value`, `value_matches_type`, `truthy`, `value_from_constant`,
`binary`, `convert`, `resolve_symbol`, `resolve_place`, `load`, `store`,
`execute`, `evaluate`, `call` i obsługi zakresów. Właśnie ich ciała mają
trafić do `interpreter_gen.rs`. Przeniesienie samego `match` na wariantach
IR przy pozostawieniu treści `load` i `call` w ręcznym module nie spełnia tego
kontraktu.

Do zapisania tych ciał język akcji potrzebuje: wariantów enum z polami,
rekordów i pól, `List<T>`, `Map<K,V>`, `Option<T>`, `Result<T,E>`, dopasowania
wzorców, typowanych przypisań, pętli, operacji na skalarnych liczbach oraz
jednego sposobu przechwycenia `Result` bez natychmiastowej propagacji błędu.
`capture(call(...))` jest konstrukcją kompilatora akcji; `with ... finally`
zdejmuje ramkę i zwalnia pamięć także po błędzie. Checker odrzuca użycie
nieznanego wariantu, błędną projekcję, brak gałęzi dopasowania i zmianę
niezmiennego `input`. Rekurencja `execute → evaluate → call → execute` jest
dozwolona i ograniczona limitem wykonania.

Miejsca pamięci są adresami złożonymi z identyfikatora alokacji i ścieżki
pól/elementów. `StorageId` ma generację, aby zwolniony i ponownie użyty slot
nie reaktywował starego adresu. Ramka wiąże `SymbolId` z adresem bieżącego
wywołania; odwołanie do lokalnej zmiennej nie przeszukuje ramek wywołujących.
Wartości agregatowe są kopiowane jako wartości, a `self` ToyCP jest przekazywany
jako jawny adres obiektu. `self` nie jest wskaźnikiem `char*` ToyC.

ToyCP obniża wywołanie metody do `Invoke` z odbiorcą jako pierwszym argumentem
`ByReference(Address)`. Wybrana metoda ma już `FunctionId`, a dostęp do pola
ma `FieldId` i sprawdzoną projekcję do bazy. Interpreter nie wybiera metody
po nazwie ani nie sprawdza widoczności. Deklaracja obiektu wywołuje konstruktor
w chwili wykonania i dopiero po sukcesie rejestruje sprzątanie. Zakończenie
zakresu albo `return` wykonuje rejestrowane destruktory w odwrotnej kolejności.
Nie wolno jednocześnie rejestrować sprzątania i dopisywać tych samych `Destroy`
do `Block` lub `Return`. Wartość zwracana jest obliczana przed destrukcją.

Ręczna warstwa ma tylko dwa rodzaje operacji:

1. Wspólne dla backendu Rust operacje liczbowe: sprawdzone `add`, `sub`,
   `mul`, `div`, `neg`, operacje `F32` i jawne konwersje. Te siedem rodzin
   intrinsic nie zna nazw ToyC, IR ani struktury ramek. Błędy mają
   typowane przyczyny; komunikaty i zakres źródłowy tworzy model.
2. Przejściowy adapter odczytujący obecne IR (`Context`) przez osiem metod
   `input`. Nie wykonuje programu, nie liczy wartości i nie zmienia pamięci.

Odczyt pliku, drukowanie komunikatów, konsola i funkcje hosta nie są
niezbędną częścią interpretera obecnego IR. Pozostają w CLI lub w osobnym,
jawnym interfejsie hosta. Algorytmy pamięci, ramek i klas nie są intrinsic.

Wynik generowania obejmuje typy `Value`, `RuntimeError`, `Control` i stan
wykonawcy, metody zarządzania pamięcią i ramkami, wywołania, funkcje wartości,
dispatch dla wszystkich wariantów IR oraz publiczne `new`, `global`,
`call_named` i limity. `interpreter_gen.rs` ma zawierać więcej kodu
wykonawcy niż pozostałe ręczne moduły wykonawcy wraz z adapterem i wspólną
warstwą liczbową. Do porównania nie wlicza się testów, parsera ani analizatora.
Szczegółowe typy i przypadki brzegowe ToyC/ToyCP opisuje
[EXECUTION_MODEL.md](../../coge/docs/EXECUTION_MODEL.pl.md).

Wdrożenie przebiega przez cztery sprawdzalne kroki: typowany model wartości
i operacji skalarnych; generowaną pamięć i inicjalizację; ramki i sprzątanie
po błędach; obniżenie ToyCP do jawnych wywołań i cyklu życia. Każdy krok
porównuje zachowanie ze starym ToyC. Po drugim kroku należy ponownie
zmierzyć rozmiar kodu; etap jest zakończony dopiero wtedy, gdy cel przewagi
części generowanej został osiągnięty. ToyCP wymaga osobnych testów metod,
`self`, konstrukcji, destrukcji, pętli, wczesnego `return` oraz błędów.

Funkcje i zapytania modelu analizy tworzą graf wywołań skierowany od funkcji
wyższego poziomu do niższego. `sema` odrzuca bezpośrednią i pośrednią rekurencję
w tej domenie oraz wywołania z ciał funkcji, których nie można rozwiązać jako
innej funkcji, zapytania,
intrinsic lub konstruktora. Liście grafu mogą też po prostu zwrócić parametr
albo stałą; nie muszą sztucznie wywoływać intrinsic. Obecna kontrola dotyczy
bezpośrednich wywołań po nazwie; metody, typy argumentów, przeciążenia i
sygnatury intrinsic wymagają następnego etapu. Przykład znajduje się w
`examples/functions.sema`.

## Wykonanie i ograniczenia

Analiza dzieci jest **jawna**: parser tworzy drzewo, ale nie uruchamia
automatycznie `analysis` potomków. Dzięki temu deklaracja może najpierw
utworzyć symbol, a dopiero potem analizować inicjalizator. `analyze(child,
context...)` zwraca wynik albo błąd z pozycją źródłową. Działania nie wykonują
programu użytkownika; budują sprawdzone symbole, typy, AST/IR i diagnostykę.

Turingowska zupełność nie jest wymogiem analizatora semantycznego. Skończone
`foreach` po drzewie oraz brak rekurencji jego funkcji pomocniczych ułatwiają
gwarancję zakończenia. Wykonawca IR musi natomiast obsługiwać pętle i rekurencję
programu użytkownika. Ma własny limit kroków i głębokości wywołań; zakaz cykli
funkcji pomocniczych analizy nie obowiązuje funkcji domeny wykonania.

Przed uznaniem `.sema` za wykonywalny format trzeba jeszcze ustalić typy
wartości i `result`, sygnatury funkcji wbudowanych, reguły przekazywania
kontekstu, propagację błędów, diagnostykę z zakresem źródłowym oraz test
zgodności wszystkich alternatyw z `.ag`. Obecne `*.sema.txt` są materiałem
wejściowym do tej normalizacji, nie programami w powyższym rdzeniu.

## Pierwszy parser

`Sema.ag` realizuje ten rdzeń jako gramatykę Agas LALR(1). Dwie klasy lexera
`MODEL` i `ACTION` wykorzystują ten sam mechanizm co rozróżnienie `>>` i
`> >` w gramatykach Agas. Słowa modelu są aktywne w `semantic_model`, a słowa
akcji w blokach działań. Akcje wewnątrz modelu dziedziczą także `MODEL`;
akcje przy regułach parsera działają bez tej klasy. Reguły parsera przywracają
kontekst gramatyczny.
W `examples/functions.sema` napis `function` jest słowem kluczowym definicji
funkcji i jednocześnie zwykłą nazwą reguły `node function`. Część tokenów,
które starsze pliki stosują jako nazwy pól (`type`, `present`, `otherwise`),
pozostaje globalna i jest dopuszczona w pozycji identyfikatora. Próba
`disable(MODEL)` na każdej regule `actionBlock` dawała konflikt kontekstów
lexera w LALR: ten sam scalony stan oczekiwał jednocześnie słów deklaracji
modelu z `MODEL=1` i identyfikatora akcji z `MODEL=0`. Po usunięciu tego
wyłączenia tablica LALR(1) ma 0 konfliktów. Jeżeli kiedyś potrzebny będzie
ścisły tryb akcji bez słów modelu również w ciałach funkcji, trzeba wężej
wyznaczyć zakres klasy `MODEL` albo rozdzielić reguły wejścia w te ciała.
Przykładowy plik
`examples/minimal.sema` można sprawdzić poleceniem:

```sh
build/bin/agas --diagnose-parse sema/grammars/Sema.ag tests/fixtures/legacy/minimal.sema
```

Wynik `status=accepted` potwierdza składnię. `Sema.ag` rozpoznaje również
dotychczasowe cztery pliki `*.sema.txt`: deklaracje `semantic_model`,
`execution_contract`, `execution result`, `rewrite`, instrukcje `analyze`,
`require`, `match`, `with`, `for` oraz ich używane tam wyrażenia. To zgodność
syntaktyczna ze szkicami. Parser nie sprawdza jeszcze typów działań ani
zgodności przepisanych produkcji ze wskazanym `.ag`. Operatory wyrażeń tworzą
obecnie sekwencję składniową; ich priorytety i znaczenie musi ustalić kolejny
etap, zanim będzie można generować kod Rust.

## Porównanie z `.ag`

`tools/compare_sema.py` porównuje reguły parsera po nazwie i rodzaju
`node`/`inline`, a alternatywy po sekwencji symboli, pól, sufiksów i etykiecie.
Pomija komentarze i bloki akcji. Zgłasza brakujące lub dodatkowe reguły i
alternatywy jako błędy, a różną kolejność jako ostrzeżenie z pozycjami do
przestawienia. Pliki `toyc.ag` i `toyc.sema.txt` mają obecnie zgodne 41 reguł
i 82 alternatywy. `toyscope.ag` rozbija puste i niepuste powtórzenia oraz
opcjonalny inicjalizator na jawne alternatywy zgodne ze wszystkimi trzema
`toyscope_*.sema.txt`: 13 reguł i 26 alternatyw. Semantyka polityki 1 ma
już nagłówek `sema ToyScope1 for "toyscope.ag";`. Generator Rust przechodzi
przez kontrolę zgodności produkcji i wiele bloków `analysis` w `program`.
Generator Rust obsługuje już nieoznaczone alternatywy `inline` z jednym
przekazywanym węzłem i dowolnymi nieoznaczonymi tokenami wokół niego:
wybiera akcję na podstawie typu AST dziecka. Obsługuje także jedyną
nieoznaczoną alternatywę reguły `node`, jeśli Agas tworzy dla niej węzeł
z pustą nazwą wariantu. Dzięki temu przechodzi przez `inline item`,
`inline initializer` i `node assignment`. Gdy oznaczona alternatywa `node`
przekazuje bezpośrednio AST dziecka, generator może teraz wykonać jej
niepuste akcje `analysis` (np. `node expression: first=term`). Generator
obsługuje również rekordy AST `inline additiveOperation` z wieloma polami
oraz tokeny przekazywane przez `inline addop`. Wygenerowany kod dla obu
kształtów przechodzi kontrolę kompilacji Rust na małym przykładzie. Dla
`toyscope_1.sema.txt` generowanie dochodzi teraz do `semantic_model` i
zatrzymuje się na braku deklaracji `rust_context`. Obecny ToyScope1 używa
ponadto nietypowanych parametrów `apply_integer`, które trzeba opisać w
modelu typowanym przed generowaniem kodu.
Przy dalszych zmianach akcji dla polityki 1
punktem odniesienia jest `toyscope_1.semadesc.txt`, nie `toyc.sema.txt`.
Pliki generatora mają teraz nazwę `src/RustGenerator.cpp` i `.h`.
Niezależny przykład `examples/typed_codegen.sema` wraz z małą biblioteką
Rust pokazuje własny kontekst `CalcContext`, wynik `Int`, dwa parametry
dziedziczone i własny intrinsic `parse_decimal`.

`toyscope_1_typed.sema` opisuje wszystkie alternatywy `toyscope.ag` w
typowanym podzbiorze akcji. Biblioteka `examples/toyscope_1_typed` zawiera
wygenerowany analizator i parser, kontekst z trwałymi identyfikatorami
zakresów, symboli, miejsc, wyrażeń i operacji oraz wykonawcę IR. Liczby mają
typ `BigInt` z biblioteki Rust `num-bigint`, zgodnie z matematycznymi liczbami
całkowitymi w opisie ToyScope1. Kontekst przechowuje zakresy źródłowe i zbiera
niezależne błędy semantyczne, zachowując operacje po błędnych instrukcjach.
`analyze_source_partial` zwraca także błędny program do diagnostyki;
`analyze_source` zwraca pierwszy komunikat dla prostego interfejsu. Wykonawca
odrzuca program z błędami. Odzyskiwanie po błędach składni pozostaje osobnym
zadaniem. Blok `execution_contract` określa wymagane zachowanie wykonania.
Opcja `generate_interpreter = true` w bloku `options` generuje teraz
`interpreter_gen.rs` z handlerów `execute` i `evaluate`: obsługuje pola
wariantów, `let`, wywołania, `return`, `if`, `foreach`, `while` oraz dopasowanie
wariantu przepływu `Continue`/`Return`. ToyC używa wygenerowanego sterowania
wykonaniem, a ręczny kod dostarcza operacje na pamięci, ramkach i wartościach.
Generowanie także tych operacji wymaga jawnego kontraktu reprezentacji wartości,
miejsc pamięci, ramek, konwersji i błędów wykonania. Jest to rozszerzenie modelu
wykonania; nie wymaga samo w sobie zmiany rozszerzenia pliku na `.agx`.
Projekt bloku `execution_model`, granicy intrinsic i migracji ToyC/ToyCP opisuje
[EXECUTION_MODEL.md](../../coge/docs/EXECUTION_MODEL.pl.md). Parser rozpoznaje już
deklaracje modelu, a generator emituje typy i pierwsze metody wykonawcy ToyC.
Pamięć, ramki, arytmetyka i cykl życia obiektów opisane w projekcie pozostają
następnym etapem.
[Wnioskowanie sygnatur](ANALYZER_INFERENCE.md) pozwala zastąpić powtarzane
deklaracje `analyzer` krótkim zestawem typowanych `inherited`. Generator
wyprowadza wyniki i wejścia reguł z akcji, wywołań i kontraktów funkcji.
ToyC używa 7 takich deklaracji zamiast 41 sygnatur, a ToyCP — 10 zamiast 57.
Starsze, bogatsze kontrakty ToyScope nadal są opisem wykonania, nie wejściem
tego generatora.
Funkcje `contains_local`, `lookup_lexical`, `declare_variable` i `read_symbol`
są zapisane jako bloki akcji `function` i generowane do `sema_lib_gen.rs`;
`ToyScopeContext` realizuje ich pierwotne operacje na strukturach danych.
Uruchomienie: `cargo test --manifest-path sema/examples/toyscope_1/Cargo.toml --offline`.

`toyscope_2_typed.sema` wiąże nową nazwę dopiero po analizie inicjalizatora;
`toyscope_3_typed.sema` rezerwuje wszystkie bezpośrednie deklaracje przed
analizą zakresu i sprawdza użycie komórek podczas wykonania. Oba warianty mają
osobne przykłady Rust w `examples/toyscope_2_typed` i
`examples/toyscope_3_typed`, korzystające ze wspólnego modelu IR oraz
wykonawcy z wariantu 1. Kontrakty wykonania nadal są zapisane w plikach
`.sema`; generator nie emituje jeszcze kodu wykonawcy.

```sh
python3 tools/compare_sema.py tests/fixtures/legacy/toyc.ag tests/fixtures/legacy/toyc.sema.txt
python3 tools/compare_sema.py tests/fixtures/legacy/toyc.ag tests/fixtures/legacy/toyc.sema.txt -o /tmp/toyc.reordered.sema.txt
```

Opcja `-o` tworzy nową kopię tylko wtedy, gdy struktury są identyczne z
dokładnością do kolejności. Przenosi całe fragmenty alternatyw razem z
komentarzami i akcjami, nie nadpisuje wejścia ani istniejącego wyjścia.
Jeżeli między regułami znajduje się tekst inny niż biały, odmawia
reorganizacji, bo nie da się bez dodatkowej decyzji ustalić, do której reguły
przypiąć taki komentarz. Ten etap jest tekstowym porównaniem struktur;
walidację składni przez `Sema.ag` wykonuje program C++ `sema`, a sprawdzanie
typów pozostaje do zrobienia.

## Program `sema` i następne etapy

Stary adapter `coge --legacy` ładuje artefakt parsera wygenerowany podczas budowania z
`Sema.ag`, sprawdza jego zgodność z bieżącą wersją gramatyki i analizuje
składnię wskazanych plików. Wypisuje `syntax OK` albo błąd z numerem wiersza
i kolumny. Na razie nie wykonuje akcji semantycznych.

```sh
cmake --build build --target coge
build/bin/coge --legacy tests/fixtures/legacy/toyc.sema.txt tests/fixtures/legacy/toyscope_1.sema.txt
```

Opcja `--calls` zbiera z poprawnych składniowo plików unikalne nazwy wywołań
i zapisuje je w porządku alfabetycznym. Plik `sema_call_names.txt` zawiera
wynik dla czterech istniejących `*.sema.txt` (93 nazwy). Wykaz obejmuje
zwykłe funkcje, konstruktory i metody, na razie bez rozstrzygania ich
sygnatur ani właścicieli metod.
Nazwę `parse_int` z ToyC ujednolicono z `parse_integer` z ToyScope, z jawną
podstawą 10. Podobne nazwy `read`/`read_initialized`,
`Store`/`CheckedStore` i `Load`/`CheckedLoad` pozostają rozdzielone:
pierwsza para działa w różnych fazach, a pozostałe odzwierciedlają różne
polityki kontroli inicjalizacji.

```sh
build/bin/coge --legacy --calls /tmp/sema_call_names.txt \
  tests/fixtures/legacy/toyc.sema.txt tests/fixtures/legacy/toyscope_1.sema.txt \
  tests/fixtures/legacy/toyscope_2.sema.txt tests/fixtures/legacy/toyscope_3.sema.txt
```

Później program może odczytać powiązane `.ag` przez parser Agas,
powtórzyć strukturalne porównanie z narzędzia powyżej na zweryfikowanych
drzewach składniowych i sprawdzić typy akcji. Starszy nagłówek `grammar ...;`
jest zgodnością przejściową; docelowy `sema Nazwa for "plik.ag";` określa
jednoznacznie wariant i źródło.

Po zbudowaniu drzewa akcji `sema` może zebrać wywołania oraz ich miejsca
użycia. To wymaga rozstrzygnięcia nazw: `analyze` i `require` są operacjami
kompilatora semantyki; `Scope(...)` i `Binary(...)` są konstruktorami;
`scope.lookup_lexical(...)` jest metodą; `apply_integer(...)` może być
zadeklarowanym `intrinsic`. Dopiero pozostałe wywołania, wraz z sygnaturami,
stanowią kandydatów do biblioteki runtime. Sam wykaz napisów przed nawiasem
nie wystarczy, bo ta sama nazwa może oznaczać różne rodzaje symboli.

Wynik tego kroku powinien być sprawdzalnym manifestem: nazwa, kategoria,
sygnatura lub brakująca sygnatura, miejsca wywołań i miejsce definicji. Błędy
nierozwiązanych nazw i niezgodnych sygnatur trzeba zgłosić przed generowaniem
`sema_gen.rs`. Implementacje runtime mogą być dołączane jako osobna biblioteka;
generator nie powinien tworzyć pustych funkcji, które pozornie spełniają
kontrakt.
