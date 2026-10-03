# Dokumenty Ag, sema i coge — format 1

[English](DOCUMENT_CLI.md) | [Polski](DOCUMENT_CLI.pl.md)

`ag` i `agas` korzystają z tej samej obsługi Ag. `sema` linkuje tylko
`agsem_core`/`agas_core`; `coge` dodaje `coge_core`. Rodzaj dokumentu określa
nagłówek, a nie rozszerzenie pliku. Dokument v1 zawiera własną gramatykę i nie
odczytuje sąsiedniego Ag przez `for`.

```sh
cmake --build build --target ag sema coge -j4
build/bin/ag --check language.ag
build/bin/sema --check --contracts runtime.json language.sema
build/bin/coge --check --contracts runtime.json language.coge
build/bin/coge --emit-sema language.sema --contracts runtime.json language.coge
build/bin/coge --emit-ag language.ag language.coge
build/bin/sema --emit-ag language.ag language.sema
build/bin/sema --emit-rust-dir generated --contracts runtime.json language.sema
build/bin/coge --emit-rust-dir generated --contracts runtime.json language.coge
```

Brak operacji oznacza `--check`. Kontrola dopuszcza wiele wejść, emisja jedno.
Operacje wykluczają się. Kontrola nie emituje Rust ani nie tworzy plików.
Wykonawca, jego properties i zadeklarowane backendy są sprawdzane także bez
`generate_interpreter`. Wybór generacji wynika z deklaracji dokumentu.

`--emit-ag` wymaga poprawnej składni i właścicieli sekcji, bez przygotowania
analizy. `--emit-sema` wymaga rozwiązanych kontraktów całej zachowywanej analizy;
nie wymaga poprawnego semantycznie ciała usuwanego wykonawcy. Biblioteczne
`projectSema(document, bound, frontend)` przyjmuje frontend jawnie, żeby ponownie
sparsować i związać wynik przy użyciu wskazanych zasobów parsera.

## Jawne kontrakty

`ContractEnvironment` jest typowanym wejściem API. CLI ładuje JSON podany przez
powtarzalne `--contracts`. Nie skanuje Rust i nie uznaje samych nazw `FooId`
za deklaracje typów. Przykładowy manifest:

```json
{
  "format": 1,
  "id": "example-runtime",
  "version": "1",
  "semantic": [
    {"name": "Context", "kind": "opaque"},
    {"name": "Handle", "kind": "opaque"},
    {
      "name": "lookup", "kind": "function",
      "parameters": [{"name": "Int"}],
      "result": {"name": "Handle"},
      "effect": "pure",
      "rust": ["crate", "lookup"]
    }
  ],
  "execution": []
}
```

Typ jest obiektem `{"name": "List", "arguments": [{"name": "Handle"}]}`.
Eksporty typów mają rodzaj `opaque` (opcjonalne `arity`), `alias` (`target`),
`record` (`fields`: nazwa → typ), `enum` (`variants`: nazwa → lista typów danych).
Opaque umożliwia przekazywanie wartości, ale nie dostęp do nieopisanych pól.
Strukturalne kontrakty generyczne bez jawnych parametrów nie są obsługiwane.

Funkcja ma `parameters`, `result`, opcjonalne `effect: pure|mutates`, `context`
i ścieżkę `rust` z segmentów identyfikatorów. `method` ma taki sam kontrakt,
ale wymaga `context` i oznacza metodę kontekstu używaną w bindings, w odrębnej
kategorii od globalnych funkcji DSL. Dzięki temu `Context.convert` nie koliduje
z funkcją wykonawcy `convert`. Eksport `role` ma `type` i może jawnie opisać rolę
kontekstu dla binding. Wszystkie odwołania manifestu muszą być domknięte w
jawnie przekazanym zestawie. Zależność semantic → execution, konflikt wersji,
niezgodna deklaracja intrinsic lub efekt są błędami.

Ręcznie przygotowane kontrakty przykładów:

- [toyc-runtime-v1.json](../contracts/toyc-runtime-v1.json)
- [toycp-runtime-v1.json](../contracts/toycp-runtime-v1.json)

Opisują publiczny runtime potrzebny analizie i wykonawcy. Nie zastępują
kompilacji i testów zgodności z rzeczywistym Rust. Typy danych działań zachowują
różnicę `Result<T>` w analizie i `Result<T,E>` w wykonawcy.

## Profil wykonawcy i typowana inspekcja (5.5)

W `execution_model` można jawnie wybrać wbudowany profil:

```text
execution_model {
    execution_profile shared_value_v1;
    // Local types, state, intrinsics, functions and properties follow.
}
```

`shared_value_v1` w wersji `1` dostarcza 46 funkcji, dziewięć handlerów
`execute` i siedem `evaluate`. Katalog zawiera strukturalne drzewa wykonania;
wybór nie odczytuje plików ani nie rozpoznaje języka po nazwie. Profil nie
dostarcza typów, stanu ani implementacji intrinsic. Wymaga zgodnego interfejsu
runtime oraz lokalnych ciał `initialize_global`, `value_matches_type` i `load`.
Efekt `load` może być czysty lub mutujący; wywołania sprawdzają rzeczywisty efekt.
Pełny kontrakt i lista elementów są w
sekcji 15 historycznego planu implementacji (nieprzeniesionego do tego repozytorium).

### Wymagania i wybór generacji

Profil jest jawnie wybierany raz, wyłącznie w `execution_model`. Bez wyboru
profilu można nadal podać wszystkie funkcje i handlery lokalnie. Ścieżka
legacy bez związanych kontraktów odrzuca profil.

Autor zachowuje lokalne typy i `runtime_state runtime: Runtime`, w szczególności
`Runtime.store: Store<SymbolId, Value>` i `Runtime.steps_remaining: Index`.
Wymagane są warianty Value i Control oraz pola RuntimeError, PlaceView,
FunctionView, OperationNode i Expression opisane w sekcji 15.4 planu.
Typy payloadów, pola, sygnatury i efekty muszą odpowiadać jawnym kontraktom.
Dodatkowe pola i warianty są dozwolone, o ile efektywne ciała je obsługują.

Trzy obowiązkowe lokalne implementacje muszą mieć ciało `function`:

| Funkcja | Parametry → wynik | Efekt |
|---|---|---|
| `initialize_global` | `OpId` → `Result<Unit, RuntimeError>` | `mutates` |
| `value_matches_type` | `Value, Type` → `Result<Bool, RuntimeError>` | czysty |
| `load` | `PlaceId, SourceRange` → `Result<Value, RuntimeError>` | czysty lub `mutates` |

Sama deklaracja intrinsic nie spełnia tego obowiązku. Profil wymaga także
interfejsów `operation_node`, `expression_node`, `literal_identity`,
`module_operations`, `variable_type`, `variable_place`, `field_types`,
`function_view` i `place_view`, z sygnaturami podanymi w sekcji 15.4.
`place_view` może mutować; wywołująca go funkcja musi mieć zgodny efekt.
Pozostałe zależności, np. Store, AgSemRuntime i AccessError, wynikają z ciał
wybranych po rozstrzygnięciu nadpisań oraz stałych interfejsów profilu.

Wybór profilu nie wybiera celu generacji. `generate_interpreter = true;`
w `generation` włącza emisję interpretera i jego properties; lowering,
C i LLVM nadal wymagają własnych deklaracji. Check sprawdza wykonawcę
i properties również bez żądania ich emisji.

### Lokalne nadpisania

Lokalna funkcja zastępuje element katalogu o tej samej nazwie, zachowując typy
parametrów, wynik i efekt. Nazwy parametrów mogą się zmienić. Lokalny handler
zastępuje element o tym samym wariancie; jego wzorzec musi pasować do payloadu.
Dwa lokalne elementy o tym samym kluczu, dwa wybory profilu, nieznany profil
lub kolizja funkcji z intrinsic są błędami. Zależności sprawdzane są dla
wybranego ciała, także po nadpisaniu. Przy profilu wymagane jest pełne pokrycie
`Operation` i `ExpressionKind` oraz wewnętrznych match enum/Option/Result
bez wildcardu. Kontrola działa także bez wyboru generacji interpretera.

Nie używa się słowa `override`. Przykład zastąpienia komunikatu błędu
w istniejącym, kompletnym dokumencie:

```text
execution_model {
    execution_profile shared_value_v1;
    // Keep the required local declarations and implementations here.
    function invalid_value(source: SourceRange) -> Result<Value, RuntimeError> {
        runtime_error "custom invalid value" at source;
    }
}
```

Handler deklaruje się w `execution_contract`, np.:

```text
execution_contract {
    // Keep the other required local handlers here.
    evaluate Load(place) { return load(place, source); }
}
```

Wiązanie `source` jest dostarczane handlerowi przez wykonawcę. Klucze
`execute Call` i `evaluate Call` są odrębne. Nadpisanie zachowuje pozycję
katalogową; pozostałe lokalne elementy trafiają za katalog w kolejności
źródłowej. Zmiana nazw parametrów lub wiązań jest dozwolona, ale zmiana liczby,
kolejności lub typów parametrów, wyniku albo efektu funkcji jest odrzucana.
Nie można usuwać elementu katalogu, zastąpić go intrinsic ani użyć kolizji
z typem lub zarezerwowanym `execute_generated`/`evaluate_generated`.
Błędne nadpisanie nie przywraca po cichu ciała domyślnego.

Properties korzystają z efektywnych sygnatur po nadpisaniu: funkcja musi
pozostać czysta, przyjmować I32 oraz opcjonalny SourceRange i zwracać
`Result<I32, RuntimeError>`. Zastąpione ciało nie dodaje swoich usuniętych
zależności; stały interfejs profilu i trzy wymagane funkcje nadal obowiązują.

### Inspekcja, API i pochodzenie

`coge --inspect-model OUT --contracts runtime.json language.coge` dodaje
sekcję `execution_model` w formacie `coge-checked-execution-v1`. Jest dostępna
również bez profilu. Zawiera efektywne sygnatury i ciała, strukturalne wzorce,
stan, typy lokalne, intrinsic, properties, pokrycie, związane zależności oraz
pochodzenie `explicit`, `profile_default` lub `profile_override`.
Zależności pokazują sprawdzone typy i efekty, deklaracje lokalne lub
id/wersję/hash manifestu ze ścieżką JSON. Źródło elementu domyślnego wskazuje
deklarację wyboru profilu i identyfikator elementu katalogu.

Inspekcja podaje hash źródła, kontraktów, katalogu i efektywnego modelu.
Hash katalogu obejmuje jego ciała i wymagania; lokalne nadpisanie zmienia
hash efektywnego modelu. `--calls OUT` uwzględnia rozwinięcie profilu,
a metadane generacji Rust zapisują katalog w `execution_profiles`.
Publiczne API zwraca posiadany `CheckedExecutionModel` przez `CogeValidation`
i `CheckedGeneration`; inspekcja i emisja korzystają z tego samego wyniku
sprawdzania, z kontrolą tożsamości wejścia i kontraktów przy ponownym użyciu.

```sh
build/bin/coge --inspect-model /tmp/toyc-profile-model.json \
  --contracts contracts/toyc-runtime-v1.json coge/examples/toyc/toyc.coge
build/bin/coge --calls /tmp/toyc-profile-calls.txt \
  --contracts contracts/toyc-runtime-v1.json coge/examples/toyc/toyc.coge
```

`execution_model.profile` jest `null` bez profilu. Model zawiera także
`interpreter_requested`, `effective_sha256`, `required_implementations`,
`coverage` oraz `representation_obligations`. Metadane Rust zapisują
`execution_profiles: []` dla czystej sema i pełnego wykonawcy bez profilu.
Typowany dowód obejmuje sygnatury, wiązania wzorców i rozwiązane zależności;
JSON zachowuje całe ciała, ale nie zapisuje typu przy każdym literale.

`CheckedExecutionModel::model()/dependencies()/effectiveHash()`,
`inspectExecutionModel` i `executionCalls` udostępniają te same sprawdzone dane.
Przeciążenie `prepareGeneration(..., const CogeValidation&)` zachowuje wynik
wcześniejszego sprawdzania, dostępny następnie przez
`CheckedGeneration::execution()`. Model posiada dane i pozostaje ważny
po zniszczeniu dokumentu oraz środowiska kontraktów. Niezgodność tożsamości
źródła, kontraktów lub wyboru generacji jest odrzucana przed emisją.

### Granica dowodu i ABI

Przy profilu funkcje i zewnętrzne ramiona dispatchera otrzymują kolejność
katalogową, a dodatkowe lokalne elementy zachowują kolejność źródłową.
Bez profilu emisja zachowuje dotychczasowy porządek. Kompilator Rust nadal
sprawdza fizyczną postać wariantów, nazwy pól, Box, Clone/Copy oraz obecność
implementacji zewnętrznych intrinsic; inspekcja wymienia te obowiązki ABI.
Projekcje usuwają deklarację profilu razem z wykonawcą i nie wymagają
sprawdzenia usuwanego katalogu.

| Zakres | Sprawdza AgSem/Coge | Pozostaje do sprawdzenia przez Rust i testy |
|---|---|---|
| Interfejsy | Dostępność nazw, logiczne typy pól, uporządkowane payloady, sygnatury i efekty | Fizyczna postać tuple/struct wariantu, nazwy pól, reprezentacja ID, Box, Clone/Copy i widoczność |
| Ciała | Typy wywołań i zwrotów, mutacje, capture, wiązania wzorców i zależności efektywnego ciała | Rzeczywista implementacja intrinsic i zgodność manifestu z Rust |
| Pokrycie z profilem | Handlery wszystkich Operation/ExpressionKind i wewnętrzne match enum/Option/Result bez wildcardu | Ogólny dowód zakończenia i poprawności algorytmów; pokrycie wzorców liczbowych |
| Zachowanie języka | Sprawdzenie deklarowanych properties | Równoważność rozwinięcia z niezależną bazą oraz wyniki programów, kolejność obliczeń, błędy, ramki i cleanup sprawdzane testami |

Typowany model gwarantuje sprawdzone logiczne kontrakty w obecnym podzbiorze
DSL. Nie odczytuje definicji Rust i nie jest ogólnym modelem jego ABI.
ToyCP zachowuje interpreter, referencje, konstrukcję/destrukcję i cleanup;
profil nie dodaje mu lowering ani backendów C/LLVM. Szablony CLI i metadane `pending` są dostępne od 6.3;
częściowy raport kompletności pozostaje kolejnym krokiem etapu 6.

Odbiór 5.5 zakończono na rzeczywistych skróconych `.coge`: Rust 126/126,
CTest 102/104 po jednym punktowym powtórzeniu poprawionego testu projekcji.
Pozostałe dwa błędy dotyczą znanego xml.ag; wynik nie oznacza pełnego zielonego
korpusu Agas. Regeneracja ośmiu przykładów jest powtarzalna bajtowo.
Historyczny odbiór i pomiary
obejmują niezależną bazę SHA-256 pełnego Rust i ścisłe porównanie wersji
z profilem, dopuszczające wyłącznie kolejność całych funkcji i zewnętrznych
ramion dispatcherów.

## Projekcje i własność wyników

Gotowe eksporty ToyC/ToyCP znajdują się w
[PROJECTIONS.pl.md](PROJECTIONS.pl.md). README opisuje
własność kopii i polecenia regeneracji; kanoniczne przykłady Agas są utrzymywane
niezależnie od tych wyników.

Projekcja sema zmienia tylko token nagłówka `coge` na `sema` i usuwa konstrukcje
wykonania. Projekcja Ag usuwa wszystkie rozszerzenia i nagłówek specyfikacji.
Wspólny mechanizm operuje na zakresach parsera, zachowuje pozostałe bajty,
komentarze, kolejność, regexy, klasy leksera i lookahead.
Obowiązuje bajtowe prawo `Ag(Sema(Coge)) == Ag(Coge)`.

Istniejący wynik wymaga `--force`. Wejście jest chronione także przez symlink
lub hardlink; flaga nie znosi tej ochrony. Chronione są również jawne manifesty
kontraktów. Zapisy używają plików tymczasowych w katalogu celu i podmiany
pojedynczych plików. `OUT.provenance.json` jest zapisywany na końcu. Zawiera
wersję schematu, nazwy, hashe źródła, wyniku i uruchomionego binarium, kontrakty
oraz mapę pochodzenia. Nie zawiera czasu; powtórzenie jest deterministyczne.
Odbiorca musi porównać hash wyniku z manifestem — dwie podmiany nie są wspólną
transakcją. Pole `source` jest informacyjne, bez automatycznego odczytu.

Emisja Rust zachowuje ochronę obcych modułów i usuwa tylko wcześniej posiadane
wyniki. Manifest sema obejmuje jej moduły; nie przyznaje własności wykonawcy
ani backendów. `--calls OUT` raportuje wywołania zaakceptowanego dokumentu,
nie służy do sprawdzania zależności warstw.

## Legacy i ograniczenia

### Migracja poleceń

| Dawne użycie | Aktualne polecenie |
| --- | --- |
| `sema tests/fixtures/legacy/toyc_full.sema` | `coge --check --contracts contracts/toyc-runtime-v1.json coge/examples/toyc/toyc.coge` |
| `sema --emit-rust-dir DIR tests/fixtures/legacy/toycp_full.sema` | `coge --emit-rust-dir DIR --contracts contracts/toycp-runtime-v1.json coge/examples/toycp/toycp.coge` |
| `sema --legacy FILE.sema` | `coge --legacy FILE.sema` dla zachowanego mieszanego źródła legacy |
| `sema sema/examples/toyscope_1/toyscope_1.sema` | `sema --check --contracts contracts/toyscope-runtime-v1.json sema/examples/toyscope_1/toyscope_1.sema` |
| `check_*.py --sema SEMA --source tests/fixtures/legacy/toyc_full.sema` | `check_*.py --coge COGE --source coge/examples/toyc/toyc.coge --contracts contracts/toyc-runtime-v1.json` |
| `regenerate_examples.py --sema SEMA` | `regenerate_examples.py --coge COGE --sema-cli SEMA` |

`COGE` i `SEMA` oznaczają ścieżki do odpowiednich binariów. Skrypty nadal
akceptują `--sema` jako alias `--coge`, więc ten dawny argument oczekuje teraz
binarium **coge**. `--sema-cli` wskazuje binarium analizy w regeneracji ToyScope.
Kontrole ToyCP wymagają kontraktu `toycp-runtime-v1.json`.

Konwersja dokumentu obejmuje nagłówek formatu 1, pełną gramatykę i lexer,
właścicieli sekcji oraz jawne kontrakty runtime. Rodzaj dokumentu określa
nagłówek. Zestaw AgSem udostępnia już zmigrowane źródła przykładów.
CLI po błędzie starego wejścia wskazuje jawne legacy i nową ścieżkę;
przekazanie dokumentu do niewłaściwego CLI wskazuje narzędzie oraz eksport
analizy. Adapter legacy nadal działa i opisuje tryb zgodności.

### Stan przykładów

Autorskie źródła ToyC/ToyCP są samodzielnymi dokumentami formatu 1:

```sh
build/bin/coge --check --contracts contracts/toyc-runtime-v1.json coge/examples/toyc/toyc.coge
build/bin/coge --check --contracts contracts/toycp-runtime-v1.json coge/examples/toycp/toycp.coge
build/bin/coge --emit-rust-dir /tmp/toyc-v1 --contracts contracts/toyc-runtime-v1.json coge/examples/toyc/toyc.coge
```

Każdy dokument zawiera pełną gramatykę, lexer i analizę oraz wybiera wspólny
profil wykonawcy z jawnymi lokalnymi różnicami; nie odczytuje
zewnętrznego `.ag`. Kontrakty runtime są jawnym wejściem. Dawne
`toyc_typed.sema` i `toycp_typed.sema` pozostają tymczasowo jako źródła legacy
do porównań oraz testów prywatnego adaptera legacy. Nie są projekcjami nowych
`.coge`. Regeneracja ToyC/ToyCP używa już nowych źródeł i kontraktów:

```sh
python3 tools/regenerate_examples.py toyc toycp --coge build/bin/coge
python3 tools/check_return_policy.py --coge build/bin/coge --source coge/examples/toyc/toyc.coge --contracts contracts/toyc-runtime-v1.json
```

Argument skryptu `--sema` jest aliasem `--coge`. Samo `sema --legacy` wyjaśnia
zmianę komendy. Legacy zachowuje historyczną walidację i nie dowodzi granic v1.
Starsze przykłady są nadal regenerowane przez `coge --legacy`.
Typowane ToyScope mają czyste projekcje `.sema` z jawnym kontraktem runtime;
analizatory są generowane przez `sema`. Pełne `.coge` zachowują historyczne
kontrakty wykonania wymagające konwersji:
[przegląd ToyScope](../sema/docs/TOYSCOPE.pl.md).

`execution result` ma właściciela i jest poprawnie usuwane z projekcji, ale
kontrola i emisja zgłaszają `coge.unsupported_execution_result`. Połączenia
wykonawcy z funkcjami sema, dla których istniejący emiter nie ma obsługi,
zgłaszają `coge.unsupported_semantic_call`. Pozostałe postacie spoza obecnego
podzbioru checkera są jawnie odrzucane. Kontrola kompletności jest opisana
poniżej w sekcji 6.4.

## Wartości enum i inspekcja rozwinięcia (5.1)

W akcji `Operator.Add` oznacza wariant enum zadeklarowanego w jawnym
kontrakcie z `kind: "enum"`. Wariant musi istnieć i nie mieć danych; ten krok
nie wprowadza konstruktorów z argumentami ani wartości enum generycznych.
Lokalna zmienna o nazwie `Operator` przesłania przestrzeń typu. Typ wyniku
pozostaje `Operator`; zwykła kontrola argumentów i wyników nadal obowiązuje.

Emisja korzysta ze ścieżki `rust` typu w kontrakcie, a przy jej braku
z dotychczasowej konwencji `crate::Operator::Add`. Nie odczytuje definicji Rust.
Schemat i wybór wariantu są sprawdzane przed emisją, a emiter odczytuje
niemutowalny plan wartości enum.

```sh
build/bin/coge --inspect-model /tmp/toyc-model.json \
  --contracts contracts/toyc-runtime-v1.json coge/examples/toyc/toyc.coge
build/bin/sema --inspect-model /tmp/toyc-sema-model.json \
  --contracts contracts/toyc-runtime-v1.json coge/examples/toyc/generated/projections/toyc.sema
```

`--inspect-model OUT` jest osobną operacją, wymaga jednego wejścia, chroni
źródło i kontrakty przed nadpisaniem, a istniejący wynik zastępuje tylko
z `--force`. Wynik JSON `agsem-checked-semantic-expansion-v3` obejmuje sygnatury
funkcji i analizatorów, jawne schematy enum oraz typowane wartości z wariantem,
ścieżką emisji i zakresem bajtów w źródle. Zawiera tożsamości źródła i kontraktów.
Coge dodaje do tego formatu `execution_configuration` z typowanym rozwinięciem
konfiguracji lowering i backendów (5.4), opisanym poniżej, oraz pełny sprawdzony
`execution_model` w formacie `coge-checked-execution-v1` (5.5), opisany powyżej.
Nie jest to pełna serializacja wszystkich polityk semantycznych. Coge kontroluje
także kontrakty wykonania; ograniczenia ToyScope opisane w
`TOYSCOPE_MIGRATION.md` nadal obowiązują.

API biblioteczne udostępnia `checkedEnumConstants(const CheckedSemantics&)`
oraz `inspectSemanticModel(const CheckedSemantics&)`. Dane pochodzą
z przygotowanego modelu, bez ponownego analizowania AST w emiterze.


## Typowane kolekcje (5.2)

W akcjach analizy i funkcjach semantycznych dostępna jest wspólna przestrzeń `List`:

```text
List.empty(ExprId)
List.single(value)
List.append(values, next_value)
```

`empty` wymaga jawnej nazwy typu elementu bez parametrów generycznych,
sprawdzanej względem typów wbudowanych i kontraktów. `single` wyznacza typ
z wartości. `append` wymaga `List<T>` i wartości dokładnie typu `T`.
Wynik jest własną listą; `append` pożycza wejście i klonuje jego elementy,
zachowując ich kolejność i wejście. Elementy muszą implementować Rust `Clone`;
ten obowiązek sprawdza kompilator Rust. Nie jest to operacja mutująca kontekst.
Operacje nie obsługują elementów AST `Node<R>` i `Token` (również w opakowaniach),
które mają osobną pożyczoną reprezentację,
ani wartości `none` bez ustalonego typu. Lokalna nazwa `List` przesłania przestrzeń
wbudowaną i nie jest automatycznie reinterpretowana.

Inspekcja v2 dodaje `collection_operations`: nazwę operacji, konkretny typ
elementu i wyniku, typy operandów wartościowych (dla `empty` brak), zasady
własności operandów, typ elementu Rust, jawne wyrażenie emisji oraz zakres źródła. API
`checkedCollectionOperations(const CheckedSemantics&)` udostępnia te same dane.
Format zastępuje pilotażowy `agsem-checked-enum-expansion-v1`; pola enum pozostają.
Emiter używa przygotowanych operacji i nie odczytuje ponownie ich AST.


## Profile lowering i backendów (5.4)

Zamiast pełnego zestawu przypisań można jawnie wybrać wspólny profil:

```text
lowering_model {
    profile = structured_core_v1;
    lower = lower_function_to_structured;
}
backend_c {
    profile = core_c_v1;
}
backend_llvm {
    profile = core_llvm_v1;
}
```

`structured_core_v1` określa 42 domyślne powiązania pól i wariantów obecnego
lowering do strukturalnego Core IR. Nazwa funkcji `lower` pozostaje wymagana.
Profil nie jest wybierany przez nazwę języka ani podobieństwo typów. Wymaga
jawnych kontraktów modelu i `rust_context Context`. Kontrola sprawdza rodzaj
record/enum, dokładne typy pól oraz uporządkowane typy danych wariantów, także
stałe zależności emitera: moduły, agregaty, miejsca, zakresy źródła i komentarze.
Nie wyszukuje zamiennika, gdy wskazane pole nie istnieje.

Każde lokalne przypisanie nadpisuje tylko swoją rolę, na przykład
`functions = my_functions;` wymaga `Context.my_functions: List<Function>`.
Duplikaty, nieznane role/profile, błędne typy i kolizje z nazwami pomocników
są błędami. Niepełne konfiguracje bez profilu nadal są odrzucane.
`core_c_v1` / `core_llvm_v1` domyślnie wybierają `emit_c` / `emit_llvm`
i sprawdzoną funkcję lowering. Można nadpisać `emit` oraz jawnie podać `lower`,
które musi zgadzać się z konfiguracją lowering. Backend wymaga lowering.

`coge --inspect-model OUT` dodaje `execution_configuration`
(format `coge-checked-configuration-v1`) z sekcjami `lowering`, `backend_c`
i `backend_llvm`; niezadeklarowana sekcja ma wartość `null`. Rozwinięcie zawiera:

- identyfikator i SHA-256 specyfikacji wybranego profilu;
- pełne powiązania z pochodzeniem `explicit`, `profile_default` lub
  `checked_lowering` oraz zakresem bajtów przypisania/profilu w źródle;
- strukturalne `TypeRef` sprawdzonych pól i danych wariantów, przypisane do ról;
- interfejsy, czysty odczyt kontekstu, zależności backendu i możliwości lowering.

SHA-256 identyfikuje tabelę domyślnych ustawień, wymagania typów i interfejs
profilu; lokalne nadpisania nie zmieniają tej tożsamości. Tożsamości wejścia
oraz manifestów pozostają w głównym modelu JSON. API
`CheckedLowering::inspection()` i `CheckedGeneration::lowering()/backends()`
udostępnia te same przygotowane dane. Emitery otrzymują rozwinięte konfiguracje,
bez syntetyzowania DSL ani ponownego parsowania tekstu.

Profil zachowuje dotychczasowe ograniczenia lowering: nazwy typów oraz
reprezentację Rust wariantów i identyfikatorów. Logiczne kontrakty nie opisują
układu record/tuple wariantów, pól identyfikatorów ani cech `Clone`/`Copy`;
te obowiązki sprawdza kompilacja wygenerowanego Rust. Profil nie poszerza
obsługi IR ToyCP ani klas i cleanup. ToyC korzysta z profili, ToyCP pozostaje
przy interpreterze. Eksport sema usuwa konfiguracje wykonania, a format inspekcji
sema ma format `agsem-checked-semantic-expansion-v3`. Profile 5.4 są
niezależne od schematu powiązań modelu 5.3 opisanego poniżej.


## Schemat powiązań modelu (5.3)

`semantic_model` może jawnie wybrać wspólny schemat:

```text
semantic_model {
    model_schema standard_semantic_v1;
    rust_context Context;
    // Policy declarations remain explicit.
    model_bindings function_return {
        return_ir = tuple;
        cleanup_scopes = no_scopes;
    }
    flow_bindings statements {
        error_ir = Error;
    }
}
```

Schemat uzupełnia porty istniejących polityk return, assignment, condition,
statement, flow i selection. Nie wybiera polityki ani nie tworzy jej deklaracji.
Wymaga sprawdzonych kontraktów pól i metod dla typu z `rust_context`.
W modelu dozwolony jest jeden schemat i jedna polityka każdej rodziny.
Nieznany lub powtórzony schemat jest błędem. Nazwa `model_schema` pozostaje
zwykłą nazwą reguły Ag poza klasą leksykalną MODEL.

Przy schemacie bloki powiązań zawierają tylko lokalne odstępstwa; blok można
pominąć, gdy wszystkie jego porty mają domyślne cele. Tryby `return_ir`
i `cleanup_scopes` oraz warianty `error_ir` i `construction_ir` pozostają jawne.
ToyC wybiera `tuple` / `no_scopes`, ToyCP `record` / `active_scopes` oraz
`construction_ir = Construct`. Deklaracje polityk, ich IR i podział modułów
pozostają w źródle. Bez schematu wszystkie powiązania nadal są wymagane.

Na przykład:

```text
assignment_bindings store_value {
    flow = branch_flow;
    poison = branch_poisoned;
}
```

zmienia tylko dwa porty assignment. Wymaga pola `C.branch_flow: Flow`
i metody `C.branch_poisoned(ExprId) -> Bool` bez mutacji kontekstu.
Nie zmienia return.flow ani return.poison. Pole o podobnym typie lub metoda
o takiej samej sygnaturze nie zastępują brakującego celu domyślnego.
Port `fields` w selection wymaga dwóch pól o wspólnej nazwie:
`C.fields: List<Field>` i `Struct.fields: List<FieldId>`; nadpisanie nazwy
sprawdza obie ścieżki. `condition.emit` wymaga budowniczego ExpressionKind,
a `return.emit` i `statement.emit` budowniczego Operation.

Walidacja porównuje strukturalne TypeRef z uwzględnieniem aliasów, pełne
sygnatury metod, ich właściciela i efekt. Sprawdza też odczyty i warianty
używane przez aktywny adapter, m.in. Function.result, Flow.reachable,
Type.Void, Operation.Return i warianty statement. Cleanup i selection
wprowadzają dodatkowe wymagania tylko przy aktywnej polityce. Cel metody musi
być metodą wybranego kontekstu; nieobsługiwane przekierowanie `rust` jest błędem.
Efekt `pure` opisuje odbiorcę, więc metoda `mark_initialized` może mutować
przekazany Flow przy niemutowanym kontekście.

Inspekcja v3 zachowuje wartości enum i kolekcje oraz dodaje:

- `binding_schema`: ID, wersję, SHA-256 kanonicznej tabeli portów i zależności
  oraz zakres deklaracji wyboru schematu;
- `model_bindings`: wszystkie porty, jawne i domyślne, w kolejności rodziny,
  polityki i portu; rodzaj celu, tryby zwrotu/cleanup, wymagane i rzeczywiste
  typy/sygnatury, efekty, odczyty/zapisy, wywołania i przekazanie parametrów;
- `binding_dependencies`: uporządkowane, sprawdzone zależności adapterów;
- `binding_representation_obligations`: obowiązki pozostawione kompilacji Rust.

Pochodzenie wskazuje wpis jawny albo wybór schematu, lokalizację polityki,
manifest (ID, wersję i hash) oraz rzeczywistą ścieżkę JSON eksportu/pola.
Zewnętrzne kontrakty nie dostają fikcyjnego zakresu w źródle DSL.
Deklaracje DSL mają rzeczywiste lokalizacje. Hash schematu jest niezależny
od nadpisań i hashy źródła/manifestów. Lokalizacje mogą zmienić się po projekcji;
typy, efekty, cele, zależności i tożsamość schematu pozostają takie same.

Publiczne API `checkedModelBindings(const CheckedSemantics&)` udostępnia
niemutowalny plan. Plan powstaje w binderze, jest zachowany w BoundSemantics
i przekazany do przygotowania. Emitery używają kompletnych map z tego planu.
Nie rozwijają tekstu DSL ani nie dobierają brakujących celów. Archiwalne
`coge --legacy` bez jawnych kontraktów zachowuje poprzednią ścieżkę; nie jest
przedstawiane jako typowane rozwinięcie. Próba użycia schematu przez preparator
bez bindera jest odrzucana.

`--emit-sema` zachowuje deklarację schematu i lokalne odstępstwa dosłownie.
Manifesty projekcji zawierają `binding_schema`. Generacja zapisuje obok Rust
`sema_generation.provenance.json` z hashami źródła, kontraktów i tożsamością
schematu (albo `null`, gdy nie został wybrany). Ten plik jest metadokumentacją,
a kod Rust pozostaje bajtowo identyczny jak dla pełnych jawnych powiązań.
Wynik chroni źródło, kontrakty, dowiązania i obcy plik metadanych przed nadpisaniem.

Diagnostyka rozróżnia m.in. `sema.unknown_model_schema`,
`sema.duplicate_model_schema`, `sema.unknown_binding_slot`,
`sema.duplicate_binding`, `sema.orphan_binding`, `sema.missing_binding`,
`sema.binding_target_mismatch`, `sema.binding_type_mismatch`,
`sema.binding_effect_mismatch` i `sema.binding_dependency_mismatch`.
Komunikaty zawierają rodzinę, politykę, port i oczekiwany/rzeczywisty cel;
błąd kontraktu wskazuje jego tożsamość i ścieżkę JSON.

Manifesty opisują logiczne typy. Układ tuple/record wariantów i nazwy ich pól,
reprezentacja ID przez `.0`, Clone/Copy, pożyczenia i ABI metod/funkcji
skojarzonych są wymaganiami istniejących adapterów, sprawdzanymi przez Rust.
5.3 nie dodaje introspekcji Rust ani ogólnego modelu ABI.

## Szablony — etap 6.3

```sh
sema --from-ag language.ag --emit-template language.sema
coge --from-ag language.ag --target interpreter --emit-template language.coge
coge --from-sema language.sema --contracts runtime.json --target interpreter --emit-template language.coge
coge --from-sema language.sema --contracts runtime.json --target c --target llvm --emit-template backends.coge
```

W przykładach `sema` i `coge` oznaczają binaria z katalogu build. Operacja
wymaga dokładnie jednego wejścia wskazanego przez `--from-ag` lub `--from-sema`.
Coge wymaga jawnego `--target`; obsługuje `interpreter`, `c`, `llvm` i wiele
różnych celów naraz. Nie wybiera profilu ani reprezentacji runtime za autora.

Z Ag kopiowana jest cała gramatyka: opcje, lexer, klasy, komendy, komentarze
i oryginalne bajty. Przy każdej autorskiej alternatywie, także `empty` i inline,
pojawiają się pusty `analysis {}` oraz:

```text
analysis_status {
  id "alt-v1:<64 lowercase hex digits>:0";
  syntax_sha256 "<64 lowercase hex digits>";
  state pending;
}
```

Znacznik należy do sema. Zapisane ID nie zależy od aktualnej pozycji;
deterministyczny podpis składni wyklucza komentarze i treść analysis.
Identyczne alternatywy przy pierwszym szablonie otrzymują różne liczniki.
Ponowne tworzenie szablonu z Ag tworzy nowe pending, bez odzyskiwania decyzji.
Duplikaty ID/bloku statusu są błędem. Zmiana składni przy starym podpisie daje
`completeness.stale_alternative` podczas check/przygotowania; eksport Ag nadal
działa, gdy jego składnia jest poprawna. Przy edycji przenosić status razem
z alternatywą. `implemented` wymaga rzeczywistego sprawdzenia ciała/interfejsu.

Z sema zachowywany jest cały tekst analizy i jej metadane; zmienia się słowo
nagłówka i dopisywane są tylko miejsca wykonania. Wiązanie zachowywanych
kontraktów wymaga `--contracts` jak przy eksporcie sema, bez wymagania
kompletności ciał analizy. Coge dodaje sekcję należącą do swojej warstwy:

```text
execution_obligations {
  targets "interpreter";
  pending "execution_contract" "ir" "schema" "Select the execution IR explicitly.";
}
```

Każde `pending` zawiera rodzaj, cel, rolę i opis wymaganego uzupełnienia.
Dla znanego enum Operation/ExpressionKind wypisane są rzeczywiste warianty
z jawnego modelu/manifestu. Bez takiego schematu pozostaje nierozwiązane
wymaganie wyboru IR. Wybrane C/LLVM dostają miejsca konfiguracji lowering
oraz właściwych bindingów backendu; niewybrane cele nie dodają takich miejsc.
Metadane są listą pracy dla autora, nie implementacją ani dowodem kompletności.
Po napisaniu wymaganej części autor usuwa odpowiadający jej wpis pending;
checker nadal sprawdza rzeczywiste deklaracje i pokrycie wariantów.

Szablon i jego `.provenance.json` zapisują się atomowo jako osobne pliki,
z mapą zakresów skopiowanych/dodanych, hashami oraz jawnymi celami.
Nadpisanie wyniku wymaga `--force`; wejście i manifesty kontraktów są chronione
również przez aliasy symlink/hardlink. Projekcja do Ag usuwa metadane i analysis;
może pozostawić tylko dodane białe znaki. Coge → sema zachowuje statusy analizy
i usuwa `execution_obligations`.

Jeśli wejście sema nie kończy się LF lub CR, szablon dodaje separator LF
przed sekcją wykonania. Dotyczy to także końcowego komentarza `//` ze spacją
lub tabulatorem: metadane zaczynają się na nowej linii. Istniejące bajty źródła
pozostają zachowane; po projekcji z powrotem do sema może zostać ten dodatkowy LF.

Szablon nie tworzy `CheckedSemantics` ani pozornie działającego wykonawcy.
Historyczny `tools/ag_to_sema_template.py` jest oznaczony jako legacy;
nowe szablony powstają przez wspólny parser Ag i API bibliotek.

## Kontrola kompletności i raport braków (6.4)

```sh
sema --check draft.sema
sema --check-complete --report completeness.json draft.sema
coge --check-complete --target c --report completeness.json \
  --contracts contracts/toyc-runtime-v1.json program.coge
```

`--check` sprawdza napisane fragmenty; brak obowiązkowej implementacji daje
ostrzeżenie i sam nie zmienia kodu wyjścia. Nieznana nazwa, zły typ, błędne
metadane lub zabroniony efekt nadal dają błąd. `--check-complete` dodatkowo
wymaga spełnienia wszystkich obowiązków wybranych celów i zwraca kod 1 przy
brakach. Obie operacje domyślnie wypisują diagnostykę z plikiem, linią i kolumną.

`--report OUT` zapisuje JSON `agsem-completeness-v1` dla jednego wejścia.
Raport powstaje także przy błędach walidacji lub niekompletnych ciałach,
jeśli parser zbudował poprawny AST. Błąd składni nie tworzy pozornego raportu.
Nadpisanie raportu wymaga `--force`; źródło i manifesty są chronione również
przez aliasy. Sam raport nie modyfikuje statusów w źródle.

Raport zawiera tożsamość źródła i kontraktów, cele, obowiązki o strukturalnych
kluczach, oczekiwane typy/sygnatury lub jawny nierozwiązany warunek, stany
`pending` / `implemented` / `no_action`, osobną walidację
`valid` / `invalid` / `blocked`, dowody, zależności, pochodzenie kontraktów
i lokalizacje z zakresami bajtowymi oraz linią/kolumną. Indeksy `diagnostics`
odsyłają do tablicy diagnostyk. Obowiązki są deterministycznie uporządkowane.
Diagnostyki braków w danych raportu mają poziom warning; tryb ścisły traktuje
je jako błędy na terminalu i kończy się kodem 1.

Każda autorska alternatywa ma obowiązek analizy, także bez `analysis_status`.
Dla niezapisanej tożsamości raport tworzy deterministyczny ID z podpisu,
ale oznacza `identity_persisted: false`; nie dowodzi to tożsamości historycznej.
Usunięcie znacznika `pending` nie usuwa obowiązku. Jawne `pending` pozostaje
pending nawet przy poprawnym ciele. Błędny podpis daje
`completeness.stale_alternative`, a fałszywe `implemented` jest błędem.

`no_action` wymaga uzasadnienia, jawnego interfejsu analizatora z wynikiem
`Unit` i braku instrukcji we wszystkich blokach tej alternatywy. Sprawdzony
plan `NoActionUnit` zwraca `Ok(())` bez analizowania dzieci. Nie zastępuje
handlerów ani wyników innych typów. Puste, sprawdzone przekazanie analizy
do dziecka jest `implemented`, z dowodem `checked_forwarding`.

Analiza jest zawsze wybrana. W coge cele wynikają z deklarowanej generacji,
konfiguracji lowering/backendów, `execution_obligations.targets` oraz
opcjonalnych, powtarzalnych `--target interpreter|c|llvm`. Wybór C/LLVM
wymaga także lowering. Brakujące konfiguracje są wykrywane również po
usunięciu całej listy wpisów pending. Niewybrany backend nie jest wymagany.
Znany schemat IR wyznacza wszystkie obowiązkowe handlery; rozszerzenie enum
wymaga nowego handlera. Kolektor wykorzystuje efektywne rozwinięcie profilu
i lokalne nadpisania, a lokalne obowiązki profilu pochodzą z jego katalogu.
Gdy IR nie jest znany, raport wymaga jego kontraktu zamiast wymyślać warianty.

`results` podaje `complete` i `can_emit` oddzielnie dla każdego celu.
Kompletność dotyczy wykrywalnych obowiązków jawnego modelu; nie dowodzi
wszystkich reguł znaczeniowych języka. `can_emit` uwzględnia dodatkowo
ograniczenia istniejącego emitera Rust, m.in. reprezentacji `empty` i bindingów
zewnętrznych funkcji. Emisja wymaga obu warunków i późniejszego pełnego
przygotowania planu; raport częściowy nigdy nie służy do emisji. ABI Rust,
zgodność reprezentacji i zakończenie wykonania pozostają poza tym dowodem.
Projekcje i szablony nadal nie wymagają kompletności; eksport sema zachowuje
wymagania domknięcia kontraktów.

## Blokada generowania i dowód przygotowania (6.5)

Generowanie Rust odmawia pracy przy nierozwiązanych obowiązkach, także
przy poprawnym ciele oznaczonym `pending` i po usunięciu wpisów pending
z metadanych wykonania. Brak obowiązkowego handlera jest sprawdzany również
w modelu bez profilu. Odmowa następuje przed zapisem plików Rust, manifestu
i provenance; `--force` nie omija kontroli kompletności.

Biblioteczne `checkCoge` sprawdza kompletność i zwraca `CogeValidation`
z niezmiennym raportem i konfiguracją. Oba przeciążenia `prepareGeneration`
wymagają kompletnych, obsługiwanych przez emitter celów oraz zgodnej
tożsamości źródła, kontraktów i wyboru generacji. Gotowy `CheckedGeneration`
zachowuje dowód także po zniszczeniu wejść; emitter ponownie sprawdza jego
powiązanie. `CogeValidation` można kopiować, lecz nie składać ręcznie ani
zmieniać jego konfiguracji; dostęp zapewniają metody `execution()`,
`lowering()`, `backends()` i `completeness()`.

Ścieżka starego adaptera nie przyjmuje samodzielnego `ExecutionInput`
formatu 1. Przekazanie `CheckedExecutionModel` do surowego
`prepareCheckedGeneration` również wymaga przejścia przez API dokumentu
z dowodem kompletności (`document.typed_preparation_required`). Historyczne
wejścia legacy i programowo budowane plany bez modelu dokumentu zachowują
dotychczasową walidację. Szablony i projekcje mogą nadal zawierać braki.

## Odbiór etapu 6 i testy (6.7)

Po utworzeniu szablonu uzupełnij kontrakty i akcje, a następnie zmień status
każdej rozwiązanej alternatywy na `implemented` albo poprawne `no_action`
z uzasadnieniem. Zachowaj jej `id` i `syntax_sha256`; zmiana składni wymaga
ponownej kontroli podpisu. Przy dodawaniu nowej alternatywy szablon lub
raport wyznacza jej własny obowiązek. Projekcja do Ag i ponowne utworzenie
szablonu ustawia wszystkie alternatywy na pending, bez odzyskiwania zatwierdzeń.

| Operacja | Brak implementacji | Rzeczywisty błąd typu/nazwy | Kompletny model z nieobsługiwaną emisją |
|---|---|---|---|
| `--check` | Warning, kod 0 | Error, kod 1 | Kod 0, jeśli sam model jest poprawny |
| `--check-complete` | Error, kod 1 | Error, kod 1 | Kod 0; raport może mieć `can_emit: false` |
| `--emit-rust-dir` | Odmowa, kod 1 | Odmowa, kod 1 | Odmowa, `document.unsupported_construct` |

Tabela zakłada poprawne argumenty i dostępne ścieżki wyjścia. Przykładem
ostatniej kolumny jest `node start : empty #Start` z jawnym analizatorem
`Unit` i pustą akcją zatwierdzoną jako `no_action`: obowiązek jest spełniony,
ale obecny emiter Rust nie obsługuje tego kształtu AST. Przy błędzie składni
raport nie powstaje. Odmowa emisji zachowuje istniejące pliki również z `--force`.

Zestaw odbioru ma etykietę CTest `agsem_stage6` i obejmuje 16 testów.
Polecenia wykonuj z głównego katalogu repozytorium:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target agsem_stage6_tests -j 4
ctest --test-dir build -L '^agsem_stage6$' --output-on-failure -j 3
```

Cel budowania przygotowuje narzędzia i testy natywne potrzebne temu zestawowi.
Etykieta obejmuje API kompletności, raport CLI, macierz odbioru 16.8, szablony,
projekcje, niezależne linkowanie analizy, granice bibliotek i generację
przykładów ToyC/ToyCP. Test projekcji przygotowuje pliki wejściowe dla testu CLI;
CTest zachowuje tę kolejność. Pełny zestaw w lokalnym odbiorze trwał około
2–3 minut. Szczegóły pokrycia są w planie, sekcje 16.13–16.14.

Do sprawdzenia samej kompletności i blokady generacji można wybrać:

```sh
ctest --test-dir build -L '^agsem_stage6$' \
  -R 'agsem_(stage6_acceptance|completeness|completeness_cli)_tests' --output-on-failure
```

Ten wybór nie zastępuje odbioru szablonów, projekcji ani zgodności przykładów.
