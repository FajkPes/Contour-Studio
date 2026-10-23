#include "core/Contour.h"
#include "core/Raster.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#ifdef _WIN32
#include "app/ImageIO.h"
#ifdef CONTOUR_HAS_PREVIEW
#include "app/Renderer.h"
#endif
#include <objbase.h>
#endif
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
    try {
        if (argc > 1 && std::filesystem::path(argv[1]).extension() == ".cgr") {
            auto originalBytes = std::filesystem::file_size(argv[1]);
            auto graph = contour::loadCompact(argv[1]);
            auto compact = contour::encodeCompact(graph);
            auto restored = contour::decodeCompact(compact);
            if (restored.strokes.size() != graph.strokes.size() ||
                restored.regions.size() != graph.regions.size())
                throw std::runtime_error("Codec verification failed");
            std::cout << "Existing CGR " << originalBytes << " bytes; CGR5 same geometry " << compact.size()
                      << " bytes; strokes " << restored.strokes.size() << "; regions "
                      << restored.regions.size() << "\n";
            if (argc > 2)
                contour::saveCompact(graph, argv[2]);
            return 0;
        }
        contour::Image image;
#ifdef _WIN32
        if (argc > 1)
            image = app::loadImage(argv[1]);
        else
#endif
        {
            image = {2048, 1152, std::vector<contour::Pixel>(2048 * 1152)};
            for (unsigned y = 0; y < image.height; ++y)
                for (unsigned x = 0; x < image.width; ++x) {
                    bool card = x % 256 > 20 && x % 256 < 230 && y % 128 > 15 && y % 128 < 110;
                    image.pixels[y * image.width + x] =
                        card ? contour::Pixel{180, 70, 30, 255} : contour::Pixel{22, 22, 22, 255};
                }
        }
        auto settings = contour::defaultsFor(argc > 5 && std::wstring(argv[5]) == L"edges"
                                                 ? contour::Settings::Mode::Edges
                                                 : contour::Settings::Mode::Regions);
        if (argc > 3)
            settings.detail = std::stof(argv[3]);
        if (argc > 4)
            settings.analysisLongSide = uint32_t(std::stoul(argv[4]));
        if (argc > 5 && std::wstring(argv[5]) == L"edges")
            settings.mode = contour::Settings::Mode::Edges;
        if (argc > 6)
            settings.pointBudget = uint32_t(std::stoul(argv[6]));
        if (argc > 7)
            settings.minContourLength = std::stof(argv[7]);
        if (argc > 8)
            settings.geometryTolerance = std::stof(argv[8]);
        if (argc > 9)
            settings.colorSensitivity = std::stof(argv[9]);
        if (argc > 10)
            settings.brightnessSensitivity = std::stof(argv[10]);
        if (argc > 11)
            settings.contrastMultiplier = std::stof(argv[11]);
        auto pipelineImage = std::make_shared<contour::Image>(image);
        std::vector<double> total, detect, pipeline;
        contour::Image pipelineFrame;
        contour::Result result;
        for (int i = 0; i < 9; ++i) {
            auto pipelineStart = std::chrono::steady_clock::now();
            auto live = std::make_shared<contour::Result>(contour::process(image, settings));
            contour::RasterScene liveScene;
            liveScene.prepare(pipelineImage, live);
            liveScene.render(pipelineFrame, 2048, 1152, 2048.f / live->width, 0, 0,
                             contour::RasterMode::Overlay, 1, 1);
            auto encoded = contour::encodeCompact(*live);
            if (i)
                pipeline.push_back(std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - pipelineStart)
                                       .count());
            result = std::move(*live);
            if (i) {
                total.push_back(result.timings.totalMs);
                detect.push_back(result.timings.detectMs);
            }
        }
        std::sort(total.begin(), total.end());
        std::sort(detect.begin(), detect.end());
        std::sort(pipeline.begin(), pipeline.end());
        std::cout << "Minimum contour length " << settings.minContourLength
                  << "; CPU pipeline median (process+prepare+2K raster+encode) "
                  << (pipeline[3] + pipeline[4]) / 2 << " ms; max " << pipeline.back() << " ms\n";
        auto &t = result.timings;
        std::cout << std::fixed << std::setprecision(3) << "Source " << image.width << 'x' << image.height
                  << " analysis " << result.width << 'x' << result.height << "\nRegions "
                  << result.regions.size() << " strokes " << result.strokes.size() << " exact vertices "
                  << result.vertices << "\nDetection median " << (detect[3] + detect[4]) / 2
                  << " ms; total median " << (total[3] + total[4]) / 2 << " ms; total max " << total.back()
                  << " ms\nLast run: resize " << t.resizeMs << ", filter " << t.filterMs << ", detect "
                  << t.detectMs << ", regions " << t.regionsMs << ", trace " << t.traceMs << ", fit "
                  << t.fitMs << " ms\n";
        std::cout << (settings.mode == contour::Settings::Mode::Edges ? "Detection parts: smooth "
                                                                      : "Region parts: buckets ")
                  << t.smoothMs
                  << (settings.mode == contour::Settings::Mode::Edges ? ", RGB gradient " : ", unions ")
                  << t.gradientMs
                  << (settings.mode == contour::Settings::Mode::Edges ? ", suppress " : ", palette ")
                  << t.suppressMs
                  << (settings.mode == contour::Settings::Mode::Edges ? ", graph " : ", seams ") << t.graphMs
                  << ", gaps " << t.linkMs << " ms\n";
        if (argc > 2)
            contour::saveSvg(result, argv[2], false);
        auto scene = contour::RasterScene();
        auto start = std::chrono::steady_clock::now();
        auto sharedImage = std::make_shared<contour::Image>(image);
        auto sharedResult = std::make_shared<contour::Result>(result);
        start = std::chrono::steady_clock::now();
        scene.prepare(sharedImage, sharedResult);
        auto prepared = std::chrono::steady_clock::now();
        contour::Image frame;
        for (int i = 0; i < 8; ++i)
            scene.render(frame, 2048, 1152, 2048.f / result.width, 0, 0, contour::RasterMode::Overlay, 1, 1);
        auto rendered = std::chrono::steady_clock::now();
        auto packed = contour::encodeCompact(result);
        auto encoded = std::chrono::steady_clock::now();
        auto restored = contour::decodeCompact(packed);
        auto decoded = std::chrono::steady_clock::now();
        auto elapsed = [](auto a, auto b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        std::cout << "Custom preparation " << elapsed(start, prepared) << " ms; render average "
                  << elapsed(prepared, rendered) / 8 << " ms; CGR " << packed.size() << " bytes; encode "
                  << elapsed(rendered, encoded) << " ms; decode " << elapsed(encoded, decoded) << " ms\n";
        if (argc > 2) {
            start = std::chrono::steady_clock::now();
            contour::saveCompact(result, std::wstring(argv[2]) + L".cgr");
            std::cout << "CGR encode+atomic save " << elapsed(start, std::chrono::steady_clock::now())
                      << " ms\n";
        }
#ifdef CONTOUR_HAS_PREVIEW
        if (argc > 2) {
            app::previewPng(image, result, std::wstring(argv[2]) + L".png");
            if (settings.mode == contour::Settings::Mode::Regions)
                app::previewPng(image, result, std::wstring(argv[2]) + L".regions.png",
                                app::ViewMode::Regions);
            contour::saveGraph(result, std::wstring(argv[2]) + L".json");
        }
#endif
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
