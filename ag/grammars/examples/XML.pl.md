# Przykład XML

[English](XML.md) | [Polski](XML.pl.md)

[`xml.ag`](xml.ag) jest samodzielną gramatyką Agas. Opisuje mały podzbiór
XML i daje AST; nie używa `.sema` ani `.coge`. Parsuje również zapisany
w repozytorium [`example.xml`](example.xml).

## Konteksty leksera

Klasy wybiera zakres reguły parsera za pomocą istniejących `enable`,
`disable` i `require`. Zagnieżdżony zakres przywraca poprzednie klasy
po wyjściu z reguły. Nie dodano trybów ani zmian w implementacji leksera.

| Klasa | Zastosowanie |
|---|---|
| ROOT | białe znaki wokół elementu głównego i początek deklaracji XML |
| TAG | nazwy, atrybuty, odstępy i zakończenia znaczników |
| TEXT | tekst elementu, także białe znaki i Unicode |
| DOUBLE | wartość atrybutu w podwójnym cudzysłowie |
| SINGLE | wartość atrybutu w pojedynczym cudzysłowie |

`TAG_SPACE` jest jawnym tokenem składni, dzięki czemu białe znaki wewnątrz
znaczników nie usuwają danych tekstowych z treści elementów. Gramatyka
nie wymaga klas kontekstu na tokenach `skip` ani kanałach ukrytych.
`</` jest jednym tokenem; LR(1) rozróżnia zamknięcie elementu od nowego
zagnieżdżonego elementu. Tablica ma 94 stany i zero konfliktów.

## Zakres

Obsługiwane są zagnieżdżone i puste elementy, mieszany tekst, atrybuty
w obu rodzajach cudzysłowów, odstępy wokół `=`, nazwy ASCII zawierające
`:`, `.`, `_` i `-` oraz opcjonalna deklaracja `<?xml ...?>`.
AST zachowuje nazwy otwierające i zamykające, wartości atrybutów, tekst
i zakresy bajtowe UTF-8. Białe znaki w treści są danymi.

Zgodność nazw elementów sprawdza konsument AST po parsowaniu.
Przykład takiego sprawdzenia to `matching_names` w
[`check_xml_example.py`](../../tools/check_xml_example.py): `<a></b>`
jest poprawne składniowo dla gramatyki i zostaje odrzucone przez tę kontrolę.
Nie wprowadza się dopasowania nazw do mechanizmu klas leksera.

To przykład podzbioru XML, bez pełnej walidacji specyfikacji. Encje,
referencje znakowe, DTD, komentarze, CDATA i instrukcje przetwarzania
nie są obsługiwane. Deklaracja XML ma składnię atrybutów; gramatyka nie
sprawdza ich wymaganej kolejności ani wartości. Pełny zbiór znaków nazw,
unikalność atrybutów i rozwiązywanie przestrzeni nazw są osobnym zakresem.

## Uruchomienie

Z katalogu głównego repozytorium:

```sh
build/bin/agas --table ag/grammars/examples/xml.ag
build/bin/agas --diagnose-parse \
    ag/grammars/examples/xml.ag ag/grammars/examples/example.xml
ctest --test-dir build --output-on-failure \
    -R '^agas_xml_example$'
```

Eksport pakietu używa istniejącej sekcji leksera v2. Zgodność AST w C++/Rust:

```sh
cargo build --offline --locked --manifest-path rust/Cargo.toml \
    --target-dir /tmp/agas-xml-rust --bin agas_ast_wire -j4
python3 ag/tools/check_xml_example.py \
    --agas build/bin/agas \
    --grammar ag/grammars/examples/xml.ag \
    --example ag/grammars/examples/example.xml \
    --ast-wire /tmp/agas-xml-rust/debug/agas_ast_wire
```

Test obejmuje 31 przypadków: poprawne i błędne wejścia, zagnieżdżenia,
puste wartości, oba cudzysłowy, EOF wewnątrz wartości, granice kontekstów,
zachowanie tekstu/UTF-8 oraz osobną kontrolę zgodności nazw po parsowaniu.
