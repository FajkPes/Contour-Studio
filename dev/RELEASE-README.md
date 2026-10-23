# Contour Studio 0.6.0

Spuštění ContourStudio.exe nebo hlavní Spustit.bat. Windows x64, Windows App Runtime 1.8.

Nové Easy / Advanced UI, English / Čeština, nápovědy pod ?, volitelná barva obrysů a skutečná velikost CGR dole. CGR lze otevřít nebo přetáhnout; statistika ukáže čtení, dekódování, přípravu a první render. Export používá předem kódovaná data v paměti.

Výchozí hrany: detail 0, násobitel 100 %, barva 89 %, jas 80 %, tolerance 0,70 px, budget 60, minimální délka 3 px, analýza 2048 px, spojování vypnuté. Analýza nabízí vlastní hodnotu 64–8192 px. Režim regionů má výchozí 1024 px. Kolečko přibližuje, tažení posouvá, dvojklik přizpůsobí pohled.

Aktuální formát CGR5; staré CGR2–CGR4 již nelze otevřít. Zachovává stávající přesnost geometrie, přesnou jednotku tahů a XPRESS_HUFF. Neobsahuje původní obrázek. Barva obrysů je preference zobrazení.

CPU optimalizace: vážené SIMD gradienty, až osm pracovníků, osmibitový graf při vypnutém spojování, menší počet alokací a rychlejší ověřování oblouků. Cíl celé 2K cesty kolem 10 ms zatím není splněný; měření a kontrola UI jsou v dev/qa/v0.6.0/README.md hlavního projektu.

Headless: ContourStudio.exe --capture "obrazek.png" "vystup" --mode edges --analysis 2048
Výstupy: original.png, overlay.png, edges.png, result.cgr, reopened.png, metrics.txt. --crop X Y W H --zoom 3 pro detail. --connect zapne spojování. UI QA: --ui-qa "vystup" "obrazek.png" vytvoří snímky obou jazyků a režimů přímo z WinUI a potom zavře vlastní instanci.
