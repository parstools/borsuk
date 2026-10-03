# ToyCP typed IR interpreter

[English](README.md) | [Polski](README.pl.md)

`python3 tools/regenerate_examples.py toycp` regeneruje parser,
analizator semantyczny, wykonawcę IR i testy własności. Polecenie
`cargo test --offline --manifest-path coge/examples/toycp/Cargo.toml`
sprawdza parser, analizator i wykonawcę na programach źródłowych ToyCP.
Do uruchomienia pliku służy
`cargo run --offline --manifest-path coge/examples/toycp/Cargo.toml --bin toycp_interpret -- PROGRAM.tcp`.

`analyze_source` łączy parser i wygenerowany `sema_gen.rs` z implementacją
intrinsics w `src/`. `Interpreter::new` przyjmuje otrzymany `Context`, a
`run_main` wykonuje IR i sprząta obiekty globalne. Testy sprawdzają pełną
ścieżkę od tekstu źródłowego, w tym metody z niejawnym `self`, definicje
poza klasą, dziedziczenie, widoczność, tablice i kolejność konstrukcji oraz
destrukcji. Model wartości, funkcje wykonawcze i graniczne testy liczbowe
pochodzą z `toycp_typed.sema`.

Przeciążenia konstruktorów są obsługiwane. Metody o tej samej nazwie nadal
mają jedną sygnaturę, a całych tablic nie można używać jako wartości;
indeksowanie tablic działa i jest sprawdzane podczas wykonania.
