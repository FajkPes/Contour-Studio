#include "core/Contour.h"
#include "core/Raster.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
contour::Image solid(uint32_t w, uint32_t h) {
    return {w, h, std::vector<contour::Pixel>(size_t(w) * h, {20, 20, 20, 255})};
}
void verifyCoverage(const contour::Result &r) {
    uint64_t area = 0;
    for (const auto &region : r.regions) {
        double rings = 0;
        require(!region.loops.empty(), "Region has no boundary");
        for (const auto &loop : region.loops) {
            require(loop.points.size() >= 4, "Invalid boundary");
            rings += loop.signedArea;
        }
        require(std::abs(rings - region.area) < 0.001, "Outer/holes area does not match component");
        area += region.area;
    }
    require(area == uint64_t(r.width) * r.height, "Incomplete coverage");
    for (auto label : r.labels)
        require(label < r.regions.size(), "Invalid region ID");
    std::vector<unsigned> uses(r.boundaries.size());
    for (const auto &region : r.regions)
        for (const auto &loop : region.loops) {
            require(!loop.segments.empty(), "Empty fitted loop");
            require(loop.start == loop.segments.back().end, "Fitted loop is not closed");
            for (auto ref : loop.boundaries) {
                require(ref.id < r.boundaries.size(), "Invalid shared boundary");
                const auto &b = r.boundaries[ref.id];
                require((ref.reversed ? b.left : b.right) == region.id, "Boundary orientation incorrect");
                ++uses[ref.id];
            }
        }
    for (size_t i = 0; i < uses.size(); ++i)
        require(uses[i] ==
                    ((r.boundaries[i].left == UINT32_MAX || r.boundaries[i].right == UINT32_MAX) ? 1u : 2u),
                "Boundary is missing or duplicated");
}
int main() {
    try {
        contour::Settings s;
        s.denoise = false;
        s.geometryTolerance = .75f;
        auto image = solid(64, 64);
        auto r = contour::process(image, s);
        require(r.regions.size() == 1, "Solid image fragmented");
        require(r.regions[0].primitive.kind == contour::PrimitiveKind::Rectangle, "Rectangle not recognized");
        verifyCoverage(r);
        for (unsigned y = 12; y < 50; ++y)
            for (unsigned x = 10; x < 52; ++x)
                image.pixels[y * 64 + x] = {200, 40, 20, 255};
        r = contour::process(image, s);
        require(r.regions.size() == 2, "Equal luminance color edge lost");
        verifyCoverage(r);
        bool rectangle = false, hole = false;
        for (auto &region : r.regions) {
            rectangle |= region.primitive.kind == contour::PrimitiveKind::Rectangle;
            hole |= region.loops.size() == 2;
        }
        require(rectangle && hole, "Rectangle or hole lost");
        require(contour::svg(r, true).find("fill-rule=\"evenodd\"") != std::string::npos,
                "SVG hole fill missing");
        image = solid(64, 64);
        image.pixels[20 * 64 + 20] = {255, 255, 255, 255};
        s.denoise = true;
        r = contour::process(image, s);
        require(r.regions.size() == 1, "Single pixel noise retained");
        s.denoise = false;
        image = solid(64, 64);
        for (auto &p : image.pixels)
            p = {0, 0, 0, 0};
        image.pixels[20 * 64 + 20] = {255, 0, 255, 0};
        r = contour::process(image, s);
        require(r.regions.size() == 1, "Invisible RGB treated as visible edge");
        // Touching diagonal objects must not corrupt winding or fill coverage.
        image = solid(64, 64);
        for (unsigned y = 10; y < 20; ++y)
            for (unsigned x = 10; x < 20; ++x)
                image.pixels[y * 64 + x] = {220, 220, 220, 255};
        for (unsigned y = 20; y < 30; ++y)
            for (unsigned x = 20; x < 30; ++x)
                image.pixels[y * 64 + x] = {220, 220, 220, 255};
        verifyCoverage(contour::process(image, s));
        // A sampled rounded UI card should become a radius-bearing primitive.
        image = solid(128, 96);
        for (unsigned y = 8; y < 88; ++y)
            for (unsigned x = 8; x < 120; ++x) {
                float qx = std::abs(x + .5f - 64) - (56 - 12), qy = std::abs(y + .5f - 48) - (40 - 12);
                if (std::hypot(std::max(qx, 0.f), std::max(qy, 0.f)) + std::min(std::max(qx, qy), 0.f) <= 12)
                    image.pixels[y * 128 + x] = {220, 220, 220, 255};
            }
        r = contour::process(image, s);
        verifyCoverage(r);
        bool rounded = false;
        for (auto &region : r.regions)
            rounded |= region.primitive.kind == contour::PrimitiveKind::RoundedRectangle;
        require(rounded, "Rounded rectangle radius not fitted");
        // Increasing geometry tolerance must not erase small disconnected objects.
        image = solid(128, 96);
        for (unsigned y = 40; y < 43; ++y)
            for (unsigned x = 50; x < 53; ++x)
                image.pixels[y * 128 + x] = {180, 80, 40, 255};
        s.geometryTolerance = 2;
        s.denoise = false;
        r = contour::process(image, s);
        verifyCoverage(r);
        require(r.regions.size() == 2, "Small disconnected high-contrast shape lost");
        auto subtle = solid(128, 96);
        for (unsigned y = 40; y < 44; ++y)
            for (unsigned x = 50; x < 54; ++x)
                subtle.pixels[y * 128 + x] = {26, 20, 20, 255};
        require(contour::process(subtle, s).regions.size() == 2, "Small isolated subtle color edge lost");
        for (const auto &b : r.boundaries)
            if (b.closed && b.right != UINT32_MAX && b.left != UINT32_MAX)
                require(b.segments.size() >= 3, "Small ring collapsed to a line");
        auto binary = contour::encodeCompact(r);
        auto restored = contour::decodeCompact(binary);
        require(restored.boundaries.size() == r.boundaries.size() &&
                    restored.regions.size() == r.regions.size(),
                "Compact graph round-trip changed topology");
        for (size_t i = 0; i < r.boundaries.size(); ++i) {
            require(restored.boundaries[i].segments.size() == r.boundaries[i].segments.size(),
                    "Compact segment count changed");
            require(restored.boundaries[i].right == r.boundaries[i].right &&
                        restored.boundaries[i].left == r.boundaries[i].left,
                    "Compact region adjacency changed");
        }
        bool legacyRejected = false;
        auto legacyHeader = binary;
        legacyHeader[3] = '4';
        try {
            contour::decodeCompact(legacyHeader);
        } catch (const std::exception &) {
            legacyRejected = true;
        }
        require(legacyRejected, "Legacy codec unexpectedly retained");
        bool rejected = false;
        auto damaged = binary;
        damaged.back() ^= 0x55;
        try {
            contour::decodeCompact(damaged);
        } catch (const std::exception &) {
            rejected = true;
        }
        require(rejected, "Corrupt compact graph accepted");
        contour::RasterScene raster;
        raster.prepare(std::make_shared<contour::Image>(image), std::make_shared<contour::Result>(r));
        contour::Image preview;
        raster.render(preview, 128, 96, 1, 0, 0, contour::RasterMode::Edges, 1, 1);
        require(preview.pixels[0].g == 24 && preview.pixels[127].g == 24,
                "Canvas frame appears in edge overlay");
        raster.render(preview, 128, 96, 1, 0, 0, contour::RasterMode::Regions, 1, 1);
        require(preview.pixels[41 * 128 + 51].g == 80, "Custom fill lost small object");
        contour::Image serialFill, parallelFill;
        raster.render(serialFill, 2048, 1152, 8.25f, 17.5f, -12.75f, contour::RasterMode::Regions, .6f, 1,
                      false);
        raster.render(parallelFill, 2048, 1152, 8.25f, 17.5f, -12.75f, contour::RasterMode::Regions, .6f, 1,
                      true);
        require(
            std::equal(serialFill.pixels.begin(), serialFill.pixels.end(), parallelFill.pixels.begin(),
                       [](auto a, auto b) { return a.b == b.b && a.g == b.g && a.r == b.r && a.a == b.a; }),
            "Parallel raster changed region fills");
        s.geometryTolerance = .75f;
        std::mt19937 rng(42);
        for (int iteration = 0; iteration < 30; ++iteration) {
            image = solid(32 + iteration % 5, 32);
            for (auto &p : image.pixels) {
                auto v = uint8_t((rng() % 4) * 70);
                p = {v, v, v, 255};
            }
            s.denoise = (iteration % 2) != 0;
            verifyCoverage(contour::process(image, s));
        }
        image = solid(256, 128);
        s.analysisLongSide = 64;
        r = contour::process(image, s);
        require(r.width == 64 && r.height == 32, "Resolution normalization failed");
        // A smooth gradient should not be split into hundreds of artificial bands.
        image = solid(256, 128);
        for (unsigned y = 0; y < 128; ++y)
            for (unsigned x = 0; x < 256; ++x) {
                auto v = uint8_t(x / 2);
                image.pixels[y * 256 + x] = {v, v, v, 255};
            }
        s.analysisLongSide = 2048;
        r = contour::process(image, s);
        require(r.regions.size() < 5, "Smooth gradient fragmented");
        verifyCoverage(r);
        // Adjacent honeycomb cells share a single boundary with opposite region references.
        image = solid(160, 128);
        std::vector<contour::Point> centers;
        for (int row = -1; row < 6; ++row)
            for (int col = -1; col < 7; ++col)
                centers.push_back({float(col * 30), float(row * 35 + (col % 2) * 17.5)});
        for (unsigned y = 0; y < image.height; ++y)
            for (unsigned x = 0; x < image.width; ++x) {
                float distance = 1e9f;
                size_t cell = 0;
                for (size_t i = 0; i < centers.size(); ++i) {
                    auto dx = x - centers[i].x, dy = y - centers[i].y;
                    auto d = dx * dx + dy * dy;
                    if (d < distance) {
                        distance = d;
                        cell = i;
                    }
                }
                image.pixels[y * image.width + x] = {uint8_t(40 + (cell % 3) * 90),
                                                     uint8_t(30 + ((cell / 3) % 3) * 100),
                                                     uint8_t(20 + ((cell / 9) % 3) * 100), 255};
            }
        r = contour::process(image, s);
        require(r.regions.size() > 20, "Honeycomb cells merged");
        verifyCoverage(r);
        // An 8K source is accepted and analyzed at the same default relative scale.
        image = solid(7680, 4320);
        r = contour::process(image, s);
        require(r.sourceWidth == 7680 && r.sourceHeight == 4320 && r.width == 2048 && r.height == 1152,
                "8K source normalization failed");
        require(r.regions.size() == 1, "8K solid fragmented");
        verifyCoverage(r);
        bool canceled = false;
        try {
            contour::process(image, s, [] { return true; });
        } catch (const std::runtime_error &) {
            canceled = true;
        }
        require(canceled, "Cancellation ignored");
        s.mode = contour::Settings::Mode::Edges;
        s.detail = 44;
        s.geometryTolerance = 1.55f;
        s.pointBudget = 8;
        s.denoise = false;
        image = solid(128, 96);
        r = contour::process(image, s);
        require(r.strokes.empty(), "Edge mode draws solid canvas frame");
        auto blank = contour::decodeCompact(contour::encodeCompact(r));
        require(blank.mode == s.mode && blank.strokes.empty(), "Empty edges round trip failed");
        image = solid(256, 96);
        for (unsigned y = 0; y < image.height; ++y)
            for (unsigned x = 0; x < image.width; ++x)
                image.pixels[y * image.width + x] = {uint8_t(x), uint8_t(x / 2), uint8_t(255 - x), 255};
        r = contour::process(image, s);
        require(r.strokes.empty(), "Smooth RGB gradient produces false edges");
        for (unsigned y = 0; y < image.height; ++y)
            for (unsigned x = 0; x < image.width; ++x) {
                auto v = uint8_t(x * x / 255);
                image.pixels[y * image.width + x] = {v, v, v, 255};
            }
        r = contour::process(image, s);
        require(r.strokes.empty(), "Smooth nonlinear gradient produces false edges");
        image = solid(128, 96);
        for (unsigned y = 0; y < 96; ++y)
            for (unsigned x = 0; x < 128; ++x)
                image.pixels[y * 128 + x] =
                    x < 64 ? contour::Pixel{0, 0, 170, 255} : contour::Pixel{0, 86, 0, 255};
        r = contour::process(image, s);
        require(r.strokes.size() == 1 && r.strokes[0].segments.size() == 1,
                "Equal luminance color edge not retained as one line");
        require(r.strokes[0].start.x == 64 && r.strokes[0].segments[0].end.x == 64, "Color edge moved");
        image = solid(128, 96);
        for (unsigned y = 20; y < 80; ++y)
            for (unsigned x = 30; x < 33; ++x)
                image.pixels[y * 128 + x] = {200, 200, 200, 255};
        r = contour::process(image, s);
        require(r.strokes.size() == 1 && r.strokes[0].segments.size() == 1 && r.strokes[0].width == 3,
                "Thin rectangle not reduced to one thick line");
        image = solid(128, 96);
        for (unsigned y = 40; y < 44; ++y)
            for (unsigned x = 50; x < 54; ++x)
                image.pixels[y * 128 + x] = {26, 20, 20, 255};
        r = contour::process(image, s);
        require(r.strokes.size() == 1 && r.strokes[0].closed, "Small subtle isolated color shape lost");
        auto roundTrip = contour::decodeCompact(contour::encodeCompact(r));
        require(roundTrip.strokes.size() == 1 &&
                    roundTrip.strokes[0].primitive.kind == r.strokes[0].primitive.kind,
                "Edge primitive round trip failed");
        image = solid(128, 96);
        for (unsigned y = 20; y < 80; ++y)
            for (unsigned x = 20; x < 108; ++x)
                if (x < 23 || x >= 105 || y < 23 || y >= 77)
                    image.pixels[y * 128 + x] = {180, 180, 180, 255};
        r = contour::process(image, s);
        require(r.strokes.size() == 1 && r.strokes[0].width == 3 &&
                    r.strokes[0].primitive.kind == contour::PrimitiveKind::Rectangle,
                "Outline rectangle not reduced to one stroke");
        roundTrip = contour::decodeCompact(contour::encodeCompact(r));
        require(roundTrip.strokes[0].width == 3 && roundTrip.strokes[0].closed,
                "Thick rectangle round trip failed");
        image = solid(160, 120);
        for (unsigned y = 20; y < 100; ++y)
            for (unsigned x = 20; x < 140; ++x) {
                float dx = std::max(std::abs(x + .5f - 80) - 44, 0.f),
                      dy = std::max(std::abs(y + .5f - 60) - 24, 0.f);
                if (dx * dx + dy * dy <= 256)
                    image.pixels[y * 160 + x] = {180, 70, 30, 255};
            }
        r = contour::process(image, s);
        require(r.strokes.size() == 1 &&
                    r.strokes[0].primitive.kind == contour::PrimitiveKind::RoundedRectangle &&
                    std::abs(r.strokes[0].primitive.radius - 16) < 2,
                "Edge rounded card lost radius");
        roundTrip = contour::decodeCompact(contour::encodeCompact(r));
        require(roundTrip.strokes[0].primitive.kind == contour::PrimitiveKind::RoundedRectangle &&
                    std::abs(roundTrip.strokes[0].primitive.radius - r.strokes[0].primitive.radius) <= .0626f,
                "Radius round trip failed");
        auto corrupted = contour::encodeCompact(r);
        corrupted.back() ^= 63;
        bool edgeRejected = false;
        try {
            contour::decodeCompact(corrupted);
        } catch (const std::runtime_error &) {
            edgeRejected = true;
        }
        require(edgeRejected, "Damaged edge file accepted");
        contour::Stroke wave;
        for (int i = 0; i <= 100; ++i)
            wave.points.push_back({float(i), float(50 + 20 * std::sin(i * .3))});
        auto end = wave.points.back();
        contour::fitStroke(wave, .2f, 8);
        require(wave.segments.size() <= 8 && wave.start == wave.points.front() &&
                    wave.segments.back().end == end,
                "Point budget/endpoints failed");
        contour::Stroke sharp;
        sharp.points = {{0, 0}, {100, 0}, {100, 10}, {100, 100}, {0, 100}};
        contour::fitStroke(sharp, 1.55f, 16);
        for (auto segment : sharp.segments)
            require(segment.radius == 0, "Straight wall bent into false circle");
        contour::Stroke attached;
        attached.closed = true;
        attached.anchored = true;
        attached.points = {{40, 10}, {80, 10}, {80, 60}, {20, 60}, {20, 10}, {40, 10}};
        contour::fitStroke(attached, 1.55f, 16);
        require(attached.start == contour::Point{40, 10} && attached.segments.back().end == attached.start &&
                    attached.primitive.kind == contour::PrimitiveKind::Path,
                "Junction anchor moved");
        contour::Result capGraph;
        capGraph.mode = contour::Settings::Mode::Edges;
        capGraph.width = capGraph.sourceWidth = 128;
        capGraph.height = capGraph.sourceHeight = 96;
        contour::Stroke capLine;
        capLine.start = {20, 40};
        capLine.segments = {{{100, 40}, 0, false}};
        capLine.width = 6;
        capLine.roundCaps = true;
        capGraph.strokes.push_back(capLine);
        auto capDecoded = contour::decodeCompact(contour::encodeCompact(capGraph));
        require(capDecoded.strokes[0].roundCaps && capDecoded.strokes[0].width == 6,
                "Line cap/width metadata lost");
        contour::RasterScene capScene;
        capScene.prepare({}, std::make_shared<contour::Result>(capDecoded));
        contour::Image capFrame;
        capScene.render(capFrame, 128, 96, 1, 0, 0, contour::RasterMode::Edges, 1, 1);
        require(capFrame.pixels[40 * 128 + 17].g > 40 && capFrame.pixels[40 * 128 + 16].g == 24,
                "Thick line cap raster incorrect");
        capScene.render(capFrame, 128, 96, 1, 0, 0, contour::RasterMode::Edges, 1, 1, true, {0, 0, 255, 255});
        require(capFrame.pixels[40 * 128 + 40].r > 240 && capFrame.pixels[40 * 128 + 40].g < 30,
                "Custom outline color ignored");
        contour::Image serialRaster, parallelRaster;
        capScene.render(serialRaster, 2048, 1152, 8.f, 90, -32, contour::RasterMode::Edges, .75f, 1.3f,
                        false);
        capScene.render(parallelRaster, 2048, 1152, 8.f, 90, -32, contour::RasterMode::Edges, .75f, 1.3f,
                        true);
        require(
            std::equal(serialRaster.pixels.begin(), serialRaster.pixels.end(), parallelRaster.pixels.begin(),
                       [](auto a, auto b) { return a.b == b.b && a.g == b.g && a.r == b.r && a.a == b.a; }),
            "Parallel raster changed caps or band edges");
        capLine.start = {0, 15};
        capLine.segments = {{{120, 91}, 0, false}};
        capLine.width = 1.5f;
        capGraph.strokes.push_back(capLine);
        capLine.start = {110, 0};
        capLine.segments = {{{12, 95}, 0, false}};
        capGraph.strokes.push_back(capLine);
        auto rasterSource = solid(128, 96);
        for (size_t i = 0; i < rasterSource.pixels.size(); ++i)
            rasterSource.pixels[i] = {uint8_t(i % 239), 70, uint8_t(i % 197), uint8_t(i % 256)};
        capScene.prepare(std::make_shared<contour::Image>(rasterSource),
                         std::make_shared<contour::Result>(capGraph));
        for (auto mode :
             {contour::RasterMode::Overlay, contour::RasterMode::Original, contour::RasterMode::Edges}) {
            capScene.render(serialRaster, 2048, 1152, 11.f, -19.5f, 42.25f, mode, .37f, 1.7f, false);
            capScene.render(parallelRaster, 2048, 1152, 11.f, -19.5f, 42.25f, mode, .37f, 1.7f, true);
            require(std::equal(
                        serialRaster.pixels.begin(), serialRaster.pixels.end(), parallelRaster.pixels.begin(),
                        [](auto a, auto b) { return a.b == b.b && a.g == b.g && a.r == b.r && a.a == b.a; }),
                    "Parallel raster changed sloped strokes, source alpha or background clipping");
        }
        auto channelImage = solid(128, 96);
        auto channelSettings = contour::defaultsFor(contour::Settings::Mode::Edges);
        channelSettings.brightnessSensitivity = 0;
        for (unsigned y = 0; y < 96; ++y)
            for (unsigned x = 64; x < 128; ++x)
                channelImage.pixels[y * 128 + x] = {200, 200, 200, 255};
        require(contour::process(channelImage, channelSettings).strokes.empty(),
                "Brightness-off retained grey edge");
        channelSettings.mode = contour::Settings::Mode::Regions;
        require(contour::process(channelImage, channelSettings).regions.size() == 1,
                "Region brightness-off retained grey edge");
        for (unsigned y = 0; y < 96; ++y)
            for (unsigned x = 64; x < 128; ++x)
                channelImage.pixels[y * 128 + x] = {200, 20, 20, 255};
        require(contour::process(channelImage, channelSettings).regions.size() > 1,
                "Chroma edge lost in regions");
        channelSettings.mode = contour::Settings::Mode::Edges;
        require(!contour::process(channelImage, channelSettings).strokes.empty(),
                "Chroma edge lost with brightness-off");
        channelSettings.colorSensitivity = channelSettings.brightnessSensitivity = 0;
        require(contour::process(channelImage, channelSettings).strokes.empty(),
                "Both channels disabled still detected edges");
        auto gainSettings = contour::defaultsFor(contour::Settings::Mode::Edges);
        gainSettings.contrastMultiplier = .01f;
        require(contour::process(channelImage, gainSettings).strokes.empty(),
                "1 percent contrast gain retained normal edges");
        gainSettings.contrastMultiplier = 2;
        require(!contour::process(channelImage, gainSettings).strokes.empty(),
                "200 percent gain lost color edge");
        gainSettings.analysisLongSide = 1536;
        auto customImage = solid(3072, 1728);
        require(contour::process(customImage, gainSettings).width == 1536, "1536 analysis failed");
        gainSettings.analysisLongSide = 1379;
        require(contour::process(customImage, gainSettings).width == 1379, "Custom analysis failed");
        gainSettings.connectGaps = false;
        require(contour::process(customImage, gainSettings).joinedGaps == 0,
                "Disabled gap linking still ran");
        auto gapImage = solid(128, 96);
        for (unsigned y = 0; y < 96; ++y)
            for (unsigned x = 0; x < 128; ++x) {
                int edge = 64 + int(std::lround(14 * std::sin(y * .17)));
                if (int(x) >= edge)
                    gapImage.pixels[y * 128 + x] = {200, 70, 40, 255};
            }
        auto gapSettings = contour::defaultsFor(contour::Settings::Mode::Edges);
        gapSettings.minContourLength = 0;
        auto joinedImage = contour::process(gapImage, gapSettings);
        require(joinedImage.joinedGaps == 0, "Unbroken wave received false gap joins");
        auto defaults = contour::defaultsFor(contour::Settings::Mode::Edges);
        require(defaults.detail == 0 && defaults.pointBudget == 60 &&
                    std::abs(defaults.geometryTolerance - .7f) < .001f,
                "Requested edge defaults lost");
        require(!defaults.connectGaps && defaults.minContourLength == 3 &&
                    std::abs(defaults.colorSensitivity - .89f) < .001f &&
                    std::abs(defaults.brightnessSensitivity - .8f) < .001f,
                "Screenshot color/brightness/filter defaults lost");
        auto islands = solid(128, 96);
        for (unsigned y = 20; y < 25; ++y)
            for (unsigned x = 20; x < 25; ++x)
                islands.pixels[y * 128 + x] = {220, 220, 220, 255};
        for (unsigned y = 40; y < 80; ++y)
            for (unsigned x = 60; x < 110; ++x)
                islands.pixels[y * 128 + x] = {220, 220, 220, 255};
        defaults.minContourLength = 0;
        auto allEdges = contour::process(islands, defaults);
        defaults.minContourLength = 32;
        auto longEdges = contour::process(islands, defaults);
        require(longEdges.strokes.size() < allEdges.strokes.size() && !longEdges.strokes.empty() &&
                    longEdges.discardedContours > 0,
                "Edge minimum length did not remove isolated short contour");
        for (const auto &stroke : longEdges.strokes)
            require(stroke.contourLength >= 32, "Short edge survived filter");
        auto regionSettings = contour::defaultsFor(contour::Settings::Mode::Regions);
        regionSettings.detail = 100;
        regionSettings.minContourLength = 0;
        auto allRegions = contour::process(islands, regionSettings);
        regionSettings.minContourLength = 32;
        auto filteredRegions = contour::process(islands, regionSettings);
        verifyCoverage(filteredRegions);
        require(filteredRegions.regions.size() == allRegions.regions.size() &&
                    filteredRegions.labels == allRegions.labels && filteredRegions.discardedContours > 0,
                "Region length filter altered closed regions");
        auto filteredDecoded = contour::decodeCompact(contour::encodeCompact(filteredRegions));
        for (size_t i = 0; i < filteredRegions.boundaries.size(); ++i)
            require(filteredDecoded.boundaries[i].visible == filteredRegions.boundaries[i].visible,
                    "Boundary visibility not preserved by CGR");
        capGraph.strokes.push_back(capLine);
        capGraph.strokes.back().start = {12, 20};
        capGraph.strokes.back().segments = {{{90, 22}, 0, false}};
        capGraph.strokes.back().width = 1;
        auto deltaDecoded = contour::decodeCompact(contour::encodeCompact(capGraph));
        bool deltaFound = false;
        for (const auto &stroke : deltaDecoded.strokes)
            deltaFound |= stroke.start == contour::Point{12, 20} && stroke.width == 1 &&
                          stroke.segments[0].end == contour::Point{90, 22};
        require(deltaFound, "CGR origin delta/default width round trip failed");
        std::cout << "Passed: edge mode RGB/gradients, small shapes, thick strokes, point budget, compact "
                     "edges; color edges, coverage, holes, rectangles, corner radius, transparency, specks, "
                     "diagonal touches, random topology, honeycomb shared edges, 8K source, resizing, "
                     "cancellation\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
