#include "Capture.h"
#include "ImageIO.h"
#include "core/Raster.h"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <objbase.h>
#include <stdexcept>

namespace app {
int captureCommand(int argc, wchar_t **argv) {
    if (argc < 2 || std::wstring(argv[1]) != L"--capture")
        return -1;
    if (argc < 4)
        return 2;
    const std::filesystem::path output(argv[3]);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    using Clock = std::chrono::steady_clock;
    auto elapsed = [](auto start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    };
    int code = 0;
    try {
        std::filesystem::create_directories(output);
        if (FAILED(com))
            throw std::runtime_error("COM initialization failed");
        auto initialMode = contour::Settings::Mode::Edges;
        for (int i = 4; i + 1 < argc; ++i)
            if (std::wstring(argv[i]) == L"--mode")
                initialMode = std::wstring(argv[i + 1]) == L"regions" ? contour::Settings::Mode::Regions
                                                                      : contour::Settings::Mode::Edges;
        auto settings = contour::defaultsFor(initialMode);
        uint32_t cropX = 0, cropY = 0, cropW = 0, cropH = 0;
        float zoom = 1;
        for (int i = 4; i < argc; ++i) {
            std::wstring option(argv[i]);
            if (option == L"--connect") { settings.connectGaps=true; continue; }
            if (option == L"--no-connect") {
                settings.connectGaps = false;
                continue;
            }
            if (option == L"--noise") {
                settings.denoise = true;
                continue;
            }
            if (option == L"--crop") {
                if (i + 4 >= argc)
                    throw std::runtime_error("Crop needs x y width height");
                cropX = std::stoul(argv[++i]);
                cropY = std::stoul(argv[++i]);
                cropW = std::stoul(argv[++i]);
                cropH = std::stoul(argv[++i]);
                continue;
            }
            if (++i >= argc)
                throw std::runtime_error("Missing capture option value");
            std::wstring value(argv[i]);
            if (option == L"--mode") {
                if (value != L"edges" && value != L"regions")
                    throw std::runtime_error("Unknown mode");
                settings.mode =
                    value == L"edges" ? contour::Settings::Mode::Edges : contour::Settings::Mode::Regions;
            } else if (option == L"--analysis")
                settings.analysisLongSide = std::stoul(value);
            else if (option == L"--points")
                settings.pointBudget = std::stoul(value);
            else if (option == L"--min-length")
                settings.minContourLength = std::stof(value);
            else if (option == L"--contrast-multiplier")
                settings.contrastMultiplier = std::stof(value);
            else if (option == L"--color-sensitivity")
                settings.colorSensitivity = std::stof(value);
            else if (option == L"--brightness-sensitivity")
                settings.brightnessSensitivity = std::stof(value);
            else if (option == L"--detail")
                settings.detail = std::stof(value);
            else if (option == L"--tolerance")
                settings.geometryTolerance = std::stof(value);
            else if (option == L"--zoom")
                zoom = std::stof(value);
            else
                throw std::runtime_error("Unknown capture option");
        }
        std::shared_ptr<contour::Image> source;
        auto result = std::make_shared<contour::Result>();
        auto extension = std::filesystem::path(argv[2]).extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](wchar_t c) { return std::towlower(c); });
        if (extension == L".cgr")
            *result = contour::loadCompact(argv[2]);
        else {
            source = std::make_shared<contour::Image>(loadImage(argv[2]));
            *result = contour::process(*source, settings);
        }
        if (result->mode == contour::Settings::Mode::Regions &&
            result->labels.size() == size_t(result->width) * result->height) {
            contour::Image segmentation{result->width, result->height,
                                        std::vector<contour::Pixel>(result->labels.size())};
            for (size_t i = 0; i < segmentation.pixels.size(); ++i)
                segmentation.pixels[i] = result->regions[result->labels[i]].color;
            savePng(segmentation, (output / L"segmentation.png").wstring());
        }
        contour::RasterScene scene;
        auto start = Clock::now();
        scene.prepare(source, result);
        const double prepare = elapsed(start);
        start = Clock::now();
        auto packed = contour::encodeCompact(*result);
        const double encode = elapsed(start);
        start = Clock::now();
        auto restored = contour::decodeCompact(packed);
        const double decode = elapsed(start);
        start = Clock::now();
        {
            std::ofstream file(output / L"result.cgr", std::ios::binary);
            file.write(reinterpret_cast<const char *>(packed.data()), packed.size());
            if (!file)
                throw std::runtime_error("Cannot save capture graph");
        }
        const double save = elapsed(start);
        std::ofstream report(output / L"metrics.txt");
        report << std::fixed << std::setprecision(3) << "version=" << CONTOUR_VERSION
               << "\nmode=" << (result->mode == contour::Settings::Mode::Edges ? "edges" : "regions")
               << "\nanalysis=" << result->width << 'x' << result->height
               << "\ncontrast_multiplier=" << settings.contrastMultiplier
               << "\ncolor_sensitivity=" << settings.colorSensitivity
               << "\nbrightness_sensitivity=" << settings.brightnessSensitivity
               << "\npoint_budget=" << settings.pointBudget
               << "\nmin_contour_length=" << settings.minContourLength
               << "\njoined_gaps=" << result->joinedGaps
               << "\ndiscarded_contours=" << result->discardedContours
               << "\ndetect_ms=" << result->timings.detectMs << "\ntrace_ms=" << result->timings.traceMs
               << "\nfit_ms=" << result->timings.fitMs << "\nfilter_ms=" << result->timings.filterMs
               << "\nresize_ms=" << result->timings.resizeMs << "\nprocess_ms=" << result->timings.totalMs
               << "\nprepare_ms=" << prepare << "\ncompact_bytes=" << packed.size()
               << "\nencode_ms=" << encode << "\ndecode_ms=" << decode << "\nsave_ms=" << save
               << "\nstrokes=" << result->strokes.size() << "\nregions=" << result->regions.size();
        size_t lines = 0, arcs = 0, rectangles = 0, rounded = 0;
        for (auto &stroke : result->strokes) {
            rectangles += stroke.primitive.kind == contour::PrimitiveKind::Rectangle;
            rounded += stroke.primitive.kind == contour::PrimitiveKind::RoundedRectangle;
            for (auto &segment : stroke.segments)
                (segment.radius ? arcs : lines)++;
        }
        report << "\nlines=" << lines << "\narcs=" << arcs << "\nrectangles=" << rectangles
               << "\nrounded=" << rounded << '\n';
        for (auto mode : {contour::RasterMode::Overlay, contour::RasterMode::Edges,
                          contour::RasterMode::Original, contour::RasterMode::Regions}) {
            if (mode == contour::RasterMode::Regions && result->mode == contour::Settings::Mode::Edges)
                continue;
            if (!source && (mode == contour::RasterMode::Original || mode == contour::RasterMode::Overlay))
                continue;
            contour::Image frame;
            start = Clock::now();
            scene.render(frame, result->width, result->height, 1, 0, 0, mode, 1, 1);
            const wchar_t *name = mode == contour::RasterMode::Overlay    ? L"overlay"
                                  : mode == contour::RasterMode::Edges    ? L"edges"
                                  : mode == contour::RasterMode::Original ? L"original"
                                                                          : L"regions";
            report << std::filesystem::path(name).string() << "_render_ms=" << elapsed(start) << '\n';
            savePng(frame, (output / (std::wstring(name) + L".png")).wstring());
        }
        // Measure the visual effect of 1/8-pixel quantization after reopening.
        contour::RasterScene reopened;
        reopened.prepare(source, std::make_shared<contour::Result>(std::move(restored)));
        contour::Image frame;
        reopened.render(frame, result->width, result->height, 1, 0, 0, contour::RasterMode::Overlay, 1, 1);
        savePng(frame, (output / L"reopened.png").wstring());
        contour::Image reference;
        scene.render(reference, result->width, result->height, 1, 0, 0, contour::RasterMode::Overlay, 1, 1);
        uint64_t difference = 0, changed = 0;
        int maximum = 0;
        for (size_t i = 0; i < frame.pixels.size(); ++i) {
            auto a = frame.pixels[i], b = reference.pixels[i];
            int d = std::max({std::abs(int(a.b) - b.b), std::abs(int(a.g) - b.g), std::abs(int(a.r) - b.r)});
            maximum = std::max(maximum, d);
            difference += d;
            changed += d != 0;
        }
        report << "reopen_mean_max_channel_error=" << double(difference) / frame.pixels.size()
               << "\nreopen_changed_percent=" << double(changed) * 100 / frame.pixels.size()
               << "\nreopen_max_channel_error=" << maximum << '\n';
        if (cropW && cropH) {
            if (!std::isfinite(zoom) || zoom < 1 || zoom > 16 || uint64_t(cropW) * zoom > 8192 ||
                uint64_t(cropH) * zoom > 8192)
                throw std::runtime_error("Invalid capture crop/zoom");
            for (auto mode : {contour::RasterMode::Overlay, contour::RasterMode::Edges}) {
                scene.render(frame, uint32_t(cropW * zoom), uint32_t(cropH * zoom), zoom,
                             -float(cropX) * zoom, -float(cropY) * zoom, mode, 1, 1);
                savePng(frame, (output /
                                (mode == contour::RasterMode::Overlay ? L"detail.png" : L"detail-edges.png"))
                                   .wstring());
            }
        }
        report.close();
        if (!report)
            throw std::runtime_error("Cannot save capture metrics");
    } catch (const std::exception &e) {
        code = 1;
        std::ofstream error(output / L"capture-error.txt");
        error << e.what();
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    return code;
}
} // namespace app
