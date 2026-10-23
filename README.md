> **Windows x64 release 0.6.0** is available in [Releases](https://github.com/FajkPes/Contour-Studio/releases/latest). Download and unpack the whole ZIP, then run `Spustit.bat`. The Microsoft Windows App Runtime 1.8 is required. Source code is under `dev/`; development builds and personal sample media are excluded.

# Contour Studio 0.6.0

C++20 / WinUI 3 aplikace s CPU detekcí, vlastním rendererem a kompaktním formátem **CGR5**. Spuštění: **Spustit.bat**, aktuální sestava v release/0.6.0, zdroje v dev.

Výchozí **Easy** režim zobrazuje jen zobrazení, hlavní citlivost, násobitel, geometrii, minimální délku/obvod a rozlišení analýzy. **Advanced** přidá režim regionů, oddělenou barvu/jas, spojování konců, budget, krytí, tloušťku, barvu obrysů a výměnný export. Popisy jsou v nápovědách pod tlačítky **?**. Výchozí jazyk je **English**, lze přepnout na **Čeština**. Jazyk, ovládací režim a barva se ukládají.

Výchozí režim 2 podle dodaného snímku: detail 0, násobitel 100 %, barva 89 %, jas 80 %, tolerance 0,70 px, spojování vypnuté, budget 60, minimální délka 3 px, analýza 2048 px. Nová skupina preferencí nastaví tento základ; pozdější vlastní změny se zachovají. Režim 1 ponechává 1024 px, detail 44, toleranci 1,55 a budget 16.

Otevři nebo přetáhni obrázek či aktuální CGR. Kolečko přibližuje, tažení posouvá, dvojklik přizpůsobí pohled. Analýza běží na pozadí s rušením. Rozlišení: 512, 768, 1024, 1536, 2048, 3072, 4096, 8192 nebo vlastní 64–8192 px delší strany, bez zvětšování zdroje.

Dole je skutečná velikost hotového CGR. Kódovaná data se počítají na pozadí a export je znovu nekóduje. Po otevření CGR jsou dostupné časy čtení, dekódování, přípravy a prvního renderu. Podrobná statistika odděluje analýzu a čas do zobrazení výsledku. Analýza zahrnuje i uvolnění pracovních bufferů; čas do zobrazení zahrnuje přípravu, kódování a prezentaci. Načtení původního obrázku se do analýzy nepočítá.

CGR5 ukládá pouze geometrii a potřebná metadata, bez původního obrázku. Regiony mají sdílené hranice pro budoucí výplně. Režim hran uchovává čáry, kruhové oblouky, obdélníky, zaoblení a rozpoznanou tloušťku. Čáry se nikdy nekreslí pomocí SVG. Barva obrysů je preference zobrazení, geometrie exportu se tím nemění.

**Staré CGR2–CGR4 se již nečtou.** Zpětná kompatibilita byla na požadavek odstraněná. CGR5 volí pro každý tah nejhrubší přesnou souřadnicovou jednotku, aniž by snížil stávající přesnost 1/8 px. XPRESS_HUFF probíhá jedním průchodem. Velikost závisí na počtu a složitosti hran; 30 kB pro každý obrázek není zaručených.

Zrychlení: vážené RGB gradienty SSE2, až osm CPU pracovníků, u vypnutého spojování osmibitový graf sousedů bez samostatných záznamů hran, méně alokací a levnější ověřování oblouků. UI a výsledky lze zachytit přímo aplikací bez Computer Use. Testy, měření a náhledy jsou v dev/qa/v0.6.0.

**Cíl celé 2K cesty kolem 10 ms stále není dosažený.** Aktuální výsledky uvádí dev/qa/v0.6.0/README.md. Detekce nerozpoznává sémanticky objekty ani text; některé textury a části stínů mohou zůstat, rozmazaný vstup nelze přesně rekonstruovat.

Windows x64, Windows App Runtime 1.8. Sestavení: **Sestavit.bat**, vývojové spuštění: **dev/Spustit-dev.bat**.
