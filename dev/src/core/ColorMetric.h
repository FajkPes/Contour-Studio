#pragma once
#include "Contour.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
namespace contour::detail {
struct ColorMetric {
    int color, brightness;
    bool identity;
    explicit ColorMetric(const Settings &s) {
        if (!std::isfinite(s.colorSensitivity) || !std::isfinite(s.brightnessSensitivity) ||
            !std::isfinite(s.contrastMultiplier) || s.contrastMultiplier < .01f || s.contrastMultiplier > 2 ||
            s.colorSensitivity < 0 || s.colorSensitivity > 1 || s.brightnessSensitivity < 0 ||
            s.brightnessSensitivity > 1)
            throw std::invalid_argument("Invalid color/brightness sensitivity");
        color = int(std::lround(s.colorSensitivity * s.contrastMultiplier * 256));
        brightness = int(std::lround(s.brightnessSensitivity * s.contrastMultiplier * 256));
        identity = color == 256 && brightness == 256;
    }
    std::array<int16_t, 3> transform(int b, int g, int r) const {
        if (identity)
            return {int16_t(b), int16_t(g), int16_t(r)};
        int luma = (29 * b + 150 * g + 77 * r + 128) >> 8;
        auto channel = [&](int value) {
            return int16_t(std::clamp((value * color + luma * (brightness - color) + 128) >> 8, -511, 511));
        };
        return {channel(b), channel(g), channel(r)};
    }
    int contrast(Pixel a, Pixel b) const {
        auto delta = transform(int(a.b) - b.b, int(a.g) - b.g, int(a.r) - b.r);
        return std::min(
            255, std::max({std::abs(int(delta[0])), std::abs(int(delta[1])), std::abs(int(delta[2]))}));
    }
};
} // namespace contour::detail
