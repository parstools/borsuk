# Core IR — wspólna reprezentacja po analizie semantycznej

[English](CORE_IR.md) | [Polski](CORE_IR.pl.md)

Core IR jest wspólnym wynikiem obniżania semantyki. Moduł
`runtime/src/structured_ir.rs` zachowuje bloki, warunki, pętle i zwrot. Backend C
odczytuje tę warstwę i emituje strukturalny C bez `goto`; backend LLVM dostanie
jej rozwinięcie do CFG przez `structured_ir::lower_to_cfg`. Moduł
`runtime/src/core_ir.rs` zawiera typy i weryfikator warstwy CFG.
Pilotażowy backend LLVM emituje tekst IR przez `runtime/src/llvm_backend.rs`;
sekcja `backend_llvm` w `.sema` generuje adapter języka.

Komentarze programu źródłowego są danymi prezentacyjnymi, nie instrukcjami
semantycznymi. ToyC i ToyCP zachowują obecnie tokeny komentarzy z kanału
`HIDDEN` w `Context.comments`, razem z treścią i zakresami bajtów. Generowany
lowering ToyC przenosi komentarze z ciała funkcji do strukturalnego modułu.
Pilotażowy backend C emituje je według pozycji źródłowych przy instrukcjach,
zachowując treść i względną kolejność. Nie obiecujemy identycznego układu
białych znaków po transpilacji.

Nie zastępuje drzewa parsowania ani obecnego IR ToyC/ToyCP: generowany etap
`lowering_model` tłumaczy IR języka na Structured Core IR. ToyC ma pilotażowy generator
`lowering_gen.rs` dla bezargumentowego `main` i osiągalnych funkcji o skalarnych
sygnaturach. Obsługuje lokalne deklaracje skalarne, inicjalizacje,
przypisania, zagnieżdżone bloki, `if`, `while`, `for` oraz `return`.
Wyrażenia obejmują stałe skalarne, odczyty lokalnych zmiennych, wywołania,
konwersje, arytmetykę i porównania. Pozostałe
konstrukcje są jawnie odrzucane. Jest emiter C11 dla tego wycinka oraz
wykonawca Core IR; obecny interpreter ToyC nadal wykonuje IR języka.

Strukturalny moduł składa się z funkcji i bloków. Instrukcje typowane są
wspólne z CFG, a `Statement` zachowuje zagnieżdżony blok, `If`, `While`,
`For` i `Return`. Warunki `While` i `For` mają osobne listy instrukcji, które rozwinięcie
umieszcza w blokach nagłówka wykonywanych przy każdej iteracji. `For` zachowuje
osobne bloki inicjalizacji i kroku; krok jest osiągalny tylko po normalnym
zakończeniu ciała. Komentarze są obecnie poboczną listą z zakresami źródła.
`lower_to_cfg` rozwija sterowanie
do skoków, po czym uruchamia weryfikator CFG; nie dodaje domyślnego zwrotu
dla funkcji wymagającej wyniku.

## Kontrakt danych

`Module` posiada tablicę typów i funkcji. Indeksy są jawnie rozdzielone na
`TypeId`, `FunctionId`, `BlockId`, `SlotId` i `ValueId`. Funkcja ma sloty,
listę slotów parametrów, opcjonalny typ wyniku, bloki i blok wejściowy.
Każdy blok kończy się dokładnie jednym terminatorem: skokiem, rozgałęzieniem,
zwrotem lub błędem. Brak terminatora jest niepoprawnym IR.

Typy pierwszego wycinka to `i32`, `f32`, `bool`, `u8`, adres, tablica i agregat.
Brak wyniku funkcji jest reprezentowany przez `None`, bez typu `void`.
Stała `Null` ma konkretny typ adresowy wskazany przez wynik instrukcji.
`i32` i `f32` odpowiadają aktualnym liczbom ToyC; nie wprowadzamy tu BigInt.
`SourceSpan` zachowuje identyfikator pliku i zakres bajtów, aby późniejsze
diagnostyki mogły wskazać kod źródłowy.

Wartość tymczasowa jest dostępna wyłącznie od swojej definicji do końca
bieżącego bloku. Dane potrzebne w innym bloku przechodzą przez slot i jawne
`load`/`store`. To celowo prostszy model niż SSA z węzłami phi. Weryfikator
odrzuca odczyt wartości z innego bloku i ponowną definicję identyfikatora
w tym samym bloku. Sloty parametrów są zainicjalizowane na wejściu; reszta
slotów wymaga inicjalizacji podczas wykonania. Analiza przepływu inicjalizacji
slotów nie jest jeszcze częścią weryfikatora Core IR.

Instrukcje pierwszego wycinka obejmują stałe, adres slotu, `load`, `store`,
konwersję, działanie arytmetyczne, porównanie skalarów z wynikiem `bool`,
wywołanie i kontrolę zakresu. Arytmetyka
`i32` deklaruje `CheckedI32`, a `f32` — `IeeeF32`. Pierwszy tryb oznacza błąd
wykonania przy przepełnieniu i dzieleniu przez zero; drugi stosuje arytmetykę
IEEE 754. `BoundsCheck(index, length)` wymaga `i32` i zgłasza błąd wykonania,
gdy `index < 0` lub `index >= length`. Te skutki muszą zostać odtworzone przez
każdy backend; sam weryfikator kontroluje tylko typy i strukturę.

ToyC obsługuje teraz lokalne jednowymiarowe tablice skalarów. Indeks ma typ
`int`; obniżanie emituje `BoundsCheck` i `IndexAddress`, a backend C tworzy
tablicę elementów oraz znaczniki inicjalizacji. `ResetArray` zeruje znaczniki
przy każdym wykonaniu deklaracji, także przy ponownym wejściu do pętli.
Odczyt niezapisanego elementu
zgłasza `variable used before initialization`. Przy zapisie indeks jest
obliczany przed prawą stroną, a sprawdzenie granic następuje po niej, zgodnie
z interpreterem. Tablice złożonych typów pozostają poza tym wycinkiem.

Struktury z polami skalarnymi lub zagnieżdżonymi strukturami są reprezentowane
przez `Aggregate`. `FieldAddress`
przyjmuje adres struktury i numer pola, a weryfikator sprawdza istnienie pola
oraz typ wynikowego adresu. Lokalna struktura ma osobne znaczniki inicjalizacji
skalarnych pól końcowych; pozycja znacznika wynika z rekurencyjnej ścieżki pól.
`ResetAggregate` zeruje je przy deklaracji, także po ponownym wejściu do
bloku. Globalne struktury zaczynają od wartości zerowych. Obniżanie ToyC oraz
backendy C i LLVM obsługują odczyt, zapis i aktualizację pól skalarnych na
dowolnej głębokości. `CopyAggregate` kopiuje całą strukturę z adresu źródłowego
do docelowego oraz przenosi stan inicjalizacji jej pól końcowych. Kopiowanie
działa także dla zagnieżdżonego podobiektu i dla źródła lub celu globalnego;
obniżanie ToyC używa go przy inicjalizacji i przypisaniu struktur. Tablice
struktur oraz przekazywanie całych struktur do funkcji pozostają do następnych
etapów.

Zmienne globalne mają osobną listę typów w module oraz instrukcje
`GlobalAddress` i `GlobalIndexAddress`. Nie zajmują slotów ramki funkcji.
Skalarne zmienne i elementy tablic globalnych zaczynają od zera, a zapis
wykonany przez jedną funkcję jest widoczny w kolejnych wywołaniach. Backend C
emituje je jako obiekty `static`, z kontrolą zakresu przy indeksowaniu.

Przypisanie złożone do elementu (`a[i] += value` i pozostałe operatory) oraz
`a[i]++/--` mają w IR języka osobną operację `IndexUpdate`. Indeks jest
obliczany jeden raz. Następnie obniżanie sprawdza granice, odczytuje stary
element wraz z jego znacznikiem inicjalizacji, oblicza prawą stronę,
wykonuje arytmetykę i zapisuje wynik. Konwersje liczbowe zachowują semantykę
interpretera, także przy zwężeniu `float → int/char` i `int → char`.

## Weryfikacja

`verify(&Module)` zwraca listę błędów zamiast kończyć na pierwszym. Sprawdza
istnienie typów, slotów, funkcji i bloków, zgodność wyników instrukcji,
operandów i sygnatur wywołań, typ warunku `bool`, terminatory oraz lokalną
dostępność wartości. Błędy zachowują lokalizację funkcji, bloku i instrukcji
oraz opcjonalny zakres źródła. Backend przyjmie wyłącznie moduł po udanej
weryfikacji; nie może pomijać nieznanych operacji.

Weryfikator nie dowodzi jeszcze osiągalności bloków, zainicjalizowania slotów
na każdej ścieżce ani bezpieczeństwa adresowania pól i elementów. W kolejnych
wycinkach trzeba dodać adresowanie agregatów i tablic, jawne krawędzie błędów
operacji sprawdzanych, analizę slotów i kontrakt obniżania węzłów `Error`.
Przed backendem C/LLVM trzeba też porównać wyniki i kolejność skutków z
obecnym interpreterem ToyC/ToyCP.

## Pierwszy `lowering_model`

W `toyc_typed.coge` sekcja `lowering_model` wymienia funkcję generatora oraz
warianty IR źródłowego: deklaracje, wyrażenia, wywołania, bloki i instrukcje
sterujące. Generator emituje z nich `lowering_gen.rs`. Dla `int main() { return 2 + 3; }` powstają
dwie stałe, `Binary(Add, CheckedI32)` i strukturalny `Return`. Generator
sprawdza rozwinięcie do CFG przed zwróceniem modułu. Test porównuje także rezultat dotychczasowego
interpretera (`5`). Nie oznacza to jeszcze, że interpreter wykonuje Core IR.

Sekcja `backend_c` wiąże tę funkcję obniżania ze wspólnym emiterem
`runtime/src/c_backend.rs`; generator tworzy `backend_c_gen.rs`. Pierwszy
test kompiluje wygenerowany C11 i porównuje wynik `main` (`5`). Dodawanie
`i32` używa funkcji pomocniczych sprawdzających przepełnienie oraz dzielenie
przez zero. Lokalne symbole są mapowane na typowane sloty, a odczyt i zapis
używają typowanych adresów Core IR.
Obniżanie obejmuje `main` oraz funkcje osiągalne przez wywołania. Obsługuje
skalary `int`, `float`, `bool`, `char`, niejawne konwersje `char → int`,
`char → float`, `int → float`, konwersję skalarów na `bool` w warunkach oraz
arytmetykę i porównania `float`. Argumenty są
obliczane kolejno do wartości tymczasowych, a parametry trafiają do slotów
funkcji. Emiter tworzy prototypy przed definicjami, więc obsługuje rekurencję
i funkcje zdefiniowane później. Wymaga bezargumentowego `main` o wyniku `int`;
odrzuca nieskalarne sloty i nieobsługiwane
instrukcje. Zagnieżdżone bloki, `if`, `while` i `for` emituje bez `goto`;
warunek pętli oblicza ponownie przy każdym obrocie.

Pierwszy wycinek LLVM przyjmuje jawny `target triple` i `data layout`.
Obsługuje `int main()` i osiągalne funkcje o skalarnych typach `int`, `bool`,
`char` oraz `float`, wraz z parametrami tych typów,
wiele bloków CFG, stałe skalarne,
lokalne sloty i tablice tych typów, odczyt, zapis, arytmetykę `i32` i `f32`
oraz porównania skalarne. Konwersje między `float` a `int` lub `char`
zachowują zaokrąglanie i nasycenie wymagane przez interpreter; do konwersji
na liczby całkowite służą intrinsics `llvm.fptosi.sat` i `llvm.fptoui.sat`.
Obsługiwane są też skoki, rozgałęzienia i `return`. Bloki mają
oddzielne nazwy wartości tymczasowych, więc mogą ponownie używać `ValueId`.
To wystarcza dla `if`, `while`, `for`, wywołań z argumentami oraz rekurencji
w obsługiwanym wycinku ToyC. Parametry są zapisywane do slotów na wejściu
funkcji; wywołania zachowują kolejność obliczania argumentów z Core IR.
Globalne skalary `int`, `bool`, `char` i `float` są emitowane jako zerowane obiekty LLVM
i dostępne przez `GlobalAddress`. Globalne tablice tych typów są zerowane jako całość;
`BoundsCheck` i `GlobalIndexAddress` sprawdzają zakres przed odczytem lub
zapisem, a adres elementu powstaje przez `getelementptr` bez `inbounds`.
Lokalna tablica ma oddzielną tablicę bajtowych znaczników inicjalizacji;
`ResetArray` zeruje je przy każdym wykonaniu deklaracji. `IndexAddress`
sprawdza zakres, odczyt sprawdza znacznik, a zapis ustawia go po zapisie
elementu. Odczyt przed zapisem kończy się tym samym błędem co w interpreterze
i backendzie C.
Globalne agregaty o polach skalarnych są zerowane jako całość.
Dodawanie, odejmowanie i mnożenie są liczone w `i64`, po czym sprawdzany jest
zakres `i32`; dzielenie sprawdza zero i `INT_MIN / -1` przed `sdiv`.
Ścieżki błędów mają obecnie ABI `write`/`exit` dla x86_64/Linux i emitują
komunikaty zgodne z interpreterem oraz backendem C. Inne operacje lub cele
są jawnie odrzucane z `Unsupported`. Test przepuszcza wygenerowany tekst przez
`llvm-as`, weryfikuje go przez `opt -passes=verify` i wykonuje przez `lli`.
