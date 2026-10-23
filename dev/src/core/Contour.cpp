#include "Contour.h"
#include "ColorMetric.h"
#include "Parallel.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#define CONTOUR_SSE2 1
#endif

namespace contour {
namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}
void check(const Cancel &cancel) {
    if (cancel && cancel())
        throw std::runtime_error("Operation canceled");
}
int difference(Pixel a, Pixel b) {
    return std::max({std::abs(int(a.r) - b.r), std::abs(int(a.g) - b.g), std::abs(int(a.b) - b.b)});
}
// Composite alpha before comparison. Transparent RGB has no visible edges.
Pixel visible(Pixel p) {
    auto c = [a = int(p.a)](uint8_t v) { return uint8_t((int(v) * a + 128 * (255 - a) + 127) / 255); };
    return {c(p.b), c(p.g), c(p.r), 255};
}
void detectRow(const Pixel *a, const Pixel *b, uint8_t *out, uint32_t count,
               const detail::ColorMetric &metric) {
    if (!metric.identity) {
        for (uint32_t x = 0; x < count; ++x)
            out[x] = uint8_t(metric.contrast(a[x], b[x]));
        return;
    }
    uint32_t x = 0;
#ifdef CONTOUR_SSE2
    for (; x + 4 <= count; x += 4) {
        auto av = _mm_loadu_si128(reinterpret_cast<const __m128i *>(a + x));
        auto bv = _mm_loadu_si128(reinterpret_cast<const __m128i *>(b + x));
        auto diff = _mm_or_si128(_mm_subs_epu8(av, bv), _mm_subs_epu8(bv, av));
        diff = _mm_max_epu8(diff, _mm_srli_epi32(diff, 8));
        diff = _mm_max_epu8(diff, _mm_srli_epi32(diff, 16));
        alignas(16) uint32_t maxima[4];
        _mm_store_si128(reinterpret_cast<__m128i *>(maxima), diff);
        out[x] = uint8_t(maxima[0]);
        out[x + 1] = uint8_t(maxima[1]);
        out[x + 2] = uint8_t(maxima[2]);
        out[x + 3] = uint8_t(maxima[3]);
    }
#endif
    for (; x < count; ++x)
        out[x] = uint8_t(difference(a[x], b[x]));
}
void filterRow(const Pixel *row, Pixel *output, uint32_t width, int threshold) {
    uint32_t x = 1;
#ifdef CONTOUR_SSE2
    auto zero = _mm_setzero_si128(), tol = _mm_set1_epi8(char(threshold)), byte = _mm_set1_epi32(255);
    for (; x + 4 < width; x += 4) {
        auto center = _mm_loadu_si128(reinterpret_cast<const __m128i *>(row + x));
        auto low = _mm_slli_epi16(_mm_unpacklo_epi8(center, zero), 2),
             high = _mm_slli_epi16(_mm_unpackhi_epi8(center, zero), 2);
        for (auto offset : {-1, 1, -int(width), int(width)}) {
            auto neighbor = _mm_loadu_si128(reinterpret_cast<const __m128i *>(row + x + offset));
            auto d = _mm_or_si128(_mm_subs_epu8(center, neighbor), _mm_subs_epu8(neighbor, center));
            auto m = _mm_cmpeq_epi8(_mm_subs_epu8(d, tol), zero);
            m = _mm_and_si128(m, _mm_srli_epi32(m, 8));
            m = _mm_and_si128(m, _mm_srli_epi32(m, 16));
            m = _mm_cmpeq_epi32(_mm_and_si128(m, byte), byte);
            auto q = _mm_or_si128(_mm_and_si128(m, neighbor), _mm_andnot_si128(m, center));
            low = _mm_add_epi16(low, _mm_unpacklo_epi8(q, zero));
            high = _mm_add_epi16(high, _mm_unpackhi_epi8(q, zero));
        }
        auto filtered = _mm_packus_epi16(_mm_srli_epi16(low, 3), _mm_srli_epi16(high, 3));
        _mm_storeu_si128(reinterpret_cast<__m128i *>(output + x), filtered);
    }
#endif
    for (; x + 1 < width; ++x) {
        auto p = row[x];
        int b = p.b * 4, g = p.g * 4, r = p.r * 4;
        for (auto offset : {-1, 1, -int(width), int(width)}) {
            auto q = (row + x)[offset];
            if (difference(p, q) > threshold)
                q = p;
            b += q.b;
            g += q.g;
            r += q.r;
        }
        output[x] = {uint8_t(b / 8), uint8_t(g / 8), uint8_t(r / 8), 255};
    }
}
void appendCorner(std::vector<Point> &points, Point p) {
    if (points.size() >= 2) {
        auto a = points[points.size() - 2], b = points.back();
        if ((b.x - a.x) * (p.y - b.y) == (b.y - a.y) * (p.x - b.x)) {
            points.back() = p;
            return;
        }
    }
    points.push_back(p);
}
struct DSU {
    std::vector<uint32_t> parent, size;
    std::vector<float> internal;
    DSU(size_t count) : parent(count), size(count, 1), internal(count) {
        std::iota(parent.begin(), parent.end(), 0u);
    }
    uint32_t root(uint32_t i) {
        while (parent[i] != i) {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        return i;
    }
    uint32_t join(uint32_t a, uint32_t b, float weight) {
        if (size[a] < size[b])
            std::swap(a, b);
        parent[b] = a;
        size[a] += size[b];
        internal[a] = std::max({weight, internal[a], internal[b]});
        return a;
    }
};
struct Edge {
    uint32_t from, to, region;
    uint8_t direction;
    bool used{};
    uint32_t peer{UINT32_MAX}, boundary{UINT32_MAX}, next{UINT32_MAX};
    bool reversed{};
};
struct UniqueEdge {
    uint32_t from, to, right, left, half;
    uint32_t nextFrom{UINT32_MAX}, nextTo{UINT32_MAX};
    bool used{};
};
} // namespace

Image resize(const Image &input, uint32_t longSide) {
    if (!input.width || !input.height || input.pixels.size() != size_t(input.width) * input.height)
        throw std::invalid_argument("Invalid image");
    if (!longSide || std::max(input.width, input.height) <= longSide)
        return input;
    double scale = double(longSide) / std::max(input.width, input.height);
    Image out{std::max(1u, uint32_t(std::round(input.width * scale))),
              std::max(1u, uint32_t(std::round(input.height * scale))),
              {}};
    out.pixels.resize(size_t(out.width) * out.height);
    struct Sample {
        uint32_t a, b;
        uint16_t weight;
    };
    std::vector<Sample> columns(out.width);
    for (uint32_t x = 0; x < out.width; ++x) {
        double sx = std::clamp((x + .5) * input.width / out.width - .5, 0.0, double(input.width - 1));
        auto x0 = uint32_t(sx);
        columns[x] = {x0, std::min(x0 + 1, input.width - 1), uint16_t(std::round((sx - x0) * 256))};
    }
    detail::parallelRows(0, out.height, out.pixels.size(), [&](uint32_t y) {
        double sy = std::clamp((y + 0.5) * input.height / out.height - 0.5, 0.0, double(input.height - 1));
        auto y0 = uint32_t(sy), y1 = std::min(y0 + 1, input.height - 1);
        double fy = sy - y0;
        const int wy = int(std::round(fy * 256));
#ifdef CONTOUR_SSE2
        const auto wyVector = _mm_set1_epi16(short(wy)), iyVector = _mm_set1_epi16(short(256 - wy));
#endif
        for (uint32_t x = 0; x < out.width; ++x) {
            auto sample = columns[x];
            auto x0 = sample.a, x1 = sample.b;
            double fx = sample.weight / 256.;
            // Resample premultiplied colors, then return straight BGRA.
            Pixel p[4]{
                input.pixels[size_t(y0) * input.width + x0], input.pixels[size_t(y0) * input.width + x1],
                input.pixels[size_t(y1) * input.width + x0], input.pixels[size_t(y1) * input.width + x1]};
#ifdef CONTOUR_SSE2
            if (p[0].a == 255 && p[1].a == 255 && p[2].a == 255 && p[3].a == 255) {
                auto zero = _mm_setzero_si128();
                __m128i v[4];
                for (int j = 0; j < 4; ++j) {
                    uint32_t packed;
                    std::memcpy(&packed, &p[j], 4);
                    v[j] = _mm_unpacklo_epi8(_mm_cvtsi32_si128(int(packed)), zero);
                }
                auto wx = _mm_set1_epi16(short(sample.weight)),
                     ix = _mm_set1_epi16(short(256 - sample.weight));
                auto top = _mm_srli_epi16(_mm_add_epi16(_mm_mullo_epi16(v[0], ix), _mm_mullo_epi16(v[1], wx)),
                                          8),
                     bottom = _mm_srli_epi16(
                         _mm_add_epi16(_mm_mullo_epi16(v[2], ix), _mm_mullo_epi16(v[3], wx)), 8);
                auto blended = _mm_srli_epi16(
                    _mm_add_epi16(_mm_mullo_epi16(top, iyVector), _mm_mullo_epi16(bottom, wyVector)), 8);
                uint32_t packed = uint32_t(_mm_cvtsi128_si32(_mm_packus_epi16(blended, zero)));
                std::memcpy(&out.pixels[size_t(y) * out.width + x], &packed, 4);
                continue;
            }
#endif
            double weights[4]{(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy}, a = 0, b = 0,
                                                                                           g = 0, r = 0;
            for (int k = 0; k < 4; ++k) {
                a += p[k].a * weights[k];
                b += p[k].b * p[k].a * weights[k];
                g += p[k].g * p[k].a * weights[k];
                r += p[k].r * p[k].a * weights[k];
            }
            out.pixels[size_t(y) * out.width + x] =
                a > 0 ? Pixel{uint8_t(std::round(b / a)), uint8_t(std::round(g / a)),
                              uint8_t(std::round(r / a)), uint8_t(std::round(a))}
                      : Pixel{0, 0, 0, 0};
        }
    });
    return out;
}

static Result processRegions(const Image &input, const Settings &settings, const Cancel &cancel) {
    const auto start = Clock::now();
    const detail::ColorMetric metric(settings);
    if (!std::isfinite(settings.detail) || settings.analysisLongSide < 64 || settings.analysisLongSide > 8192)
        throw std::invalid_argument("Invalid settings");
    if (!std::isfinite(settings.minContourLength) || settings.minContourLength < 0 ||
        settings.minContourLength > 1024)
        throw std::invalid_argument("Invalid minimum contour length");
    auto work = resize(input, settings.analysisLongSide);
    Result out;
    out.sourceWidth = input.width;
    out.sourceHeight = input.height;
    out.width = work.width;
    out.height = work.height;
    const uint32_t w = work.width, h = work.height;
    const size_t n = size_t(w) * h;
    if (n > 67108864)
        throw std::invalid_argument("Analysis exceeds 64 megapixels");
    auto t = Clock::now();
    out.timings.resizeMs = ms(start, t);
    for (auto &p : work.pixels)
        if (p.a != 255)
            p = visible(p);
    check(cancel);
    const float detail = std::clamp(settings.detail, 0.f, 100.f);
    const int threshold = std::max(2, int(std::round(14 - detail * 0.12f)));
    if (settings.denoise && w > 2 && h > 2) {
        auto filtered = work.pixels;
        // Edge-aware five-tap filter: never blur across a contrast above 2x threshold.
        for (uint32_t y = 1; y + 1 < h; ++y) {
            if ((y & 127) == 0)
                check(cancel);
            filterRow(work.pixels.data() + size_t(y) * w, filtered.data() + size_t(y) * w, w, threshold * 2);
        }
        work.pixels = std::move(filtered);
    }
    auto next = Clock::now();
    out.timings.filterMs = ms(t, next);
    t = next;
    std::vector<uint8_t> horizontal(n), vertical(n);
    detail::parallelRows(0, h, n, [&](uint32_t y) {
        auto row = work.pixels.data() + size_t(y) * w;
        if (w > 1)
            detectRow(row + 1, row, horizontal.data() + size_t(y) * w + 1, w - 1, metric);
        if (y)
            detectRow(row, row - w, vertical.data() + size_t(y) * w, w, metric);
    });
    check(cancel);
    next = Clock::now();
    out.timings.detectMs = ms(t, next);
    t = next;
    // Felzenszwalb-Huttenlocher adaptive component thresholds (independent implementation).
    // 256 buckets replace comparison sorting. Smooth shading can remain one region,
    // while an edge in an otherwise flat UI receives a much stricter threshold.
    DSU dsu(n);
    out.labels.resize(n);
    std::array<uint32_t, 256> counts{};
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            size_t i = size_t(y) * w + x;
            if (x) {
                if (horizontal[i])
                    ++counts[horizontal[i]];
                else {
                    // This pixel has not yet participated in a union. Append it to
                    // the left run without finding its already-known singleton root.
                    auto left = dsu.root(uint32_t(i - 1));
                    dsu.parent[i] = left;
                    ++dsu.size[left];
                }
            }
            if (y) {
                if (vertical[i])
                    ++counts[vertical[i]];
                else {
                    auto a = dsu.root(uint32_t(i)), b = dsu.root(uint32_t(i - w));
                    if (a != b)
                        dsu.join(a, b, 0);
                }
            }
        }
    std::array<uint32_t, 257> offsets{};
    for (size_t i = 0; i < 256; ++i)
        offsets[i + 1] = offsets[i] + counts[i];
    auto cursors = offsets;
    std::vector<uint32_t> sorted(offsets[256]);
    for (uint32_t y = 0; y < h; ++y) {
        if ((y & 127) == 0)
            check(cancel);
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t i = y * w + x;
            if (x && horizontal[i])
                sorted[cursors[horizontal[i]]++] = i * 2;
            if (y && vertical[i])
                sorted[cursors[vertical[i]]++] = i * 2 + 1;
        }
    }
    out.timings.smoothMs = ms(t, Clock::now());
    auto part = Clock::now();
    const float k = float((120 + (100 - detail) * 7) * std::max(1., double(w) * h / (2048. * 1152.)));
    for (uint32_t weight = 0; weight < 256; ++weight) {
        check(cancel);
        for (uint32_t j = offsets[weight]; j < offsets[weight + 1]; ++j) {
            auto encoded = sorted[j], pixel = encoded / 2, neighbor = pixel - ((encoded & 1) ? w : 1);
            if (dsu.parent[pixel] == dsu.parent[neighbor])
                continue;
            uint32_t a = dsu.root(pixel), b = dsu.root(neighbor);
            if (a != b && (weight - dsu.internal[a]) * dsu.size[a] <= k &&
                (weight - dsu.internal[b]) * dsu.size[b] <= k)
                dsu.join(a, b, float(weight));
        }
    }
    // Small components join their least-contrasting neighbor in the same sorted order.
    // Size only identifies candidates; never erase a coherent contrasting small object.
    const uint32_t minimumArea = 16;
    // Component sizes only grow. Edges between two already-large components can
    // never participate in the small-component cleanup, even after later unions.
    std::vector<uint8_t> initiallySmall(n);
    detail::parallelRows(0, h, n, [&](uint32_t y) {
        for (size_t i = size_t(y) * w; i < size_t(y + 1) * w; ++i) {
            auto root = uint32_t(i);
            while (dsu.parent[root] != root)
                root = dsu.parent[root];
            initiallySmall[i] = uint8_t(dsu.size[root] < minimumArea);
        }
    });
    for (uint32_t weight = 0; minimumArea > 1 && weight < 256; ++weight) {
        check(cancel);
        for (uint32_t j = offsets[weight]; j < offsets[weight + 1]; ++j) {
            auto encoded = sorted[j], pixel = encoded / 2, neighbor = pixel - ((encoded & 1) ? w : 1);
            if (!initiallySmall[pixel] && !initiallySmall[neighbor])
                continue;
            if (dsu.parent[pixel] == dsu.parent[neighbor])
                continue;
            uint32_t a = dsu.root(pixel), b = dsu.root(neighbor);
            auto unstable = [&](uint32_t region) {
                return dsu.size[region] < minimumArea &&
                       ((settings.denoise && dsu.size[region] == 1) ||
                        (weight <= dsu.internal[region] + threshold &&
                         !(dsu.size[region] >= 2 && dsu.internal[region] <= 1 && weight > 2)));
            };
            if (a != b && (unstable(a) || unstable(b)))
                dsu.join(a, b, float(weight));
        }
    }
    out.timings.gradientMs = ms(part, Clock::now());
    part = Clock::now();
    std::vector<uint32_t> compact(dsu.parent.size(), UINT32_MAX);
    std::vector<std::array<uint64_t, 3>> colors;
    std::vector<float> variation;
    for (size_t i = 0; i < n; ++i) {
        uint32_t root = dsu.root(uint32_t(i));
        auto &id = compact[root];
        if (id == UINT32_MAX) {
            id = uint32_t(out.regions.size());
            out.regions.push_back(Region{id});
            colors.push_back({});
            variation.push_back(dsu.internal[root]);
        }
        out.labels[i] = id;
        auto &region = out.regions[id];
        ++region.area;
        colors[id][0] += work.pixels[i].b;
        colors[id][1] += work.pixels[i].g;
        colors[id][2] += work.pixels[i].r;
    }
    for (auto &region : out.regions)
        region.color = {uint8_t(colors[region.id][0] / region.area),
                        uint8_t(colors[region.id][1] / region.area),
                        uint8_t(colors[region.id][2] / region.area), 255};
    // Remove weak seams introduced by scanline noise or smooth quantized gradients.
    // Use the whole common border, not one weak contact that could leak across a silhouette.
    out.timings.suppressMs = ms(part, Clock::now());
    part = Clock::now();
    struct BorderContrast {
        uint64_t sum{};
        uint32_t count{};
        uint8_t maximum{};
    };
    std::unordered_map<uint64_t, BorderContrast> contrasts;
    contrasts.reserve(out.regions.size() * 4);
    auto border = [&](uint32_t a, uint32_t b, uint8_t strength) {
        if (a == b)
            return;
        if (a > b)
            std::swap(a, b);
        auto &c = contrasts[(uint64_t(a) << 32) | b];
        c.sum += strength;
        ++c.count;
        c.maximum = std::max(c.maximum, strength);
    };
    for (uint32_t y = 0; y < h; ++y) {
        if ((y & 127) == 0)
            check(cancel);
        for (uint32_t x = 0; x < w; ++x) {
            size_t i = size_t(y) * w + x;
            if (x)
                border(out.labels[i], out.labels[i - 1], horizontal[i]);
            if (y)
                border(out.labels[i], out.labels[i - w], vertical[i]);
        }
    }
    DSU regionDsu(out.regions.size());
    for (size_t i = 0; i < out.regions.size(); ++i)
        regionDsu.size[i] = out.regions[i].area;
    for (const auto &[pair, c] : contrasts) {
        auto a0 = uint32_t(pair >> 32), b0 = uint32_t(pair);
        const float limit =
            std::max(2.f, std::min(float(threshold), 1.f + std::max(variation[a0], variation[b0]) * 1.5f));
        // Flat UI fields use a strict contrast threshold; textured/shaded regions
        // use their own internal variation. Region mean differences across a gradient
        // must not turn arbitrary segmentation seams into visible vector edges.
        bool flatField = variation[a0] <= 1 || variation[b0] <= 1;
        bool flatLeak = flatField && metric.contrast(out.regions[a0].color, out.regions[b0].color) >
                                         std::max(12, threshold * 2);
        if (!flatLeak && c.maximum <= std::max(4.f, limit * 2) && c.sum < limit * c.count) {
            uint32_t a = regionDsu.root(a0), b = regionDsu.root(b0);
            if (a != b)
                regionDsu.join(a, b, 0);
        }
    }
    std::vector<uint32_t> regionMap(out.regions.size(), UINT32_MAX);
    std::vector<Region> merged;
    std::vector<std::array<uint64_t, 3>> mergedColors;
    for (size_t i = 0; i < out.regions.size(); ++i) {
        auto root = regionDsu.root(uint32_t(i));
        auto &id = regionMap[root];
        if (id == UINT32_MAX) {
            id = uint32_t(merged.size());
            merged.push_back(Region{id});
            mergedColors.push_back({});
        }
        merged[id].area += out.regions[i].area;
        for (size_t c = 0; c < 3; ++c)
            mergedColors[id][c] += colors[i][c];
    }
    // One root lookup per region instead of a write-heavy lookup per pixel.
    std::vector<uint32_t> finalRegion(out.regions.size());
    for (size_t i = 0; i < finalRegion.size(); ++i)
        finalRegion[i] = regionMap[regionDsu.root(uint32_t(i))];
    detail::parallelRanges(0, h, n, [&](uint32_t first, uint32_t last) {
        for (size_t i = size_t(first) * w; i < size_t(last) * w; ++i)
            out.labels[i] = finalRegion[out.labels[i]];
    });
    for (auto &r : merged)
        r.color = {uint8_t(mergedColors[r.id][0] / r.area), uint8_t(mergedColors[r.id][1] / r.area),
                   uint8_t(mergedColors[r.id][2] / r.area), 255};
    out.regions = std::move(merged);
    out.timings.graphMs = ms(part, Clock::now());
    // Release the large pixel graph before constructing the sparse vector topology.
    dsu.parent.clear();
    dsu.parent.shrink_to_fit();
    dsu.size.clear();
    dsu.size.shrink_to_fit();
    dsu.internal.clear();
    dsu.internal.shrink_to_fit();
    sorted.clear();
    sorted.shrink_to_fit();
    compact.clear();
    compact.shrink_to_fit();
    next = Clock::now();
    out.timings.regionsMs = ms(t, next);
    t = next;
    // Directed lattice edges, region on the right. Each boundary belongs to both regions.
    std::vector<Edge> edges;
    edges.reserve(std::min(n, size_t(1000000)));
    auto add = [&](uint32_t fx, uint32_t fy, uint32_t tx, uint32_t ty, uint32_t region, uint8_t direction,
                   uint32_t other = UINT32_MAX) {
        uint32_t first = uint32_t(edges.size());
        edges.push_back({fy * (w + 1) + fx, ty * (w + 1) + tx, region, direction});
        if (other != UINT32_MAX) {
            edges[first].peer = first + 1;
            edges.push_back(
                {ty * (w + 1) + tx, fy * (w + 1) + fx, other, uint8_t((direction + 2) % 4), false, first});
        }
    };
    for (uint32_t y = 0; y < h; ++y) {
        if ((y & 127) == 0)
            check(cancel);
        for (uint32_t x = 0; x < w; ++x) {
            auto r = out.labels[size_t(y) * w + x];
            if (!y)
                add(x, y, x + 1, y, r, 0);
            if (!x)
                add(x, y + 1, x, y, r, 3);
            if (x + 1 == w)
                add(x + 1, y, x + 1, y + 1, r, 1);
            else {
                auto other = out.labels[size_t(y) * w + x + 1];
                if (other != r)
                    add(x + 1, y, x + 1, y + 1, r, 1, other);
            }
            if (y + 1 == h)
                add(x + 1, y + 1, x, y + 1, r, 2);
            else {
                auto other = out.labels[size_t(y + 1) * w + x];
                if (other != r)
                    add(x + 1, y + 1, x, y + 1, r, 2, other);
            }
        }
        if (edges.size() > 12000000)
            throw std::runtime_error("Too many contours: lower detail or analysis resolution");
    }
    const size_t cornerCount = size_t(w + 1) * (h + 1);
    std::vector<uint32_t> heads(cornerCount, UINT32_MAX), uniqueHeads(cornerCount, UINT32_MAX);
    std::vector<uint8_t> degree(cornerCount);
    std::vector<UniqueEdge> unique;
    unique.reserve(edges.size() / 2 + 2 * (w + h));
    for (uint32_t i = 0; i < edges.size(); ++i) {
        auto &edge = edges[i];
        edge.next = heads[edge.from];
        heads[edge.from] = i;
        if (edge.peer != UINT32_MAX && edge.peer < i)
            continue;
        uint32_t half = i;
        if (edge.peer != UINT32_MAX && edges[edge.peer].region < edge.region)
            half = edge.peer;
        auto &chosen = edges[half];
        uint32_t index = uint32_t(unique.size());
        unique.push_back({chosen.from, chosen.to, chosen.region,
                          chosen.peer == UINT32_MAX ? UINT32_MAX : edges[chosen.peer].region, half,
                          uniqueHeads[chosen.from], uniqueHeads[chosen.to]});
        uniqueHeads[chosen.from] = uniqueHeads[chosen.to] = index;
        ++degree[chosen.from];
        ++degree[chosen.to];
    }
    auto point = [&](uint32_t vertex) { return Point{float(vertex % (w + 1)), float(vertex / (w + 1))}; };
    auto otherAt = [&](uint32_t vertex, uint32_t excluded) {
        for (uint32_t j = uniqueHeads[vertex]; j != UINT32_MAX;
             j = unique[j].from == vertex ? unique[j].nextFrom : unique[j].nextTo)
            if (j != excluded)
                return j;
        return UINT32_MAX;
    };
    auto endpoint = [&](uint32_t vertex, uint32_t edge) {
        if (degree[vertex] != 2)
            return true;
        auto j = otherAt(vertex, edge);
        return j == UINT32_MAX || unique[j].right != unique[edge].right ||
               unique[j].left != unique[edge].left;
    };
    auto traceBoundary = [&](uint32_t first, bool reversed) {
        Boundary boundary;
        boundary.points.reserve(16);
        boundary.right = reversed ? unique[first].left : unique[first].right;
        boundary.left = reversed ? unique[first].right : unique[first].left;
        uint32_t index = uint32_t(out.boundaries.size()), edge = first;
        uint32_t startVertex = reversed ? unique[first].to : unique[first].from, vertex = startVertex;
        appendCorner(boundary.points, point(vertex));
        for (size_t steps = 0; steps <= unique.size(); ++steps) {
            auto &u = unique[edge];
            if (u.used)
                break;
            u.used = true;
            bool back = u.to == vertex;
            auto &half = edges[u.half];
            half.boundary = index;
            half.reversed = back;
            if (half.peer != UINT32_MAX) {
                edges[half.peer].boundary = index;
                edges[half.peer].reversed = !back;
            }
            vertex = back ? u.from : u.to;
            appendCorner(boundary.points, point(vertex));
            if (vertex == startVertex) {
                boundary.closed = true;
                boundary.anchored = endpoint(startVertex, first);
                break;
            }
            if (endpoint(vertex, edge))
                break;
            uint32_t following = otherAt(vertex, edge);
            if (following == UINT32_MAX || unique[following].used)
                break;
            edge = following;
        }
        out.boundaries.push_back(std::move(boundary));
    };
    // Open chains first, then closed rings. Endpoints are junctions and stay fixed during fit.
    for (uint32_t i = 0; i < unique.size(); ++i)
        if (!unique[i].used) {
            if (endpoint(unique[i].from, i))
                traceBoundary(i, false);
            else if (endpoint(unique[i].to, i))
                traceBoundary(i, true);
        }
    for (uint32_t i = 0; i < unique.size(); ++i)
        if (!unique[i].used)
            traceBoundary(i, false);
    for (uint32_t i = 0; i < edges.size(); ++i) {
        if ((i & 8191) == 0)
            check(cancel);
        if (edges[i].used)
            continue;
        Loop loop;
        loop.points.reserve(32);
        loop.boundaries.reserve(8);
        uint32_t e = i;
        bool closed = false;
        for (size_t steps = 0; steps <= edges.size(); ++steps) {
            auto &edge = edges[e];
            if (edge.used)
                break;
            edge.used = true;
            appendCorner(loop.points, {float(edge.from % (w + 1)), float(edge.from / (w + 1))});
            if (loop.boundaries.empty() || loop.boundaries.back().id != edge.boundary)
                loop.boundaries.push_back({edge.boundary, edge.reversed});
            if (edge.to == edges[i].from) {
                closed = true;
                break;
            }
            uint32_t chosen = UINT32_MAX;
            int rank = 10;
            for (uint32_t j = heads[edge.to]; j != UINT32_MAX; j = edges[j].next) {
                if (edges[j].used || edges[j].region != edge.region)
                    continue;
                int turn = (edges[j].direction + 4 - edge.direction) % 4;
                int priority = turn == 1 ? 0 : turn == 0 ? 1 : turn == 3 ? 2 : 3;
                if (priority < rank) {
                    rank = priority;
                    chosen = j;
                }
            }
            if (chosen == UINT32_MAX)
                break;
            e = chosen;
        }
        if (!closed)
            throw std::runtime_error("Contour topology failed");
        if (loop.boundaries.size() > 1 && loop.boundaries.front().id == loop.boundaries.back().id)
            loop.boundaries.erase(loop.boundaries.begin());
        // Remove only collinear vertices: exact shared boundaries, no gaps or overlaps.
        // Lossy independent simplification would break later region fills.
        std::vector<Point> simple;
        simple.reserve(loop.points.size());
        for (size_t j = 0; j < loop.points.size(); ++j) {
            auto a = loop.points[(j + loop.points.size() - 1) % loop.points.size()], b = loop.points[j],
                 c = loop.points[(j + 1) % loop.points.size()];
            if ((b.x - a.x) * (c.y - b.y) != (b.y - a.y) * (c.x - b.x))
                simple.push_back(b);
        }
        loop.points = std::move(simple);
        for (size_t j = 0; j < loop.points.size(); ++j) {
            auto a = loop.points[j], b = loop.points[(j + 1) % loop.points.size()];
            loop.signedArea += (double(a.x) * b.y - double(b.x) * a.y) / 2;
        }
        out.vertices += loop.points.size();
        out.regions[edges[i].region].loops.push_back(std::move(loop));
    }
    next = Clock::now();
    out.timings.traceMs = ms(t, next);
    t = next;
    check(cancel);
    fitGeometry(out, settings.geometryTolerance);
    for (auto &boundary : out.boundaries) {
        float length = 0;
        for (size_t i = 1; i < boundary.points.size(); ++i)
            length += std::hypot(boundary.points[i].x - boundary.points[i - 1].x,
                                 boundary.points[i].y - boundary.points[i - 1].y);
        boundary.visible = length >= settings.minContourLength;
        if (!boundary.visible && boundary.right != UINT32_MAX && boundary.left != UINT32_MAX)
            ++out.discardedContours;
    }
    next = Clock::now();
    out.timings.fitMs = ms(t, next);
    out.timings.totalMs = ms(start, next);
    return out;
}

std::string svg(const Result &result, bool filled) {
    if (!result.width || !result.height)
        throw std::invalid_argument("Empty result");
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << std::fixed << std::setprecision(3);
    s << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << result.sourceWidth << "\" height=\""
      << result.sourceHeight << "\" viewBox=\"0 0 " << result.width << ' ' << result.height << "\">\n";
    s << "<metadata>Contour Studio " << CONTOUR_VERSION << "; analysis=" << result.width << 'x'
      << result.height << "; CPU; shared boundaries</metadata>\n";
    if (result.mode == Settings::Mode::Edges) {
        s << "<g fill=\"none\" stroke=\"#39e6b4\" stroke-linejoin=\"round\">\n";
        for (const auto &stroke : result.strokes) {
            auto p = stroke.primitive;
            if (p.kind != PrimitiveKind::Path) {
                s << "<rect x=\"" << p.x << "\" y=\"" << p.y << "\" width=\"" << p.width << "\" height=\""
                  << p.height << "\" rx=\"" << p.radius << "\" stroke-width=\"" << stroke.width << "\"/>\n";
                continue;
            }
            s << "<path stroke-width=\"" << stroke.width << "\" stroke-linecap=\""
              << (stroke.roundCaps ? "round" : "butt") << "\" d=\"M" << stroke.start.x << ' '
              << stroke.start.y;
            for (auto segment : stroke.segments) {
                if (segment.radius > 0)
                    s << 'A' << segment.radius << ' ' << segment.radius << " 0 0 "
                      << (segment.clockwise ? 1 : 0) << ' ' << segment.end.x << ' ' << segment.end.y;
                else
                    s << 'L' << segment.end.x << ' ' << segment.end.y;
            }
            if (stroke.closed)
                s << 'Z';
            s << "\"/>\n";
        }
        s << "</g></svg>\n";
        return s.str();
    }
    s << "<g fill-rule=\"evenodd\" stroke-linejoin=\"round\""
      << (filled ? " stroke=\"none\"" : " fill=\"none\" stroke=\"#39e6b4\" stroke-width=\"0.65\"") << ">\n";
    if (!filled) {
        for (size_t i = 0; i < result.boundaries.size(); ++i) {
            const auto &boundary = result.boundaries[i];
            if (!boundary.visible || boundary.left == UINT32_MAX || boundary.right == UINT32_MAX)
                continue; // Canvas closure is topology, not an image edge.
            s << "<path id=\"boundary-" << i << "\" data-right=\"" << boundary.right << "\" data-left=\""
              << boundary.left << "\" d=\"M" << boundary.start.x << ' ' << boundary.start.y;
            for (auto segment : boundary.segments) {
                if (segment.radius > 0)
                    s << 'A' << segment.radius << ' ' << segment.radius << " 0 0 "
                      << (segment.clockwise ? 1 : 0) << ' ' << segment.end.x << ' ' << segment.end.y;
                else
                    s << 'L' << segment.end.x << ' ' << segment.end.y;
            }
            if (boundary.closed)
                s << 'Z';
            s << "\"/>\n";
        }
        s << "</g>\n</svg>\n";
        return s.str();
    }
    for (const auto &region : result.regions) {
        const auto &primitive = region.primitive;
        if (primitive.kind != PrimitiveKind::Path && region.loops.size() == 1 &&
            region.loops[0].boundaries.size() == 1 &&
            result.boundaries[region.loops[0].boundaries[0].id].closed) {
            s << "<rect id=\"region-" << region.id << "\" x=\"" << primitive.x << "\" y=\"" << primitive.y
              << "\" width=\"" << primitive.width << "\" height=\"" << primitive.height << "\" rx=\""
              << primitive.radius << "\"";
            if (filled)
                s << " fill=\"rgb(" << int(region.color.r) << ',' << int(region.color.g) << ','
                  << int(region.color.b) << ")\"";
            s << "/>\n";
            continue;
        }
        s << "<path id=\"region-" << region.id << "\" data-area=\"" << region.area << "\"";
        if (filled)
            s << " fill=\"rgb(" << int(region.color.r) << ',' << int(region.color.g) << ','
              << int(region.color.b) << ")\"";
        s << " d=\"";
        for (const auto &loop : region.loops) {
            if (loop.segments.empty())
                continue;
            s << 'M' << loop.start.x << ' ' << loop.start.y;
            for (auto segment : loop.segments) {
                if (segment.radius > 0)
                    s << 'A' << segment.radius << ' ' << segment.radius << " 0 0 "
                      << (segment.clockwise ? 1 : 0) << ' ' << segment.end.x << ' ' << segment.end.y;
                else
                    s << 'L' << segment.end.x << ' ' << segment.end.y;
            }
            s << 'Z';
        }
        s << "\"/>\n";
    }
    s << "</g>\n</svg>\n";
    return s.str();
}
Result process(const Image &input, const Settings &settings, const Cancel &cancel) {
    auto started = Clock::now();
    auto result = settings.mode == Settings::Mode::Edges ? processEdges(input, settings, cancel)
                                                         : processRegions(input, settings, cancel);
    result.timings.totalMs = ms(started, Clock::now());
    return result;
}

void saveText(const std::string &data, const std::wstring &path) {
    const std::filesystem::path destination(path),
        temporary(path + L".tmp." + std::to_wstring(Clock::now().time_since_epoch().count()));
    try {
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file || !file.write(data.data(), std::streamsize(data.size())))
                throw std::runtime_error("Cannot write SVG");
            file.close();
            if (!file)
                throw std::runtime_error("Cannot finish SVG");
        }
        // Destination selected with overwrite confirmation in the app; native replace is atomic.
#ifdef _WIN32
        // std::filesystem::rename on Windows doesn't replace an existing destination.
        extern bool replaceFile(const std::filesystem::path &, const std::filesystem::path &);
        if (!replaceFile(temporary, destination))
            throw std::runtime_error("Cannot replace SVG file");
#else
        std::filesystem::rename(temporary, destination);
#endif
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temporary, ec);
        throw;
    }
}
void saveSvg(const Result &result, const std::wstring &path, bool filled) {
    saveText(svg(result, filled), path);
}
void saveGraph(const Result &r, const std::wstring &path) {
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << std::fixed << std::setprecision(3);
    if (r.mode == Settings::Mode::Edges) {
        s << "{\"format\":\"contour-edges\",\"version\":1,\"source\":[" << r.sourceWidth << ','
          << r.sourceHeight << "],\"analysis\":[" << r.width << ',' << r.height << "],\"strokes\":[";
        for (size_t i = 0; i < r.strokes.size(); ++i) {
            if (i)
                s << ',';
            auto &stroke = r.strokes[i];
            auto p = stroke.primitive;
            s << "{\"start\":[" << stroke.start.x << ',' << stroke.start.y << "],\"width\":" << stroke.width
              << ",\"roundCaps\":" << (stroke.roundCaps ? "true" : "false")
              << ",\"closed\":" << (stroke.closed ? "true" : "false") << ",\"primitive\":[" << int(p.kind)
              << ',' << p.x << ',' << p.y << ',' << p.width << ',' << p.height << ',' << p.radius
              << "],\"segments\":[";
            for (size_t j = 0; j < stroke.segments.size(); ++j) {
                if (j)
                    s << ',';
                auto q = stroke.segments[j];
                s << '[' << q.end.x << ',' << q.end.y << ',' << q.radius << ',' << q.clockwise << ']';
            }
            s << "]}";
        }
        s << "]}";
        saveText(s.str(), path);
        return;
    }
    s << "{\"format\":\"contour-graph\",\"version\":1,\"source\":[" << r.sourceWidth << ',' << r.sourceHeight
      << "],\"analysis\":[" << r.width << ',' << r.height << "],\"outside\":null,\"boundaries\":[";
    for (size_t i = 0; i < r.boundaries.size(); ++i) {
        if (i)
            s << ',';
        auto &b = r.boundaries[i];
        s << "{\"id\":" << i << ",\"right\":";
        if (b.right == UINT32_MAX)
            s << "null";
        else
            s << b.right;
        s << ",\"left\":";
        if (b.left == UINT32_MAX)
            s << "null";
        else
            s << b.left;
        s << ",\"visible\":" << (b.visible ? "true" : "false")
          << ",\"closed\":" << (b.closed ? "true" : "false") << ",\"start\":[" << b.start.x << ','
          << b.start.y << "],\"segments\":[";
        for (size_t j = 0; j < b.segments.size(); ++j) {
            if (j)
                s << ',';
            auto q = b.segments[j];
            s << '[' << q.end.x << ',' << q.end.y << ',' << q.radius << ',' << (q.clockwise ? 1 : 0) << ']';
        }
        s << "]}";
    }
    s << "],\"regions\":[";
    for (size_t i = 0; i < r.regions.size(); ++i) {
        if (i)
            s << ',';
        auto &region = r.regions[i];
        auto &p = region.primitive;
        s << "{\"id\":" << region.id << ",\"area\":" << region.area << ",\"rgb\":[" << int(region.color.r)
          << ',' << int(region.color.g) << ',' << int(region.color.b) << "],\"primitive\":{\"kind\":\""
          << (p.kind == PrimitiveKind::Rectangle          ? "rectangle"
              : p.kind == PrimitiveKind::RoundedRectangle ? "roundedRectangle"
                                                          : "path")
          << "\",\"bounds\":[" << p.x << ',' << p.y << ',' << p.width << ',' << p.height
          << "],\"radius\":" << p.radius << "},\"loops\":[";
        for (size_t j = 0; j < region.loops.size(); ++j) {
            if (j)
                s << ',';
            s << '[';
            for (size_t k = 0; k < region.loops[j].boundaries.size(); ++k) {
                if (k)
                    s << ',';
                auto ref = region.loops[j].boundaries[k];
                s << '[' << ref.id << ',' << (ref.reversed ? 1 : 0) << ']';
            }
            s << ']';
        }
        s << "]}";
    }
    s << "]}";
    saveText(s.str(), path);
}
} // namespace contour
#ifdef _WIN32
#include <Windows.h>
namespace contour {
bool replaceFile(const std::filesystem::path &from, const std::filesystem::path &to) {
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
} // namespace contour
#endif
