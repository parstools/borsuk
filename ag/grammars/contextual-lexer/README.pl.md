# Klasy lexera w plikach `.ag`

[English](README.md) | [Polski](README.pl.md)

Składnię opisuje [AG_FORMAT.md](../../docs/AG_FORMAT.pl.md#klasy-lexera-sterowane-zakresem-reguły-parsera).

| Plik | Przypadek |
| --- | --- |
| `Shift.ag` | `a>>b` → `SHR`, `c>>d` → dwa `GT` |
| `GatedBrackets.ag` | domyślnie ani `SHR`, ani `GT`; gałęzie włączają osobne klasy |
| `Keywords.ag` | `@read;` / `@write;` → słowa kluczowe, `#read;` → `IDENT` |
| `Combined.ag` | `0read>>;`, `1read>>;`, `2read>>;`, `3read>>;`: cztery maski |
| `BadContext.ag` | celowo sprzeczny wybór `SHR` lub `GT` po tym samym `X` |
| `Nested.ag` | `read outer<read<int>>>>write;`: zagnieżdżone wyłączenie i przywrócenie klas |
| [`../examples/xml.ag`](../examples/XML.md) | tekst, znaczniki i oba rodzaje cudzysłowów, sterowane zakresem reguł parsera |

`Nested.ag` celowo pokazuje również ograniczenie LALR: tablica nie ma
konfliktów ACTION, ale scalenie miesza konteksty lexera. Dla tego pliku
należy zmienić `parser = LALR` na `parser = LR`; LR(1) i LR(2) działają.
Test automatyczny sprawdza oba wyniki, w tym odrzucenie konfiguracji LALR.

Uruchomienie z katalogu głównego repozytorium:

```sh
cmake --build build --target agas agas_contextual_generation_tests -j4
build/bin/tests/agas_contextual_generation_tests
build/bin/agas --table ag/grammars/contextual-lexer/Combined.ag
printf '3read>>;' > /tmp/combined.txt
build/bin/agas --diagnose-parse ag/grammars/contextual-lexer/Combined.ag /tmp/combined.txt
```

Test porównuje modele obu frontendów, LR/LALR z k=1/2, sekwencje tokenów,
rekurencję i odtwarzanie kontekstu. Sprawdza też błędne deklaracje,
limit 64 klas i użycie bitu 63 bez tworzenia wszystkich kombinacji.

Eksport `--emit-package` zachowuje plan kontekstu w sekcji lexera v2.
Pakiet buduje AST zarówno w C++, jak i Rust. Test obejmujący oba runtime:

```sh
cargo build --manifest-path rust/Cargo.toml --bin agas_ast_wire
python3 ag/tools/check_contextual_artifacts.py
```

Sprawdza 182 przypadki, w tym LR/LALR z k=1/2, kanał ukryty, komentarze
niechciwe, Unicode, bit 63 oraz odrzucenie czterech uszkodzonych pakietów.
Kontrakt opisuje [LEXER_CONTEXT.md](../../docs/LEXER_CONTEXT.pl.md).

## Weryfikacja wdrożenia

- Nowe testy klas oraz 36 testów CTest przechodzą; zgodność runtime pakietu
  w Rust: 5/5, reprodukcja ośmiu plików artefaktu: identyczna bajtowo.
- Historyczny wynik 36/36 pomijał trzy niezależne blokady: niedokończony
  `examples/xml.ag` psuł testy adaptera/scalania, a eksport `toycm` używał
  istniejącego katalogu z poprzedniego przebiegu. W odbiorze 7.4 poprawiono
  XML i izolowano katalogi testu eksportu. Wszystkie trzy testy przechodzą;
  razem z testem XML i regresjami frontendu/kontekstów: 6/6. XML ma
  31 przypadków zgodnych w C++/Rust. Test ToyCM przechodzi trzy kolejne
  uruchomienia i nadal sprawdza odmowę nadpisania istniejących danych.
- Regresja głównych gramatyk Vist przechodzi wraz z pełnym dotychczasowym
  pokryciem alternatyw i kwantyfikatorów.
