# ANTLR jako oracle leksera Agasa

[English](LEXER_ORACLE.md) | [Polski](LEXER_ORACLE.pl.md)

Wygenerowany lekser ANTLR 4.10 jest używany wyłącznie w testach różnicowych
frontendu bootstrapowego. Nie jest częścią docelowej ścieżki tokenizacji ani
źródłem semantyki formatu `.ag`; tę definiuje `AG_FORMAT.md`.

## Porównywane dane

Dla poprawnego wejścia test porównuje każdy niespominięty token:

- symboliczną nazwę terminala;
- kanał;
- tekst tokenu;
- bajtowy offset początku w oryginalnym UTF-8.

Porównanie obejmuje `Ag.ag`, wszystkie pliki `.ag` w katalogu `grammars/` oraz
przypadki celowane: pierwszeństwo słów kluczowych nad identyfikatorami,
escape'y, klasy znaków, Unicode przed kolejnym tokenem i kilka komentarzy
blokowych. Dla wejść błędnych test wymaga od obu lekserów odrzucenia, ale nie
wymaga identycznego sposobu odzyskiwania po błędzie.

Aktualny przypięty wynik:

```text
files=11 focused=4 invalid=4 documented_differences=1 mismatches=0
```

## Jawne różnice

Bootstrapowe `Ag.g4` dopuszcza zakończenie `DOC_COMMENT` i `BLOCK_COMMENT`
przez EOF. `Ag.ag` wymaga jawnego `*/`, dlatego własny lexer odrzuca
niedomknięty komentarz. Jest to zamierzona różnica: brak terminatora ma być
błędem wskazywanym użytkownikowi, a zachowanie ANTLR nie jest w tym przypadku
oracle zgodności.

ANTLR po błędzie leksykalnym może zgłosić problem i kontynuować od późniejszego
znaku. Własny lexer kończy tokenizację na pierwszym błędzie. Testy negatywne
porównują więc fakt odrzucenia, nie dalszy strumień tokenów.

Gramatyka języka Agasa pomija komentarze i białe znaki przez `skip`, więc nie
dostarcza przykładu nazwanego kanału do porównania z tym oracle. Semantyka
`channel(NAME)` jest sprawdzana osobnym testem wygenerowanego leksera na
syntetycznej gramatyce.
