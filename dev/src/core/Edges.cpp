#include "ColorMetric.h"
#include "Contour.h"
#include "Parallel.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif
namespace contour {
namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}
int contrast(Pixel a, Pixel b) {
    return std::max({std::abs(int(a.b) - b.b), std::abs(int(a.g) - b.g), std::abs(int(a.r) - b.r)});
}
struct Edge {
    uint32_t from, to, nextFrom, nextTo;
    uint16_t response;
    uint8_t flags;
};
void lineFromThinPrimitive(Stroke &s) {
    auto p = s.primitive;
    if (p.kind == PrimitiveKind::Path)
        return;
    float shortSide = std::min(p.width, p.height), longSide = std::max(p.width, p.height);
    if (shortSide > 8 || longSide < shortSide * 4)
        return;
    s.roundCaps = p.radius >= shortSide * .4f;
    s.width = shortSide;
    float inset = s.roundCaps ? shortSide * .5f : 0;
    Point a, b;
    if (p.width < p.height) {
        a = {p.x + p.width * .5f, p.y + inset};
        b = {a.x, p.y + p.height - inset};
    } else {
        a = {p.x + inset, p.y + p.height * .5f};
        b = {p.x + p.width - inset, a.y};
    }
    s.primitive = {};
    s.start = a;
    s.segments = {Segment{b, 0, false}};
    s.closed = false;
}
void combineBorders(std::vector<Stroke> &strokes, const Image &image) {
    std::vector<uint8_t> removed(strokes.size());
    auto pixel = [&](float x, float y) {
        auto ix = std::clamp(int(x), 0, int(image.width) - 1),
             iy = std::clamp(int(y), 0, int(image.height) - 1);
        return image.pixels[size_t(iy) * image.width + ix];
    };
    std::vector<size_t> primitives;
    for (size_t i = 0; i < strokes.size(); ++i)
        if (strokes[i].primitive.kind != PrimitiveKind::Path)
            primitives.push_back(i);
    for (size_t position = 0; position < primitives.size(); ++position) {
        auto i = primitives[position];
        if (removed[i] || strokes[i].primitive.kind == PrimitiveKind::Path)
            continue;
        auto a = strokes[i].primitive;
        for (size_t next = position + 1; next < primitives.size(); ++next) {
            auto j = primitives[next];
            if (removed[j] || strokes[j].primitive.kind == PrimitiveKind::Path)
                continue;
            auto b = strokes[j].primitive;
            if (std::abs(a.x + a.width * .5f - b.x - b.width * .5f) > .6f ||
                std::abs(a.y + a.height * .5f - b.y - b.height * .5f) > .6f)
                continue;
            bool swapped = a.width < b.width;
            auto outer = swapped ? b : a, inner = swapped ? a : b;
            float inset = (outer.width - inner.width) * .5f;
            if (inset < .5f || inset > 8 || std::abs((outer.height - inner.height) * .5f - inset) > .6f)
                continue;
            float cx = outer.x + outer.width * .5f, cy = outer.y + outer.height * .5f;
            int matches = 0;
            matches += contrast(pixel(cx, outer.y - 1), pixel(cx, inner.y + 1)) <= 3;
            matches +=
                contrast(pixel(cx, outer.y + outer.height), pixel(cx, inner.y + inner.height - 1)) <= 3;
            matches += contrast(pixel(outer.x - 1, cy), pixel(inner.x + 1, cy)) <= 3;
            matches += contrast(pixel(outer.x + outer.width, cy), pixel(inner.x + inner.width - 1, cy)) <= 3;
            if (matches < 3)
                continue;
            auto &target = strokes[i];
            target.width = inset;
            target.primitive = {outer.kind,
                                outer.x + inset * .5f,
                                outer.y + inset * .5f,
                                outer.width - inset,
                                outer.height - inset,
                                std::max(0.f, (outer.radius + inner.radius) * .5f)};
            expandPrimitive(target);
            removed[j] = 1;
            a = target.primitive;
        }
    }
    std::vector<Stroke> combined;
    combined.reserve(strokes.size());
    for (size_t i = 0; i < strokes.size(); ++i)
        if (!removed[i]) {
            if (strokes[i].width == 1)
                lineFromThinPrimitive(strokes[i]);
            combined.push_back(std::move(strokes[i]));
        }
    strokes = std::move(combined);
}
} // namespace
Result processEdges(const Image &input, const Settings &settings, const Cancel &canceled) {
    const detail::ColorMetric metric(settings);
    auto contrast = [&](Pixel a, Pixel b) { return metric.contrast(a, b); };
    auto check = [&] {
        if (canceled && canceled())
            throw std::runtime_error("Operation canceled");
    };
    check();
    auto begin = Clock::now(), stage = begin;
    if (!std::isfinite(settings.detail) || !std::isfinite(settings.geometryTolerance) ||
        !std::isfinite(settings.minContourLength) || settings.minContourLength < 0 ||
        settings.minContourLength > 1024 || settings.analysisLongSide < 64 ||
        settings.analysisLongSide > 8192)
        throw std::invalid_argument("Invalid edge settings");
    auto image = resize(input, settings.analysisLongSide);
    Result result;
    result.mode = Settings::Mode::Edges;
    result.sourceWidth = input.width;
    result.sourceHeight = input.height;
    result.width = image.width;
    result.height = image.height;
    result.timings.resizeMs = elapsed(stage, Clock::now());
    stage = Clock::now();
    const auto w = image.width, h = image.height;
    auto n = size_t(w) * h;
    detail::parallelRanges(0, uint32_t(n), n, [&](uint32_t first, uint32_t last) {
        for (auto i = first; i < last; ++i) {
            auto &p = image.pixels[i];
            if (p.a != 255) {
                int a = p.a;
                p = {uint8_t((p.b * a + 128 * (255 - a)) / 255),
                     uint8_t((p.g * a + 128 * (255 - a)) / 255),
                     uint8_t((p.r * a + 128 * (255 - a)) / 255), 255};
            }
        }
    });
    // Optional median removes impulses without averaging across a color boundary.
    if (settings.denoise && w > 2 && h > 2) {
        auto original = image.pixels;
        for (uint32_t y = 1; y + 1 < h; ++y) {
            if (!(y & 127))
                check();
            for (uint32_t x = 1; x + 1 < w; ++x) {
                size_t i = size_t(y) * w + x;
                auto center = original[i];
                Pixel neighbors[]{original[i - 1], original[i + 1], original[i - w], original[i + w]};
                int agreed = 0;
                for (auto p : neighbors)
                    agreed += contrast(p, neighbors[0]) <= 2;
                if (agreed == 4 && contrast(center, neighbors[0]) > 12)
                    image.pixels[i] = neighbors[0];
            }
        }
    }
    result.timings.filterMs = elapsed(stage, Clock::now());
    stage = Clock::now();
    auto rows = [&](uint32_t first, uint32_t last, auto action) {
        detail::parallelRows(first, last, n, action);
        check();
    };
    // A fixed small kernel supplies local gradient evidence even with the optional
    // impulse filter disabled. It never rejects a contour by its length.
    std::vector<Pixel> horizontal(n), smooth(n);
    auto average = [](Pixel a, Pixel b, Pixel c) {
        return Pixel{uint8_t((a.b + 2 * b.b + c.b + 2) / 4), uint8_t((a.g + 2 * b.g + c.g + 2) / 4),
                     uint8_t((a.r + 2 * b.r + c.r + 2) / 4), 255};
    };
    rows(0, h, [&](uint32_t y) {
        size_t start = size_t(y) * w;
        uint32_t x = 0;
#if defined(_M_X64) || defined(__SSE2__)
        horizontal[start] = average(image.pixels[start], image.pixels[start], image.pixels[start + (w > 1)]);
        x = 1;
        for (; x + 4 < w; x += 4) {
            auto a = _mm_loadu_si128(reinterpret_cast<const __m128i *>(image.pixels.data() + start + x - 1)),
                 b = _mm_loadu_si128(reinterpret_cast<const __m128i *>(image.pixels.data() + start + x)),
                 c = _mm_loadu_si128(reinterpret_cast<const __m128i *>(image.pixels.data() + start + x + 1));
            _mm_storeu_si128(reinterpret_cast<__m128i *>(horizontal.data() + start + x),
                             _mm_avg_epu8(_mm_avg_epu8(a, c), b));
        }
#endif
        for (; x < w; ++x) {
            size_t i = start + x;
            horizontal[i] = average(image.pixels[i - (x ? 1 : 0)], image.pixels[i],
                                    image.pixels[i + (x + 1 < w ? 1 : 0)]);
        }
    });
    rows(0, h, [&](uint32_t y) {
        size_t start = size_t(y) * w;
        uint32_t x = 0;
        auto a = horizontal.data() + start - (y ? w : 0), b = horizontal.data() + start,
             c = horizontal.data() + start + (y + 1 < h ? w : 0);
#if defined(_M_X64) || defined(__SSE2__)
        for (; x + 4 <= w; x += 4)
            _mm_storeu_si128(
                reinterpret_cast<__m128i *>(smooth.data() + start + x),
                _mm_avg_epu8(_mm_avg_epu8(_mm_loadu_si128(reinterpret_cast<const __m128i *>(a + x)),
                                          _mm_loadu_si128(reinterpret_cast<const __m128i *>(c + x))),
                             _mm_loadu_si128(reinterpret_cast<const __m128i *>(b + x))));
#endif
        for (; x < w; ++x)
            smooth[start + x] = average(a[x], b[x], c[x]);
    });
    result.timings.smoothMs = elapsed(stage, Clock::now());
    auto part = Clock::now();
    std::vector<uint16_t> magnitude(n);
    std::vector<uint8_t> direction(n);
    auto record = [&](size_t i, const int16_t *dx, const int16_t *dy) {
        auto weightedX = metric.transform(dx[0], dx[1], dx[2]),
             weightedY = metric.transform(dy[0], dy[1], dy[2]);
        dx = weightedX.data();
        dy = weightedY.data();
        int gx = 0, gy = 0, best = 0;
        for (int channel = 0; channel < 3; ++channel) {
            int ax = std::abs(dx[channel]), ay = std::abs(dy[channel]),
                score = std::max(ax, ay) + (std::min(ax, ay) * 106 >> 8);
            if (score > best) {
                best = score;
                gx = dx[channel];
                gy = dy[channel];
            }
        }
        int ax = std::abs(gx), ay = std::abs(gy);
        magnitude[i] = uint16_t(best);
        direction[i] = uint8_t(ay * 1000 < ax * 414 ? 0 : ax * 1000 < ay * 414 ? 2 : gx * gy > 0 ? 1 : 3);
    };
    // SIMD central RGB differences after local smoothing; alpha is excluded.
    auto gradients = [&]<bool Weighted>() {
        if (w > 2 && h > 2)
            rows(1, h - 1, [&](uint32_t y) {
                uint32_t x = 1;
#if defined(_M_X64) || defined(__SSE2__)
                auto zero = _mm_setzero_si128();
                for (; x + 4 < w; x += 4) {
                    size_t i = size_t(y) * w + x;
                    auto a = _mm_loadu_si128(reinterpret_cast<const __m128i *>(smooth.data() + i - 1)),
                         b = _mm_loadu_si128(reinterpret_cast<const __m128i *>(smooth.data() + i + 1)),
                         c = _mm_loadu_si128(reinterpret_cast<const __m128i *>(smooth.data() + i - w)),
                         d = _mm_loadu_si128(reinterpret_cast<const __m128i *>(smooth.data() + i + w));
                    auto selectPair = [&](size_t index, __m128i dx, __m128i dy) {
                        if constexpr (Weighted) {
                            auto weighted = [&](const __m128i value) {
                                auto sums =
                                    _mm_madd_epi16(value, _mm_set_epi16(0, 77, 150, 29, 0, 77, 150, 29));
                                sums = _mm_add_epi32(sums, _mm_shuffle_epi32(sums, _MM_SHUFFLE(2, 3, 0, 1)));
                                sums = _mm_srai_epi32(_mm_add_epi32(sums, _mm_set1_epi32(128)), 8);
                                auto coefficient = _mm_set1_epi16(int16_t(metric.color));
                                auto lo = _mm_mullo_epi16(value, coefficient),
                                     hi = _mm_mulhi_epi16(value, coefficient);
                                auto lumaCoefficient =
                                    _mm_set1_epi32(uint16_t(metric.brightness - metric.color));
                                auto firstLuma = _mm_madd_epi16(_mm_shuffle_epi32(sums, 0), lumaCoefficient);
                                auto secondLuma =
                                    _mm_madd_epi16(_mm_shuffle_epi32(sums, 170), lumaCoefficient);
                                auto first = _mm_srai_epi32(
                                    _mm_add_epi32(_mm_add_epi32(_mm_unpacklo_epi16(lo, hi), firstLuma),
                                                  _mm_set1_epi32(128)),
                                    8);
                                auto second = _mm_srai_epi32(
                                    _mm_add_epi32(_mm_add_epi32(_mm_unpackhi_epi16(lo, hi), secondLuma),
                                                  _mm_set1_epi32(128)),
                                    8);
                                return _mm_min_epi16(
                                    _mm_set1_epi16(511),
                                    _mm_max_epi16(_mm_set1_epi16(-511), _mm_packs_epi32(first, second)));
                            };
                            dx = weighted(dx);
                            dy = weighted(dy);
                        }
                        auto absolute = [](__m128i value) {
                            auto sign = _mm_srai_epi16(value, 15);
                            return _mm_sub_epi16(_mm_xor_si128(value, sign), sign);
                        };
                        auto ax = absolute(dx), ay = absolute(dy);
                        auto score = _mm_add_epi16(
                            _mm_max_epi16(ax, ay),
                            _mm_srli_epi16(_mm_mullo_epi16(_mm_min_epi16(ax, ay), _mm_set1_epi16(106)), 8));
                        auto broadcastB = [](__m128i v) {
                            return _mm_shufflehi_epi16(_mm_shufflelo_epi16(v, 0), 0);
                        };
                        auto broadcastG = [](__m128i v) {
                            return _mm_shufflehi_epi16(_mm_shufflelo_epi16(v, 85), 85);
                        };
                        auto broadcastR = [](__m128i v) {
                            return _mm_shufflehi_epi16(_mm_shufflelo_epi16(v, 170), 170);
                        };
                        auto choose = [](__m128i mask, __m128i a, __m128i b) {
                            return _mm_or_si128(_mm_and_si128(mask, a), _mm_andnot_si128(mask, b));
                        };
                        auto best = broadcastB(score), gx = broadcastB(dx), gy = broadcastB(dy);
                        auto green = broadcastG(score), red = broadcastR(score);
                        auto greenWins = _mm_cmpgt_epi16(green, best);
                        best = choose(greenWins, green, best);
                        gx = choose(greenWins, broadcastG(dx), gx);
                        gy = choose(greenWins, broadcastG(dy), gy);
                        auto redWins = _mm_cmpgt_epi16(red, best);
                        best = choose(redWins, red, best);
                        gx = choose(redWins, broadcastR(dx), gx);
                        gy = choose(redWins, broadcastR(dy), gy);
                        alignas(16) int16_t xs[8], ys[8], scores[8];
                        _mm_store_si128(reinterpret_cast<__m128i *>(xs), gx);
                        _mm_store_si128(reinterpret_cast<__m128i *>(ys), gy);
                        _mm_store_si128(reinterpret_cast<__m128i *>(scores), best);
                        for (int pixel = 0; pixel < 2; ++pixel) {
                            int vx = xs[pixel * 4], vy = ys[pixel * 4], abx = std::abs(vx),
                                aby = std::abs(vy);
                            magnitude[index + pixel] = uint16_t(scores[pixel * 4]);
                            direction[index + pixel] = uint8_t(aby * 1000 < abx * 414   ? 0
                                                               : abx * 1000 < aby * 414 ? 2
                                                               : vx * vy > 0            ? 1
                                                                                        : 3);
                        }
                    };
                    selectPair(i, _mm_sub_epi16(_mm_unpacklo_epi8(b, zero), _mm_unpacklo_epi8(a, zero)),
                               _mm_sub_epi16(_mm_unpacklo_epi8(d, zero), _mm_unpacklo_epi8(c, zero)));
                    selectPair(i + 2, _mm_sub_epi16(_mm_unpackhi_epi8(b, zero), _mm_unpackhi_epi8(a, zero)),
                               _mm_sub_epi16(_mm_unpackhi_epi8(d, zero), _mm_unpackhi_epi8(c, zero)));
                }
#endif
                for (; x + 1 < w; ++x) {
                    size_t i = size_t(y) * w + x;
                    auto a = smooth[i - 1], b = smooth[i + 1], c = smooth[i - w], d = smooth[i + w];
                    int16_t dx[]{int16_t(int(b.b) - a.b), int16_t(int(b.g) - a.g), int16_t(int(b.r) - a.r)},
                        dy[]{int16_t(int(d.b) - c.b), int16_t(int(d.g) - c.g), int16_t(int(d.r) - c.r)};
                    record(i, dx, dy);
                }
            });
    };
    if (metric.identity)
        gradients.template operator()<false>();
    else
        gradients.template operator()<true>();
    result.timings.gradientMs = elapsed(part, Clock::now());
    part = Clock::now();
    const int high = std::max(2, int(std::lround(14 - std::clamp(settings.detail, 0.f, 100.f) * .12f))) * 2,
              low = std::max(3, high / 4);
    std::vector<uint8_t> candidates(n), strong(n);
    std::vector<std::vector<uint32_t>> activeRows(h);
    if (h > 2)
        rows(1, h - 1, [&](uint32_t y) {
            for (uint32_t x = 1; x + 1 < w; ++x) {
                size_t i = size_t(y) * w + x;
                int value = magnitude[i];
                if (value < low)
                    continue;
                int d = direction[i], offset = d == 0   ? 1
                                               : d == 1 ? int(w) + 1
                                               : d == 2 ? int(w)
                                                        : int(w) - 1;
                if (value < magnitude[i - offset] || value <= magnitude[i + offset])
                    continue;
                // The local linear-ramp response must fall on both sides. This rejects
                // steady shading, including ramps brighter than the contrast threshold.
                int nx = d == 0 || d == 1 ? 1 : d == 3 ? -1 : 0, ny = d == 0 ? 0 : 1;
                if (int(x) - nx * 2 < 0 || int(x) + nx * 2 >= int(w) || int(y) - ny * 2 < 0 ||
                    int(y) + ny * 2 >= int(h))
                    continue;
                size_t before = size_t(int(y) - ny * 2) * w + int(x) - nx * 2,
                       after = size_t(int(y) + ny * 2) * w + int(x) + nx * 2;
                int sharpness = value * 2 - int(magnitude[before]) - int(magnitude[after]);
                if (sharpness < low)
                    continue;
                int rawJump =
                    std::max(contrast(image.pixels[i], image.pixels[size_t(int(y) - ny) * w + int(x) - nx]),
                             contrast(image.pixels[i], image.pixels[size_t(int(y) + ny) * w + int(x) + nx]));
                candidates[i] = 1;
                activeRows[y].push_back(uint32_t(i));
                strong[i] = uint8_t(value >= high && sharpness >= value && rawJump >= high / 2);
                if (!strong[i]) {
                    int nx3 = nx * 3, ny3 = ny * 3;
                    if (int(x) - nx3 >= 0 && int(x) + nx3 < int(w) && int(y) - ny3 >= 0 &&
                        int(y) + ny3 < int(h)) {
                        auto a = image.pixels[before], b = image.pixels[after];
                        auto aa = image.pixels[size_t(int(y) - ny3) * w + int(x) - nx3],
                             bb = image.pixels[size_t(int(y) + ny3) * w + int(x) + nx3];
                        strong[i] = uint8_t(sharpness >= value && rawJump >= 3 &&
                                            contrast(a, b) >= std::max(3, high / 4) && contrast(a, aa) <= 1 &&
                                            contrast(b, bb) <= 1);
                    }
                }
            }
        });
    result.timings.suppressMs = elapsed(part, Clock::now());
    part = Clock::now();
    if (!settings.connectGaps) {
        // Each pixel owns eight adjacency bits. This replaces the per-edge records,
        // linked lists and full-width head indices when no gap bridges are needed.
        std::vector<uint8_t> links(n), degree(n);
        std::vector<uint32_t> counts(h);
        static const auto population = [] {
            std::array<uint8_t, 256> t{};
            for (int i = 1; i < 256; ++i)
                t[i] = t[i >> 1] + (i & 1);
            return t;
        }();
        const int offsets[]{1, int(w), int(w) + 1, int(w) - 1, -1, -int(w), -int(w) - 1, -int(w) + 1};
        const int opposite[]{4, 5, 6, 7, 0, 1, 2, 3};
        const Point steps[]{{1, 0}, {0, 1}, {1, 1}, {-1, 1}, {-1, 0}, {0, -1}, {-1, -1}, {1, -1}};
        rows(1, h > 1 ? h - 1 : 1, [&](uint32_t y) {
            uint32_t count = 0;
            for (auto i : activeRows[y]) {
                auto east = candidates[i + 1], south = candidates[i + w], west = candidates[i - 1],
                     north = candidates[i - w];
                uint8_t mask = east | (south << 1) | ((candidates[i + w + 1] && !east && !south) << 2) |
                               ((candidates[i + w - 1] && !west && !south) << 3) | (west << 4) |
                               (north << 5) | ((candidates[i - w - 1] && !west && !north) << 6) |
                               ((candidates[i - w + 1] && !east && !north) << 7);
                links[i] = mask;
                degree[i] = population[mask];
                count += degree[i];
            }
            counts[y] = count;
        });
        uint64_t edgeCount = 0;
        for (auto count : counts)
            edgeCount += count;
        edgeCount /= 2;
        if (edgeCount > 3000000)
            throw std::runtime_error("Too many edges: lower sensitivity or analysis resolution");
        result.timings.graphMs = elapsed(part, Clock::now());
        result.timings.detectMs = elapsed(stage, Clock::now());
        stage = Clock::now();
        Stroke working;
        result.strokes.reserve(size_t(edgeCount / 12));
        auto point = [&](uint32_t vertex) { return Point{float(vertex % w), float(vertex / w)}; };
        auto trace = [&](uint32_t start, unsigned heading) {
            auto &stroke = working;
            stroke.points.clear();
            stroke.points.reserve(16);
            stroke.closed = false;
            stroke.anchored = degree[start] > 2;
            stroke.contourLength = 0;
            Point current = point(start);
            stroke.points.push_back(current);
            uint32_t vertex = start, samples = 0;
            uint64_t response = 0;
            bool convincing = false;
            while (links[vertex] & (1u << heading)) {
                auto next = uint32_t(int(vertex) + offsets[heading]);
                links[vertex] &= uint8_t(~(1u << heading));
                links[next] &= uint8_t(~(1u << opposite[heading]));
                convincing |= strong[vertex] || strong[next];
                response += (magnitude[vertex] + magnitude[next]) / 2;
                ++samples;
                stroke.contourLength +=
                    heading == 2 || heading == 3 || heading == 6 || heading == 7 ? 1.41421356f : 1.f;
                current.x += steps[heading].x;
                current.y += steps[heading].y;
                auto &points = stroke.points;
                if (points.size() >= 2) {
                    auto a = points[points.size() - 2], b = points.back();
                    if ((b.x - a.x) * (current.y - b.y) == (b.y - a.y) * (current.x - b.x))
                        points.back() = current;
                    else
                        points.push_back(current);
                } else
                    points.push_back(current);
                vertex = next;
                if (vertex == start) {
                    stroke.closed = true;
                    break;
                }
                if (degree[vertex] != 2 || !links[vertex])
                    break;
                heading = std::countr_zero(unsigned(links[vertex]));
            }
            if (convincing && stroke.points.size() >= 2 &&
                stroke.contourLength >= settings.minContourLength) {
                stroke.importance = float(response) / std::max(1u, samples) *
                                    std::sqrt(std::min(stroke.contourLength, 256.f)) *
                                    (stroke.closed ? 1.2f : 1.f);
                result.vertices += stroke.points.size();
                result.strokes.push_back(std::move(stroke));
                working = {};
            } else if (convincing)
                ++result.discardedContours;
        };
        for (uint32_t y = 1; y + 1 < h; ++y) {
            if (!(y & 127))
                check();
            for (auto i : activeRows[y])
                for (unsigned heading = 0; heading < 4; ++heading) {
                    if (!(links[i] & (1u << heading)))
                        continue;
                    auto next = uint32_t(int(i) + offsets[heading]);
                    if (degree[i] != 2)
                        trace(i, heading);
                    else if (degree[next] != 2)
                        trace(next, opposite[heading]);
                }
        }
        for (uint32_t y = 1; y + 1 < h; ++y)
            for (auto i : activeRows[y])
                while (links[i])
                    trace(i, std::countr_zero(unsigned(links[i])));
    } else {
        std::vector<Edge> edges;
        edges.reserve(std::min(n, size_t(300000)));
        std::vector<uint32_t> heads(n, UINT32_MAX);
        std::vector<uint8_t> degree(n);
        auto add = [&](uint32_t from, uint32_t to) {
            auto index = uint32_t(edges.size());
            edges.push_back({from, to, heads[from], heads[to],
                             uint16_t((magnitude[from] + magnitude[to]) / 2),
                             uint8_t(strong[from] || strong[to] ? 1 : 0)});
            heads[from] = heads[to] = index;
            ++degree[from];
            ++degree[to];
        };
        for (uint32_t y = 1; y + 1 < h; ++y) {
            if (!(y & 127))
                check();
            for (auto i : activeRows[y]) {
                if (candidates[i + 1])
                    add(i, i + 1);
                if (candidates[i + w])
                    add(i, i + w);
                // Diagonal connections are needed at a slanted edge, but not when an
                // orthogonal route already joins the same three pixels (tiny triangles).
                if (candidates[i + w + 1] && !candidates[i + 1] && !candidates[i + w])
                    add(i, i + w + 1);
                if (candidates[i + w - 1] && !candidates[i - 1] && !candidates[i + w])
                    add(i, i + w - 1);
            }
            if (edges.size() > 3000000)
                throw std::runtime_error("Too many edges: lower sensitivity or analysis resolution");
        }
        result.timings.graphMs = elapsed(part, Clock::now());
        part = Clock::now();
        if (settings.connectGaps) {
            std::vector<uint32_t> endpoints;
            for (const auto &edge : edges) {
                if (degree[edge.from] == 1)
                    endpoints.push_back(edge.from);
                if (degree[edge.to] == 1)
                    endpoints.push_back(edge.to);
            }
            std::vector<uint32_t> proposals(endpoints.size(), UINT32_MAX);
            auto outward = [&](uint32_t vertex) {
                auto end = vertex;
                uint32_t previous = UINT32_MAX;
                for (int step = 0; step < 4; ++step) {
                    auto edge = heads[end];
                    if (edge == previous)
                        edge = edges[edge].from == end ? edges[edge].nextFrom : edges[edge].nextTo;
                    if (edge == UINT32_MAX)
                        break;
                    auto next = edges[edge].from == end ? edges[edge].to : edges[edge].from;
                    previous = edge;
                    end = next;
                    if (degree[end] != 2)
                        break;
                }
                float dx = float(vertex % w) - float(end % w), dy = float(vertex / w) - float(end / w);
                float length = std::hypot(dx, dy);
                return Point{length ? dx / length : 0, length ? dy / length : 0};
            };
            contour::detail::parallelRows(0, uint32_t(endpoints.size()), n, [&](uint32_t index) {
                auto from = endpoints[index];
                auto tangent = outward(from);
                uint32_t target = UINT32_MAX;
                float bestDistance = 25;
                for (int distance = 2; distance <= 4; ++distance)
                    for (int side = -1; side <= 1; ++side) {
                        int x = int(from % w + tangent.x * distance - tangent.y * side + .5f);
                        int y = int(from / w + tangent.y * distance + tangent.x * side + .5f);
                        if (x < 2 || y < 2 || x + 2 >= int(w) || y + 2 >= int(h))
                            continue;
                        auto to = uint32_t(y) * w + uint32_t(x);
                        if (to == from || degree[to] != 1)
                            continue;
                        float dx = float(x) - float(from % w), dy = float(y) - float(from / w);
                        float squared = dx * dx + dy * dy;
                        if (squared <= 2 || squared > 20 || squared >= bestDistance)
                            continue;
                        float length = std::sqrt(squared);
                        if ((dx * tangent.x + dy * tangent.y) < length * .92f)
                            continue;
                        auto other = outward(to);
                        if (dx * other.x + dy * other.y > -length * .92f)
                            continue;
                        // A bridge must still cross a color transition, not empty space.
                        bool supported = true;
                        int nx = int(std::lround(-dy / length)), ny = int(std::lround(dx / length));
                        auto sideColors = [&](uint32_t vertex) {
                            int vx = int(vertex % w), vy = int(vertex / w);
                            auto a = image.pixels[size_t(vy + ny) * w + vx + nx];
                            auto b = image.pixels[size_t(vy - ny) * w + vx - nx];
                            return std::array<int, 3>{int(a.r) - b.r, int(a.g) - b.g, int(a.b) - b.b};
                        };
                        auto colorFrom = sideColors(from), colorTo = sideColors(to);
                        int agreement = 0, normFrom = 0, normTo = 0;
                        for (int channel = 0; channel < 3; ++channel) {
                            agreement += colorFrom[channel] * colorTo[channel];
                            normFrom += colorFrom[channel] * colorFrom[channel];
                            normTo += colorTo[channel] * colorTo[channel];
                        }
                        if (agreement <= 0 || float(agreement) * agreement < .5f * normFrom * normTo)
                            continue;
                        for (int step = 1; step < int(std::ceil(length)); ++step) {
                            int mx = int(from % w + dx * step / length + .5f);
                            int my = int(from / w + dy * step / length + .5f);
                            auto a = image.pixels[size_t(my + ny) * w + mx + nx];
                            auto b = image.pixels[size_t(my - ny) * w + mx - nx];
                            if (contrast(a, b) < 3 || magnitude[size_t(my) * w + mx] <
                                                          std::min(magnitude[from], magnitude[to]) / 4) {
                                supported = false;
                                break;
                            }
                        }
                        if (supported) {
                            target = to;
                            bestDistance = squared;
                        }
                    }
                proposals[index] = target;
            });
            check();
            for (size_t index = 0; index < endpoints.size(); ++index) {
                auto from = endpoints[index], target = proposals[index];
                if (target != UINT32_MAX && degree[from] == 1 && degree[target] == 1) {
                    add(from, target);
                    ++result.joinedGaps;
                }
            }
        }
        result.timings.linkMs = elapsed(part, Clock::now());
        result.timings.detectMs = elapsed(stage, Clock::now());
        stage = Clock::now();
        auto point = [&](uint32_t v) { return Point{float(v % w), float(v / w)}; };
        auto nextAt = [&](uint32_t vertex, uint32_t excluded) {
            for (auto j = heads[vertex]; j != UINT32_MAX;
                 j = edges[j].from == vertex ? edges[j].nextFrom : edges[j].nextTo)
                if (j != excluded)
                    return j;
            return UINT32_MAX;
        };
        Stroke working;
        result.strokes.reserve(edges.size() / 12);
        auto trace = [&](uint32_t first, uint32_t start) {
            auto &stroke = working;
            stroke.points.clear();
            stroke.points.reserve(16);
            stroke.closed = false;
            stroke.contourLength = 0;
            uint64_t response = 0;
            uint32_t samplesCount = 0;
            stroke.anchored = degree[start] > 2;
            uint32_t vertex = start, edge = first;
            bool strong = false;
            Point current = point(vertex);
            stroke.points.push_back(current);
            for (size_t count = 0; count <= edges.size(); ++count) {
                auto &e = edges[edge];
                if (e.flags & 2)
                    break;
                e.flags |= 2;
                strong |= (e.flags & 1) != 0;
                auto delta = e.to > e.from ? e.to - e.from : e.from - e.to;
                stroke.contourLength += delta == 1 || delta == w ? 1.f
                                        : delta == w - 1 || delta == w + 1
                                            ? 1.41421356f
                                            : std::hypot(float(e.to % w) - float(e.from % w),
                                                         float(e.to / w) - float(e.from / w));
                response += e.response;
                ++samplesCount;
                auto nextVertex = e.from == vertex ? e.to : e.from;
                int step = int(nextVertex) - int(vertex);
                if (step == 1)
                    current.x += 1;
                else if (step == -1)
                    current.x -= 1;
                else if (step == int(w))
                    current.y += 1;
                else if (step == -int(w))
                    current.y -= 1;
                else if (step == int(w) + 1) {
                    current.x += 1;
                    current.y += 1;
                } else if (step == int(w) - 1) {
                    current.x -= 1;
                    current.y += 1;
                } else if (step == -int(w) + 1) {
                    current.x += 1;
                    current.y -= 1;
                } else if (step == -int(w) - 1) {
                    current.x -= 1;
                    current.y -= 1;
                } else
                    current = point(nextVertex);
                vertex = nextVertex;
                auto p = current;
                auto &samples = stroke.points;
                if (samples.size() >= 2) {
                    auto a = samples[samples.size() - 2], b = samples.back();
                    if ((b.x - a.x) * (p.y - b.y) == (b.y - a.y) * (p.x - b.x))
                        samples.back() = p;
                    else
                        samples.push_back(p);
                } else
                    samples.push_back(p);
                if (vertex == start) {
                    stroke.closed = true;
                    break;
                }
                if (degree[vertex] != 2)
                    break;
                auto next = nextAt(vertex, edge);
                if (next == UINT32_MAX || edges[next].flags & 2)
                    break;
                edge = next;
            }
            if (strong && stroke.points.size() >= 2 &&
                stroke.contourLength >= std::max(0.f, settings.minContourLength)) {
                stroke.importance = float(response) / std::max(1u, samplesCount) *
                                    std::sqrt(std::min(stroke.contourLength, 256.f)) *
                                    (stroke.closed ? 1.2f : 1.f);
                result.vertices += stroke.points.size();
                result.strokes.push_back(std::move(stroke));
                working = {};
            } else if (strong)
                ++result.discardedContours;
        };
        for (uint32_t i = 0; i < edges.size(); ++i) {
            if (!(i & 8191))
                check();
            auto &e = edges[i];
            if (e.flags & 2)
                continue;
            if (degree[e.from] != 2)
                trace(i, e.from);
            else if (degree[e.to] != 2)
                trace(i, e.to);
        }
        for (uint32_t i = 0; i < edges.size(); ++i)
            if (!(edges[i].flags & 2))
                trace(i, edges[i].from);
    }
    std::stable_sort(result.strokes.begin(), result.strokes.end(),
                     [](const Stroke &a, const Stroke &b) { return a.importance > b.importance; });
    result.timings.traceMs = elapsed(stage, Clock::now());
    stage = Clock::now();
    const uint32_t fitStride = (uint32_t(result.strokes.size()) + 3) / 4;
    detail::parallelRows(0, fitStride * 4, n, [&](uint32_t task) {
        uint32_t i = (task % fitStride) * 4 + task / fitStride;
        if (i >= result.strokes.size())
            return;
        if (!(i & 255))
            check();
        auto &stroke = result.strokes[i];
        fitStroke(stroke, std::clamp(settings.geometryTolerance, 0.f, 2.f), settings.pointBudget);
        stroke.points.clear();
        stroke.points.shrink_to_fit();
    });
    combineBorders(result.strokes, image);
    result.timings.fitMs = elapsed(stage, Clock::now());
    result.timings.totalMs = elapsed(begin, Clock::now());
    return result;
}
} // namespace contour
