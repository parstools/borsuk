# Agas — ograniczony EBNF i budowanie AST

[English](AG_FORMAT.md) | [Polski](AG_FORMAT.pl.md)

## Cel formatu

Agas ma służyć do opisywania gramatyk, z których można generować parsery LR w różnych językach docelowych. Składnia ma być prostsza od pełnego EBNF z ANTLR i wymuszać nadawanie nazw większym fragmentom gramatyki. Dzięki temu:

- każdy ważny fragment ma stabilną nazwę;
- łatwo wskazać konkretny symbol w produkcji;
- diagnostyka może podawać nazwę reguły, `RuleId` i pozycję symbolu;
- reguły techniczne nie muszą zaśmiecać AST;
- publiczne API wygenerowanego AST nie zależy od wewnętrznej numeracji stanów LR.

## Opcje tabeli

Blok `options` wybiera konstrukcję tabeli i długość lookaheadu:

```text
options {
    parser = LALR;
    lookahead = 2;
}
```

`parser` przyjmuje obecnie `LR` dla kanonicznego LR(k) albo `LALR` dla
bezpośredniej konstrukcji LALR(k) na grafie rdzeni LR(0). `lookahead` musi być
dodatnią liczbą całkowitą. Są to żądania jawne: generator nie zwiększa `k`,
nie przełącza algorytmu i nie rozstrzyga po cichu konfliktów. Konfliktowa
tabela może służyć diagnostyce, ale nie jest eksportowana jako deterministyczny
artefakt parsera.

Pierwsza reguła parsera jest regułą startową. Agas ostrzega o pozostałych
regułach parsera, do których nie prowadzi żaden łańcuch odwołań od tej reguły;
samo odwołanie między dwiema nieosiągalnymi regułami nie usuwa ostrzeżenia.
Opcja `warningsAsErrors = true;` w bloku `options` zamienia wszystkie
ostrzeżenia walidacji gramatyki w błędy. Domyślnie ostrzeżenia nie zatrzymują
generowania artefaktu. Dotyczy to reguł parsera, nie nieużytych tokenów
leksera: te mogą być potrzebne do tokenizacji wejścia.

## Jawne rozstrzyganie konfliktów

Blok `conflicts` pozwala wskazać dokładną parę akcji shift/reduce oraz wybraną
akcję. Nazwa po `#` jest etykietą alternatywy reguły parsera:

```ag
conflicts {
    prefer shift ELSE over reduce ifStatement#IfStatement;
    // Alternatively: prefer reduce ifStatement#IfStatement over shift ELSE;
}
```

W jednej gramatyce należy zadeklarować tylko jedną politykę dla danej komórki
ACTION. Deklaracja pasuje wyłącznie do konfliktu z dokładnie tym przesunięciem
i tą redukcją; nieużyta lub nakładająca się deklaracja jest błędem. Przy
`lookahead > 1` terminal `ELSE` jest pierwszym symbolem pełnego słowa
lookahead; polityka może rozstrzygnąć kilka takich słów. Pozostałe konflikty
nie są rozstrzygane automatycznie.

Dla klasycznego dangling-else `shift` wiąże `else` z najbliższym `if`, zaś
`reduce` może wiązać go z bardziej zewnętrznym `if`. Są to dwa świadome
wybory semantyki niejednoznacznej gramatyki, a nie dowód jej jednoznaczności.
`agas --table` podaje liczbę rozstrzygniętych konfliktów; wygenerowany pakiet
przechowuje wybraną tabelę oraz diagnostykę decyzji w `diagnostics.json`.

## Ograniczony EBNF reguł parsera

W regułach parsera obowiązują następujące ograniczenia:

1. alternatywy `|` występują tylko na najwyższym poziomie nazwanej reguły;
2. operatory `?`, `*` i `+` dotyczą dokładnie jednego symbolu albo literału;
3. nie ma anonimowych grup parsera w nawiasach `(...)`;
4. pustą produkcję zapisuje się jawnie jako `empty`;
5. złożony fragment opcjonalny albo powtarzany trzeba wydzielić do nazwanej reguły.

Niedozwolony zapis:

```text
node function
    : TYPE IDENT LPAREN (parameter (COMMA parameter)*)? RPAREN block
    ;
```

Odpowiednik w Agas:

```text
node function
    : type=TYPE name=IDENT parameters=parameterBlock body=block
    ;

inline parameterBlock
    : LPAREN parameters=parameterList? RPAREN
    ;

node parameterList
    : first=parameter rest=additionalParameter*
    ;

inline additionalParameter
    : COMMA value=parameter
    ;
```

Takie rozbicie daje nazwy `parameterBlock`, `parameterList` i `additionalParameter`, które mogą być używane w komunikatach, generatorze kodu oraz testach.

Ograniczenie dotyczy reguł parsera. Reguły leksera opisują języki regularne, dlatego mogą używać zagnieżdżonych grup `(...)`. Grupy leksera nie tworzą elementów AST parsowanego programu.

## Kontrakt leksera

Agas ma własny frontend reguł leksera, ale nie powinien kopiować algorytmów
automatowych. Rozwijanie fragmentów, semantyka kwantyfikatorów, priorytety,
kanały i diagnostyka należą do Agasa. NFA, DFA, klasy równoważności alfabetu i
minimalizacja powinny być współdzielone z Żbikiem przez jego publiczne API.
Docelowy lekser nie zależy od runtime ANTLR.

### Wybór tokenu

W każdym położeniu wejścia obowiązują następujące reguły:

1. rozpatrywane są wszystkie niefagmentowe reguły leksera;
2. wygrywa reguła dopasowująca najdłuższy fragment wejścia;
3. remis rozstrzyga wcześniejsza reguła w pliku `.ag`;
4. brak dopasowania jest błędem wskazującym początek tokenu i pierwszy punkt
   kodowy, dla którego nie można kontynuować.

Jest to zasada maximal munch z priorytetem kolejności reguł. Fragment nie
emituje tokenu i nie uczestniczy samodzielnie w wyborze; jest nazwanym wyrażeniem
regularnym rozwijanym w używających go regułach. Rekurencja bezpośrednia i
pośrednia między fragmentami jest błędem.

### Kwantyfikatory zachłanne i leniwe

`?`, `*` i `+` są domyślnie zachłanne. `*?` oraz `+?` są leniwe. Dla leniwego
kwantyfikatora wybierana jest najmniejsza liczba powtórzeń, która pozwala
dopasować pozostałą część tej samej reguły. Agas przyjmuje tu semantykę ANTLR:
po przejściu przez leniwą podregułę późniejsze decyzje tej reguły wybierają
pierwszą pasującą alternatywę, zamiast ponownie maksymalizować jej długość.
Przykładowo `.*? ('a' | 'ab')` kończy się alternatywą `'a'`. Jeżeli w regule
występuje kilka leniwych decyzji, rozstrzyga się je od lewej do prawej.
Dopiero tak wybrane dopasowania całych reguł uczestniczą w wyborze
najdłuższego tokenu i rozstrzyganiu remisu kolejnością reguł.

Przykładowo:

```text
BLOCK_COMMENT : '/*' .*? '*/' -> channel(HIDDEN);
```

kończy token na pierwszym `*/`, które pozwala zakończyć regułę. Usunięcie
znacznika `lazy` zakończyłoby go na najdalszym możliwym `*/`. Implementacja nie
może zatem przekazać obu zapisów do zwykłego DFA jako tego samego języka
regularnego. Potrzebny jest automat zachowujący priorytety ścieżek albo
równoważna transformacja o sprawdzonej semantyce.

### Unicode i pozycje źródłowe

Docelowy alfabet składa się z wartości skalarnych Unicode, a wejście jest
kodowane w UTF-8. `.` dopasowuje jeden punkt kodowy, nie jeden bajt. Literały,
klasy, zakresy i ich negacje również działają na punktach kodowych. Niepoprawna
sekwencja UTF-8 jest błędem leksera i nie może zostać potraktowana jako kilka
niezależnych znaków.

Unicode nie wymaga gęstej tablicy obejmującej cały zakres kodów. Klasy znaków
i przejścia DFA mają być przechowywane jako kanoniczne, rozłączne przedziały.
Dla gramatyki używającej tylko ASCII liczba klas równoważności pozostaje więc
zbliżona do liczby klas leksera bajtowego. W Żbiku należy dodać ogólny alfabet
przedziałowy albo osobny lekser UTF-8, zachowując `ByteLexer` tam, gdzie nadal
jest przydatny do danych binarnych i testów.

Pierwsza wersja kompilatora Unicode powinna obsługiwać zwykłe escape'y,
`\xNN`, `\uNNNN`, `\UNNNNNNNN` oraz przynajmniej właściwości potrzebne
identyfikatorom języków programowania, w szczególności `XID_Start` i
`XID_Continue`. Generator Żbika rozwija je obecnie z danych ICU i udostępnia
numer użytej wersji Unicode. Przed zamrożeniem artefaktu wersja danych musi być
przypięta i zapisana w manifeście; runtime wykonujący gotowy automat nie
powinien wymagać biblioteki Unicode.

Zakres tokenu jest zapisywany jako półotwarty przedział bajtów UTF-8. Dane
diagnostyczne mogą dodatkowo przechowywać numer linii i kolumnę liczoną w
punktach kodowych. Adapter IDE odpowiada za konwersję tych pozycji na indeksy
UTF-16 wymagane przez platformę JetBrains; żaden z tych trzech rodzajów indeksu
nie może być nazywany ogólnie `offset` bez podania jednostki.

### Kanały i komendy

Pierwsza wersja obsługuje dokładnie dwie komendy:

- `skip` bez argumentu całkowicie usuwa dopasowany token, zachowując postęp i
  prawidłowe pozycje kolejnych tokenów;
- `channel(NAME)` emituje token na kanale zadeklarowanym w bloku `channels`.

Brak komendy oznacza kanał domyślny. Parser otrzymuje wyłącznie tokeny kanału
domyślnego, natomiast pełny wynik leksera zachowuje tokeny kanałów nazwanych
dla komentarzy, formatowania i narzędzi IDE. Reguła nie może łączyć `skip` z
`channel`, powtarzać komendy ani umieszczać komendy na fragmencie. `skip` nie
przyjmuje argumentu, `channel` wymaga dokładnie jednego zadeklarowanego kanału,
a nieznane komendy są błędami. Reguła wysyłana do `skip` albo nazwanego kanału
nie może być użyta jako terminal reguły parsera.

Komendy ANTLR takie jak `more`, `type`, `mode`, `pushMode` i `popMode` nie
należą do pierwszej wersji. Importer `.g4` ma zgłosić je w raporcie strat,
zamiast je ignorować.

## Produkcja i jej tożsamość

Każda alternatywa każdej reguły otrzymuje własny, stabilny w obrębie zbudowanej gramatyki `RuleId`:

```text
node primary
    : value=IDENT
    | value=NUMBER
    ;
```

Może zostać wewnętrznie przedstawiona jako:

```text
RuleId 31: primary -> IDENT
RuleId 32: primary -> NUMBER
```

Parser LR nie potrzebuje nazw alternatyw, aby je rozróżnić. Komórka ACTION wskazuje redukcję przez `RuleId`. Podczas redukcji maszyna LR zna więc dokładną produkcję, nawet jeśli kilka produkcji ma wspólny prefiks.

Drzewo nie powstaje z samej tablicy LR. Do jego zbudowania potrzebne są:

```text
tablica ACTION/GOTO
+ sekwencja wykonanych redukcji
+ RuleId każdej redukcji
+ metadane node/inline
+ etykiety field=Symbol
+ opcjonalne nazwy #Alternatyw
```

Generator może tworzyć wewnętrzne funkcje redukcji nazwane na przykład `reduce_rule_31`. Są one detalem wygenerowanego parsera i nie wymagają publicznej nazwy zapisanej po `#`.

## Reguły `node`

`node` opisuje nazwany węzeł AST. Generator pomija jednak przezroczyste
przejścia do innej reguły oraz grupowanie opisane poniżej.

```text
node function
    : type=TYPE name=IDENT parameters=parameterBlock body=block
    ;
```

Nienazwane symbole, takie jak nawiasy, przecinki i średniki, są potrzebne parserowi, ale domyślnie nie stają się polami AST. Etykieta elementu wskazuje dane przechowywane w węźle:

```text
name=IDENT
body=block
arguments=argument*
resultType=type?
```

Sufiks określa naturalną krotność pola:

| Zapis | Typ logiczny pola |
|---|---|
| `value=X` | dokładnie jedno `X` |
| `value=X?` | opcjonalne `X` |
| `values=X*` | lista zero lub więcej `X` |
| `values=X+` | niepusta lista `X` |

### Automatyczne przekazywanie wyrażeń

Generator rozpoznaje alternatywę z dokładnie jednym wymaganym, nazwanym
odwołaniem do reguły oraz pozostałymi nazwanymi odwołaniami z `?` lub `*`.
Alternatywa nie może zawierać innych symboli. Na przykład:

```text
node additive : first=multiplicative rest=additiveTail*;
```

Gdy `rest` jest puste, wynikiem redukcji jest bezpośrednio `first`, wraz
z jego rzeczywistym typem węzła. Gdy ogon jest obecny, powstaje `additive`
z kompletem pól. Sama reguła `node alias : value=other;` zawsze przekazuje
wynik `other`. Obecne `?`, także zawierające pusty węzeł, nie jest nieobecnym
`?`. `+` nie kwalifikuje się do tego uproszczenia. Własny token, nienazwany
symbol albo drugi wymagany operand zachowuje konstrukcję. Dzięki temu np.
`RETURN value=expr SEMI` nie traci swojej tożsamości.

Osobno generator rozpoznaje dokładnie `LPAREN value=expr RPAREN`, gdy
nienazwane tokeny oznaczają pojedyncze znaki `(` i `)` oraz `value` jest
wymaganym odwołaniem do reguły. Rozpoznanie korzysta z definicji leksera,
nie z nazw `LPAREN` i `RPAREN`; działa też z literałami parsera. Nawiasy nie
tworzą węzła. `2+3`, `2+((((3))))` i `((2+3))` mają identyczną strukturę
AST, choć różne pozycje źródłowe. `(2+3)*4` nadal ma inne drzewo niż
`2+3*4`, ponieważ kolejność operacji wynika z rozbioru składni.

Jest to obecnie reguła generatora, bez dodatkowej składni `.ag`. Nie obejmuje
`[]`, `{}` ani przecinków. Następny krok to jawna polityka AST dla konkretnych
produkcji w `.ag`, w tym wskazywanie grupowania oraz składanie postfixów.
Nie należy utożsamiać wszystkich użyć danego znaku z grupowaniem.

Przekazana wartość zachowuje `sourceSpan`, a jej `recognizedSpan` obejmuje
cały rozpoznany zapis. Dla `(a+b) << c` węzeł sumy ma treść `a+b`, ale
rozpoznany zakres `(a+b)`. Pozwala to semantyce sprawdzić obecność nawiasów
bez odtwarzania technicznych węzłów. Wiele par nawiasów nie zwiększa
głębokości AST. Metadane produkcji i ślad redukcji do pokrycia pozostają
pełne; promowanie następuje podczas składania wartości, a nie w automacie LR.

Nazwy `#Wariant` identyfikują utworzony węzeł; promowana alternatywa nie
tworzy dodatkowego węzła tylko po to, by zachować swoją nazwę wariantu.

## Reguły `inline`

`inline` oznacza rusztowanie gramatyki. Reguła uczestniczy w konstrukcji automatu LR i ma własne produkcje oraz `RuleId`, ale jej redukcja nie tworzy osobnego węzła AST.

```text
inline additionalParameter
    : COMMA value=parameter
    ;
```

Etykiety elementów, takie jak `value=parameter`, są dozwolone również w
`inline`. Wynik alternatywy jest określony wyłącznie przez liczbę etykiet:

- bez etykiet powstaje wartość jednostkowa;
- jedna etykieta przekazuje wskazaną wartość;
- kilka etykiet tworzy rekord techniczny o nazwanych polach.

Rekord techniczny nie jest węzłem AST i nie ma samodzielnej tożsamości.
Zewnętrzna etykieta wiąże cały wynik wywołanej reguły `inline`. Pola rekordu
nie są niejawnie wstrzykiwane do rodzica. Dzięki temu propagacja przez wiele
poziomów jest lokalna, a nazwa z wnętrza reguły nie może przypadkiem kolidować
z polem rodzica.

Kształt wyniku ustala generator. Nie zależy on od obecności pola opcjonalnego
w konkretnym wejściu. Alternatywy o różnych kształtach tworzą jawny typ
sumaryczny w schemacie neutralnym; generator języka docelowego nie może
wybierać własnej niejawnej konwersji.

Alternatywy reguły `inline` nie mogą mieć nazw `#...`. Parser i generator rozpoznają je po `RuleId`; publiczne nazwy wariantów byłyby mylące, skoro `inline` nie tworzy wariantu AST.

Poprawnie:

```text
inline optionValue
    : value=identifier
    | value=INTEGER
    | value=STRING_LITERAL
    ;
```

Niepoprawnie:

```text
inline optionValue
    : value=identifier     #IdentifierOption
    | value=INTEGER        #IntegerOption
    ;
```

## Jak wybierać między `node` i `inline`

Rodzaj reguły jest obowiązkowo zapisany jawnie przez autora gramatyki. `node`
tworzy węzeł z wyjątkiem powyższych przezroczystych redukcji, a `inline`
przekazuje albo spłaszcza swoje wartości. Brak modyfikatora jest błędem składniowym.

Podstawowa reguła projektowa brzmi:

> Jeżeli fragment powinien mieć własną tożsamość, zakres źródłowy albo być używany przez późniejsze etapy kompilatora, powinien być `node`. Jeżeli istnieje tylko po to, aby zapisać gramatykę bez zagnieżdżonego EBNF, powinien być `inline`.

Typowymi regułami `node` są deklaracje, definicje funkcji, instrukcje, wyrażenia, typy, parametry, pola struktur i elementy inicjalizatorów. Typowymi regułami `inline` są ogony list, opcjonalne fragmenty, wybory operatorów, reguły delegujące oraz techniczne fragmenty wydzielone z anonimowych grup EBNF.

Lista parametrów może być `node`; jej jednoelementowa postać podlega
automatycznemu przekazaniu, jeśli ma poniższy kształt. Ogon rozpoczynający
się przecinkiem jest rusztowaniem składniowym:

```text
node parameterList
    : first=parameter rest=additionalParameter*
    ;

inline additionalParameter
    : COMMA value=parameter
    ;
```

Reguła delegująca również zazwyczaj powinna być `inline`, jeśli właściwe dzieci już tworzą samodzielne węzły:

```text
inline statement
    : value=ifStatement
    | value=whileStatement
    | value=returnStatement
    ;
```

W tym przykładzie nie powstaje dodatkowy węzeł `Statement`. Rodzajem obiektu jest bezpośrednio `IfStatement`, `WhileStatement` albo `ReturnStatement`.

## Pola, tokeny i krotności

Nieetykietowany symbol uczestniczy w parsowaniu oraz w zakresie całej
redukcji, ale nie tworzy pola i nie przekazuje niejawnie swojego wyniku.
Znaczący terminal musi więc otrzymać etykietę. Wartość terminala zachowuje
co najmniej rodzaj tokenu, tekst i półotwarty zakres bajtów UTF-8.

Kwantyfikatory mają następujące znaczenie neutralne:

| Zapis | Wartość redukcji |
|---|---|
| `value=X` | jedno `X` |
| `value=X?` | `Optional<X>` |
| `values=X*` | lista `X`, również pusta |
| `values=X+` | niepusta lista `X` |

Zagnieżdżone opcje i listy nie są automatycznie spłaszczane. Brak wartości,
obecna pusta lista i wartość jednostkowa są różnymi wynikami. `node : empty`
tworzy węzeł bez pól, natomiast `inline : empty` daje wartość jednostkową.
`*` i `+` nad symbolem nullable są błędem, ponieważ pozwalałyby wykonywać
nieograniczoną liczbę pustych powtórzeń.

Pomocnicze produkcje powtórzeń zachowują kolejność wejściową. Runtime może
użyć mutowalnego buildera lub trwałej listy, ale nie może odwrócić elementów
ani kopiować całego prefiksu przy każdym dopisaniu.

Dla nienazwanych alternatyw `node` schemat zawiera sumę pól ze wszystkich
alternatyw. Pole niewystępujące w danej alternatywie ma jawnie zaznaczoną
nieobecność, inną od pustej listy i od `Optional::None` wewnątrz pola.
Jeżeli to samo pole ma w różnych alternatywach inny symbol albo krotność,
neutralny schemat zapisuje sumę tych typów. Generator języka docelowego może
przedstawić ją jako wariant, sealed type lub ich odpowiednik, zachowując
wszystkie przypadki. Nazwane alternatywy mają osobne schematy pól, a pełna
tożsamość wariantu obejmuje nazwę reguły i nazwę po `#`.

Neutralny schemat rozróżnia następujące konstruktory typów: jednostkę, token,
wynik nazwanej reguły, węzeł, rekord techniczny, `Optional<T>`, `List<T>` oraz
`NonEmptyList<T>` oraz `Choice<T...>` dla warunkowego wyniku promowania.
Suma typów jest uporządkowaną listą różnych dopuszczalnych
typów, dzięki czemu format nie zależy od składni sum w C++, Ruście ani Javie.

Każda alternatywa zachowuje własny indeks źródłowy, nazwę wariantu, wynik i
pola. Nienazwany `node` ma dodatkowo scalony publiczny zestaw pól. Przy każdym
polu schemat zapisuje sumę typów oraz informację, czy całe pole nie występuje
w części alternatyw. Ta informacja jest niezależna od `Optional<T>`: pole może
być obecne w każdej alternatywie i jednocześnie zawierać opcję albo może nie
istnieć w danej alternatywie.

Wyniki alternatyw `inline` tworzą neutralną sumę wyników reguły. Alternatywa
bez pól daje jednostkę, z jednym polem jego typ, a z kilkoma polami nazwany
rekord techniczny. Odwołanie do innej reguły pozostaje odwołaniem po nazwie,
co pozwala opisać schematy rekurencyjne bez rozwijania ich w nieskończoność.

Promowana alternatywa `node` ma wynik będący odwołaniem do reguły dziecka.
Warunkowe promowanie ma wynik `choice` zawierający typ utworzonego węzła
oraz typ przekazanego dziecka; `resultTypes` całej reguły zawiera oba.
Pola alternatywy nadal opisują przechwycenia źródłowe, ale nie należy ich
odczytywać z wyniku, który został przekazany z innej reguły.

## Zakresy wartości parsera

Każda wartość stosu parsera przechowuje osobno payload semantyczny i zakres
rozpoznanego fragmentu. Przekazanie dziecka przez `inline` zachowuje payload
i zakres dziecka, ale sama redukcja ma również zakres obejmujący wszystkie
symbole produkcji, w tym nieetykietowane nawiasy lub separatory. Rodzic używa
zakresu redukcji dla swojej pozycji RHS.

Zakres wejścia jest półotwartym przedziałem bajtów UTF-8. Redukcja pusta ma
zakres punktowy przy pierwszym niezużytym tokenie, a na końcu wejścia przy
EOF. Pozycja pochodzi ze stosu parsera i strumienia tokenów, nie z bieżącej
pozycji leksera, ponieważ lexer mógł odczytać lookahead. Zakresy zapisane
w metadanych produkcji wskazują definicję w pliku `.ag` i są odrębnym rodzajem
danych.

Jeżeli produkcja zawiera przynajmniej jeden niepusty składnik, punktowe
zakresy pustych helperów nie rozszerzają jej zakresu do początku kolejnego
tokenu. Produkcja złożona wyłącznie z pustych składników zachowuje zakres
punktowy.

## Neutralny program redukcji

Każda produkcja BNF ma jedną instrukcję indeksowaną przez `RuleId`. Pierwszy
zestaw operacji obejmuje:

- wartość jednostkową i przekazanie pozycji RHS;
- utworzenie `node` albo rekordu technicznego z nazwanymi polami;
- `OptionalSome` i `OptionalNone`;
- pustą listę, listę jednoelementową i dopisanie elementu.

Indeksy operandów odnoszą się do rzeczywistej prawej strony produkcji po
rozwinięciu EBNF. Instrukcja zawiera politykę zakresu: zakres RHS albo punkt
przy lookaheadzie dla produkcji pustej. Program wykonuje się bez źródłowego
`SyntaxDocument` i bez diagnostycznych nazw produkcji. Nie zawiera kodu C++,
Rusta ani Javy.

Program jest walidowany przed wykonaniem: liczba instrukcji, kolejność
`RuleId`, indeksy RHS, liczba operandów, obecność nazw typów i polityka zakresu
muszą odpowiadać gramatyce. Błąd programu lub artefaktu jest odrębny od błędu
składni parsowanego pliku. Format artefaktu używa jawnych szerokości liczb,
a nie zależnego od platformy `size_t`.

`GeneratedParserTable` przechowuje schemat AST obok programu redukcji. Runtime
wykonuje wyłącznie instrukcje, natomiast schemat służy walidacji artefaktu,
generatorom statycznych typów i narzędziom IDE.

Maszyna AST utrzymuje równolegle stos stanów LR i stos wartości neutralnych.
Shift odkłada token z tekstem i zakresem bajtowym. Reduce zdejmuje dokładnie
tyle wartości, ile wynosi RHS produkcji, wykonuje instrukcję wskazaną przez
`RuleId`, po czym używa GOTO dla lewej strony. Accept wymaga jednego korzenia
i zużycia wszystkich tokenów kanału domyślnego.

ACTION jest wybierane pełnym słowem lookahead o długości skonfigurowanego `k`;
przy końcu wejścia słowo zawiera EOF. Tokeny nazwanych kanałów pozostają w
wyniku leksera dla IDE, ale nie trafiają na stos parsera. Puste redukcje używają
początku następnego tokenu kanału domyślnego albo długości całego wejścia przy
EOF, również gdy na końcu występuje pominięty tekst.

Adapter języka Agasa interpretuje neutralne węzły utworzone przez `Ag.ag` i
odtwarza `SyntaxDocument`, w tym enumy symboli i kwantyfikatorów, listy,
opcje oraz pozycje linia/kolumna. Jest osobną warstwą nad ogólnym runtime;
inne języki mogą generować własne modele lub korzystać bezpośrednio z
neutralnego AST.

Publiczna ścieżka self-hosted zwraca jeden rodzaj wyniku dla poprawnego
`SyntaxDocument` oraz błędów leksykalnych, składniowych i adaptera. Błąd
składni zawiera zakres bieżącego tokenu, stan LR, pełne otrzymane słowo
lookahead i listę słów oczekiwanych w tym stanie. Dla `k > 1` miejsce może być
wcześniejsze niż raportowane przez parser LL, ponieważ brak poprawnej
kontynuacji jest widoczny przed zużyciem wszystkich tokenów słowa lookahead.

Końcowy strukturalny `EOF` pierwszej reguły nie jest symbolem BNF i nie może
otrzymać etykiety pola. Odpowiada mu akceptacja parsera, a nie wartość na
stosie redukcji.

Praktyczny test podczas projektowania AST brzmi:

> Czy analizator semantyczny albo inny późniejszy etap będzie chciał odwiedzić obiekt tego typu?

Jeżeli tak, właściwy jest zwykle `node`. Jeżeli analizator będzie chciał od razu otrzymać dzieci reguły, właściwy jest zwykle `inline`. W razie wątpliwości decyzję należy podejmować według docelowego API AST, a nie według długości reguły ani liczby jej alternatyw.

Wybór `node` lub `inline` nie zmienia rozpoznawanego języka, produkcji, `RuleId` ani automatu LR. Zmienia wyłącznie sposób składania wyniku redukcji w AST.

## Nazwy alternatyw `#Nazwa`

Nazwa po `#` nie rozstrzyga gramatyki i nie usuwa konfliktów LR. Określa wyłącznie publiczny wariant AST dla reguły `node`.

Bez nazw alternatyw:

```text
node literal
    : value=NUMBER
    | value=STRING
    ;
```

Obie produkcje tworzą jeden rodzaj węzła `Literal`. Węzeł może wewnętrznie zachować `RuleId` albo numer alternatywy.

Z nazwami alternatyw:

```text
node literal
    : value=NUMBER #NumberLiteral
    | value=STRING #StringLiteral
    ;
```

Generator może utworzyć osobne warianty:

```cpp
using Literal = std::variant<NumberLiteral, StringLiteral>;
```

Dla reguły `node` obowiązuje zasada „wszystkie albo żadna”:

- można nie nazwać żadnej alternatywy;
- można nazwać każdą alternatywę;
- nie można nazwać tylko części alternatyw.

Zabroniony jest więc zapis:

```text
node literal
    : value=NUMBER #NumberLiteral
    | value=STRING
    ;
```

## Wspólny pierwszy symbol

Uwaga: poniższy schemat `if/else`, po uzupełnieniu o zwykłe instrukcje bazowe,
ilustruje także problem dangling else. Nie dowodzi, że sama numeracja `RuleId`
rozwiązuje konflikt: potrzebna jest jednoznaczna gramatyka, jawna polityka
konfliktów albo GLR. Nazwy alternatyw pozostają wyłącznie metadanymi AST.

Wspólny pierwszy symbol nie wymusza nadawania nazw alternatywom:

```text
node statement
    : IF condition THEN statement
    | IF condition THEN statement ELSE statement
    ;
```

Parser LR rozróżnia produkcje za pomocą stanów, lookaheadu i `RuleId`. Nazwy po `#` nie uczestniczą w tej decyzji. Można je dodać, jeśli obie produkcje mają tworzyć różne warianty AST:

```text
node statement
    : IF condition THEN statement                #IfStatement
    | IF condition THEN statement ELSE statement #IfElseStatement
    ;
```

Jeżeli dwie produkcje są identyczne, nadanie im różnych nazw również nie rozwiązuje konfliktu:

```text
node value
    : IDENT #FirstValue
    | IDENT #SecondValue
    ;
```

Produkcje mają różne `RuleId`, ale deterministyczny parser otrzymuje konflikt reduce/reduce. `#FirstValue` i `#SecondValue` są tylko nazwami wyników, a nie priorytetami ani warunkami wyboru.

## Preprocesor i pliki `.agi`

Przed frontendem Agas działa osobny, mały preprocesor tekstowy. Jest to część ładowania źródeł, a nie część gramatyki Agas. Jego zadaniem jest wyłącznie składanie wejścia z plików oraz wybieranie fragmentów zależnych od zadanego wariantu. Nie jest to preprocesor C i nie rozwija makr w treści reguł.

Lexer i parser Agas zawsze dostają jeden, już rozwinięty tekst. Nie widzą dyrektyw `#include`, `#ifdef`, `#else` ani `#endif`, dlatego dyrektywy nie są opisane regułami w `Ag.g4` ani `Ag.ag` i nie trafiają do modelu gramatyki, `RuleId` ani AST.

Plik główny ma rozszerzenie `.ag`. Fragmenty przeznaczone do włączenia mają rozszerzenie `.agi` (Agas include). Dzięki temu dużą gramatykę można podzielić według dziedzin, na przykład:

```text
grammar C;

#include "declarations.agi"
#include "expressions.agi"
#include "statements.agi"
```

Plik `.agi` nie jest samodzielną gramatyką i nie zawiera deklaracji `grammar`. Powinien zawierać kompletne elementy najwyższego poziomu, przede wszystkim całe reguły. Po rozwinięciu wszystkich włączeń wynik musi być jednym poprawnym dokumentem Agas.

Pierwsza wersja preprocesora obsługuje tylko:

```text
#include "relative/path.agi"
#ifdef SYMBOL
#else
#endif
```

Symbole warunkowe są przekazywane do frontendu przez wywołujący program lub konfigurację generatora. Przykładowo ten sam plik może przechowywać wariant C90 i późniejszy:

```text
#ifdef C90
node iterationStatement
    : WHILE LPAREN expression RPAREN statement
    ;
#else
node iterationStatement
    : WHILE LPAREN expression RPAREN statement
    | FOR LPAREN forDeclaration expression? SEMI expression? RPAREN statement
    ;
#endif
```

`#ifdef` można zagnieżdżać. W jednym bloku może wystąpić najwyżej jedno `#else`. Każdy plik musi sam bilansować swoje `#ifdef` i `#endif`; blok warunkowy nie może zaczynać się w jednym pliku, a kończyć w innym. Dyrektywa `#include` w nieaktywnym bloku nie wczytuje pliku.

Ścieżka `#include` jest rozwiązywana względem katalogu pliku zawierającego dyrektywę. Dozwolone są tylko pliki `.agi`. Cykl włączeń jest błędem, a diagnostyka powinna pokazać cały łańcuch plików. Wielokrotne włączenie tego samego pliku nie jest niejawnie pomijane: jeżeli spowoduje powtórzenie reguł, zgłosi to zwykła walidacja gramatyki.

Pierwsza wersja celowo nie obsługuje `#define`, `#undef`, `#if`, `#elif`, makr funkcyjnych, podstawiania tekstu, operatorów arytmetycznych ani składni `#include <...>`. Jeżeli okażą się potrzebne, mogą zostać dodane później bez zmiany podstawowej gramatyki Agas.

### Rozróżnienie dyrektywy i `#Alternative`

Znaczenie `#` zależy od jego położenia w fizycznej linii źródła:

- jeżeli `#` jest pierwszym znakiem innym niż spacja lub tabulator, rozpoczyna dyrektywę preprocesora;
- jeżeli przed `#` znajduje się symbol produkcji, jest to token `HASH` rozpoczynający nazwę alternatywy.

Poprawnie:

```text
#include "expressions.agi"

node literal
    : value=NUMBER #NumberLiteral
    | value=STRING #StringLiteral
    ;
```

Nazwa alternatywy musi zatem znajdować się na tej samej fizycznej linii co końcowa część alternatywy. Taki zapis jest błędny, ponieważ druga linia zostanie rozpoznana jako dyrektywa:

```text
node literal
    : value=NUMBER
      #NumberLiteral
    ;
```

Rozpoznanie dyrektyw następuje przed uruchomieniem frontendu Agas. Do lexera i parsera trafia już rozwinięty strumień bez linii dyrektyw. W tym strumieniu `HASH` oznacza wyłącznie nazwę alternatywy. Preprocesor powinien razem z tekstem przekazać mapowanie wygenerowanych linii na plik i linię źródłową, aby błędy parsera wskazywały właściwy plik `.ag` albo `.agi`.

## Reguły walidacji

Frontend Agas powinien po zbudowaniu modelu gramatyki sprawdzić co najmniej:

1. `inline` nie ma żadnej nazwy alternatywy `#...`;
2. wieloalternatywne `node` ma nazwy przy wszystkich alternatywach albo przy żadnej;
3. nazwy alternatyw w jednej regule są unikalne;
4. nazwy pól w jednej alternatywie są unikalne, chyba że format później jawnie dopuści akumulację;
5. operator `?`, `*` albo `+` jest przypisany do pojedynczego symbolu;
6. pusta produkcja jest zapisana przez `empty`;
7. dwie tekstowo identyczne produkcje pozostają różnymi `RuleId` i są raportowane jako możliwe źródło konfliktu;
8. nazwy po `#` nie wpływają na FIRST, FOLLOW, budowę stanów LR ani tablicę ACTION/GOTO.

Bootstrapowy `Ag.g4` może składniowo przyjąć nazwę alternatywy także tam, gdzie później okaże się ona niedozwolona. Dzięki temu właściwy frontend Agas zgłosi precyzyjny błąd semantyczny, na przykład:

```text
reguła inline `optionValue` nie może nazywać alternatywy `IntegerOption`
```

zamiast ogólnego błędu składni przy znaku `#`.

## Podsumowanie

Trzy różne mechanizmy mają trzy różne zadania:

| Mechanizm | Zadanie |
|---|---|
| `RuleId` | wewnętrzna, jednoznaczna tożsamość produkcji dla LR |
| `field=Symbol` | wskazanie wartości przekazywanej lub zapisywanej w AST |
| `#Alternative` | opcjonalna publiczna nazwa wariantu węzła `node` |

`RuleId` jest zawsze obowiązkowy i wystarcza maszynie LR. Etykiety pól projektują zawartość drzewa. Nazwy alternatyw są potrzebne tylko wtedy, gdy użytkownik chce otrzymać osobne, nazwane warianty AST.

## Klasy lexera sterowane zakresem reguły parsera

```ag
lexerClasses {
    SHIFT = true;
    WORDS = false;
}

node typeName -> disable(SHIFT)
    : IDENT arguments?
    ;
inline arguments : LT typeName GT;
inline expression -> enable(SHIFT) : IDENT SHR IDENT;
inline keyword -> enable(WORDS) : READ | WRITE;

SHR : '>>' -> require(SHIFT);
GT : '>';
LT : '<';
READ : 'read' -> require(WORDS);
WRITE : 'write' -> require(WORDS);
IDENT : [a-z]+;
```

Deklaracje nadają nazwy maksymalnie 64 niezależnym bitom, w kolejności
wystąpienia; `true`/`false` określa początkową aktywność. Brak `require`
oznacza regułę zawsze aktywną. Kilka `require(...)` przy tokenie wymaga
wszystkich wskazanych klas. Priorytet reguł i najdłuższe dopasowanie
obowiązują wśród aktywnych reguł.

Nagłówek reguły parsera może zawierać `enable(...)` i `disable(...)`.
Kontekst dziedziczy się przez wywołania, także przez pomocniki EBNF;
zagnieżdżona deklaracja przesłania wskazane bity. Po zakończeniu reguły
obowiązuje kontekst wywołującego. Nie są to wykonywane akcje push/pop.

Generator specjalizuje osiągalne pary reguła–maska, a nie cały zbiór
potęgowy klas. Limit bezpieczeństwa wynosi obecnie 4096 osiągalnych
kontekstów. Plan lexera uwzględnia stan LR i już poznany prefiks podglądu,
więc działa także dla k > 1. Sprzeczne wymagania klas oraz brak aktywnej
reguły dla wymaganego terminala są błędem. Scalenie LALR może wprowadzić
sprzeczność kontekstów mimo braku konfliktów ACTION; wtedy warto sprawdzić LR.

Ścieżka C++ obsługuje `--table` i `--diagnose-parse`; ta druga wypisuje
tokeny i maski. `--emit-package` eksportuje również kontekst lexera,
redukcje AST i metadane pokrycia. Pakiet wykonują runtime C++ i Rust;
`--parse-package PAKIET PLIK` wypisuje AST w JSON. Kontrakt opisuje
[LEXER_CONTEXT.md](LEXER_CONTEXT.md). Eksport samego DSL
i statycznego kodu Rust dla klas pozostaje zablokowany: potrzebny jest
pełny pakiet z planem kontekstu.
Reguły `skip` i `channel` mogą być bezwarunkowe; połączenie ich z `require`
nie jest jeszcze obsługiwane przez parser kontekstowy.
