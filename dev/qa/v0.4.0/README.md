# Kontrola 0.4.0

7. 10. 2026, Release x64 MSVC 19.51. Bez Computer Use. PNG vytváří ContourStudio.exe --capture se stejným RasterScene jako aplikace.

Výchozí režim 2: detail 0, tolerance 1,3 px, budget 30 úseků, analýza 2048, minimum délky 6 px. Režim 1 má oddělené nastavení 44 / 1,55 / 16; filtr délky je dostupný v obou režimech. Nula filtr vypne. Šumový přepínač je odstraněný z UI.

Aktuální měření vzorku 22877 (Sprite Garden, zvednutá ruka), zdroj 3840×2160, analýza 2048×1152:

- Minimum 6: 6 064 tahů, CGR 45 729 B. Medián analýzy 54,128 ms, CPU řetězce 67,449 ms. Samostatné kódování 1,766 ms.
- Minimum 16: 3 022 tahů, CGR 30 377 B. Medián analýzy 53,276 ms, CPU řetězce 64,349 ms. Kódování 1,025 ms.
- Minimum 32: 1 449 tahů, CGR 18 651 B. Medián analýzy 54,553 ms, CPU řetězce 63,668 ms. Kódování 0,660 ms. Poslední série měla kolísání zátěže (maximum řetězce 74,744 ms); není zaručeným limitem.
- Analýza 1024 při minimu 6: CGR 17 877 B, analýza 15,077 ms, CPU řetězec 24,374 ms.
- Analýza 512 při minimu 6: CGR 6 277 B, analýza 7,609 ms, CPU řetězec 14,349 ms.
- UI 22924, analýza 2048 při minimu 6: CGR 9 106 B, analýza 32,912 ms, CPU řetězec 39,588 ms.

Benchmark má devět průchodů, první vynechává a uvádí medián osmi dalších. CPU řetězec měří skutečný čas process + prepare + nový overlay raster 2048×1152 + encode; nezahrnuje dekódování vstupu, zápis na disk ani upload/prezentaci ve WinUI. Proto se nesmí označit za kompletní latenci interaktivní aplikace. Rychlostní cíl 5–10 ms pro 2K není splněný.

CGR3 na přesně stejné geometrii starého CGR2 zmenšil 75 174 B na 59 724 B (codec-comparison.txt). Nejde o úbytek detailů: dekódovaný starý graf byl znovu zakódován. Nový zápis vynechává tloušťku 1 px, používá delta počátky a prostorové pořadí nezávislých tahů, potom XPRESS_HUFF. Geometrie je nadále na 1/8 pixelu. CGR2 z 0.2.0 i 0.3.0 se načetl.

Spojování před filtrem délky vyhledává blízké konce přibližně do 4 px. Kontroluje návaznost tečny na obou koncích, shodný RGB směr přechodu a barevný/gradientový důkaz v mezeře. Propojení přidává do původního grafu, potom jej trasuje a měří délku spojené čáry. Na vzorku 22877 přidalo 1 005 spojení. Je to omezená heuristika; počet není důkazem správnosti každého spojení. Není to neomezené prodlužování čar do vzdálených objektů. Režim 1 již má společný uzavřený graf a tato heuristika se na něj neuplatňuje.

Vizuálně byly prohlédnuty skutečné overlay PNG před a po délkovém filtru, finální výřez sprite-detail/detail.png (obličej, vlasy, rukáv, vzor bundy) a overlay režimu 1. Minimum 32 výrazně odstraní osamocené úlomky, ale ztrácí i drobné písmo, detaily karet a textur. Dlouhé nechtěné hrany mohou zůstat. Některé obrysy jsou přerušené a malé geometrické artefakty se stále vyskytují; detekce nerozumí sémanticky objektům.

Výplň regionů: regions-all a regions-filter32 mají obě 2 044 regionů v analýze 512. regions.png má v obou SHA256 0A9756F7633F11FCBD54A9F816C9EEA03BCA2708615027623AAAC784DAD6BC3E. Filtr zachoval výplň beze změny, skryl 4 604 krátkých hranic v překryvu. CGR v režimu 1 musí tyto hranice uchovat pro topologii, takže jej tento filtr podstatně nezmenší.

Release sestavení a ctest prošly. Nové jednotkové kontroly: screenshotové defaulty, minimum délky v režimu 2, zachovaná topologie a výplň v režimu 1, viditelnost hranic po CGR round trip, delta počátky a implicitní tloušťka, nepřerušená vlnitá hrana nedostala falešná propojení. Zůstaly testy RGB při podobném jasu, hladkých gradientů, šířky, rámečků, radiusu, budgetu, konců, rasteru, poškozených CGR, plástve, průhlednosti, 8K zdroje a rušení. Interaktivní ovládání WinUI nebylo automaticky proklikáno.

sprite-default, sprite-filter16, sprite-filter32 a sprite-detail jsou finální PNG/CGR z vydané 0.4.0. old-codec je zachycení vydané 0.3.0 se shodným detailem/tolerancí/budgetem. Early, sprite-6/16/32 a sprite-joined16 artefakty jsou průběžné pokusy před finálním spojováním a nemají se zaměňovat s finálním měřením. Starší kontrola všech 25 vzorků patří 0.3.0.
