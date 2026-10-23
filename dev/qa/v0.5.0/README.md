# Kontrola 0.5.0

Výsledky vytvořila skutečná aplikace přes --capture bez okna, bez Computer Use. Zkontrolované náhledy: sample0 UI 22924, sample1 postava 22877, sample2 tráva 23026, sample3 rozmazaný battlebus; oba režimy s výchozími hodnotami. Dále pes 22879 při 1024 a 4096 px a režim hran 1536 px. PNG obsahují původní obrázek, překryv, hrany, regionové výplně a nezjednodušenou segmentaci.

## Chyba psa

Původní 0.4.0 při analýze 4096 (zdroj 3840×2160, bez zvětšování) slila tělo s modrým pozadím: [před](dog-old4096/regions.png). 0.5.0 zachovává hlavu, tělo i červené oblečení: [po](dog4096/regions.png). Oprava brání sloučení přes hranici s výrazným úsekem a úniku objektu do téměř jednotného pozadí. Zachovává také více některých dekorativních přechodů. Náklady 4K jsou stále vysoké: proces 469 ms, CGR 191 367 B; původní verze 525 ms / 186 437 B. Nejde o opakovaný medián.

Přiblížení tlapek: [jas 100 %](paws-1/detail.png), [jas 35 %](paws-.35/detail.png). Stejné nastavení: 1536 px, barva 100 %, detail 0, tolerance 1,3, budget 30, minimální délka 12 px, spojování vypnuté. Druhý snímek potlačil volné čáry pod tlapkami. Některé slabé detaily oblečení také zmizely, citlivost tedy není sémantický filtr stínů.

## Benchmark

Devět analýz, první zahřívací; medián osmi. CPU řetězec = process + příprava + 2048×1152 překryv + encode; nezahrnuje načtení obrázku, disk a WinUI prezentaci. Process zahrnuje zmenšení.

- Hrany 2K, vzorek 22877: řetězec 49,43 ms, process 41,74 ms, render 3,00 ms, CGR 47 554 B. [Log](benchmark-edges2048.txt).
- Regiony 2K: řetězec 278,84 ms, process 245,13 ms, render 3,43 ms, CGR 342 872 B. [Log](benchmark-regions2048.txt).
- Regiony 1024: řetězec 87,54 ms, process 73,62 ms, render 2,50 ms (stále 2K výstup), CGR 171 827 B. [Log](benchmark-regions1024.txt).

Cíl celého převodu pod 10 ms není splněný. Fáze jsou v jednotlivých logách. Starší log regionů má převzaté názvy RGB gradient/suppress/graph; v režimu regionů znamenají unions/palette/seams. Benchmark nyní tiskne správné názvy.

CGR4 na shodné geometrii z původní verze: 376 550 → 323 315 B, úspora 14,1 %, bez změny geometrie nebo přesnosti. [Log](codec-regions.txt). Nové zpřesnění malých kontrastních obrysů může zvýšit počet segmentů a velikost souboru. Krátké hranice režimu 1 se ve formátu zachovávají kvůli uzavřeným výplním.

Zbývající nedostatky: textury a tvrdší části gradientů mohou tvořit nežádoucí hrany; malé písmo v režimu 1 při 1024 ztrácí detaily; rozmazaný battlebus nelze přesně rekonstruovat. Kontrola WinUI ovládání je kompilací, ne automatickým proklikáním.

Finální Release sestava prošla testem core (ctest). Dodatečné testy porovnaly každý pixel sériového a souběžného renderu regionů, šikmých tahů, překryvu, zdrojové průhlednosti a ořezu pozadí. Capture načetl soubory z 0.2.0, 0.3.0, 0.4.0 i nové regiony CGR4 (compat0–compat3), všechny skončily kódem 0. Release smoke přes capture.ps1 ověřil 1536 px, jas 35 %, budget 30 a vypnuté spojování: 8066 B, 685 tahů, 0 spojení. SHA256 dev a release exe je shodné; root Spustit.bat vybírá release/latest.txt = 0.5.0.
