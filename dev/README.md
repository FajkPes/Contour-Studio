# Vývoj Contour Studio 0.6.0

Build a publish: `powershell -NoProfile -ExecutionPolicy Bypass -File dev/build.ps1 -Publish`. C++20, MSVC, SDK 26100, WinUI 3 / Windows App SDK 1.8. Verze v version.txt a app.manifest. Dev/build je generovaný, release má jednotlivé verze.

Main.cpp vytváří WinUI, úlohy na pozadí, preference a cached CGR. Localization.h obsahuje anglické/české fráze a bindings pro živé přepnutí textů, voleb a nápověd. Easy schovává celé skupiny Advanced, nastavení uchovává; při návratu do Easy se export přepne na CGR. Preference analýzy jsou regionsV5/edgesV5/analysisV5, vzhled a jazyk v ui.

Renderer pouze předává hotovou CPU bitmapu přes softwarový WARP. RasterScene má volitelnou barvu obrysů a paralelní vodorovné pásy. Core defaultsFor určuje headless hodnoty. Režim bez spojování používá osmibitové sousedství každého kandidáta, druhý graf podporuje nepřilehlé propojení mezer. Oba trasují hranici jednou a zachovávají konce/spojení. Parallel.h používá Windows thread pool do osmi pracovníků. Ověřování kruhového oblouku nejprve zkontroluje čtverce radiálních vzdáleností a teprve potom úhly.

Compact.cpp čte a zapisuje pouze CGR5, žádné větve pro staré verze. Tah má přesnou souřadnicovou jednotku 1/8, 1/4, 1/2 nebo 1 px podle skutečné kvantované geometrie. Poloměr a tloušťka zůstávají na 1/8 px. Souřadnice a reference jsou delta varinty, malé úseky byte příkazy. Regionový graf uchovává sdílené hrany a výplně. XPRESS_HUFF má předem alokovaný výstup a jeden průchod; při neúspěchu či větším výstupu se použije nekomprimovaný payload. Checksum a validace rozměrů/počtů/reference zůstávají. saveCompactBytes ukládá cached data atomicky; loadCompact může měřit čtení a decode samostatně.

Capture: `powershell -NoProfile -ExecutionPolicy Bypass -File dev/capture.ps1 -Image "samples/Snímek obrazovky (22877).png" -Output dev/qa/moje -Dev`. -Connect zapne spojování, -BrightnessSensitivity a -ColorSensitivity jsou procenta, -ContrastMultiplier 1–200. Bez -Dev používá aktuální release. Přímé CLI přijímá citlivosti jako zlomky.

UI QA: ContourStudio.exe --ui-qa "absolutni-vystup" "absolutni-obrazek". Vlastní instance nepoužívá preference a neposouvá uživatelovy aplikace; její okno se umístí mimo plochu. RenderTargetBitmap zachytí skutečné WinUI a vloží vlastní aktuální renderer do oblasti SwapChainPanel. Výstupy en/cs-easy, en/cs-advanced, advanced-bottom, vlastní barva a otevřený CGR. ui-state.txt ověřuje hodnoty a load statistiku. ui-error.txt značí neúspěch; úspěšná složka nemá tento soubor.

Benchmark: contour_bench.exe "obrazek.png" "vystup.svg" 0 2048 edges 60 3 .7 .89 .8 1. Devět průchodů, první zahřívací, medián osmi. CPU řetězec je process + prepare + 2048×1152 overlay + encode, mimo WIC/disk/WinUI. Total procesu měří veřejný wrapper i uvolnění pracovních bufferů. Počátky fáze se mohou lišit od součtu vlivem alokací a ukončení práce. UI navíc ukazuje čas do zobrazení skutečného výsledku.

Testy: hrany RGB, gradienty, malé tvary, geometrie, konce, budget, sdílené hranice/topologie, CGR5 round trip a odmítnutí poškození/starých hlaviček, průhlednost, 8K, rušení, pixelová shoda sériového a paralelního rendereru a vlastní barva. Nové důkazy v qa/v0.6.0; historické QA složky zůstávají záznamem tehdejších verzí.
