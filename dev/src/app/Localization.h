#pragma once
#include <functional>
#include <string>
#include <vector>
namespace app::locale {
inline bool czech = false;
inline std::vector<std::function<void()>> bindings;
inline winrt::hstring translate(std::wstring_view input) {
    std::wstring out(input);
    if (!czech) {
        static const std::pair<const wchar_t *, const wchar_t *> phrases[] = {
            {L"Tolerance zjednodušení v pixelech analýzy. Vyšší hodnoty šetří data; malé kontrastní obrysy "
             L"se chrání.",
             L"Simplification tolerance in analysis pixels. Higher values save data; small high-contrast "
             L"outlines are protected."},
            {L"Síla světlých a tmavých přechodů. Snížení potlačí stíny, ale může odstranit bílé či šedé "
             L"písmo.",
             L"Strength of light and dark transitions. Lower values suppress shadows but may remove white or "
             L"grey text."},
            {L"Maximum úseků volné čáry. Konce a spojení zůstávají; primitiva mají vlastní kompaktní zápis.",
             L"Maximum segments per free path. Endpoints and junctions stay fixed; primitives use compact "
             L"records."},
            {L"CGR uchovává kompaktní geometrii bez originálního obrázku. SVG a JSON jsou výměnné formáty.",
             L"CGR stores compact geometry without the original image. SVG and JSON are interchange "
             L"formats."},
            {L"Skutečná velikost aktuálního CGR a naměřené časy. Časy načtení se zobrazí při otevření CGR.",
             L"Actual size of the current CGR and measured timings. Load timings appear when opening CGR."},
            {L"Odstraní čáry kratší než tato délka. Uzavřené čáry používají obvod. Nula vypne filtr.",
             L"Removes paths shorter than this length. Closed paths use perimeter. Zero disables filtering."},
            {L"Režim 1 vytváří uzavřené regiony pro výplně. Režim 2 hledá ostré hrany bez regionů.",
             L"Mode 1 creates closed regions for fills. Mode 2 detects sharp edges without regions."},
            {L"Spojuje pouze blízké souhlasné konce s barevným důkazem. Platí pro režim 2.",
             L"Joins nearby aligned endpoints supported by color evidence. Applies to mode 2."},
            {L"Připraveno · PNG, JPEG, BMP, TIFF a další formáty podle systémových kodeků",
             L"Ready · Images and CGR files"},
            {L"Násobí citlivost obou složek. 100 % je běžná citlivost; rozsah 1–200 %.",
             L"Scales both sensitivity channels. 100 % is normal; range 1–200 %."},
            {L"Delší strana analýzy, bez zvětšování zdroje. Vlastní rozsah 64–8192 px.",
             L"Longest analysis side, without upscaling the source. Custom range 64–8192 px."},
            {L"Vyšší hodnota zachytí slabší hrany a více detailů, ale také textury.",
             L"Higher values capture weaker edges and more detail, including textures."},
            {L"Hotovo · kolečkem přibliž, tažením posuň · kliknutím zjistíš region",
             L"Done · wheel to zoom, drag to pan, click to inspect a region"},
            {L"Zvolí originál, překryv hran, samotné hrany nebo výplně regionů.",
             L"Selects the original image, edge overlay, edges only, or region fills."},
            {L"Síla barevných přechodů nezávisle na jasu. Nula složku vypne.",
             L"Strength of color transitions independently of brightness. Zero disables this channel."},
            {L"Barva obrysů pouze při zobrazení, uložená jako preference.",
             L"Display outline color, saved as a preference."},
            {L"CGR načten · původní rastrový obrázek není součástí grafu",
             L"CGR loaded · original image is not stored in the graph"},
            {L"Zpracování zrušeno. Předchozí výsledek zůstává.",
             L"Processing cancelled. Previous result retained."},
            {L"Tloušťka vykreslení; nemění uloženou geometrii.",
             L"Display line width; does not change saved geometry."},
            {L" úseků", L" segments"},
            {L"Čas do zobrazení: ", L"Time to result: "},
            {L"Analýza celkem: ", L"Analysis total: "},
            {L"Sdílené hranice · uzavřené regiony · pouze CPU",
             L"Shared boundaries · closed regions · CPU only"},
            {L"Nastavení změněno · čekám na dokončení úpravy…",
             L"Settings changed · waiting for adjustment…"},
            {L"CPU: detekce, regiony a sdílená geometrie…", L"CPU: detection, regions and shared geometry…"},
            {L"CPU: ostré RGB hrany a úsporná geometrie…", L"CPU: sharp color edges and compact geometry…"},
            {L"Původní obrázek není součástí CGR grafu.", L"The original image is not stored in CGR."},
            {L"Hotovo · kolečkem přibliž, tažením posuň", L"Done · wheel to zoom, drag to pan"},
            {L"Otevři obrázek nebo ho sem přetáhni.", L"Open an image or CGR, or drop it here."},
            {L" úseků · koncové body se zachovají", L" segments · endpoints retained"},
            {L"Kolečko: zoom   ·   Tažení: posun", L"Wheel: zoom   ·   Drag: pan"},
            {L"Krytí obrysů pouze při zobrazení.", L"Outline opacity for display only."},
            {L"2 · Pouze ostré barevné hrany", L"2 · Sharp color edges"},
            {L"Celková citlivost · násobič", L"Sensitivity multiplier"},
            {L"JSON · graf pro další práci", L"JSON · region graph"},
            {L" px analýzy · 0 vypne filtr", L" analysis px · 0 disables filtering"},
            {L"Spojovat blízké konce hran", L"Join nearby edge endpoints"},
            {L"zaoblený obdélník, radius ", L"rounded rectangle, radius "},
            {L"Budget bodů na jednu čáru", L"Point budget per path"},
            {L" % · světlé a tmavé hrany", L" % · light and dark edges"},
            {L"Obrázky a vektorový graf", L"Images and CGR graph"},
            {L"Tolerance zjednodušení: ", L"Simplification tolerance: "},
            {L"Minimální délka / obvod", L"Minimum length / perimeter"},
            {L"Delší strana v pixelech", L"Longest side in pixels"},
            {L" regionů · uložený graf", L" regions · saved graph"},
            {L"SVG · vyplněné regiony", L"SVG · filled regions"},
            {L" % · barevné přechody", L" % · color transitions"},
            {L"1 · Uzavřené regiony", L"1 · Closed regions"},
            {L"CGR · kompaktní graf", L"CGR · compact graph"},
            {L"SVG · společné hrany", L"SVG · shared edges"},
            {L" čar · uložený graf", L" paths · saved graph"},
            {L"Citlivost a detail", L"Main sensitivity"},
            {L"Citlivost na barvu", L"Color sensitivity"},
            {L"Zatím bez obrázku.", L"No image loaded."},
            {L"Kompaktní graf CGR", L"Compact CGR graph"},
            {L"Příprava renderu: ", L"Render preparation: "},
            {L"Rozlišení analýzy", L"Analysis resolution"},
            {L"Zrušit zpracování", L"Cancel processing"},
            {L"Graf regionů JSON", L"JSON region graph"},
            {L"Raster příprava: ", L"Render preparation: "},
            {L"Vyřazeno délkou: ", L"Removed by length: "},
            {L"polygon / oblouky", L"polygon / arcs"},
            {L"přizpůsobit oknu", L"fit to window"},
            {L"Originál + hrany", L"Original + edges"},
            {L"Vyplněné regiony", L"Filled regions"},
            {L"Citlivost na jas", L"Brightness sensitivity"},
            {L"Načítám obrázek…", L"Loading image or CGR…"},
            {L"Spojené mezery: ", L"Joined gaps: "},
            {L"Otevřít obrázek", L"Open image or CGR"},
            {L"Ukládám export…", L"Saving export…"},
            {L"Všechny soubory", L"All files"},
            {L"Způsob analýzy", L"Analysis mode"},
            {L" ms · snímek: ", L" ms · frame: "},
            {L"Režim ovládání", L"Controls"},
            {L"Velikost CGR: ", L"CGR size: "},
            {L"První render: ", L"First render: "},
            {L"Tloušťka čáry", L"Line width"},
            {L"Naměřené časy", L"Statistics"},
            {L"výrazné tvary", L"strong shapes"},
            {L" · nad cílem", L" · above target"},
            {L"Barva obrysů", L"Outline color"},
            {L"Vybrat barvu", L"Choose color"},
            {L"Dekódování: ", L"Decoding: "},
            {L"Pouze hrany", L"Edges only"},
            {L"Přizpůsobit", L"Fit"},
            {L" px analýzy", L" analysis px"},
            {L"jemné hrany", L"fine edges"},
            {L"Geometrie: ", L"Geometry: "},
            {L" zaoblených", L" rounded rectangles"},
            {L" · pod 5 ms", L" · below 5 ms"},
            {L"Exportovat", L"Export"},
            {L"Dvojklik: ", L"Double-click: "},
            {L"Vektor SVG", L"SVG vector"},
            {L"Zmenšení: ", L"Resize: "},
            {L" obdélníků", L" rectangles"},
            {L" · plocha ", L" · area "},
            {L"Jednoduchý", L"Easy"},
            {L"Kódování: ", L"Encoding: "},
            {L"Zobrazení", L"Display"},
            {L"Geometrie", L"Geometry"},
            {L"Uloženo: ", L"Saved: "},
            {L"Detekce: ", L"Detection: "},
            {L"Regiony: ", L"Regions: "},
            {L"Pokročilý", L"Advanced"},
            {L"Originál", L"Original"},
            {L"Vlastní…", L"Custom…"},
            {L"vyvážené", L"balanced"},
            {L"Obrysy: ", L"Tracing: "},
            {L"Celkem: ", L"Analysis total: "},
            {L" regionů", L" regions"},
            {L" oblouků", L" arcs"},
            {L"Analýza ", L"Analysis "},
            {L"obdélník", L"rectangle"},
            {L"Překryv", L"Overlay"},
            {L"Chyba: ", L"Error: "},
            {L"Filtr: ", L"Filter: "},
            {L" úseček", L" lines"},
            {L"Čtení: ", L"File read: "},
            {L"Zdroj ", L"Source "},
            {L"Krytí", L"Opacity"},
            {L"Jazyk", L"Language"},
            {L" čar", L" paths"},
        };
        for (auto [cs, en] : phrases) {
            size_t pos = 0;
            while ((pos = out.find(cs, pos)) != std::wstring::npos) {
                out.replace(pos, wcslen(cs), en);
                pos += wcslen(en);
            }
        }
    }
    return winrt::hstring(out);
}
inline void refresh() {
    for (auto &update : bindings)
        update();
}
struct Label {
    winrt::Microsoft::UI::Xaml::Controls::TextBlock control{nullptr};
    std::wstring original;
};
inline std::vector<Label> labels;
inline void setText(winrt::Microsoft::UI::Xaml::Controls::TextBlock control, std::wstring_view original) {
    for (auto &item : labels)
        if (item.control == control) {
            item.original = original;
            control.Text(translate(original));
            return;
        }
    labels.push_back({control, std::wstring(original)});
    control.Text(translate(original));
}
inline void refreshLabels() {
    for (auto &label : labels)
        label.control.Text(translate(label.original));
}
} // namespace app::locale
