#pragma once
#include "Contour.h"
#include <memory>
namespace contour {
enum class RasterMode { Overlay, Original, Edges, Regions };
struct RasterLine {
    Point a, b;
    float width{1};
    bool roundStart{}, roundEnd{};
};
// Own CPU geometry rasterizer. Presentation APIs only upload the finished bitmap.
class RasterScene {
  public:
    void prepare(std::shared_ptr<const Image> source, std::shared_ptr<const Result> result);
    void render(Image &output, uint32_t width, uint32_t height, float scale, float tx, float ty,
                RasterMode mode, float opacity, float lineWidth, bool parallel = true,
                Pixel outline = {180, 230, 57, 255}) const;
    uint32_t regionAt(uint32_t x, uint32_t y) const;

  private:
    std::shared_ptr<const Image> source_;
    std::shared_ptr<const Result> result_;
    Image fills_;
    std::vector<uint32_t> regionIds_;
    std::vector<RasterLine> lines_;
};
} // namespace contour
