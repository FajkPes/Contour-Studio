#include "Contour.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#include <Windows.h>
#include <compressapi.h>
#endif

namespace contour {
namespace {
struct Writer {
    std::vector<uint8_t> bytes;
    void number(uint32_t v) {
        while (v >= 128) {
            bytes.push_back(uint8_t(v) | 128);
            v >>= 7;
        }
        bytes.push_back(uint8_t(v));
    }
    void signedNumber(int32_t v) { number((uint32_t(v) << 1) ^ uint32_t(v >> 31)); }
    void coordinate(float v) { signedNumber(int32_t(std::lround(v * 8))); }
};
struct Reader {
    const std::vector<uint8_t> &bytes;
    size_t offset{};
    uint8_t byte() {
        if (offset >= bytes.size())
            throw std::runtime_error("Truncated contour command");
        return bytes[offset++];
    }
    uint32_t number() {
        uint32_t v = 0;
        for (unsigned shift = 0; shift <= 28; shift += 7) {
            if (offset >= bytes.size())
                throw std::runtime_error("Truncated contour file");
            auto b = bytes[offset++];
            if (shift == 28 && (b & 240))
                throw std::runtime_error("Invalid contour integer");
            v |= uint32_t(b & 127) << shift;
            if (!(b & 128))
                return v;
        }
        throw std::runtime_error("Invalid contour integer");
    }
    int32_t signedNumber() {
        auto v = number();
        return int32_t((v >> 1) ^ uint32_t(-int32_t(v & 1)));
    }
    float coordinate() { return signedNumber() / 8.f; }
};
uint32_t checksum(const std::vector<uint8_t> &v) {
    uint32_t h = 2166136261u;
    for (auto b : v) {
        h ^= b;
        h *= 16777619u;
    }
    return h;
}
void word(std::vector<uint8_t> &v, uint32_t n) {
    for (int i = 0; i < 4; ++i)
        v.push_back(uint8_t(n >> (i * 8)));
}
uint32_t wordAt(const std::vector<uint8_t> &v, size_t i) {
    return uint32_t(v[i]) | (uint32_t(v[i + 1]) << 8) | (uint32_t(v[i + 2]) << 16) |
           (uint32_t(v[i + 3]) << 24);
}
} // namespace
namespace {
void writeStroke(Writer &w, const Stroke &stroke, int32_t &previousX, int32_t &previousY) {
    auto p = stroke.primitive;
    auto width = uint32_t(std::lround(stroke.width * 8));
    unsigned coordinateShift = 3;
    auto inspect = [&](float value) {
        auto q = uint32_t(int32_t(std::lround(value * 8)));
        while (coordinateShift && (q & ((1u << coordinateShift) - 1)))
            --coordinateShift;
    };
    if (p.kind == PrimitiveKind::Path) {
        inspect(stroke.start.x);
        inspect(stroke.start.y);
        for (const auto &segment : stroke.segments) {
            inspect(segment.end.x);
            inspect(segment.end.y);
        }
    } else {
        for (auto value : {p.x, p.y, p.width, p.height})
            inspect(value);
    }

    w.number(uint32_t(p.kind) | (stroke.closed ? 4u : 0u) | (stroke.roundCaps ? 8u : 0u) |
             (width == 8 ? 0u : 16u) | (coordinateShift << 5));
    if (width != 8)
        w.number(width);
    auto start = [&](Point point) {
        auto x = int32_t(std::lround(point.x * 8)), y = int32_t(std::lround(point.y * 8));
        w.signedNumber(x - previousX);
        w.signedNumber(y - previousY);
        previousX = x;
        previousY = y;
    };
    if (p.kind != PrimitiveKind::Path) {
        start({p.x, p.y});
        for (auto v : {p.width, p.height})
            w.signedNumber(int32_t(std::lround(v * 8)) >> coordinateShift);
        if (p.kind == PrimitiveKind::RoundedRectangle)
            w.coordinate(p.radius);
        return;
    }
    start(stroke.start);
    w.number(uint32_t(stroke.segments.size()));
    int x = int(std::lround(stroke.start.x * 8)), y = int(std::lround(stroke.start.y * 8));
    for (auto s : stroke.segments) {
        int nx = int(std::lround(s.end.x * 8)), ny = int(std::lround(s.end.y * 8)), dx = nx - x, dy = ny - y;
        if (s.radius == 0 && dx % 8 == 0 && dy % 8 == 0 && std::abs(dx) <= 56 && std::abs(dy) <= 56)
            w.bytes.push_back(uint8_t((dx / 8 + 7) * 15 + dy / 8 + 7));
        else {
            w.bytes.push_back(s.radius > 0 ? 226 : 225);
            w.signedNumber(dx >> coordinateShift);
            w.signedNumber(dy >> coordinateShift);
            if (s.radius > 0)
                w.number((uint32_t(std::lround(s.radius * 8)) << 1) | (s.clockwise ? 1u : 0u));
        }
        x = nx;
        y = ny;
    }
}
Result readStrokes(Reader &read) {
    Result r;
    r.mode = Settings::Mode::Edges;
    r.sourceWidth = read.number();
    r.sourceHeight = read.number();
    r.width = read.number();
    r.height = read.number();
    auto count = read.number();
    if (!r.sourceWidth || !r.sourceHeight || uint64_t(r.sourceWidth) * r.sourceHeight > 67108864 ||
        !r.width || !r.height || r.width > 8192 || r.height > 8192 || count > read.bytes.size() / 3)
        throw std::runtime_error("Invalid edge file dimensions/counts");
    auto valid = [&](Point p) { return p.x >= 0 && p.y >= 0 && p.x <= r.width && p.y <= r.height; };
    int64_t previousX = 0, previousY = 0;
    auto start = [&]() {
        previousX += read.signedNumber();
        previousY += read.signedNumber();
        if (previousX < 0 || previousY < 0 || previousX > int64_t(r.width) * 8 ||
            previousY > int64_t(r.height) * 8)
            throw std::runtime_error("Invalid edge anchor delta");
        return Point{float(previousX) / 8, float(previousY) / 8};
    };
    for (uint32_t i = 0; i < count; ++i) {
        Stroke s;
        auto flags = read.number();
        if (flags > 127u || (flags & 3) > 2)
            throw std::runtime_error("Invalid edge primitive");
        const unsigned coordinateShift = flags >> 5;
        s.closed = (flags & 4) != 0;
        s.roundCaps = (flags & 8) != 0;
        s.width = !(flags & 16) ? 1.f : read.number() / 8.f;
        if (s.width <= 0 || s.width > 8192)
            throw std::runtime_error("Invalid edge width");
        s.primitive.kind = PrimitiveKind(flags & 3);
        if (s.primitive.kind != PrimitiveKind::Path) {
            auto &p = s.primitive;
            auto origin = start();
            p.x = origin.x;
            p.y = origin.y;
            p.width = float(int64_t(read.signedNumber()) * (int64_t(1) << coordinateShift)) / 8;
            p.height = float(int64_t(read.signedNumber()) * (int64_t(1) << coordinateShift)) / 8;
            if (p.kind == PrimitiveKind::RoundedRectangle)
                p.radius = read.coordinate();
            if (!valid({p.x, p.y}) || !valid({p.x + p.width, p.y + p.height}) || p.width <= 0 ||
                p.height <= 0 || p.radius < 0 || p.radius > std::min(p.width, p.height) * .5f)
                throw std::runtime_error("Invalid edge bounds");
            expandPrimitive(s);
        } else {
            s.start = start();
            if (!valid(s.start))
                throw std::runtime_error("Invalid edge start");
            auto segments = read.number();
            if (!segments || segments > 4096 || segments > read.bytes.size() - read.offset)
                throw std::runtime_error("Invalid edge point count");
            s.segments.reserve(segments);
            int x = int(std::lround(s.start.x * 8)), y = int(std::lround(s.start.y * 8));
            for (uint32_t j = 0; j < segments; ++j) {
                auto command = read.byte();
                int dx{}, dy{};
                uint32_t radius{};
                if (command < 225) {
                    dx = (int(command) / 15 - 7) * 8;
                    dy = (int(command) % 15 - 7) * 8;
                } else if (command <= 226) {
                    auto sx = int64_t(read.signedNumber()) * (int64_t(1) << coordinateShift);
                    auto sy = int64_t(read.signedNumber()) * (int64_t(1) << coordinateShift);
                    if (std::abs(sx) > 65536 || std::abs(sy) > 65536)
                        throw std::runtime_error("Invalid edge delta");
                    dx = int(sx);
                    dy = int(sy);
                    if (command == 226)
                        radius = read.number();
                } else
                    throw std::runtime_error("Unknown edge command");
                int64_t nx = int64_t(x) + dx, ny = int64_t(y) + dy;
                if (nx < 0 || ny < 0 || nx > int64_t(r.width) * 8 || ny > int64_t(r.height) * 8 ||
                    (radius >> 1) > 800000)
                    throw std::runtime_error("Invalid edge coordinates");
                x = int(nx);
                y = int(ny);
                s.segments.push_back({{x / 8.f, y / 8.f}, (radius >> 1) / 8.f, (radius & 1) != 0});
            }
            if (s.closed && s.segments.back().end != s.start)
                throw std::runtime_error("Edge ring is not closed");
        }
        r.strokes.push_back(std::move(s));
    }
    if (read.offset != read.bytes.size())
        throw std::runtime_error("Unexpected edge data");
    return r;
}
} // namespace
std::vector<uint8_t> encodeCompact(const Result &r) {
    Writer w;
    w.bytes.reserve((r.mode == Settings::Mode::Edges ? r.strokes.size() : r.boundaries.size()) * 24);
    if (r.mode == Settings::Mode::Edges) {
        w.number(UINT32_MAX);
        for (auto v : {r.sourceWidth, r.sourceHeight, r.width, r.height, uint32_t(r.strokes.size())})
            w.number(v);
        int32_t previousX = 0, previousY = 0;
        // Independent strokes have no index references. Nearby origins compress better;
        // analysis keeps its importance order, only the serialized order changes.
        auto spread = [](uint32_t v) {
            v = (v | (v << 8)) & 0x00ff00ff;
            v = (v | (v << 4)) & 0x0f0f0f0f;
            v = (v | (v << 2)) & 0x33333333;
            return (v | (v << 1)) & 0x55555555;
        };
        std::vector<std::pair<uint32_t, const Stroke *>> ordered;
        ordered.reserve(r.strokes.size());
        for (const auto &s : r.strokes) {
            auto x = s.primitive.kind == PrimitiveKind::Path ? s.start.x : s.primitive.x;
            auto y = s.primitive.kind == PrimitiveKind::Path ? s.start.y : s.primitive.y;
            ordered.emplace_back(spread(uint32_t(x)) | (spread(uint32_t(y)) << 1), &s);
        }
        std::stable_sort(ordered.begin(), ordered.end(), [](auto a, auto b) { return a.first < b.first; });
        for (auto entry : ordered)
            writeStroke(w, *entry.second, previousX, previousY);
    } else {
        for (auto v : {r.sourceWidth, r.sourceHeight, r.width, r.height, uint32_t(r.boundaries.size()),
                       uint32_t(r.regions.size())})
            w.number(v);
        int32_t previousRight = 0, previousX = 0, previousY = 0;
        for (const auto &b : r.boundaries) {
            int32_t right = b.right == UINT32_MAX ? 0 : int32_t(b.right + 1);
            int32_t left = b.left == UINT32_MAX ? 0 : int32_t(b.left + 1);
            w.signedNumber(right - previousRight);
            w.signedNumber(left - right);
            previousRight = right;
            w.number((b.closed ? 1u : 0u) | (b.visible ? 0u : 2u));
            auto startX = int32_t(std::lround(b.start.x * 8)), startY = int32_t(std::lround(b.start.y * 8));
            w.signedNumber(startX - previousX);
            w.signedNumber(startY - previousY);
            previousX = startX;
            previousY = startY;
            w.number(uint32_t(b.segments.size()));
            int32_t x = int32_t(std::lround(b.start.x * 8)), y = int32_t(std::lround(b.start.y * 8));
            for (auto s : b.segments) {
                int32_t nx = int32_t(std::lround(s.end.x * 8)), ny = int32_t(std::lround(s.end.y * 8));
                int32_t dx = nx - x, dy = ny - y;
                if (s.radius == 0 && dx % 8 == 0 && dy % 8 == 0 && std::abs(dx) <= 56 && std::abs(dy) <= 56)
                    w.bytes.push_back(uint8_t((dx / 8 + 7) * 15 + dy / 8 + 7));
                else {
                    w.bytes.push_back(s.radius > 0 ? 226 : 225);
                    w.signedNumber(dx);
                    w.signedNumber(dy);
                    if (s.radius > 0)
                        w.number((uint32_t(std::lround(s.radius * 8)) << 1) | (s.clockwise ? 1u : 0u));
                }
                x = nx;
                y = ny;
            }
        }
        for (const auto &region : r.regions) {
            w.number(region.area);
            w.bytes.push_back(region.color.b);
            w.bytes.push_back(region.color.g);
            w.bytes.push_back(region.color.r);
            auto p = region.primitive;
            w.number(uint32_t(p.kind));
            if (p.kind != PrimitiveKind::Path) {
                for (auto v : {p.x, p.y, p.width, p.height, p.radius})
                    w.coordinate(v);
            }
            w.number(uint32_t(region.loops.size()));
            for (const auto &loop : region.loops) {
                w.number(uint32_t(loop.boundaries.size()));
                int32_t previousId = 0;
                for (auto ref : loop.boundaries) {
                    int32_t delta = int32_t(ref.id) - previousId;
                    previousId = int32_t(ref.id);
                    auto zigzag = (uint32_t(delta) << 1) ^ uint32_t(delta >> 31);
                    w.number((zigzag << 1) | (ref.reversed ? 1u : 0u));
                }
            }
        }
    }
    std::vector<uint8_t> payload;
    if (w.bytes.size() > 64 * 1024 * 1024)
        throw std::runtime_error("Contour graph exceeds file limit");
    uint8_t packed = 0;
#ifdef _WIN32
    COMPRESSOR_HANDLE compressor{};
    if (CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &compressor)) {
        SIZE_T size{};
        // A roomy bound avoids running the compressor once just to query its output size.
        std::vector<uint8_t> candidate(w.bytes.size() + w.bytes.size() / 8 + 65536);
        if (Compress(compressor, w.bytes.data(), w.bytes.size(), candidate.data(), candidate.size(), &size) &&
            size < w.bytes.size()) {
            candidate.resize(size);
            payload = std::move(candidate);
            packed = 1;
        }
        CloseCompressor(compressor);
    }
#endif
    const auto rawSize = uint32_t(w.bytes.size());
    const auto hash = checksum(w.bytes);
    if (!packed)
        payload = std::move(w.bytes);
    std::vector<uint8_t> file{'C', 'G', 'R', '5', packed};
    file.reserve(13 + payload.size());
    word(file, rawSize);
    word(file, hash);
    file.insert(file.end(), payload.begin(), payload.end());
    return file;
}
Result decodeCompact(const std::vector<uint8_t> &file) {
    if (file.size() < 13 || std::memcmp(file.data(), "CGR5", 4) != 0 || file[4] > 1)
        throw std::runtime_error("Invalid contour header");
    const auto size = wordAt(file, 5);
    if (size > 64 * 1024 * 1024)
        throw std::runtime_error("Contour file too large");
    std::vector<uint8_t> bytes;
    if (!file[4])
        bytes.assign(file.begin() + 13, file.end());
    else {
#ifdef _WIN32
        bytes.resize(size);
        DECOMPRESSOR_HANDLE d{};
        if (!CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &d))
            throw std::runtime_error("Cannot decode contour file");
        SIZE_T written{};
        bool ok =
            Decompress(d, file.data() + 13, file.size() - 13, bytes.data(), bytes.size(), &written) != 0;
        CloseDecompressor(d);
        if (!ok || written != size)
            throw std::runtime_error("Corrupted compressed contour file");
#else
        throw std::runtime_error("This compressed contour file requires Windows");
#endif
    }
    if (bytes.size() != size || checksum(bytes) != wordAt(file, 9))
        throw std::runtime_error("Contour checksum mismatch");
    Reader read{bytes};
    Result r;
    r.sourceWidth = read.number();
    if (r.sourceWidth == UINT32_MAX)
        return readStrokes(read);
    r.sourceHeight = read.number();
    r.width = read.number();
    r.height = read.number();
    auto nb = read.number(), nr = read.number();
    if (!r.width || !r.height || r.width > 8192 || r.height > 8192 || nb > bytes.size() / 6 ||
        nr > bytes.size() / 4)
        throw std::runtime_error("Invalid contour dimensions/counts");
    int32_t previousRight = 0, previousX = 0, previousY = 0;
    r.boundaries.resize(nb);
    r.regions.resize(nr);
    for (auto &b : r.boundaries) {
        int64_t right, left;
        right = int64_t(previousRight) + read.signedNumber();
        left = right + read.signedNumber();
        previousRight = int32_t(right);
        if (right < 0 || left < 0 || right > nr || left > nr)
            throw std::runtime_error("Invalid contour region");
        b.right = right ? uint32_t(right - 1) : UINT32_MAX;
        b.left = left ? uint32_t(left - 1) : UINT32_MAX;
        auto boundaryFlags = read.number();
        if (boundaryFlags > 3)
            throw std::runtime_error("Invalid boundary visibility flags");
        b.closed = (boundaryFlags & 1) != 0;
        b.visible = (boundaryFlags & 2) == 0;
        {
            int64_t sx = int64_t(previousX) + read.signedNumber(),
                    sy = int64_t(previousY) + read.signedNumber();
            if (sx < 0 || sy < 0 || sx > int64_t(r.width) * 8 || sy > int64_t(r.height) * 8)
                throw std::runtime_error("Invalid boundary origin delta");
            previousX = int32_t(sx);
            previousY = int32_t(sy);
            b.start = {previousX / 8.f, previousY / 8.f};
        }
        if (b.start.x < 0 || b.start.y < 0 || b.start.x > r.width || b.start.y > r.height)
            throw std::runtime_error("Invalid contour start");
        auto n = read.number();
        if (!n || n > bytes.size() - read.offset)
            throw std::runtime_error("Invalid segment count");
        b.segments.reserve(n);
        int32_t x = int32_t(std::lround(b.start.x * 8)), y = int32_t(std::lround(b.start.y * 8));
        for (uint32_t i = 0; i < n; ++i) {
            auto command = read.byte();
            int32_t dx{}, dy{};
            uint32_t radius{};
            if (command < 225) {
                dx = (int(command) / 15 - 7) * 8;
                dy = (int(command) % 15 - 7) * 8;
            } else if (command <= 226) {
                dx = read.signedNumber();
                dy = read.signedNumber();
                if (command == 226)
                    radius = read.number();
            } else
                throw std::runtime_error("Unknown contour command");
            int64_t nx = int64_t(x) + dx, ny = int64_t(y) + dy;
            if (nx < 0 || ny < 0 || nx > int64_t(r.width) * 8 || ny > int64_t(r.height) * 8)
                throw std::runtime_error("Invalid contour coordinates");
            x = int32_t(nx);
            y = int32_t(ny);
            if ((radius >> 1) > 800000)
                throw std::runtime_error("Invalid contour radius");
            b.segments.push_back({{x / 8.f, y / 8.f}, (radius >> 1) / 8.f, (radius & 1) != 0});
        }
    }
    for (uint32_t id = 0; id < nr; ++id) {
        auto &region = r.regions[id];
        region.id = id;
        region.area = read.number();
        auto blue = read.byte(), green = read.byte(), red = read.byte();
        region.color = {blue, green, red, 255};
        auto kind = read.number();
        if (kind > 2)
            throw std::runtime_error("Invalid primitive");
        region.primitive.kind = PrimitiveKind(kind);
        if (kind) {
            auto &p = region.primitive;
            p.x = read.coordinate();
            p.y = read.coordinate();
            p.width = read.coordinate();
            p.height = read.coordinate();
            p.radius = read.coordinate();
        }
        auto loops = read.number();
        if (loops > bytes.size())
            throw std::runtime_error("Invalid loop count");
        for (uint32_t j = 0; j < loops; ++j) {
            Loop loop;
            auto refs = read.number();
            if (!refs || refs > nb || refs > bytes.size() - read.offset)
                throw std::runtime_error("Invalid loop references");
            loop.boundaries.reserve(refs);
            int32_t previousId = 0;
            for (uint32_t k = 0; k < refs; ++k) {
                auto v = read.number();
                {
                    auto zigzag = v >> 1;
                    int32_t delta = int32_t((zigzag >> 1) ^ uint32_t(-int32_t(zigzag & 1)));
                    int64_t idDelta = int64_t(previousId) + delta;
                    if (idDelta < 0 || idDelta >= nb)
                        throw std::runtime_error("Invalid boundary reference delta");
                    previousId = int32_t(idDelta);
                    v = (uint32_t(idDelta) << 1) | (v & 1);
                }
                if ((v >> 1) >= nb)
                    throw std::runtime_error("Invalid boundary reference");
                auto &b = r.boundaries[v >> 1];
                if (((v & 1) ? b.left : b.right) != id)
                    throw std::runtime_error("Invalid boundary orientation");
                loop.boundaries.push_back({v >> 1, (v & 1) != 0});
            }
            region.loops.push_back(std::move(loop));
        }
    }
    if (read.offset != bytes.size())
        throw std::runtime_error("Unexpected contour data");
    fitGeometry(r, 0);
    return r;
}
void saveCompactBytes(const std::vector<uint8_t> &bytes, const std::wstring &path) {
    extern void saveText(const std::string &, const std::wstring &);
    saveText(std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size()), path);
}
void saveCompact(const Result &r, const std::wstring &path) { saveCompactBytes(encodeCompact(r), path); }
Result loadCompact(const std::wstring &path, double *readMs, double *decodeMs) {
    auto started = std::chrono::steady_clock::now();
    auto size = std::filesystem::file_size(path);
    if (size > 64 * 1024 * 1024 || size < 13)
        throw std::runtime_error("Invalid contour file size");
    std::vector<uint8_t> bytes(size);
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(size)))
        throw std::runtime_error("Cannot read contour file");
    auto loaded = std::chrono::steady_clock::now();
    auto result = decodeCompact(bytes);
    auto decoded = std::chrono::steady_clock::now();
    if (readMs)
        *readMs = std::chrono::duration<double, std::milli>(loaded - started).count();
    if (decodeMs)
        *decodeMs = std::chrono::duration<double, std::milli>(decoded - loaded).count();
    return result;
}
} // namespace contour
