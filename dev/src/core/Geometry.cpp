#include "Contour.h"
#include "Parallel.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>

namespace contour {
namespace {
float distance(Point a, Point b) { return std::hypot(a.x - b.x, a.y - b.y); }
float lineError(Point p, Point a, Point b) {
    float dx = b.x - a.x, dy = b.y - a.y, len = dx * dx + dy * dy;
    if (len == 0)
        return (p.x - a.x) * (p.x - a.x) + (p.y - a.y) * (p.y - a.y);
    float t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len, 0.f, 1.f);
    float ex = p.x - a.x - t * dx, ey = p.y - a.y - t * dy;
    return ex * ex + ey * ey;
}
void simplify(const std::vector<Point> &p, size_t a, size_t b, float tolerance,
              std::vector<size_t> &indices) {
    // Iterative Douglas-Peucker avoids stack overflow on complex/noisy outlines.
    std::vector<std::pair<size_t, size_t>> stack{{a, b}};
    while (!stack.empty()) {
        auto [left, right] = stack.back();
        stack.pop_back();
        float error = tolerance * tolerance;
        size_t split = left;
        for (size_t i = left + 1; i < right; ++i) {
            float d = lineError(p[i], p[left], p[right]);
            if (d > error) {
                error = d;
                split = i;
            }
        }
        if (split != left) {
            indices.push_back(split);
            stack.emplace_back(left, split);
            stack.emplace_back(split, right);
        }
    }
}
bool arc(const std::vector<Point> &p, size_t a, size_t b, float tolerance, Segment &out) {
    if (b - a < 4)
        return false;
    Point first = p[a], middle = p[(a + b) / 2], last = p[b];
    double determinant = 2 * (double(first.x) * (middle.y - last.y) + double(middle.x) * (last.y - first.y) +
                              double(last.x) * (first.y - middle.y));
    if (std::abs(determinant) < 1e-4)
        return false;
    auto sq = [](Point q) { return double(q.x) * q.x + double(q.y) * q.y; };
    double cx = (sq(first) * (middle.y - last.y) + sq(middle) * (last.y - first.y) +
                 sq(last) * (first.y - middle.y)) /
                determinant;
    double cy = (sq(first) * (last.x - middle.x) + sq(middle) * (first.x - last.x) +
                 sq(last) * (middle.x - first.x)) /
                determinant;
    double radius = std::hypot(first.x - cx, first.y - cy);
    if (radius < 2 || radius > 100000)
        return false;
    // Most trial circles fail the radial fit. Reject them using squared distances
    // before paying for any point angles or square roots.
    const double lower = std::max(0.0, radius - tolerance), upper = radius + tolerance;
    const double lowerSquared = lower * lower, upperSquared = upper * upper;
    auto outside = [&](double x, double y) {
        double dx = x - cx, dy = y - cy, squared = dx * dx + dy * dy;
        return squared < lowerSquared || squared > upperSquared;
    };
    for (size_t i = a + 1; i <= b; ++i) {
        if (outside(p[i].x, p[i].y))
            return false;
        for (int k = 1; k < 4; ++k) {
            double t = k * .25, x = p[i - 1].x + (p[i].x - p[i - 1].x) * t,
                   y = p[i - 1].y + (p[i].y - p[i - 1].y) * t;
            if (outside(x, y))
                return false;
        }
    }
    double previous = std::atan2(first.y - cy, first.x - cx), sweep = 0;
    int direction = 0;
    for (size_t i = a + 1; i <= b; ++i) {
        double angle = std::atan2(p[i].y - cy, p[i].x - cx),
               delta = std::remainder(angle - previous, 6.283185307179586);
        if (std::abs(delta) > 1e-6) {
            int sign = delta > 0 ? 1 : -1;
            if (direction && sign != direction)
                return false;
            direction = sign;
        }
        sweep += delta;
        previous = angle;
    }
    if (std::abs(sweep) < 0.30 || std::abs(sweep) > 3.141592653589793 + 0.01)
        return false;
    // Avoid replacing almost-flat bends with huge-radius arcs.
    if (radius * (1 - std::cos(std::abs(sweep) / 2)) < tolerance * 1.5)
        return false;
    out = {last, float(radius), direction > 0};
    return true;
}
bool roundedRect(Region &region, float tolerance) {
    if (region.loops.size() != 1)
        return false;
    const auto &points = region.loops.front().points;
    if (points.size() < 4)
        return false;
    float xmin = points[0].x, xmax = xmin, ymin = points[0].y, ymax = ymin;
    for (auto p : points) {
        xmin = std::min(xmin, p.x);
        xmax = std::max(xmax, p.x);
        ymin = std::min(ymin, p.y);
        ymax = std::max(ymax, p.y);
    }
    float width = xmax - xmin, height = ymax - ymin;
    if (points.size() == 4 && std::abs(std::abs(region.loops[0].signedArea) - width * height) < 0.001) {
        region.primitive = {PrimitiveKind::Rectangle, xmin, ymin, width, height, 0};
        return true;
    }
    if (width < 8 || height < 8 || tolerance <= 0)
        return false;
    // Radius candidates come from the four straight tangency runs. All corners must
    // fit a single radius; a text glyph or irregular silhouette fails the distance check.
    float topMin = xmax, topMax = xmin, bottomMin = xmax, bottomMax = xmin;
    for (auto p : points) {
        if (p.y == ymin) {
            topMin = std::min(topMin, p.x);
            topMax = std::max(topMax, p.x);
        }
        if (p.y == ymax) {
            bottomMin = std::min(bottomMin, p.x);
            bottomMax = std::max(bottomMax, p.x);
        }
    }
    float r = std::sqrt(std::max(
        0.f, float((width * height - std::abs(region.loops[0].signedArea)) / (4 - 3.141592653589793))));
    if (!std::isfinite(r))
        return false;
    if (r < 2 || r > std::min(width, height) / 2)
        return false;
    auto sdf = [&](Point p) {
        float qx = std::abs(p.x - (xmin + xmax) / 2) - (width / 2 - r),
              qy = std::abs(p.y - (ymin + ymax) / 2) - (height / 2 - r);
        return std::hypot(std::max(qx, 0.f), std::max(qy, 0.f)) + std::min(std::max(qx, qy), 0.f) - r;
    };
    for (size_t i = 0; i < points.size(); ++i) {
        auto a = points[i], b = points[(i + 1) % points.size()];
        int samples = std::max(1, int(std::ceil(distance(a, b))));
        for (int k = 0; k <= samples; ++k) {
            float t = float(k) / samples;
            if (std::abs(sdf({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t})) > tolerance)
                return false;
        }
    }
    region.primitive = {PrimitiveKind::RoundedRectangle, xmin, ymin, width, height, r};
    return true;
}
} // namespace
void expandPrimitive(Stroke &stroke) {
    auto p = stroke.primitive;
    float l = p.x, t = p.y, r = l + p.width, b = t + p.height, c = p.radius;
    if (p.kind == PrimitiveKind::Rectangle) {
        stroke.start = {l, t};
        stroke.segments = {{{r, t}, 0, false}, {{r, b}, 0, false}, {{l, b}, 0, false}, {{l, t}, 0, false}};
        stroke.closed = true;
    }
    if (p.kind == PrimitiveKind::RoundedRectangle) {
        stroke.start = {l + c, t};
        stroke.segments = {{{r - c, t}, 0, false}, {{r, t + c}, c, true},  {{r, b - c}, 0, false},
                           {{r - c, b}, c, true},  {{l + c, b}, 0, false}, {{l, b - c}, c, true},
                           {{l, t + c}, 0, false}, {{l + c, t}, c, true}};
        stroke.closed = true;
    }
}
void fitStroke(Stroke &stroke, float tolerance, uint32_t segmentBudget) {
    if (!stroke.points.empty()) {
        Point lo = stroke.points.front(), hi = lo;
        for (auto p : stroke.points) {
            lo.x = std::min(lo.x, p.x);
            lo.y = std::min(lo.y, p.y);
            hi.x = std::max(hi.x, p.x);
            hi.y = std::max(hi.y, p.y);
        }
        if (stroke.closed && std::max(hi.x - lo.x, hi.y - lo.y) <= 48 &&
            stroke.importance > 80.f * std::sqrt(std::min(stroke.contourLength, 256.f)) * 1.2f)
            tolerance = std::min(tolerance, .5f);
    }
    auto &p = stroke.points;
    if (p.size() < 2)
        return;
    if (stroke.closed && !stroke.anchored) {
        Region region;
        Loop loop;
        loop.points.assign(p.begin(), p.end() - 1);
        for (size_t i = 0; i + 1 < p.size(); ++i)
            loop.signedArea += (double(p[i].x) * p[i + 1].y - double(p[i + 1].x) * p[i].y) * .5;
        region.loops.push_back(std::move(loop));
        Point lo = p[0], hi = lo;
        for (auto q : p) {
            lo.x = std::min(lo.x, q.x);
            lo.y = std::min(lo.y, q.y);
            hi.x = std::max(hi.x, q.x);
            hi.y = std::max(hi.y, q.y);
        }
        float width = hi.x - lo.x, height = hi.y - lo.y, shortSide = std::min(width, height),
              longSide = std::max(width, height);
        if (shortSide > 0 && shortSide <= 8 && longSide >= shortSide * 4 &&
            std::abs(region.loops[0].signedArea) >= width * height * .8f) {
            stroke.width = shortSide;
            stroke.closed = false;
            float firstMin = 1e9f, firstMax = -1e9f, lastMin = 1e9f, lastMax = -1e9f;
            for (auto q : p) {
                float axial = width < height ? q.y : q.x, transverse = width < height ? q.x : q.y;
                if (axial == (width < height ? lo.y : lo.x)) {
                    firstMin = std::min(firstMin, transverse);
                    firstMax = std::max(firstMax, transverse);
                }
                if (axial == (width < height ? hi.y : hi.x)) {
                    lastMin = std::min(lastMin, transverse);
                    lastMax = std::max(lastMax, transverse);
                }
            }
            stroke.roundCaps = shortSide >= 4 && firstMax - firstMin < shortSide * .5f &&
                               lastMax - lastMin < shortSide * .5f;
            float inset = stroke.roundCaps ? shortSide * .5f : 0;
            stroke.start = width < height ? Point{(lo.x + hi.x) * .5f, lo.y + inset}
                                          : Point{lo.x + inset, (lo.y + hi.y) * .5f};
            Point end =
                width < height ? Point{stroke.start.x, hi.y - inset} : Point{hi.x - inset, stroke.start.y};
            stroke.segments = {Segment{end, 0, false}};
            return;
        }
        bool rectangle = std::abs(region.loops[0].signedArea) >= width * height * .95f;
        for (size_t i = 1; i < p.size() && rectangle; ++i)
            for (int k = 0; k <= 2; ++k) {
                float t = k * .5f;
                Point q{p[i - 1].x + (p[i].x - p[i - 1].x) * t, p[i - 1].y + (p[i].y - p[i - 1].y) * t};
                if (std::min({q.x - lo.x, hi.x - q.x, q.y - lo.y, hi.y - q.y}) > std::min(1.25f, tolerance))
                    rectangle = false;
            }
        if (rectangle) {
            stroke.primitive = {PrimitiveKind::Rectangle, lo.x, lo.y, width, height, 0};
            expandPrimitive(stroke);
            return;
        }
        if (roundedRect(region, tolerance)) {
            stroke.primitive = region.primitive;
            expandPrimitive(stroke);
            return;
        }
        p.pop_back();
        auto origin = std::min_element(
            p.begin(), p.end(), [](Point a, Point b) { return a.y < b.y || (a.y == b.y && a.x < b.x); });
        std::rotate(p.begin(), origin, p.end());
        p.push_back(p.front());
    }
    float local = tolerance;
    if (stroke.closed) {
        Point lo = p[0], hi = lo;
        for (auto q : p) {
            lo.x = std::min(lo.x, q.x);
            lo.y = std::min(lo.y, q.y);
            hi.x = std::max(hi.x, q.x);
            hi.y = std::max(hi.y, q.y);
        }
        local = std::min(local, std::min(hi.x - lo.x, hi.y - lo.y) * .18f);
    }
    struct Span {
        size_t a, b, split;
        float error;
        bool operator<(const Span &other) const { return error < other.error; }
    };
    auto span = [&](size_t a, size_t b) {
        Span item{a, b, a, 0};
        for (size_t i = a + 1; i < b; ++i) {
            float e = lineError(p[i], p[a], p[b]);
            if (e > item.error) {
                item.error = e;
                item.split = i;
            }
        }
        return item;
    };
    std::vector<size_t> indices{0, p.size() - 1};
    std::priority_queue<Span> queue;
    if (stroke.closed) {
        size_t opposite = 1;
        float farthest = 0;
        for (size_t i = 1; i + 1 < p.size(); ++i) {
            float d = lineError(p[i], p[0], p[0]);
            if (d > farthest) {
                farthest = d;
                opposite = i;
            }
        }
        indices.push_back(opposite);
        queue.push(span(0, opposite));
        queue.push(span(opposite, p.size() - 1));
    } else
        queue.push(span(0, p.size() - 1));
    // Best-first splitting allocates the point budget where geometry needs it most.
    auto limit = std::clamp(segmentBudget, 4u, 256u) + 1;
    while (!queue.empty() && indices.size() < limit) {
        auto item = queue.top();
        queue.pop();
        if (item.error <= local * local || item.split == item.a)
            break;
        indices.push_back(item.split);
        queue.push(span(item.a, item.split));
        queue.push(span(item.split, item.b));
    }
    std::sort(indices.begin(), indices.end());
    stroke.start = p[0];
    stroke.segments.clear();
    for (size_t k = 0; k + 1 < indices.size();) {
        size_t selected = k + 1;
        Segment s{p[indices[selected]], 0, false};
        for (size_t end = k + 2; end < indices.size() && end <= k + 16; ++end) {
            Segment candidate;
            if (arc(p, indices[k], indices[end], local, candidate)) {
                selected = end;
                s = candidate;
            }
        }
        stroke.segments.push_back(s);
        k = selected;
    }
}
void fitGeometry(Result &result, float tolerance) {
    tolerance = std::clamp(tolerance, 0.f, 2.f);
    auto pixels = size_t(result.width) * result.height;
    detail::parallelRows(0, uint32_t(result.regions.size()), pixels,
                         [&](uint32_t i) { roundedRect(result.regions[i], tolerance); });
    detail::parallelRows(0, uint32_t(result.boundaries.size()), pixels, [&](uint32_t boundaryIndex) {
        auto &boundary = result.boundaries[boundaryIndex];
        if (boundary.points.empty())
            return;
        float localTolerance = tolerance;
        if (boundary.closed) {
            auto lo = boundary.points.front(), hi = lo;
            for (auto q : boundary.points) {
                lo.x = std::min(lo.x, q.x);
                lo.y = std::min(lo.y, q.y);
                hi.x = std::max(hi.x, q.x);
                hi.y = std::max(hi.y, q.y);
            }
            localTolerance = std::min(tolerance, std::min(hi.x - lo.x, hi.y - lo.y) * .18f);
            if (std::max(hi.x - lo.x, hi.y - lo.y) <= 48 && boundary.left != UINT32_MAX &&
                boundary.right != UINT32_MAX) {
                auto a = result.regions[boundary.left].color, b = result.regions[boundary.right].color;
                int contrast =
                    std::max({std::abs(int(a.r) - b.r), std::abs(int(a.g) - b.g), std::abs(int(a.b) - b.b)});
                if (contrast >= 80)
                    localTolerance = std::min(localTolerance, .5f);
            }
        }
        // Preserve small disconnected rings rather than collapsing them to a doubled line.
        auto p = boundary.points;
        if (boundary.closed) {
            if (!boundary.anchored) {
                p.pop_back();
                auto origin = std::min_element(p.begin(), p.end(), [](Point a, Point b) {
                    return a.y < b.y || (a.y == b.y && a.x < b.x);
                });
                std::rotate(p.begin(), origin, p.end());
                p.push_back(p.front());
            }
        }
        boundary.start = p.front();
        boundary.segments.clear();
        std::vector<size_t> indices{0, p.size() - 1};
        if (boundary.closed) {
            size_t opposite = 1;
            float farthest = 0;
            for (size_t i = 1; i + 1 < p.size(); ++i) {
                float d = distance(p[0], p[i]);
                if (d > farthest) {
                    farthest = d;
                    opposite = i;
                }
            }
            indices.push_back(opposite);
            simplify(p, 0, opposite, localTolerance, indices);
            simplify(p, opposite, p.size() - 1, localTolerance, indices);
        } else
            simplify(p, 0, p.size() - 1, localTolerance, indices);
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        // Fit circular arcs over successive short line pieces. Long straights remain lines.
        for (size_t k = 0; k + 1 < indices.size();) {
            size_t selected = k + 1;
            Segment segment{p[indices[selected]], 0, false};
            for (size_t end = k + 2; end < indices.size() && end <= k + 24; ++end) {
                Segment candidate;
                if (arc(p, indices[k], indices[end], localTolerance, candidate)) {
                    selected = end;
                    segment = candidate;
                }
            }
            boundary.segments.push_back(segment);
            k = selected;
        }
        // Isolated rounded cards share one complete ring with their background.
        // Fit it once and reuse the same radius from both sides.
        if (boundary.closed && !boundary.anchored) {
            const Region *card = nullptr;
            if (boundary.right != UINT32_MAX &&
                result.regions[boundary.right].primitive.kind == PrimitiveKind::RoundedRectangle)
                card = &result.regions[boundary.right];
            else if (boundary.left != UINT32_MAX &&
                     result.regions[boundary.left].primitive.kind == PrimitiveKind::RoundedRectangle)
                card = &result.regions[boundary.left];
            if (card) {
                auto q = card->primitive;
                float l = q.x, t = q.y, r = q.x + q.width, b = q.y + q.height, c = q.radius;
                boundary.start = {l + c, t};
                boundary.segments = {{{r - c, t}, 0, false}, {{r, t + c}, c, true},  {{r, b - c}, 0, false},
                                     {{r - c, b}, c, true},  {{l + c, b}, 0, false}, {{l, b - c}, c, true},
                                     {{l, t + c}, 0, false}, {{l + c, t}, c, true}};
                // Clockwise means the card is on the right of this ring.
                if (boundary.right != card->id) {
                    std::vector<Segment> reverse;
                    for (size_t i = boundary.segments.size(); i-- > 0;) {
                        auto end = i ? boundary.segments[i - 1].end : boundary.start;
                        reverse.push_back(
                            {end, boundary.segments[i].radius, !boundary.segments[i].clockwise});
                    }
                    boundary.segments = std::move(reverse);
                }
            }
        }
    });
    // Assemble closed region paths from canonical shared chains. No independent fitting.
    for (auto &region : result.regions)
        for (auto &loop : region.loops) {
            loop.segments.clear();
            size_t count = 0;
            for (auto ref : loop.boundaries)
                count += result.boundaries[ref.id].segments.size();
            loop.segments.reserve(count);
            bool first = true;
            for (auto ref : loop.boundaries) {
                const auto &boundary = result.boundaries[ref.id];
                Point start = ref.reversed ? boundary.segments.back().end : boundary.start;
                if (first) {
                    loop.start = start;
                    first = false;
                }
                if (!ref.reversed)
                    loop.segments.insert(loop.segments.end(), boundary.segments.begin(),
                                         boundary.segments.end());
                else
                    for (size_t i = boundary.segments.size(); i-- > 0;) {
                        auto end = i ? boundary.segments[i - 1].end : boundary.start;
                        loop.segments.push_back(
                            {end, boundary.segments[i].radius, !boundary.segments[i].clockwise});
                    }
            }
        }
}
} // namespace contour
