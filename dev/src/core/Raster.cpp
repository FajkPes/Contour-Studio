#include "Raster.h"
#include "Parallel.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace contour {
namespace {
void flatten(Point a, const Segment &s, std::vector<RasterLine> &out) {
    auto b = s.end;
    float dx = b.x - a.x, dy = b.y - a.y, chord = std::hypot(dx, dy);
    if (s.radius <= 0 || chord < 1e-6f) {
        out.push_back({a, b});
        return;
    }
    float radius = std::max(s.radius, chord * .5f),
          h = std::sqrt(std::max(0.f, radius * radius - chord * chord * .25f));
    float side = s.clockwise ? 1.f : -1.f, cx = (a.x + b.x) * .5f - dy / chord * h * side,
          cy = (a.y + b.y) * .5f + dx / chord * h * side;
    float begin = std::atan2(a.y - cy, a.x - cx),
          sweep = std::remainder(std::atan2(b.y - cy, b.x - cx) - begin, 6.28318530718f);
    if (s.clockwise && sweep < 0)
        sweep += 6.28318530718f;
    if (!s.clockwise && sweep > 0)
        sweep -= 6.28318530718f;
    int steps =
        std::clamp(int(std::ceil(std::abs(sweep) /
                                 std::max(.005f, 2 * std::acos(std::clamp(1 - .08f / radius, -1.f, 1.f))))),
                   1, 2048);
    Point previous = a;
    for (int i = 1; i <= steps; ++i) {
        float angle = begin + sweep * i / steps;
        Point end = i == steps ? b : Point{cx + radius * std::cos(angle), cy + radius * std::sin(angle)};
        out.push_back({previous, end});
        previous = end;
    }
}
void blend(Pixel &p, Pixel color, float coverage) {
    int alpha = std::clamp(int(coverage * 256), 0, 256);
    p.b = uint8_t((p.b * (256 - alpha) + color.b * alpha) >> 8);
    p.g = uint8_t((p.g * (256 - alpha) + color.g * alpha) >> 8);
    p.r = uint8_t((p.r * (256 - alpha) + color.r * alpha) >> 8);
    p.a = 255;
}
// Coverage-based segment stroke; restrict work to its minor axis, not its bounding box area.
void stroke(Image &image, Point a, Point b, float width, float opacity, int first, int last, Pixel outline) {
    bool steep = std::abs(b.y - a.y) > std::abs(b.x - a.x);
    if (steep) {
        std::swap(a.x, a.y);
        std::swap(b.x, b.y);
    }
    if (a.x > b.x)
        std::swap(a, b);
    float dx = b.x - a.x;
    if (dx < 1e-6f)
        return;
    float gradient = (b.y - a.y) / dx, half = width * .5f * std::sqrt(1 + gradient * gradient);
    int major = int(steep ? image.height : image.width), minor = int(steep ? image.width : image.height);
    int begin = std::max(0, int(std::floor(a.x - half))),
        end = std::min(major - 1, int(std::ceil(b.x + half)));
    if (steep) {
        begin = std::max(begin, first);
        end = std::min(end, last - 1);
    }
    for (int x = begin; x <= end; ++x) {
        float center = a.y + std::clamp(x + .5f - a.x, 0.f, dx) * gradient;
        int low = std::max(0, int(std::floor(center - half - 1))),
            high = std::min(minor - 1, int(std::ceil(center + half + 1)));
        if (!steep) {
            low = std::max(low, first);
            high = std::min(high, last - 1);
        }
        float endpoint = std::clamp(std::min(x + 1.f, b.x) - std::max(float(x), a.x), 0.f, 1.f);
        for (int y = low; y <= high; ++y) {
            float coverage =
                std::clamp(half + .5f - std::abs(y + .5f - center), 0.f, 1.f) * endpoint * opacity;
            if (coverage <= 0)
                continue;
            auto ix = steep ? y : x, iy = steep ? x : y;
            blend(image.pixels[size_t(iy) * image.width + ix], outline, coverage);
        }
    }
}
void roundCap(Image &image, Point center, float width, float opacity, int first, int last, Pixel outline) {
    float r = width * .5f;
    int x0 = std::max(0, int(std::floor(center.x - r - 1))),
        x1 = std::min(int(image.width) - 1, int(std::ceil(center.x + r + 1))),
        y0 = std::max(0, int(std::floor(center.y - r - 1))),
        y1 = std::min(int(image.height) - 1, int(std::ceil(center.y + r + 1)));
    y0 = std::max(y0, first);
    y1 = std::min(y1, last - 1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            float coverage =
                std::clamp(r + .5f - std::hypot(x + .5f - center.x, y + .5f - center.y), 0.f, 1.f) * opacity;
            if (coverage > 0)
                blend(image.pixels[size_t(y) * image.width + x], outline, coverage);
        }
}
} // namespace
void RasterScene::prepare(std::shared_ptr<const Image> source, std::shared_ptr<const Result> result) {
    source_ = std::move(source);
    result_ = std::move(result);
    lines_.clear();
    fills_ = {};
    regionIds_.clear();
    if (!result_)
        return;
    const auto &r = *result_;
    if (r.mode == Settings::Mode::Edges) {
        for (const auto &s : r.strokes) {
            auto first = lines_.size();
            auto start = s.start;
            for (auto segment : s.segments) {
                flatten(start, segment, lines_);
                start = segment.end;
            }
            for (size_t i = first; i < lines_.size(); ++i)
                lines_[i].width = s.width;
            if (s.roundCaps && lines_.size() > first) {
                lines_[first].roundStart = true;
                lines_.back().roundEnd = true;
            }
        }
        return;
    }
    fills_ = {r.width, r.height, std::vector<Pixel>(size_t(r.width) * r.height, {32, 24, 21, 255})};
    regionIds_.assign(fills_.pixels.size(), UINT32_MAX);
    if (r.labels.size() == fills_.pixels.size())
        for (size_t i = 0; i < r.labels.size(); ++i) {
            fills_.pixels[i] = r.regions[r.labels[i]].color;
            regionIds_[i] = r.labels[i];
        }
    for (const auto &b : r.boundaries) {
        if (!b.visible || b.left == UINT32_MAX || b.right == UINT32_MAX)
            continue;
        auto start = b.start;
        for (auto s : b.segments) {
            flatten(start, s, lines_);
            start = s.end;
        }
    }
    // Reuse row capacities across regions instead of allocating once per region/row.
    std::vector<std::vector<float>> scanRows(r.height);
    // Scanline filling of fitted vectors. Holes follow even-odd parity.
    for (const auto &region : r.regions) {
        std::vector<RasterLine> edges;
        for (const auto &loop : region.loops) {
            auto start = loop.start;
            for (auto s : loop.segments) {
                flatten(start, s, edges);
                start = s.end;
            }
        }
        if (edges.empty())
            continue;
        float ymin = float(r.height), ymax = 0;
        for (auto e : edges) {
            ymin = std::min({ymin, e.a.y, e.b.y});
            ymax = std::max({ymax, e.a.y, e.b.y});
        }
        int first = std::clamp(int(std::ceil(ymin - .5f)), 0, int(r.height)),
            last = std::clamp(int(std::ceil(ymax - .5f)), 0, int(r.height));
        auto &rows = scanRows;
        for (int y = first; y < last; ++y)
            rows[y].clear();
        for (auto e : edges) {
            if (e.a.y == e.b.y)
                continue;
            if (e.a.y > e.b.y)
                std::swap(e.a, e.b);
            int from = std::max(first, int(std::ceil(e.a.y - .5f))),
                to = std::min(last, int(std::ceil(e.b.y - .5f)));
            float slope = (e.b.x - e.a.x) / (e.b.y - e.a.y);
            for (int y = from; y < to; ++y)
                rows[y].push_back(e.a.x + (y + .5f - e.a.y) * slope);
        }
        for (int y = first; y < last; ++y) {
            auto &hits = rows[y];
            std::sort(hits.begin(), hits.end());
            for (size_t j = 0; j + 1 < hits.size(); j += 2) {
                int a = std::clamp(int(std::ceil(hits[j] - .5f)), 0, int(r.width)),
                    b = std::clamp(int(std::ceil(hits[j + 1] - .5f)), 0, int(r.width));
                auto offset = size_t(y) * r.width;
                std::fill(fills_.pixels.begin() + offset + a, fills_.pixels.begin() + offset + b,
                          region.color);
                std::fill(regionIds_.begin() + offset + a, regionIds_.begin() + offset + b, region.id);
            }
        }
    }
}
uint32_t RasterScene::regionAt(uint32_t x, uint32_t y) const {
    if (!result_ || x >= fills_.width || y >= fills_.height)
        return UINT32_MAX;
    return regionIds_[size_t(y) * fills_.width + x];
}
void RasterScene::render(Image &out, uint32_t width, uint32_t height, float scale, float tx, float ty,
                         RasterMode mode, float opacity, float lineWidth, bool parallel,
                         Pixel outline) const {
    if (width > 16384 || height > 16384 || !width || !height || scale <= 0)
        throw std::runtime_error("Invalid render size");
    out.width = width;
    out.height = height;
    out.pixels.resize(size_t(width) * height);
    auto sw = result_ ? result_->width : (source_ ? source_->width : 0),
         sh = result_ ? result_->height : (source_ ? source_->height : 0);
    const Image *bitmap = mode == RasterMode::Regions ? &fills_ : source_.get();
    const bool showBitmap = sw && sh && mode != RasterMode::Edges && bitmap && !bitmap->pixels.empty();
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    float mx = 0, my = 0;
    std::vector<unsigned> columns;
    if (showBitmap) {
        x0 = std::clamp(int(std::floor(tx)), 0, int(width));
        x1 = std::clamp(int(std::ceil(tx + sw * scale)), 0, int(width));
        y0 = std::clamp(int(std::floor(ty)), 0, int(height));
        y1 = std::clamp(int(std::ceil(ty + sh * scale)), 0, int(height));
        columns.resize(std::max(0, x1 - x0));
        mx = bitmap->width / (sw * scale);
        my = bitmap->height / (sh * scale);
        for (int x = x0; x < x1; ++x)
            columns[x - x0] = std::clamp(int((x + .5f - tx) * mx), 0, int(bitmap->width) - 1);
    }
    auto paint = [&](uint32_t first, uint32_t last) {
        if (!showBitmap || x0 != 0 || x1 != int(width) || y0 > int(first) || y1 < int(last))
            std::fill(out.pixels.begin() + size_t(first) * width, out.pixels.begin() + size_t(last) * width,
                      Pixel{32, 24, 21, 255});
        if (showBitmap) {
            for (int y = std::max(y0, int(first)); y < std::min(y1, int(last)); ++y) {
                auto sy = std::clamp(int((y + .5f - ty) * my), 0, int(bitmap->height) - 1);
                auto row = bitmap->pixels.data() + size_t(sy) * bitmap->width;
                auto dst = out.pixels.data() + size_t(y) * width;
                for (int x = x0; x < x1; ++x) {
                    auto p = row[columns[x - x0]];
                    if (p.a != 255) {
                        auto a = p.a;
                        p.b = uint8_t((p.b * a + 32 * (255 - a)) / 255);
                        p.g = uint8_t((p.g * a + 24 * (255 - a)) / 255);
                        p.r = uint8_t((p.r * a + 21 * (255 - a)) / 255);
                        p.a = 255;
                    }
                    dst[x] = p;
                }
            }
        }
        if (mode == RasterMode::Overlay || mode == RasterMode::Edges)
            for (auto e : lines_) {
                e.a = {e.a.x * scale + tx, e.a.y * scale + ty};
                e.b = {e.b.x * scale + tx, e.b.y * scale + ty};
                const float strokeWidth = result_->mode == Settings::Mode::Edges
                                              ? lineWidth * std::max(.65f, e.width * scale)
                                              : lineWidth;
                float margin = strokeWidth * .5f + 1;
                if (std::max(e.a.x, e.b.x) < -margin || std::min(e.a.x, e.b.x) > width + margin ||
                    std::max(e.a.y, e.b.y) < first - margin || std::min(e.a.y, e.b.y) > last + margin)
                    continue;
                stroke(out, e.a, e.b, strokeWidth, opacity, int(first), int(last), outline);
                if (e.roundStart)
                    roundCap(out, e.a, strokeWidth, opacity, int(first), int(last), outline);
                if (e.roundEnd)
                    roundCap(out, e.b, strokeWidth, opacity, int(first), int(last), outline);
            }
    };
    if (parallel)
        detail::parallelRanges(0, height, size_t(width) * height, paint);
    else
        paint(0, height);
}
} // namespace contour
