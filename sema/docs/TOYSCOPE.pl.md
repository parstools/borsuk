# ToyScope — przegląd i migracja 4.4

[English](TOYSCOPE.md) | [Polski](TOYSCOPE.pl.md)

## Klasyfikacja

| Dokumenty | Warstwa i stan |
| --- | --- |
| `toyscope_{1,2,3}_typed.coge` | Autorskie źródła formatu 1: pełna gramatyka, analiza i zachowany kontrakt wykonania |
| `toyscope_{1,2,3}_typed.sema` | Generowane projekcje formatu 1: gramatyka i analiza, bez kontraktu wykonania |
| `toyscope_{1,2,3}_typed.sema.provenance.json` | Manifesty eksportów: źródło `.coge`, hashe, kontrakt runtime i mapy pochodzenia |
| `generated/projections/toyscope_{1,2,3}_typed.ag` i manifesty | Eksporty gramatyki z tego samego źródła; kanonicznego `toyscope.ag` nie nadpisują |
| `examples/scope_codegen.sema` | Dokument wyłącznie analizy, pozostaje `.sema` w jawnej ścieżce legacy |
| `examples/scope_codegen_bad_*.sema` | Negatywne warianty analizy do testów legacy; pozostają `.sema` |
| `toyscope_{1,2,3}.sema.txt` | Historyczne szkice mieszane z kontraktami i `execution result`; wymagają konwersji, pozostają wejściami testów legacy |
| `toyscope_{1,2,3}.semadesc.txt`, `toyscope-polityka.txt` | Opisy słowne i dokumentacja polityk, nie źródła CLI formatu 1 |

Typowane źródła nie mogą pozostać autorskimi `.sema`, ponieważ zawierały
`execution_contract`. Przeniesiono cały opis do `.coge`, a `.sema` utworzono
przez oficjalną projekcję. Zachowano nazwy specyfikacji, wszystkie działania
analizy i kontrakty wykonania. Nie zamieniano globalnie rozszerzeń szkiców
ani dokumentacji. Projekcji nie należy edytować ręcznie.

## Działająca ścieżka

Analizator powstaje przez CLI `sema`, które nie linkuje biblioteki wykonawcy.
Jawny [kontrakt runtime](../../contracts/toyscope-runtime-v1.json) opisuje typy
udostępniane przez Rust przykładów, używane we wszystkich trzech politykach.
Typy są opaque; funkcje intrinsic i ich sygnatury są jawnie deklarowane
w źródłach. Nie wyprowadzano kontraktu przez skanowanie kodu Rust.

```sh
cmake --build build --target sema coge ag -j4
build/bin/sema --check \
    --contracts contracts/toyscope-runtime-v1.json sema/examples/toyscope_1/toyscope_1.sema
python3 tools/regenerate_examples.py \
    toyscope_1_typed toyscope_2_typed toyscope_3_typed
```

Regeneracja najpierw eksportuje `.sema` i `.ag` przez `coge`, następnie generuje
analizator z czystej projekcji przez `sema` oraz parser z eksportowanego Ag.
Odświeża projekcje, manifesty i wzorce Rust. `--sema-cli` wskazuje binarium
analizy; `--coge` wskazuje binarium projekcji. Dawny `--sema` pozostaje aliasem
`--coge`, zgodnie z wcześniejszym interfejsem skryptu.

Wykonawca pozostaje ręczny: `examples/toyscope_1_typed/src/execute.rs` oraz
implementacje polityk 2 i 3 korzystające z tego samego runtime. Analogicznie
`examples/scope_codegen/src/ir_exec.rs` jest ręcznym wykonawcą IR; jego obecność
w pakiecie Rust nie zmienia właściciela dokumentu wyłącznie analizy.

## Konwersja wykonawcy

Zachowane `execution_contract` typowanych `.coge` nadal używają historycznych
działań, m.in. `require ... else reject_execution`, `with ... finally`,
tworzenia ramek i implicit runtime. Nie są gotowym źródłem generowanego
wykonawcy formatu 1. `coge --check` zgłasza obecnie `coge.invalid_contract`
z `unsupported execution statement: requireStatement`; eksport analizy jest
poprawny i niezależny od tej nieobsługiwanej części.

Pełna konwersja wymaga jawnego `execution_model`: typów ramek i stanu,
funkcji tworzenia/zwalniania ramek, odczytu/zapisu, błędów i efektów oraz
przeniesienia kontraktów do obsługiwanych działań. Należy zachować różnice
polityk: stan inicjalizatora w polityce 1, wiązanie po inicjalizatorze
w polityce 2 i rezerwację nazw/stanu w polityce 3. Gotowość wykonawcy trzeba
potwierdzić zgodnością z niezależnymi testami Rust każdej polityki.

Historyczne `.sema.txt` wymagają dodatkowo konwersji nieobsługiwanych query,
rewrite i `execution result`. Odbiór 4.4 klasyfikuje je jako szkice do konwersji;
nie przedstawia ich jako działających interpreterów.

## Weryfikacja

`agsem_toyscope_layer_tests` sprawdza eksport analizy, zgodność zapisanej
projekcji, odrzucenie `.coge` przez `sema`, jawny brak obsługi wykonawcy,
kontrolę `.sema` w katalogu bez źródła `.coge` i eksport Ag. Trzy dotychczasowe
testy `sema_toyscope_*_typed_codegen` porównują analizatory oraz parsery
z zapisanymi wzorcami. Testy Rust weryfikują zachowanie wszystkich trzech polityk.
