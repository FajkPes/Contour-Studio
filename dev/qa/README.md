# Kontrola verze 0.1.0

Sestavení Release x64 MSVC 19.51; výchozí detail 50, potlačení šumu zapnuté, tolerance 0,75 px a delší strana analýzy 2048 px. Datum: 7. 10. 2026. Časy jsou lokální měření, nikoli záruka pro jiný počítač. Benchmark vynechá první průchod a uvádí medián osmi dalších.

- `ui.svg`: vzorek 22924, 2560 × 1440 → 2048 × 1152. 910 regionů; rozdíly barev 2,462 ms, celý převod 165,254 ms.
- `character.svg`: vzorek 22876, 3840 × 2160 → 2048 × 1152. 3327 regionů; rozdíly barev 2,403 ms, celý převod 263,499 ms.
- `grass.svg`: vzorek 23026, 2560 × 1440 → 2048 × 1152. 4071 regionů; rozdíly barev 2,427 ms, celý převod 313,172 ms.
- `blur.svg`: rozmazaný battlebus, 2560 × 1440 → 2048 × 1152. 1864 regionů; rozdíly barev 2,473 ms, celý převod 245,642 ms.

Každý vzorek má SVG společných hran, `.svg.json` s grafem, `.svg.png` s překryvem a `.svg.regions.png` s vyplněnými regiony. Časy jednotlivých fází jsou v `*-timings.txt`. Celkový čas nezahrnuje WIC dekódování, sestavení Direct2D prostředků ani export.

Vizuálně zkontrolovány překryvy UI, postavy, trávy a battlebusu. Postava i výrazné části oblečení jsou oddělené; ve složitém pozadí zůstává mnoho malých regionů. V UI jsou zachované obrysy panelů a tlačítek, běžné větší texty i ikony. Většina proužkování pozadí se sloučila, několik falešných vodorovných hran v levém dolním rohu zůstává. Jemné UI prvky včetně slabých dekorativních křížků se mohou sloučit s pozadím. U trávy se zachytí část kontrastních stébel; slabá nebo překrytá stébla nejsou všechna samostatná. Rozmazaný battlebus ukazuje přibližné obrysy a slévání detailů; nelze z něj obnovit původní ostrou geometrii.

Automatické testy prošly v plné i CoreOnly sestavě. Kontrolují přesnou plochu pixelových regionů včetně otvorů, uzavření fitted smyček, platné směry referencí a právě dvě použití každé vnitřní společné hranice. Samostatný test plástve kontroluje tuto vlastnost na sousedících buňkách. Test 8K používá syntetický jednobarevný 7680 × 4320 vstup s výchozí 2K analýzou; není benchmarkem plné 8K geometrie.

Vydanou aplikaci jsem spustil, přes nativní dialog otevřel vzorek 22876 a ověřil dokončení analýzy, překryv a změnu detailu z 0 na 50 s novým výpočtem. Ctrl+S otevřelo exportní dialog; samotné uložení přes tento dialog nebylo dokončeno. Zápis SVG a JSON byl ověřen benchmarkem. SHA-256 publikovaných souborů odpovídají manifestu.

**Požadovaný převod pod 5 ms není splněný.** Největší čas spotřebovává segmentace a u členitých obrázků také trasování a fit geometrie. Vektorový graf sdílí hranice, ale zůstává závislý na složitosti vstupu; označení ultralehký není zárukou malé velikosti každého exportu.
