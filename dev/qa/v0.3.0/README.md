# Kontrola 0.3.0

7. 10. 2026, Release x64 MSVC 19.51. Režim 2, detail 44, volitelný šum vypnutý, tolerance 1,55 px, budget 16. Detekce a rasterizace na CPU, žádné Computer Use. PNG generuje ContourStudio.exe --capture přes stejný RasterScene jako interaktivní aplikace.

Benchmark vynechává první z devíti průchodů, uvádí medián osmi dalších. Celkový výpočet zahrnuje normalizaci většího zdroje na rozlišení analýzy; nezahrnuje dekódování, render ani export. Detekční fáze režimu 2 zahrnuje vyhlazení, RGB gradient, lokální ostrost, potlačení nemaxim a graf. Je širší než detekční fáze režimu 1.

- UI 22924, analýza 2048 × 1152: převod 43,934 ms, detekce 22,699 ms, CGR 16 548 B. Příprava 0,378 ms, průměr osmi renderů 2K 4,197 ms. Encode + atomické uložení 1,741 ms.
- Postava 22876, analýza 2048 × 1152: převod 77,924 ms, detekce 37,697 ms, CGR 98 352 B. Příprava 1,540 ms, render 10,275 ms. Encode + atomické uložení 4,205 ms.
- Postava 1024 × 576: převod 21,979 ms, CGR 33 791 B. Benchmarkový render je stále do 2048 × 1152 (7,587 ms); capture při skutečné velikosti 1024 má vlastní měření v metrics.txt.
- Postava 512 × 288: převod 7,341 ms, CGR 12 087 B. Benchmarkový 2K render 6,352 ms; capture render skutečného 512 náhledu je přibližně 1 ms.

Přesné protokoly: character-benchmark.txt, ui-benchmark.txt, character1024-benchmark.txt a character512-benchmark.txt. Jde o lokální měření, ne záruku pro jiný počítač.

Každá capture složka obsahuje original.png, overlay.png, edges.png, result.cgr, reopened.png a metrics.txt. character2048 navíc detail.png a detail-edges.png: výřez tlačítek zvětšený třikrát vlastním rendererem. grass2048 (23026), blur2048 (tropical storm ... frame557) mají jednorázové časy, nikoli medián. regions512 ověřuje zachovaný režim 1; reopen-regions-v02 načtení CGR z 0.2.0. reopen-edges ověřuje samostatné načtení nového grafu. Znovukódovaný již kvantizovaný CGR vykreslil v obou importních případech identické pixely.

Všech 25 PNG ve samples úspěšně prošlo capture při analýze 512. Výstupy jsou v samples512/původní-název/. Vizuálně prohlédnuty: hlavní 2K UI, postava, tráva a battlebus, zvětšený výřez tlačítek, 512 náhledy obou battlebusů, postavy 22877 a UI/herních vzorků 22918–22923, 22925–22929, 22950, 22953, 22954, 23006, 23012, 23013 a 23035.

Pozorování: většina proužkování na čistém UI pozadí se nestala vektorovou hranou, UI dekorace a křížky zůstaly. Obrysy tlačítek a postavy jsou výrazně úspornější než samostatná pixelová detekce. V texturách, trávě a některých světelných přechodech ale zůstávají krátké či přerušované čáry. U zvýrazněného tlačítka Equip A Sprite zůstávají také některé vnitřní čáry; rozpoznávání nesplývá se sémantickým oddělením hladkého stínování. Na battlebusu není obnovena přesná geometrie rozmazaných detailů. Při 512 mizí část drobného textu. Nejde o bezchybnou detekci všech hran.

CGR kvantizuje geometrii na 1/8 px. Postava při prvním uložení má průměr maximální kanálové odchylky rasteru 0,107/255 a změnu 1,321 % pixelů; většinou jde o posun antialiasingu. UI 0,045/255. Největší lokální rozdíl může být vysoký na jednotlivém pixelu, pokud tenká světlá čára přeskočí na sousední pixel. reopening už kvantizovaného grafu je stabilní.

Automatické testy prošly: RGB hrany při podobném jasu, lineární i nelineární hladké gradienty bez čar, jemný 4 × 4 barevný tvar, třípixelový pás jako jedna čára, rámeček jako jeden obdélníkový tah, zaoblená karta s radiusem, budget a konce čar, pevný bod spojení, absence falešného oblouku přes rovnou stěnu, metadata tloušťky a kulatých krytek i jejich rasterizace, CGR round trip a odmítnutí poškození. Dále zůstaly všechny dřívější regionové kontroly včetně plástve, otvorů, sdílených hran a 8K zdroje. 8K test je jednobarevný 7680 × 4320 zdroj s analýzou 2048; není výkonnostním testem plné 8K analýzy.

Nové WinUI ovládací prvky byly sestavené, jejich přímé klikání v této verzi nebylo automatizovaně ověřeno. Výpočet, renderer, PNG, CGR, SVG a JSON byly ověřeny bez okna. Úspěšný build a ctest jsou podmínkou publikování skriptem build.ps1.

Cíl celého 2K převodu pod 5 ms není splněný. CGR pod 30 kB je splněný u tohoto UI a menších analýz, ne u členité postavy nebo trávy. Rozpoznání tloušťky se omezuje na vhodné pásy a obdélníkové rámečky; obecné složité tenké kontury mohou zůstat dvěma obrysy.
