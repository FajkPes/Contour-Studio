#pragma once
#include "core/Contour.h"
#include <string>
namespace app {
contour::Image loadImage(const std::wstring &path);
void savePng(const contour::Image &, const std::wstring &path);
} // namespace app
