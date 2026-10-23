#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace contour {
struct Pixel {
    uint8_t b{}, g{}, r{}, a{255};
};
struct Image {
    uint32_t width{}, height{};
    std::vector<Pixel> pixels;
};
struct Point {
    float x{}, y{};
    bool operator==(const Point &) const = default;
};
struct Segment {
    Point end{};
    float radius{};
    bool clockwise{};
}; // radius=0 means a line.
struct BoundaryRef {
    uint32_t id{};
    bool reversed{};
};
struct Loop {
    std::vector<Point> points;
    double signedArea{};
    Point start{};
    std::vector<Segment> segments;
    std::vector<BoundaryRef> boundaries;
};
struct Boundary {
    uint32_t right{}, left{UINT32_MAX};
    bool closed{};
    bool anchored{};    // Closed chain attached to a junction; its start must stay fixed.
    bool visible{true}; // Hidden short overlay edges still close fillable region loops.
    std::vector<Point> points;
    Point start{};
    std::vector<Segment> segments;
};
enum class PrimitiveKind { Path, Rectangle, RoundedRectangle };
struct Primitive {
    PrimitiveKind kind{PrimitiveKind::Path};
    float x{}, y{}, width{}, height{}, radius{};
};
struct Region {
    uint32_t id{}, area{};
    Pixel color{};
    std::vector<Loop> loops; // Even-odd fill: outer rings and holes.
    Primitive primitive;
};
struct Settings {
    enum class Mode { Regions, Edges };
    float detail{44};                // 0..100, controls contrast and speck removal.
    uint32_t analysisLongSide{2048}; // Stable relative detail across source resolutions.
    bool denoise{false};
    float geometryTolerance{1.55f}; // Analysis pixels; bounded fitting error.
    Mode mode{Mode::Regions};
    uint32_t pointBudget{16};    // Maximum segments per free path; endpoints retained.
    float contrastMultiplier{1}; // .01..2: global contrast gain.
    float colorSensitivity{1}, brightnessSensitivity{1}; // 0 ignores channel, 1 keeps full contrast.
    bool connectGaps{true};                              // Bridge small aligned gaps backed by RGB evidence.
    float minContourLength{6}; // Analysis pixels: open length / closed perimeter; 0 disables.
};
inline Settings defaultsFor(Settings::Mode mode) {
    Settings s;
    s.mode = mode;
    s.analysisLongSide = mode == Settings::Mode::Regions ? 1024 : 2048;
    if (mode == Settings::Mode::Edges) {
        s.detail = 0;
        s.geometryTolerance = .7f;
        s.pointBudget = 60;
        s.colorSensitivity = .89f;
        s.brightnessSensitivity = .8f;
        s.connectGaps = false;
        s.minContourLength = 3;
    }
    return s;
}
struct Stroke {
    Point start{};
    std::vector<Segment> segments;
    std::vector<Point> points; // Working samples; never saved in compact files.
    Primitive primitive;
    float width{1};
    bool closed{}, roundCaps{};
    bool anchored{};                     // Working junction constraint; fitting retains this start.
    float contourLength{}, importance{}; // Analysis evidence; not stored in CGR.
};
struct Timings {
    double resizeMs{}, filterMs{}, detectMs{}, regionsMs{}, traceMs{}, fitMs{}, totalMs{};
    double smoothMs{}, gradientMs{}, suppressMs{}, graphMs{}, linkMs{};
};
struct Result {
    Settings::Mode mode{Settings::Mode::Regions};
    uint32_t sourceWidth{}, sourceHeight{}, width{}, height{};
    std::vector<uint32_t> labels;
    std::vector<Region> regions;
    std::vector<Boundary> boundaries; // Each shared edge stored once; regions reference it.
    std::vector<Stroke> strokes;      // Mode 2: sparse edges without region segmentation.
    uint64_t vertices{};
    uint32_t discardedContours{}, joinedGaps{};
    Timings timings;
};
using Cancel = std::function<bool()>;
Result process(const Image &, const Settings &, const Cancel &canceled = {});
Result processEdges(const Image &, const Settings &, const Cancel &canceled = {});
Image resize(const Image &, uint32_t longSide);
std::string svg(const Result &, bool filled);
void saveSvg(const Result &, const std::wstring &path, bool filled);
void saveGraph(const Result &, const std::wstring &path);
std::vector<uint8_t> encodeCompact(const Result &);
Result decodeCompact(const std::vector<uint8_t> &);
void saveCompact(const Result &, const std::wstring &path);
void saveCompactBytes(const std::vector<uint8_t> &, const std::wstring &path);
Result loadCompact(const std::wstring &path, double *readMs = nullptr, double *decodeMs = nullptr);
void fitGeometry(Result &, float tolerance);
void fitStroke(Stroke &, float tolerance, uint32_t segmentBudget);
void expandPrimitive(Stroke &);
} // namespace contour
