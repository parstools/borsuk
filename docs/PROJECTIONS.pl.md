# Projekcje ToyC, ToyCP i ToyScope

[English](PROJECTIONS.md) | [Polski](PROJECTIONS.pl.md)

Ten katalog zawiera wyniki kroku 4.2, śledzone w Git. Pliki `.sema` i `.ag`
są generowane przez `coge` z autorskich dokumentów formatu 1; zmiany języka
należy wprowadzać w źródłowym `.coge`, a następnie regenerować projekcje.

## Właściciele plików

| Pliki | Właściciel i przeznaczenie |
| --- | --- |
| `coge/examples/toyc/toyc.coge`, `coge/examples/toycp/toycp.coge` | Źródła autorskie AgSem: pełna gramatyka, analiza i wykonawca |
| `contracts/*-runtime-v1.json` | Jawne kontrakty ręcznego runtime przykładów |
| `*_typed.sema` w tym katalogu | Projekcje `coge`: gramatyka i analiza, bez wykonawcy |
| `*_typed.ag` w tym katalogu | Projekcje `coge`: gramatyka i lexer, bez rozszerzeń |
| `*.provenance.json` w tym katalogu | Metadane `coge`: hashe, kontrakty i mapy pochodzenia |
| `tests/fixtures/legacy/toyc_full.sema`, `tests/fixtures/legacy/toycp_full.sema` | Dawne mieszane źródła legacy, tymczasowo dla porównań i testów adaptera |
| `ag/grammars/examples/toyc.ag`, `toycp.ag` | Kanoniczne przykłady Agas, utrzymywane niezależnie; eksport ich nie nadpisuje |
| `tests/fixtures/legacy/toyc.ag` | Dawna kopia gramatyki dla ścieżki legacy ToyC; obecnie bajtowo zgodna z kanoniczną |
| `coge/examples/{toyc,toycp}/generated/` | Wzorce wygenerowanego Rust, regenerowane z autorskich `.coge` |
| `sema/examples/toyscope_N/toyscope_N.coge`, N = 1, 2, 3 | Pełne źródła ToyScope z zachowanym kontraktem wykonania do konwersji |
| `sema/examples/toyscope_N/toyscope_N.sema` i manifesty | Czyste projekcje analizy, generowane przez `coge`, używane przez `sema` |
| `toyscope_{1,2,3}_typed.ag` i manifesty w tym katalogu | Eksporty gramatyki ToyScope; nie nadpisują kanonicznego `tests/fixtures/legacy/toyscope.ag` |

Dawne `.sema` poza tym katalogiem nie są projekcjami formatu 1. Katalog
`build/bin/stage3-fixtures/` należy do testów i zawiera
tymczasowe wyniki; nie jest źródłem autorskim ani katalogiem wzorców.

## Regeneracja

Z katalogu głównego repozytorium:

```sh
cmake --build build --target coge -j4
python3 tools/regenerate_examples.py toyc toycp
python3 tools/regenerate_examples.py toyscope_1 toyscope_2 toyscope_3
```

Skrypt odświeża projekcje i wzorce Rust. Aby odświeżyć wyłącznie projekcje:

```sh
for language in toyc toycp; do
    build/bin/coge --force \
        --emit-sema "coge/examples/${language}/generated/projections/${language}.sema" \
        --contracts "contracts/${language}-runtime-v1.json" \
        "coge/examples/${language}/${language}.coge"
    build/bin/coge --force \
        --emit-ag "coge/examples/${language}/generated/projections/${language}.ag" \
        "coge/examples/${language}/${language}.coge"
done
```

Każda projekcja ma własny manifest `OUT.provenance.json`. Manifest opisuje
bajty źródła, wyniku i uruchomionego generatora; przebudowa binarium może
zmienić `tool_sha256`, nawet gdy tekst projekcji pozostaje taki sam.
Ścieżka `source` jest informacyjna i nie jest odczytywana przy użyciu projekcji.
Do sprawdzenia `.sema` należy jawnie podać odpowiedni kontrakt runtime.
Stan konwersji wykonawcy ToyScope opisuje
[TOYSCOPE_MIGRATION.md](../sema/docs/TOYSCOPE.pl.md).

Odbiór obejmuje kontrolę obu eksportów, zgodność hashy manifestów oraz prawo
projekcji: eksport Ag z `.sema` jest bajtowo równy eksportowi bezpośrednio
z `.coge`. Eksporty można używać po skopiowaniu do katalogu bez `.coge`
i bez oryginalnego `.ag`.
