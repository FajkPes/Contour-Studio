#include <Windows.h>
#undef GetCurrentTime
#include "Capture.h"
#include "ImageIO.h"
#include "Renderer.h"
#include <MddBootstrap.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <microsoft.ui.xaml.window.h>
#include <robuffer.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <sstream>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Microsoft::UI::Xaml::Markup;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Windows::Foundation;

#include "Localization.h"
using app::locale::setText;
namespace {
std::wstring qaOutput, qaImage;
SolidColorBrush color(uint32_t rgb) {
    return SolidColorBrush(
        winrt::Windows::UI::Color{255, uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)});
}
TextBlock text(const wchar_t *value, double size = 14, uint32_t rgb = 0xdde3ee) {
    TextBlock t;
    setText(t, value);
    t.FontSize(size);
    t.Foreground(color(rgb));
    t.TextWrapping(TextWrapping::Wrap);
    return t;
}
Button button(const wchar_t *value) {
    Button b;
    std::wstring original(value);
    b.Content(box_value(app::locale::translate(original)));
    app::locale::bindings.push_back(
        [b, original] { b.Content(box_value(app::locale::translate(original))); });
    b.Padding({14, 8, 14, 8});
    return b;
}
ComboBox choices(std::initializer_list<const wchar_t *> values) {
    ComboBox b;
    b.HorizontalAlignment(HorizontalAlignment::Stretch);
    std::vector<std::wstring> originals;
    for (auto value : values) {
        originals.emplace_back(value);
        b.Items().Append(box_value(app::locale::translate(value)));
    }
    app::locale::bindings.push_back([b, originals] {
        auto selected = b.SelectedIndex();
        for (uint32_t i = 0; i < originals.size(); ++i)
            b.Items().SetAt(i, box_value(app::locale::translate(originals[i])));
        b.SelectedIndex(selected);
    });
    b.SelectedIndex(0);
    return b;
}
void addSection(StackPanel panel, const wchar_t *title) {
    auto t = text(title, 15, 0xf5f7fc);
    t.Margin({0, 20, 0, 4});
    panel.Children().Append(t);
}
winrt::hstring number(double value, int digits = 2) {
    std::wostringstream s;
    s << std::fixed << std::setprecision(digits) << value;
    return hstring(s.str());
}
void shortcut(Button b, winrt::Windows::System::VirtualKey key) {
    KeyboardAccelerator a;
    a.Key(key);
    a.Modifiers(winrt::Windows::System::VirtualKeyModifiers::Control);
    b.KeyboardAccelerators().Append(a);
}
std::wstring chooseFile(HWND owner, bool save, int kind = 0) {
    com_ptr<IFileDialog> dialog;
    if (save)
        check_hresult(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(dialog.put())));
    else
        check_hresult(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(dialog.put())));
    FILEOPENDIALOGOPTIONS options{};
    check_hresult(dialog->GetOptions(&options));
    check_hresult(
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST)));
    auto imagesName = app::locale::translate(L"Obrázky a vektorový graf");
    auto allName = app::locale::translate(L"Všechny soubory");
    COMDLG_FILTERSPEC image[]{
        {imagesName.c_str(), L"*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif;*.webp;*.jxr;*.cgr"},
        {allName.c_str(), L"*.*"}};
    auto compactName = app::locale::translate(L"Kompaktní graf CGR"),
         svgName = app::locale::translate(L"Vektor SVG"),
         graphName = app::locale::translate(L"Graf regionů JSON");
    COMDLG_FILTERSPEC compact[]{{compactName.c_str(), L"*.cgr"}};
    COMDLG_FILTERSPEC svg[]{{svgName.c_str(), L"*.svg"}}, graph[]{{graphName.c_str(), L"*.json"}};
    check_hresult(dialog->SetFileTypes(save ? 1 : 2, save ? (kind == 0   ? compact
                                                             : kind == 3 ? graph
                                                                         : svg)
                                                          : image));
    if (save) {
        dialog->SetDefaultExtension(kind == 0 ? L"cgr" : kind == 3 ? L"json" : L"svg");
        dialog->SetFileName(kind == 0   ? L"contours.cgr"
                            : kind == 3 ? L"regions.json"
                            : kind == 2 ? L"regions.svg"
                                        : L"edges.svg");
    }
    HRESULT shown = dialog->Show(owner);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    check_hresult(shown);
    com_ptr<IShellItem> item;
    check_hresult(dialog->GetResult(item.put()));
    PWSTR path{};
    check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH, &path));
    std::wstring result(path);
    CoTaskMemFree(path);
    return result;
}
std::wstring preferencesPath() {
    wchar_t local[32768]{};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768))
        return {};
    auto path = std::filesystem::path(local) / L"ContourStudio";
    std::filesystem::create_directories(path);
    return (path / L"settings.ini").wstring();
}
} // namespace

class App : public ApplicationT<App, IXamlMetadataProvider> {
  public:
    void OnLaunched(LaunchActivatedEventArgs const &) {
        Resources().MergedDictionaries().Append(XamlControlsResources());
        window_ = Window();
        window_.Title(L"Contour Studio · CPU · " + to_hstring(CONTOUR_VERSION));
        window_.AppWindow().Resize({1320, 880});
        auto native = window_.as<IWindowNative>();
        check_hresult(native->get_WindowHandle(&hwnd_));
        root_ = Grid();
        root_.RequestedTheme(ElementTheme::Dark);
        root_.Language(L"en-US");
        root_.Background(color(0x10131a));
        for (auto height : {GridLength{1, GridUnitType::Auto}, GridLength{1, GridUnitType::Star},
                            GridLength{1, GridUnitType::Auto}}) {
            RowDefinition row;
            row.Height(height);
            root_.RowDefinitions().Append(row);
        }
        Grid header;
        header.Padding({24, 18, 24, 18});
        ColumnDefinition titleColumn;
        titleColumn.Width({1, GridUnitType::Star});
        ColumnDefinition actionColumn;
        actionColumn.Width({1, GridUnitType::Auto});
        header.ColumnDefinitions().Append(titleColumn);
        header.ColumnDefinitions().Append(actionColumn);
        StackPanel titles;
        titles.Spacing(4);
        titles.Children().Append(text(L"Contour Studio", 26, 0xffffff));
        titles.Children().Append(text(L"Sdílené hranice · uzavřené regiony · pouze CPU", 12, 0x9eaabd));
        header.Children().Append(titles);
        StackPanel actions;
        actions.Orientation(Orientation::Horizontal);
        actions.Spacing(10);
        actions.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(actions, 1);
        auto open = button(L"Otevřít obrázek");
        open.Background(color(0x39e6b4));
        open.Foreground(color(0x081c16));
        shortcut(open, winrt::Windows::System::VirtualKey::O);
        open.Click([this](auto &&, auto &&) { openDialog(); });
        actions.Children().Append(open);
        export_ = button(L"Exportovat");
        shortcut(export_, winrt::Windows::System::VirtualKey::S);
        export_.IsEnabled(false);
        export_.Click([this](auto &&, auto &&) { exportDialog(); });
        actions.Children().Append(export_);
        header.Children().Append(actions);
        root_.Children().Append(header);
        Grid work;
        Grid::SetRow(work, 1);
        work.Margin({16, 0, 16, 0});
        ColumnDefinition canvasColumn;
        canvasColumn.Width({1, GridUnitType::Star});
        ColumnDefinition settingsColumn;
        settingsColumn.Width({300, GridUnitType::Pixel});
        work.ColumnDefinitions().Append(canvasColumn);
        work.ColumnDefinitions().Append(settingsColumn);
        Border canvasBorder;
        canvasBorder.CornerRadius({12, 12, 12, 12});
        canvasBorder.Background(color(0x151820));
        canvasBorder.BorderBrush(color(0x2b3240));
        canvasBorder.BorderThickness({1, 1, 1, 1});
        canvasBorder.Margin({0, 0, 14, 0});
        Grid canvasGrid;
        panel_ = SwapChainPanel();
        canvasGrid.Children().Append(panel_);
        empty_ = text(L"Otevři obrázek nebo ho sem přetáhni.\n\nKolečko: zoom   ·   Tažení: posun\nDvojklik: "
                      L"přizpůsobit oknu",
                      16, 0x9eaabd);
        empty_.HorizontalAlignment(HorizontalAlignment::Center);
        empty_.VerticalAlignment(VerticalAlignment::Center);
        empty_.IsHitTestVisible(false);
        canvasGrid.Children().Append(empty_);
        canvasBorder.Child(canvasGrid);
        work.Children().Append(canvasBorder);
        ScrollViewer scroll;
        settingsScroll_ = scroll;
        Grid::SetColumn(scroll, 1);
        scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        StackPanel settings;
        settings.Spacing(10);
        settings.Margin({4, 0, 10, 16});
        scroll.Content(settings);
        work.Children().Append(scroll);
        language_ = choices({L"English", L"Čeština"});
        app::locale::bindings.pop_back(); // Native language names do not change.
        settings.Children().Append(language_);
        uiMode_ = choices({L"Jednoduchý", L"Pokročilý"});
        settings.Children().Append(uiMode_);
        auto field = [&](const wchar_t *label, const wchar_t *help, bool advanced = false) {
            StackPanel group;
            group.Spacing(4);
            group.Margin({0, 12, 0, 0});
            Grid heading;
            ColumnDefinition titleColumn, helpColumn;
            titleColumn.Width({1, GridUnitType::Star});
            helpColumn.Width({1, GridUnitType::Auto});
            heading.ColumnDefinitions().Append(titleColumn);
            heading.ColumnDefinitions().Append(helpColumn);
            auto caption = text(label, 13, 0xdde3ee);
            caption.VerticalAlignment(VerticalAlignment::Center);
            heading.Children().Append(caption);
            auto question = button(L"?");
            question.Width(28);
            question.Height(28);
            question.Padding({0, 0, 0, 0});
            question.CornerRadius({14, 14, 14, 14});
            question.Background(color(0x242c38));
            std::wstring helpOriginal(help);
            app::locale::bindings.push_back([question, helpOriginal] {
                ToolTipService::SetToolTip(question, box_value(app::locale::translate(helpOriginal)));
            });
            ToolTipService::SetToolTip(question, box_value(app::locale::translate(help)));
            auto flyout = Flyout();
            auto helpText = text(help, 13);
            helpText.MaxWidth(280);
            flyout.Content(helpText);
            question.Flyout(flyout);
            Grid::SetColumn(question, 1);
            heading.Children().Append(question);
            group.Children().Append(heading);
            settings.Children().Append(group);
            if (advanced)
                advancedFields_.push_back(group);
            return group;
        };
        auto display =
            field(L"Zobrazení", L"Zvolí originál, překryv hran, samotné hrany nebo výplně regionů.");
        view_ = choices({L"Originál + hrany", L"Originál", L"Pouze hrany", L"Vyplněné regiony"});
        view_.SelectionChanged([this](auto &&, auto &&) {
            if (updatingUi_)
                return;
            if (result_ && result_->mode == contour::Settings::Mode::Edges && view_.SelectedIndex() == 3) {
                view_.SelectedIndex(2);
                return;
            }
            if (graphMode_ && view_.SelectedIndex() == 1) {
                view_.SelectedIndex(2);
                setText(status_, L"Původní obrázek není součástí CGR grafu.");
                return;
            }
            renderer_.mode = app::ViewMode(std::max(0, view_.SelectedIndex()));
            draw();
        });
        display.Children().Append(view_);
        StackPanel zoomActions;
        zoomActions.Orientation(Orientation::Horizontal);
        zoomActions.Spacing(8);
        auto fit = button(L"Přizpůsobit");
        fit.Click([this](auto &&, auto &&) { renderer_.fit(); });
        auto one = button(L"Detail 4×");
        one.Click([this](auto &&, auto &&) {
            renderer_.fit();
            renderer_.zoomAt(4, float(panel_.ActualWidth() / 2), float(panel_.ActualHeight() / 2));
        });
        zoomActions.Children().Append(fit);
        zoomActions.Children().Append(one);
        display.Children().Append(zoomActions);
        auto analysis = field(
            L"Způsob analýzy",
            L"Režim 1 vytváří uzavřené regiony pro výplně. Režim 2 hledá ostré hrany bez regionů.", true);
        analysisMode_ = choices({L"1 · Uzavřené regiony", L"2 · Pouze ostré barevné hrany"});
        analysisMode_.SelectedIndex(1);
        analysis.Children().Append(analysisMode_);
        auto slider = [](double minimum, double maximum, double step, double value) {
            Slider control;
            control.Minimum(minimum);
            control.Maximum(maximum);
            control.StepFrequency(step);
            control.Value(value);
            return control;
        };
        auto sensitivity = field(L"Citlivost a detail",
                                 L"Vyšší hodnota zachytí slabší hrany a více detailů, ale také textury.");
        detail_ = slider(0, 100, 1, 0);
        sensitivity.Children().Append(detail_);
        detailLabel_ = text(L"0", 12, 0x9eaabd);
        sensitivity.Children().Append(detailLabel_);
        auto multiplier = field(L"Celková citlivost · násobič",
                                L"Násobí citlivost obou složek. 100 % je běžná citlivost; rozsah 1–200 %.");
        contrastMultiplier_ = slider(1, 200, 1, 100);
        multiplier.Children().Append(contrastMultiplier_);
        multiplierLabel_ = text(L"100 %", 12, 0x9eaabd);
        multiplier.Children().Append(multiplierLabel_);
        auto chroma = field(L"Citlivost na barvu",
                            L"Síla barevných přechodů nezávisle na jasu. Nula složku vypne.", true);
        colorSensitivity_ = slider(0, 100, 1, 89);
        chroma.Children().Append(colorSensitivity_);
        colorLabel_ = text(L"89 %", 12, 0x9eaabd);
        chroma.Children().Append(colorLabel_);
        auto brightness = field(L"Citlivost na jas",
                                L"Síla světlých a tmavých přechodů. Snížení potlačí stíny, ale může "
                                L"odstranit bílé či šedé písmo.",
                                true);
        brightnessSensitivity_ = slider(0, 100, 1, 80);
        brightness.Children().Append(brightnessSensitivity_);
        brightnessLabel_ = text(L"80 %", 12, 0x9eaabd);
        brightness.Children().Append(brightnessLabel_);
        denoise_ = ToggleSwitch();
        denoise_.IsOn(false);
        auto geometry = field(L"Geometrie", L"Tolerance zjednodušení v pixelech analýzy. Vyšší hodnoty šetří "
                                            L"data; malé kontrastní obrysy se chrání.");
        tolerance_ = slider(0, 2, .05, .7);
        geometry.Children().Append(tolerance_);
        geometryLabel_ = text(L"0.70 px", 12, 0x9eaabd);
        geometry.Children().Append(geometryLabel_);
        auto joining =
            field(L"Spojovat blízké konce hran",
                  L"Spojuje pouze blízké souhlasné konce s barevným důkazem. Platí pro režim 2.", true);
        connectGaps_ = ToggleSwitch();
        connectGaps_.IsOn(false);
        app::locale::bindings.push_back([control = connectGaps_] {
            control.OnContent(box_value(app::locale::czech ? L"Zapnuto" : L"On"));
            control.OffContent(box_value(app::locale::czech ? L"Vypnuto" : L"Off"));
        });
        connectGaps_.OnContent(box_value(L"On"));
        connectGaps_.OffContent(box_value(L"Off"));
        joining.Children().Append(connectGaps_);
        auto budget = field(
            L"Budget bodů na jednu čáru",
            L"Maximum úseků volné čáry. Konce a spojení zůstávají; primitiva mají vlastní kompaktní zápis.",
            true);
        pointBudget_ = slider(4, 256, 1, 60);
        budget.Children().Append(pointBudget_);
        budgetLabel_ = text(L"60", 12, 0x9eaabd);
        budget.Children().Append(budgetLabel_);
        auto length =
            field(L"Minimální délka / obvod",
                  L"Odstraní čáry kratší než tato délka. Uzavřené čáry používají obvod. Nula vypne filtr.");
        minLength_ = slider(0, 64, 1, 3);
        length.Children().Append(minLength_);
        lengthLabel_ = text(L"3 px", 12, 0x9eaabd);
        length.Children().Append(lengthLabel_);
        auto resolution = field(L"Rozlišení analýzy",
                                L"Delší strana analýzy, bez zvětšování zdroje. Vlastní rozsah 64–8192 px.");
        resolution_ = choices({L"512 px", L"768 px", L"1024 px", L"1536 px", L"2048 px", L"3072 px",
                               L"4096 px", L"8192 px", L"Vlastní…"});
        resolution_.SelectedIndex(4);
        resolution.Children().Append(resolution_);
        customResolution_ = NumberBox();
        customResolution_.Minimum(64);
        customResolution_.Maximum(8192);
        customResolution_.Value(2048);
        customResolution_.SmallChange(64);
        customResolution_.LargeChange(256);
        customResolution_.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact);
        customResolution_.Visibility(Visibility::Collapsed);
        resolution.Children().Append(customResolution_);
        auto opacity = field(L"Krytí", L"Krytí obrysů pouze při zobrazení.", true);
        opacity_ = slider(0, 1, .05, 1);
        opacity.Children().Append(opacity_);
        auto width = field(L"Tloušťka čáry", L"Tloušťka vykreslení; nemění uloženou geometrii.", true);
        lineWidth_ = slider(.5, 3, .25, 1);
        width.Children().Append(lineWidth_);
        auto outline =
            field(L"Barva obrysů", L"Barva obrysů pouze při zobrazení, uložená jako preference.", true);
        outlinePicker_ = ColorPicker();
        outlinePicker_.IsAlphaEnabled(false);
        outlinePicker_.Color({255, 57, 230, 180});
        auto colorFlyout = Flyout();
        colorFlyout.Content(outlinePicker_);
        auto colorButton = button(L"Vybrat barvu");
        colorButton.Flyout(colorFlyout);
        outline.Children().Append(colorButton);
        outlinePicker_.ColorChanged([this, colorButton](auto &&, auto &&) {
            auto value = outlinePicker_.Color();
            renderer_.outline = {value.B, value.G, value.R, 255};
            colorButton.BorderBrush(SolidColorBrush(value));
            colorButton.BorderThickness({2, 2, 2, 2});
            draw();
        });
        auto format = field(
            L"Export",
            L"CGR uchovává kompaktní geometrii bez originálního obrázku. SVG a JSON jsou výměnné formáty.",
            true);
        exportKind_ = choices({L"CGR · kompaktní graf", L"SVG · společné hrany", L"SVG · vyplněné regiony",
                               L"JSON · graf pro další práci"});
        format.Children().Append(exportKind_);
        auto statistics = field(
            L"Naměřené časy",
            L"Skutečná velikost aktuálního CGR a naměřené časy. Časy načtení se zobrazí při otevření CGR.");
        stats_ = text(L"Zatím bez obrázku.", 12, 0x9eaabd);
        statistics.Children().Append(stats_);
        applyUiMode();
        progress_ = ProgressRing();
        progress_.Width(24);
        progress_.Height(24);
        progress_.Visibility(Visibility::Collapsed);
        settings.Children().Append(progress_);
        cancel_ = button(L"Zrušit zpracování");
        cancel_.Visibility(Visibility::Collapsed);
        cancel_.Click([this](auto &&, auto &&) {
            ++generation_;
            debounce_.Stop();
            busy(false);
            setText(status_, L"Zpracování zrušeno. Předchozí výsledek zůstává.");
        });
        settings.Children().Append(cancel_);
        root_.Children().Append(work);
        status_ =
            text(L"Připraveno · PNG, JPEG, BMP, TIFF a další formáty podle systémových kodeků", 12, 0x9eaabd);
        Grid footer;
        footer.Margin({24, 12, 24, 12});
        ColumnDefinition statusColumn, statsColumn;
        statusColumn.Width({1, GridUnitType::Star});
        statsColumn.Width({1, GridUnitType::Auto});
        footer.ColumnDefinitions().Append(statusColumn);
        footer.ColumnDefinitions().Append(statsColumn);
        status_.VerticalAlignment(VerticalAlignment::Center);
        footer.Children().Append(status_);
        fileStats_ = text(L"", 12, 0xdde3ee);
        fileStats_.MaxWidth(620);
        fileStats_.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(fileStats_, 1);
        footer.Children().Append(fileStats_);
        Grid::SetRow(footer, 2);
        root_.Children().Append(footer);
        window_.Content(root_);
        renderer_.initialize(panel_.as<::IUnknown>().get());
        panel_.SizeChanged([this](auto &&, auto &&) {
            try {
                float dpi = panel_.XamlRoot() ? float(panel_.XamlRoot().RasterizationScale()) : 1;
                renderer_.resize(float(panel_.ActualWidth()), float(panel_.ActualHeight()), dpi);
            } catch (const std::exception &e) {
                report(e.what());
            }
        });
        panel_.PointerWheelChanged([this](auto &&, PointerRoutedEventArgs const &e) {
            auto p = e.GetCurrentPoint(panel_);
            auto pos = p.Position();
            renderer_.zoomAt(std::pow(1.15f, float(p.Properties().MouseWheelDelta()) / 120), pos.X, pos.Y);
            e.Handled(true);
        });
        panel_.PointerPressed([this](auto &&, PointerRoutedEventArgs const &e) {
            auto p = e.GetCurrentPoint(panel_);
            if (p.Properties().IsLeftButtonPressed()) {
                dragging_ = true;
                lastPointer_ = p.Position();
                panel_.CapturePointer(e.Pointer());
                auto id = renderer_.regionAt(lastPointer_.X, lastPointer_.Y);
                if (result_ && id != UINT32_MAX) {
                    const auto &r = result_->regions[id];
                    setText(status_, L"Region " + to_hstring(id) + L" · plocha " + to_hstring(r.area) +
                                         L" px · " +
                                         (r.primitive.kind == contour::PrimitiveKind::Rectangle ? L"obdélník"
                                          : r.primitive.kind == contour::PrimitiveKind::RoundedRectangle
                                              ? L"zaoblený obdélník, radius " + to_hstring(r.primitive.radius)
                                              : L"polygon / oblouky"));
                }
            }
        });
        panel_.PointerMoved([this](auto &&, PointerRoutedEventArgs const &e) {
            if (!dragging_)
                return;
            auto p = e.GetCurrentPoint(panel_).Position();
            renderer_.pan(p.X - lastPointer_.X, p.Y - lastPointer_.Y);
            lastPointer_ = p;
        });
        panel_.PointerReleased([this](auto &&, auto &&) {
            dragging_ = false;
            panel_.ReleasePointerCaptures();
        });
        panel_.PointerCaptureLost([this](auto &&, auto &&) { dragging_ = false; });
        panel_.DoubleTapped([this](auto &&, auto &&) { renderer_.fit(); });
        panel_.AllowDrop(true);
        panel_.DragOver([](auto &&, DragEventArgs const &e) {
            e.AcceptedOperation(winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
        });
        panel_.Drop([this](auto &&, DragEventArgs const &e) { drop(e); });
        debounce_ = DispatcherTimer();
        debounce_.Interval(std::chrono::milliseconds(160));
        debounce_.Tick([this](auto &&, auto &&) {
            debounce_.Stop();
            analyze();
        });
        if (qaOutput.empty())
            loadPreferences();
        language_.SelectionChanged([this](auto &&, auto &&) {
            if (updatingUi_)
                return;
            updatingUi_ = true;
            app::locale::czech = language_.SelectedIndex() == 1;
            root_.Language(app::locale::czech ? L"cs-CZ" : L"en-US");
            app::locale::refresh();
            app::locale::refreshLabels();
            updatingUi_ = false;
        });
        uiMode_.SelectionChanged([this](auto &&, auto &&) {
            if (updatingUi_)
                return;
            applyUiMode();
            if (uiMode_.SelectedIndex() == 0)
                exportKind_.SelectedIndex(0);
        });
        setText(detailLabel_, to_hstring(int(detail_.Value())));
        setText(geometryLabel_, number(tolerance_.Value(), 2) + L" px");
        detail_.ValueChanged([this](auto &&, auto &&) {
            setText(detailLabel_, to_hstring(int(detail_.Value())));
            changed();
        });
        tolerance_.ValueChanged([this](auto &&, auto &&) {
            setText(geometryLabel_, number(tolerance_.Value(), 2) + L" px");
            changed();
        });
        contrastMultiplier_.ValueChanged([this](auto &&, auto &&) {
            setText(multiplierLabel_, to_hstring(int(contrastMultiplier_.Value())) + L" %");
            changed();
        });
        colorSensitivity_.ValueChanged([this](auto &&, auto &&) {
            setText(colorLabel_, to_hstring(int(colorSensitivity_.Value())) + L" %");
            changed();
        });
        brightnessSensitivity_.ValueChanged([this](auto &&, auto &&) {
            setText(brightnessLabel_, to_hstring(int(brightnessSensitivity_.Value())) + L" %");
            changed();
        });
        denoise_.Toggled([this](auto &&, auto &&) { changed(); });
        analysisMode_.SelectionChanged([this](auto &&, auto &&) {
            if (updatingUi_)
                return;
            rememberMode();
            activeMode_ = std::clamp(analysisMode_.SelectedIndex(), 0, 1);
            applyMode();
            pointBudget_.IsEnabled(!graphMode_ && analysisMode_.SelectedIndex() == 1);
            minLength_.IsEnabled(!graphMode_);
            if (analysisMode_.SelectedIndex() == 1 && view_.SelectedIndex() == 3)
                view_.SelectedIndex(0);
            changed();
        });
        pointBudget_.ValueChanged([this](auto &&, auto &&) {
            setText(budgetLabel_, to_hstring(int(pointBudget_.Value())) + L" úseků");
            changed();
        });
        minLength_.ValueChanged([this](auto &&, auto &&) {
            setText(lengthLabel_, to_hstring(int(minLength_.Value())) + L" px");
            changed();
        });
        connectGaps_.Toggled([this](auto &&, auto &&) { changed(); });
        customResolution_.ValueChanged([this](auto &&, auto &&) {
            if (resolution_.SelectedIndex() == 8)
                changed();
        });
        resolution_.SelectionChanged([this](auto &&, auto &&) {
            customResolution_.Visibility(resolution_.SelectedIndex() == 8 ? Visibility::Visible
                                                                          : Visibility::Collapsed);
            changed();
        });
        opacity_.ValueChanged([this](auto &&, auto &&) {
            renderer_.opacity = float(opacity_.Value());
            draw();
        });
        lineWidth_.ValueChanged([this](auto &&, auto &&) {
            renderer_.lineWidth = float(lineWidth_.Value());
            draw();
        });
        window_.Closed([this](auto &&, auto &&) {
            closed_ = true;
            ++generation_;
            debounce_.Stop();
            savePreferences();
        });
        window_.Activate();
        int argc{};
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!qaOutput.empty()) {
            window_.AppWindow().Move({-16000, 0});
            uiCapture();
        } else if (argv && argc > 1)
            load(argv[1]);
        if (argv)
            LocalFree(argv);
    }
    IXamlType GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const &type) {
        return provider_.GetXamlType(type);
    }
    IXamlType GetXamlType(hstring const &name) { return provider_.GetXamlType(name); }
    com_array<XmlnsDefinition> GetXmlnsDefinitions() { return provider_.GetXmlnsDefinitions(); }

  private:
    fire_and_forget uiCapture() {
        auto strong = get_strong();
        apartment_context ui;
        try {
            std::filesystem::create_directories(qaOutput);
            load(qaImage);
            for (int i = 0; i < 400 && !result_; ++i) {
                co_await resume_after(std::chrono::milliseconds(50));
                co_await ui;
            }
            if (!result_ || !compact_)
                throw std::runtime_error("UI capture analysis failed");
            auto graphPath = (std::filesystem::path(qaOutput) / L"ui-result.cgr").wstring();
            contour::saveCompactBytes(*compact_, graphPath);
            std::wostringstream state;
            state << L"detail=" << detail_.Value() << L" color=" << colorSensitivity_.Value()
                  << L" brightness=" << brightnessSensitivity_.Value() << L" multiplier="
                  << contrastMultiplier_.Value() << L" tolerance=" << tolerance_.Value() << L" budget="
                  << pointBudget_.Value() << L" minLength=" << minLength_.Value() << L" analysis="
                  << analysisPixels() << L" connect=" << connectGaps_.IsOn() << L" bytes="
                  << compact_->size();
            for (int lang = 0; lang < 2; ++lang)
                for (int advanced = 0; advanced < 2; ++advanced) {
                    language_.SelectedIndex(lang);
                    uiMode_.SelectedIndex(advanced);
                    root_.UpdateLayout();
                    co_await resume_after(std::chrono::milliseconds(200));
                    co_await ui;
                    co_await snapshot(
                        (std::filesystem::path(qaOutput) /
                         (std::wstring(lang ? L"cs-" : L"en-") + (advanced ? L"advanced.png" : L"easy.png")))
                            .wstring());
                    if (advanced) {
                        settingsScroll_.ChangeView(
                            nullptr, box_value(settingsScroll_.ScrollableHeight()).as<IReference<double>>(),
                            nullptr, true);
                        co_await resume_after(std::chrono::milliseconds(150));
                        co_await ui;
                        co_await snapshot((std::filesystem::path(qaOutput) /
                                           (lang ? L"cs-advanced-bottom.png" : L"en-advanced-bottom.png"))
                                              .wstring());
                        settingsScroll_.ChangeView(nullptr, box_value(0.0).as<IReference<double>>(), nullptr,
                                                   true);
                    }
                }
            auto normalOutline = outlinePicker_.Color();
            outlinePicker_.Color({255, 255, 40, 80});
            co_await snapshot((std::filesystem::path(qaOutput) / L"custom-outline.png").wstring());
            outlinePicker_.Color(normalOutline);
            language_.SelectedIndex(0);
            uiMode_.SelectedIndex(0);
            auto cgrToken = generation_.load();
            load(graphPath);
            for (int i = 0; i < 400 && (!graphMode_ || generation_.load() == cgrToken); ++i) {
                co_await resume_after(std::chrono::milliseconds(50));
                co_await ui;
            }
            if (!graphMode_)
                throw std::runtime_error("UI capture CGR load failed");
            co_await resume_after(std::chrono::milliseconds(250));
            co_await ui;
            co_await snapshot((std::filesystem::path(qaOutput) / L"en-cgr.png").wstring());
            state << L"\n" << stats_.Text().c_str();
            std::ofstream log(std::filesystem::path(qaOutput) / L"ui-state.txt");
            log << to_string(hstring(state.str()));
        } catch (const std::exception &e) {
            std::ofstream log(std::filesystem::path(qaOutput) / L"ui-error.txt");
            log << e.what();
        } catch (const hresult_error &e) {
            std::ofstream log(std::filesystem::path(qaOutput) / L"ui-error.txt");
            log << to_string(e.message());
        }
        co_await ui;
        window_.Close();
    }
    IAsyncAction snapshot(std::wstring path) {
        winrt::Microsoft::UI::Xaml::Media::Imaging::RenderTargetBitmap bitmap;
        co_await bitmap.RenderAsync(root_);
        auto buffer = co_await bitmap.GetPixelsAsync();
        auto access = buffer.as<::Windows::Storage::Streams::IBufferByteAccess>();
        byte *data{};
        check_hresult(access->Buffer(&data));
        contour::Image pixels;
        pixels.width = bitmap.PixelWidth();
        pixels.height = bitmap.PixelHeight();
        pixels.pixels.resize(size_t(pixels.width) * pixels.height);
        std::memcpy(pixels.pixels.data(), data, pixels.pixels.size() * sizeof(contour::Pixel));
        auto pos = panel_.TransformToVisual(root_).TransformPoint({0, 0});
        float sx = pixels.width / float(root_.ActualWidth()),
              sy = pixels.height / float(root_.ActualHeight());
        int left = int(std::round(pos.X * sx)), top = int(std::round(pos.Y * sy));
        int w = int(std::round(panel_.ActualWidth() * sx)), h = int(std::round(panel_.ActualHeight() * sy));
        const auto &frame = renderer_.frame();
        if (!frame.pixels.empty())
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    int dx = left + x, dy = top + y;
                    if (dx >= 0 && dy >= 0 && dx < int(pixels.width) && dy < int(pixels.height))
                        pixels.pixels[size_t(dy) * pixels.width + dx] =
                            frame.pixels[size_t(std::min(frame.height - 1, uint32_t(y) * frame.height / h)) *
                                             frame.width +
                                         std::min(frame.width - 1, uint32_t(x) * frame.width / w)];
                }
        app::savePng(pixels, path);
    }
    ScrollViewer settingsScroll_{nullptr};
    bool updatingUi_{};
    ComboBox language_{nullptr}, uiMode_{nullptr};
    ColorPicker outlinePicker_{nullptr};
    std::vector<UIElement> advancedFields_;
    std::shared_ptr<const std::vector<uint8_t>> compact_;
    void applyUiMode() {
        for (auto field : advancedFields_)
            field.Visibility(uiMode_.SelectedIndex() == 1 ? Visibility::Visible : Visibility::Collapsed);
    }
    Window window_{nullptr};
    Grid root_{nullptr};
    SwapChainPanel panel_{nullptr};
    TextBlock empty_{nullptr}, status_{nullptr}, stats_{nullptr}, fileStats_{nullptr}, detailLabel_{nullptr},
        geometryLabel_{nullptr}, budgetLabel_{nullptr}, lengthLabel_{nullptr};
    Button export_{nullptr}, cancel_{nullptr};
    Slider detail_{nullptr}, tolerance_{nullptr}, opacity_{nullptr}, lineWidth_{nullptr},
        pointBudget_{nullptr}, minLength_{nullptr}, colorSensitivity_{nullptr},
        brightnessSensitivity_{nullptr}, contrastMultiplier_{nullptr};
    ToggleSwitch denoise_{nullptr};
    ComboBox view_{nullptr}, resolution_{nullptr}, exportKind_{nullptr}, analysisMode_{nullptr};
    ProgressRing progress_{nullptr};
    DispatcherTimer debounce_{nullptr};
    winrt::Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider provider_;
    app::Renderer renderer_;
    HWND hwnd_{};
    std::atomic<uint64_t> generation_{};
    bool closed_{}, dragging_{}, graphMode_{};
    Point lastPointer_{};
    std::shared_ptr<const contour::Image> image_;
    std::shared_ptr<const contour::Result> result_;
    std::wstring path_, preferences_;
    NumberBox customResolution_{nullptr};
    ToggleSwitch connectGaps_{nullptr};
    TextBlock colorLabel_{nullptr}, brightnessLabel_{nullptr}, multiplierLabel_{nullptr};
    struct ModeValues {
        double detail, tolerance, points, color, brightness, multiplier;
        int resolution;
        bool connect;
    };
    ModeValues modeValues_[2]{{44, 1.55, 16, 100, 100, 100, 1024, false},
                              {0, .7, 60, 89, 80, 100, 2048, false}};
    int activeMode_{1};
    void rememberMode() {
        modeValues_[activeMode_] = {detail_.Value(),
                                    tolerance_.Value(),
                                    pointBudget_.Value(),
                                    colorSensitivity_.Value(),
                                    brightnessSensitivity_.Value(),
                                    contrastMultiplier_.Value(),
                                    int(analysisPixels()),
                                    connectGaps_.IsOn()};
    }
    void applyMode() {
        const auto &v = modeValues_[activeMode_];
        detail_.Value(v.detail);
        tolerance_.Value(v.tolerance);
        pointBudget_.Value(v.points);
        colorSensitivity_.Value(v.color);
        brightnessSensitivity_.Value(v.brightness);
        setText(colorLabel_, to_hstring(int(v.color)) + L" %");
        setText(brightnessLabel_, to_hstring(int(v.brightness)) + L" %");
        contrastMultiplier_.Value(v.multiplier);
        setText(multiplierLabel_, to_hstring(int(v.multiplier)) + L" %");
        setAnalysisPixels(v.resolution);
        connectGaps_.IsOn(v.connect);
        connectGaps_.IsEnabled(activeMode_ == 1 && !graphMode_);
    }
    static constexpr uint32_t resolutions_[8]{512, 768, 1024, 1536, 2048, 3072, 4096, 8192};
    uint32_t analysisPixels() const {
        int index = resolution_.SelectedIndex();
        if (index >= 0 && index < 8)
            return resolutions_[index];
        auto value = customResolution_.Value();
        return std::isfinite(value) ? uint32_t(std::clamp(std::lround(value), 64l, 8192l)) : 2048;
    }
    void setAnalysisPixels(int value) {
        value = std::clamp(value, 64, 8192);
        customResolution_.Value(value);
        int index = 8;
        for (int i = 0; i < 8; ++i)
            if (resolutions_[i] == uint32_t(value))
                index = i;
        resolution_.SelectedIndex(index);
        customResolution_.Visibility(index == 8 ? Visibility::Visible : Visibility::Collapsed);
    }
    contour::Settings configuration() const {
        contour::Settings s;
        s.detail = float(detail_.Value());
        s.denoise = false;
        s.geometryTolerance = float(tolerance_.Value());
        s.analysisLongSide = analysisPixels();
        s.connectGaps = connectGaps_.IsOn();
        s.mode = analysisMode_.SelectedIndex() == 1 ? contour::Settings::Mode::Edges
                                                    : contour::Settings::Mode::Regions;
        s.pointBudget = uint32_t(pointBudget_.Value());
        s.contrastMultiplier = float(contrastMultiplier_.Value() / 100);
        s.colorSensitivity = float(colorSensitivity_.Value() / 100);
        s.brightnessSensitivity = float(brightnessSensitivity_.Value() / 100);
        s.minContourLength = float(minLength_.Value());
        return s;
    }
    void draw() {
        try {
            renderer_.draw();
        } catch (const std::exception &e) {
            report(e.what());
        }
    }
    void report(const char *message) {
        if (!closed_) {
            status_.Foreground(color(0xffa990));
            setText(status_, L"Chyba: " + to_hstring(message));
        }
    }
    void busy(bool state) {
        progress_.IsActive(state);
        progress_.Visibility(state ? Visibility::Visible : Visibility::Collapsed);
        cancel_.Visibility(state ? Visibility::Visible : Visibility::Collapsed);
        export_.IsEnabled(!state && bool(result_));
    }
    void changed() {
        if (updatingUi_)
            return;
        ++generation_;
        if (!image_)
            return;
        export_.IsEnabled(false);
        setText(status_, L"Nastavení změněno · čekám na dokončení úpravy…");
        debounce_.Stop();
        debounce_.Start();
    }
    void openDialog() {
        try {
            auto path = chooseFile(hwnd_, false);
            if (!path.empty())
                load(std::move(path));
        } catch (const hresult_error &e) {
            report(to_string(e.message()).c_str());
        }
    }
    fire_and_forget drop(DragEventArgs args) {
        auto strong = get_strong();
        auto deferral = args.GetDeferral();
        try {
            auto items = co_await args.DataView().GetStorageItemsAsync();
            if (items.Size()) {
                auto file = items.GetAt(0).try_as<winrt::Windows::Storage::StorageFile>();
                if (file)
                    load(std::wstring(file.Path()));
            }
        } catch (const hresult_error &e) {
            report(to_string(e.message()).c_str());
        }
        deferral.Complete();
    }
    fire_and_forget load(std::wstring path) {
        auto strong = get_strong();
        auto token = ++generation_;
        auto loadStarted = std::chrono::steady_clock::now();
        debounce_.Stop();
        busy(true);
        status_.Foreground(color(0x9eaabd));
        setText(status_, L"Načítám obrázek…");
        apartment_context ui;
        co_await resume_background();
        std::shared_ptr<contour::Image> image;
        std::shared_ptr<contour::Result> graph;
        std::shared_ptr<const std::vector<uint8_t>> encoded;
        double readMs{}, decodeMs{}, prepareMs{};
        uintmax_t fileBytes{};
        auto graphScene = std::make_shared<contour::RasterScene>();
        std::string error;
        try {
            auto extension = std::filesystem::path(path).extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](wchar_t c) { return std::towlower(c); });
            if (extension == L".cgr") {
                graph = std::make_shared<contour::Result>(contour::loadCompact(path, &readMs, &decodeMs));
                fileBytes = std::filesystem::file_size(path);
                image = std::make_shared<contour::Image>();
                image->width = graph->width;
                image->height = graph->height;
                auto preparation = std::chrono::steady_clock::now();
                graphScene->prepare(image, graph);
                prepareMs =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - preparation)
                        .count();
                encoded = std::make_shared<const std::vector<uint8_t>>(contour::encodeCompact(*graph));
            } else
                image = std::make_shared<contour::Image>(app::loadImage(path));
        } catch (const std::exception &e) {
            error = e.what();
        }
        co_await ui;
        if (closed_ || token != generation_)
            co_return;
        if (!error.empty()) {
            busy(false);
            report(error.c_str());
            co_return;
        }
        try {
            image_ = std::move(image);
            path_ = std::move(path);
            result_.reset();
            compact_.reset();
            renderer_.setImage(image_);
            graphMode_ = bool(graph);
            detail_.IsEnabled(!graphMode_);
            denoise_.IsEnabled(!graphMode_);
            tolerance_.IsEnabled(!graphMode_);
            contrastMultiplier_.IsEnabled(!graphMode_);
            colorSensitivity_.IsEnabled(!graphMode_);
            brightnessSensitivity_.IsEnabled(!graphMode_);
            resolution_.IsEnabled(!graphMode_);
            customResolution_.IsEnabled(!graphMode_);
            connectGaps_.IsEnabled(!graphMode_ && analysisMode_.SelectedIndex() == 1);
            analysisMode_.IsEnabled(!graphMode_);
            pointBudget_.IsEnabled(!graphMode_ && analysisMode_.SelectedIndex() == 1);
            minLength_.IsEnabled(!graphMode_);
            empty_.Visibility(Visibility::Collapsed);
            window_.Title(L"Contour Studio · " + std::filesystem::path(path_).filename().wstring());
            if (graph) {
                result_ = std::move(graph);
                compact_ = std::move(encoded);
                renderer_.setResult(result_, graphScene);
                view_.SelectedIndex(result_->mode == contour::Settings::Mode::Edges ? 2 : 3);
                busy(false);
                export_.IsEnabled(true);
                std::wostringstream info;
                info << std::fixed << std::setprecision(2) << L"Velikost CGR: " << fileBytes << L" B\n"
                     << L"Čtení: " << readMs << L" ms\nDekódování: " << decodeMs << L" ms\nPříprava renderu: "
                     << prepareMs << L" ms\nPrvní render: " << renderer_.lastRenderMs
                     << L" ms\nČas do zobrazení: "
                     << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                  loadStarted)
                            .count()
                     << L" ms\n"
                     << (result_->mode == contour::Settings::Mode::Edges ? result_->strokes.size()
                                                                         : result_->regions.size())
                     << (result_->mode == contour::Settings::Mode::Edges ? L" čar" : L" regionů");
                setText(stats_, info.str());
                setText(fileStats_, L"CGR: " + to_hstring(fileBytes) + L" B · Dekódování: " +
                                        number(decodeMs) + L" ms · Příprava renderu: " + number(prepareMs) +
                                        L" ms · První render: " + number(renderer_.lastRenderMs) + L" ms");
                setText(status_, L"CGR načten · původní rastrový obrázek není součástí grafu");
            } else
                analyze();
        } catch (const std::exception &e) {
            busy(false);
            report(e.what());
        }
    }
    fire_and_forget analyze() {
        if (!image_ || closed_ || graphMode_)
            co_return;
        auto strong = get_strong();
        auto image = image_;
        auto settings = configuration();
        auto displayStarted = std::chrono::steady_clock::now();
        auto token = ++generation_;
        busy(true);
        setText(status_, settings.mode == contour::Settings::Mode::Edges
                             ? L"CPU: ostré RGB hrany a úsporná geometrie…"
                             : L"CPU: detekce, regiony a sdílená geometrie…");
        apartment_context ui;
        co_await resume_background();
        std::shared_ptr<contour::Result> result;
        auto scene = std::make_shared<contour::RasterScene>();
        double prepareMs{}, encodeMs{};
        std::shared_ptr<const std::vector<uint8_t>> encoded;
        std::string error;
        try {
            result = std::make_shared<contour::Result>(
                contour::process(*image, settings, [this, token] { return generation_.load() != token; }));
            auto preparation = std::chrono::steady_clock::now();
            scene->prepare(image, result);
            prepareMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - preparation)
                    .count();
            if (generation_.load() != token)
                co_return;
            auto encoding = std::chrono::steady_clock::now();
            encoded = std::make_shared<const std::vector<uint8_t>>(contour::encodeCompact(*result));
            encodeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - encoding)
                           .count();
        } catch (const std::exception &e) {
            error = e.what();
        }
        co_await ui;
        if (closed_ || token != generation_)
            co_return;
        busy(false);
        if (!error.empty()) {
            report(error.c_str());
            co_return;
        }
        try {
            result_ = std::move(result);
            compact_ = std::move(encoded);
            if (result_->mode == contour::Settings::Mode::Edges && view_.SelectedIndex() == 3)
                view_.SelectedIndex(0);
            renderer_.setResult(result_, scene);
            export_.IsEnabled(true);
            auto displayMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - displayStarted)
                    .count();
            const auto &t = result_->timings;
            uint64_t lines = 0, arcs = 0, rectangles = 0, rounded = 0;
            for (auto &b : result_->boundaries)
                for (auto &s : b.segments)
                    (s.radius ? arcs : lines)++;
            for (auto &r : result_->regions) {
                rectangles += r.primitive.kind == contour::PrimitiveKind::Rectangle;
                rounded += r.primitive.kind == contour::PrimitiveKind::RoundedRectangle;
            }
            for (auto &stroke : result_->strokes) {
                for (auto &segment : stroke.segments)
                    (segment.radius ? arcs : lines)++;
                rectangles += stroke.primitive.kind == contour::PrimitiveKind::Rectangle;
                rounded += stroke.primitive.kind == contour::PrimitiveKind::RoundedRectangle;
            }
            std::wostringstream info;
            info << std::fixed << std::setprecision(2) << L"Velikost CGR: " << compact_->size()
                 << L" B\nKódování: " << encodeMs << L" ms\nČas do zobrazení: " << displayMs
                 << L" ms\n\nDetekce: " << t.detectMs << L" ms"
                 << (t.detectMs < 5 ? L" · pod 5 ms" : L" · nad cílem") << L"\nFiltr: " << t.filterMs
                 << L" ms\nRegiony: " << t.regionsMs << L" ms\nObrysy: " << t.traceMs << L" ms\nGeometrie: "
                 << t.fitMs << L" ms\nZmenšení: " << t.resizeMs << L" ms\nCelkem: " << t.totalMs << L" ms\n\n"
                 << L"Raster příprava: " << prepareMs << L" ms · snímek: " << renderer_.lastRenderMs
                 << L" ms\n"
                 << (result_->mode == contour::Settings::Mode::Edges ? result_->strokes.size()
                                                                     : result_->regions.size())
                 << (result_->mode == contour::Settings::Mode::Edges ? L" čar\n" : L" regionů\n") << lines
                 << L" úseček · " << arcs << L" oblouků\n"
                 << rectangles << L" obdélníků · " << rounded << L" zaoblených\n\nAnalýza " << result_->width
                 << L" × " << result_->height << L"\nZdroj " << image_->width << L" × " << image_->height;
            setText(fileStats_, L"CGR: " + to_hstring(compact_->size()) + L" B · Kódování: " +
                                    number(encodeMs) + L" ms · Celkem: " + number(t.totalMs) +
                                    L" ms · render: " + number(renderer_.lastRenderMs) + L" ms");
            setText(stats_, info.str() + L"\nVyřazeno délkou: " + to_hstring(result_->discardedContours) +
                                L"\nSpojené mezery: " + to_hstring(result_->joinedGaps));
            status_.Foreground(color(0x9eaabd));
            setText(status_, result_->mode == contour::Settings::Mode::Edges
                                 ? L"Hotovo · kolečkem přibliž, tažením posuň"
                                 : L"Hotovo · kolečkem přibliž, tažením posuň · kliknutím zjistíš region");
        } catch (const std::exception &e) {
            report(e.what());
        }
    }
    fire_and_forget exportDialog() {
        auto strong = get_strong();
        if (!result_)
            co_return;
        std::wstring path;
        int kind = exportKind_.SelectedIndex();
        if (kind == 2 && result_->mode == contour::Settings::Mode::Edges) {
            kind = 1;
            exportKind_.SelectedIndex(1);
        }
        try {
            path = chooseFile(hwnd_, true, kind);
        } catch (const hresult_error &e) {
            report(to_string(e.message()).c_str());
            co_return;
        }
        if (path.empty())
            co_return;
        auto result = result_;
        auto compact = compact_;
        apartment_context ui;
        setText(status_, L"Ukládám export…");
        co_await resume_background();
        std::string error;
        const auto saveStarted = std::chrono::steady_clock::now();
        uintmax_t savedBytes{};
        try {
            if (kind == 0) {
                if (compact)
                    contour::saveCompactBytes(*compact, path);
                else
                    contour::saveCompact(*result, path);
            } else if (kind == 3)
                contour::saveGraph(*result, path);
            else
                contour::saveSvg(*result, path, kind == 2);
            savedBytes = std::filesystem::file_size(path);
        } catch (const std::exception &e) {
            error = e.what();
        }
        co_await ui;
        if (closed_)
            co_return;
        if (error.empty())
            setText(status_, L"Uloženo: " + path + L" · " + to_hstring(savedBytes) + L" B · " +
                                 to_hstring(std::chrono::duration<double, std::milli>(
                                                std::chrono::steady_clock::now() - saveStarted)
                                                .count()) +
                                 L" ms");
        else
            report(error.c_str());
    }
    void loadPreferences() {
        try {
            preferences_ = preferencesPath();
            if (preferences_.empty())
                return;
            language_.SelectedIndex(GetPrivateProfileIntW(L"ui", L"czech", 0, preferences_.c_str()) ? 1 : 0);
            app::locale::czech = language_.SelectedIndex() == 1;
            uiMode_.SelectedIndex(GetPrivateProfileIntW(L"ui", L"advanced", 0, preferences_.c_str()) ? 1 : 0);
            auto rgb = GetPrivateProfileIntW(L"ui", L"outline", 0x39e6b4, preferences_.c_str());
            outlinePicker_.Color({255, uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)});
            app::locale::refresh();
            app::locale::refreshLabels();
            applyUiMode();
            for (int i = 0; i < 2; ++i) {
                const auto section = i ? L"edgesV5" : L"regionsV5";
                auto &v = modeValues_[i];
                v.multiplier = std::clamp(
                    int(GetPrivateProfileIntW(section, L"multiplier", 100, preferences_.c_str())), 1, 200);
                v.color = std::clamp(
                    int(GetPrivateProfileIntW(section, L"color", int(v.color), preferences_.c_str())), 0,
                    100);
                v.brightness = std::clamp(int(GetPrivateProfileIntW(section, L"brightness", int(v.brightness),
                                                                    preferences_.c_str())),
                                          0, 100);
                v.resolution = std::clamp(int(GetPrivateProfileIntW(section, L"analysisPixels", v.resolution,
                                                                    preferences_.c_str())),
                                          64, 8192);
                v.connect = GetPrivateProfileIntW(section, L"connect", v.connect, preferences_.c_str()) != 0;
                v.detail = std::clamp(
                    int(GetPrivateProfileIntW(section, L"detail", int(v.detail), preferences_.c_str())), 0,
                    100);
                v.points = std::clamp(
                    int(GetPrivateProfileIntW(section, L"points", int(v.points), preferences_.c_str())), 4,
                    256);
                v.tolerance = std::clamp(int(GetPrivateProfileIntW(section, L"tolerance100",
                                                                   int(std::round(v.tolerance * 100)),
                                                                   preferences_.c_str())),
                                         0, 200) /
                              100.0;
            }
            activeMode_ =
                std::clamp(int(GetPrivateProfileIntW(L"analysisV5", L"mode", 1, preferences_.c_str())), 0, 1);
            analysisMode_.SelectedIndex(activeMode_);
            applyMode();
            pointBudget_.IsEnabled(activeMode_ == 1);
            minLength_.Value(std::clamp(
                int(GetPrivateProfileIntW(L"analysisV5", L"minLength", 3, preferences_.c_str())), 0, 64));
            setText(lengthLabel_, to_hstring(int(minLength_.Value())) + L" px");
            setText(budgetLabel_, to_hstring(int(pointBudget_.Value())) + L" úseků");
            denoise_.IsOn(false);

        } catch (...) {
            preferences_.clear();
        }
    }
    void savePreferences() {
        if (preferences_.empty())
            return;
        auto write = [&](const wchar_t *key, int value) {
            auto s = std::to_wstring(value);
            WritePrivateProfileStringW(L"analysisV5", key, s.c_str(), preferences_.c_str());
        };
        WritePrivateProfileStringW(L"ui", L"czech", app::locale::czech ? L"1" : L"0", preferences_.c_str());
        WritePrivateProfileStringW(L"ui", L"advanced", uiMode_.SelectedIndex() == 1 ? L"1" : L"0",
                                   preferences_.c_str());
        auto rgb = outlinePicker_.Color();
        auto rgbText = std::to_wstring((uint32_t(rgb.R) << 16) | (uint32_t(rgb.G) << 8) | rgb.B);
        WritePrivateProfileStringW(L"ui", L"outline", rgbText.c_str(), preferences_.c_str());
        rememberMode();
        for (int i = 0; i < 2; ++i) {
            const auto section = i ? L"edgesV5" : L"regionsV5";
            const auto &v = modeValues_[i];
            auto modeWrite = [&](const wchar_t *key, int value) {
                auto text = std::to_wstring(value);
                WritePrivateProfileStringW(section, key, text.c_str(), preferences_.c_str());
            };
            modeWrite(L"multiplier", int(v.multiplier));
            modeWrite(L"color", int(v.color));
            modeWrite(L"brightness", int(v.brightness));
            modeWrite(L"analysisPixels", v.resolution);
            modeWrite(L"connect", v.connect);
            modeWrite(L"detail", int(v.detail));
            modeWrite(L"points", int(v.points));
            modeWrite(L"tolerance100", int(std::round(v.tolerance * 100)));
        }
        write(L"mode", activeMode_);
        write(L"minLength", int(minLength_.Value()));
        write(L"resolution", resolution_.SelectedIndex());
    }
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc{};
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const int capture = argv ? app::captureCommand(argc, argv) : -1;
    if (argv && argc >= 4 && std::wstring_view(argv[1]) == L"--ui-qa") {
        qaOutput = argv[2];
        qaImage = argv[3];
    }
    if (argv)
        LocalFree(argv);
    if (capture >= 0)
        return capture;
    PACKAGE_VERSION minimum{};
    HRESULT initialized =
        MddBootstrapInitialize2(0x00010008, L"", minimum, MddBootstrapInitializeOptions_None);
    if (FAILED(initialized)) {
        MessageBoxW(nullptr,
                    L"Windows App Runtime 1.8 (x64) is missing.\nInstall it from "
                    L"https://learn.microsoft.com/windows/apps/windows-app-sdk/downloads\n\nThis app "
                    L"requires Windows 10 1809 or newer.",
                    L"Contour Studio · missing runtime", MB_OK | MB_ICONERROR);
        return 1;
    }
    int code = 0;
    try {
        init_apartment(apartment_type::single_threaded);
        Application::Start([](auto &&) { make<App>(); });
    } catch (const hresult_error &e) {
        MessageBoxW(nullptr, e.message().c_str(), L"Contour Studio", MB_OK | MB_ICONERROR);
        code = 2;
    } catch (const std::exception &e) {
        auto message = to_hstring(e.what());
        MessageBoxW(nullptr, message.c_str(), L"Contour Studio", MB_OK | MB_ICONERROR);
        code = 3;
    }
    MddBootstrapShutdown();
    return code;
}
