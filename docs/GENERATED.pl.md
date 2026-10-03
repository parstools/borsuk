# Pliki generowane przykładów Rust

[English](GENERATED.md) | [Polski](GENERATED.pl.md)

Każdy przykład trzyma kod ręczny w `src/`, a artefakty generatorów w
`generated/` obok niego:

| Plik | Zawartość |
| --- | --- |
| `parser_gen.rs` | parser, lekser i redukcje AST z Agas |
| `sema_gen.rs` | analizatory z bloków `analysis` |
| `sema_lib_gen.rs` | funkcje `semantic_model` |
| `interpreter_gen.rs` | typy, stan i funkcje wykonawcy IR |
| `interpreter_properties_gen.rs` | testy graniczne z `properties` |

Nie każdy przykład potrzebuje wszystkich plików. Rust dołącza je przez
`#[path = "../generated/..."]` lub `include!("../generated/...")`.
Ręczne testy ToyC i ToyCP pozostają w odpowiednich plikach
`src/interpreter_tests.rs`. ToyCP korzysta z wygenerowanego `sema_gen.rs` do
budowy IR z drzewa parsera, a następnie wykonuje je przez
`interpreter_gen.rs`. Testy ToyCP obejmują całą ścieżkę od tekstu źródłowego
do wyniku.

Regeneracja wszystkich przykładów, z katalogu głównego repozytorium:

```sh
cmake --build build --target coge -j4
python3 tools/regenerate_examples.py
```

Można wskazać przykład, np. `python3 tools/regenerate_examples.py toyc`,
oraz ścieżki do narzędzi przez `--coge` i `--agas` (`--sema` pozostaje aliasem
`--coge`). ToyC/ToyCP korzystają z `coge/examples/{toyc,toycp}/{toyc,toycp}.coge`
i jawnych kontraktów `contracts/*-runtime-v1.json`. Parser powstaje
z gramatyki eksportowanej z tego samego `.coge`, bez odczytu osobnego źródła Ag.
Skrypt odświeża także `.sema`, `.ag` i manifesty w
[PROJECTIONS.pl.md](PROJECTIONS.pl.md).
Typowane ToyScope korzystają z `.coge` jako pełnego opisu i z generowanej
projekcji `.sema` jako wejścia CLI analizy. Ich wykonawca pozostaje ręczny;
zachowane kontrakty wymagają konwersji opisanej w
[TOYSCOPE.pl.md](../sema/docs/TOYSCOPE.pl.md). Skrypt odświeża projekcje
i generuje analizatory przez `sema` (binarium można wskazać przez `--sema-cli`).
Pozostałe przykłady korzystają nadal z jawnego `--legacy`.

Pomoc skryptów wyjaśnia migrację argumentów. Dawny `--sema PATH` oczekuje
binarium `coge`; osobny `--sema-cli PATH` wskazuje analizator. Zmigrowane
skrypty `check_*` wymagają samodzielnego źródła coge i `--contracts FILE`, a przy brakujących
argumentach pokazują konkretne ścieżki przykładu. Zestaw zamienników dawnych
poleceń jest w [DOCUMENT_CLI.pl.md](DOCUMENT_CLI.pl.md#migracja-poleceń).

Regeneracja zapisuje bezpośredni wynik generatora. Testy porównujące wzorce
ToyC/ToyCP i typowanych ToyScope normalizują obie kopie przez `rustfmt` (wymagane w `PATH`),
więc zmiana samego formatowania wzorca nie zmienia wyniku testu. Parser
pozostaje porównywany bajtowo. Moduły Rust są własnością manifestu
`sema_modules.manifest`; po usunięciu deklaracji modułu regeneracja usuwa
tylko wcześniej posiadane pliki.

Pliki pozostają śledzone w Git jako wzorce porównań CTest. Katalogi
`generated/` będzie można ignorować po przeniesieniu przetestowanych części
do docelowych repozytoriów `zbik` i `borsuk`, gdy odpowiednie repozytorium
będzie regenerować artefakty przed budowaniem i sprawdzać je we własnych
testach. Wtedy testy AgSem trzeba oprzeć na regeneracji zamiast na porównaniu
z zapisanymi tutaj wzorcami.

## Odbiór migracji

```sh
ctest --test-dir build --output-on-failure \
    -R 'agsem_migrated_examples_tests|agsem_toyscope_layer_tests'
cargo test --offline --manifest-path coge/examples/toyc/Cargo.toml -- --test-threads=1
cargo test --offline --manifest-path coge/examples/toycp/Cargo.toml
```

`agsem_migrated_examples_tests` kopiuje wyłącznie autorskie `.coge` i kontrakt
runtime do osobnego katalogu każdego języka. Eksportuje `.sema` i `.ag`,
sprawdza hashe manifestów, generuje pełny Rust ToyC/ToyCP, usuwa `.coge`
i generuje analizę przez samo `sema`. Porównuje wszystkie pliki Rust oraz
manifesty modułów ze wzorcami, a parsery bajtowo. Dla typowanych ToyScope
porównuje parser i analizę; wykonawca pozostaje ręczny i wymaga opisanej
konwersji kontraktów, sprawdzanej osobnym testem granic warstw.

Niezależne testy Cargo weryfikują zachowanie programów. Odbiór obejmuje także
`sema/examples/toyscope_{1,2,3}/Cargo.toml`,
`sema/examples/scope_codegen/Cargo.toml` i `rust/crates/sema-runtime/Cargo.toml`.

## Pełny odbiór Rust i projekcji

Po regeneracji uruchomić wszystkie osiem przykładów oraz oba runtime.
Wspólny katalog Cargo pozwala używać raz skompilowanych zależności;
`--locked --offline` zachowuje przypięte wersje i używa lokalnego cache.
Jeden wątek testów zapewnia kolejność także dla przypadków interpretera
zmieniających środowisko procesu.

```sh
for example in toyc toycp scope_codegen typed_codegen typed_mixed_codegen \
    toyscope_1 toyscope_2 toyscope_3; do
    cargo test --offline --locked --manifest-path "sema/examples/$example/Cargo.toml" \
        --target-dir /tmp/agsem-rust-acceptance -j4 -- --nocapture --test-threads=1 || exit 1
done
cargo test --offline --locked --manifest-path rust/crates/sema-runtime/Cargo.toml \
    --target-dir /tmp/agsem-rust-acceptance -j4 -- --nocapture --test-threads=1 || exit 1
cargo test --offline --locked --manifest-path rust/Cargo.toml \
    --target-dir /tmp/agsem-rust-acceptance -j4 -- --nocapture --test-threads=1 || exit 1
ctest --test-dir build --output-on-failure -j3 \
    -R '^(agsem_document_projection_tests|agsem_semantic_link_tests|agsem_document_cli_tests|agsem_migrated_examples_tests|agsem_toyscope_layer_tests)$'
```

Testy backendu C kompilują i uruchamiają emitowane programy przez `cc`.
Wykonanie LLVM wymaga `clang`, `llvm-as`, `opt` i `lli` oraz obsługiwanego
hosta; bieżący odbiór używa x86_64 Linux. Sprawdzić wersje i dostępność
tych narzędzi przed testami. `--nocapture` pokazuje komunikaty o pominięciu
LLVM, jeśli środowisko nie spełnia wymagań. Przy odbiorze backendu należy
potwierdzić, że jego wykonanie rzeczywiście odbyło się.

Testy projekcji potwierdzają bajty i model `Ag(Sema(Coge)) == Ag(Coge)`,
mapy pochodzenia oraz zgodność analizy z eksportowaną `.sema`. Test migracji
porównuje generowany Rust i parsery z zapisanymi wzorcami w izolowanych
katalogach; granice ToyScope i niezależne linkowanie `sema` są sprawdzane
osobno. Wyniki końcowego odbioru są w
sekcji 17.5 historycznego planu (nieprzeniesionego do tego repozytorium):
CTest 109/109, Rust 256/256 z wykonaniem C/LLVM oraz regeneracja 90/90
artefaktów powtarzalnych, po poprawkach XML i eksportu ToyCM.
