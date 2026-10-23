# Ověření 0.6.0

Měřeno lokálně v Release, CPU, bez GPU akcelerace analýzy. Benchmark má jeden zahřívací a osm měřených průchodů. Časy závisejí na obrázku a počítači.

## Výkon

Vstup `samples/Snímek obrazovky (22877).png`, 3840 × 2160. Režim hran: detail 0, násobič 100 %, barva 89 %, jas 80 %, geometrie 0,7 px, 60 bodů, minimální délka 3 px, spojování vypnuté.

- Analýza 2048: celá CPU cesta 37,13 ms, samotné zpracování 30,03 ms; 61 712 B. Viz `final2048.txt`.
- Analýza 1024: celá CPU cesta 13,34 ms, zpracování 9,33 ms; 24 749 B. Viz `final1024.txt`.
- Analýza 512: celá CPU cesta 5,49 ms, zpracování 3,05 ms; 9 047 B. Viz `final512.txt`.
- Uzavřené regiony 1024: celá CPU cesta 70,45 ms, zpracování 58,22 ms; 171 827 B. Viz `reserved-regions.txt`.

Celá CPU cesta zahrnuje zmenšení, analýzu, přípravu geometrie, render 2048 × 1152 a kódování. Nezahrnuje dekódování vstupního PNG, disk ani plánování WinUI. Cíl 10 ms při 2K zatím splněný není. Výchozí rozlišení režimu hran zůstává 2048.

Před optimalizacemi v této sérii měřil stejný 2K test 61,38 ms (`before-simd.txt`), nyní 37,13 ms, přibližně o 40 % méně. Poslední paralelizace přípravy snížila 38,26 na 37,13 ms; 512px varianta se snížením prahu paralelizace zrychlila z 10,30 na 5,49 ms. Rezervace paměti topologie snížila režim regionů z 75,96 na 70,45 ms.

## Formát a ukládání

CGR5 používá přesnou adaptivní mřížku souřadnic, malé příkazy, prostorové řazení hran a jediný průchod XPRESS_HUFF. Stejná geometrie hlavního snímku zabírá 61 712 B oproti 62 467 B předchozího kódování, tedy pouze o 1,2 % méně; nejde o dramatickou úsporu. Kvantizace zůstává 1/8 pixelu. UI drží hotové kódování v paměti a při exportu je znovu nepočítá. Uložení je atomické. Čte se pouze CGR5, staré formáty byly úmyslně odstraněny.

UI test načetl CGR: čtení 0,36 ms, dekódování 1,58 ms, příprava renderu 1,32 ms, první render 2,09 ms; skutečný čas do výsledku v UI 13,36 ms. Viz `ui-release/ui-state.txt`.

## Kontroly

- Core testy: barevné hrany, gradienty, malé tvary, průhlednost, sdílené hranice, díry, topologie, 8K vstup, kompaktní roundtrip, odmítnutí starého formátu, vlastní barva a shoda paralelního renderu.
- SHA256 výsledku hran shodné mezi `edges2048.svg.cgr` a `final2048.svg.cgr`.
- SHA256 regionů shodné mezi `final-regions.svg.cgr` a `reserved-regions.svg.cgr`.
- Vlastní snímky aplikace bez Computer Use: `ui-release/`. Vizuálně prohlédnut Easy česky, Advanced anglicky včetně spodních statistik a vlastní červené obrysy; předchozí série také oba jazyky a načtený CGR.
- Vizuálně prohlédnut pes (`dog-regions/regions.png`) a UI (`ui-edges/overlay.png`), oba případy též exportovány a znovu dekódovány. Regiony stále mohou rozdělit některé pozvolné přechody; tato série optimalizací to neřeší. Znovu otevřený výsledek se může nepatrně lišit kvůli stávající kvantizaci na 1/8 px.
