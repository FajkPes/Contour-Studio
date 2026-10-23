#pragma once
#include "core/Contour.h"
#include "core/Raster.h"
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <memory>
#include <vector>
#include <wrl/client.h>
namespace app {
enum class ViewMode { Overlay, Original, Edges, Regions };
class Renderer {
  public:
    void initialize(IUnknown *panel);
    void resize(float width, float height, float dpiScale);
    void setImage(std::shared_ptr<const contour::Image> image);
    void setResult(std::shared_ptr<const contour::Result> result,
                   std::shared_ptr<contour::RasterScene> scene = {});
    void draw();
    void fit();
    void zoomAt(float amount, float x, float y);
    void pan(float dx, float dy);
    uint32_t regionAt(float x, float y) const;
    const contour::Image &frame() const { return frame_; }
    float zoom() const {
        return zoom_;
    }
    ViewMode mode{ViewMode::Overlay};
    float opacity{1.f}, lineWidth{1.0f};
    double lastRenderMs{};
    contour::Pixel outline{180, 230, 57, 255};

  private:
    template <class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> d3d_;
    Ptr<IDXGISwapChain1> swap_;
    Ptr<ID2D1Factory1> factory_;
    Ptr<ID2D1DeviceContext> context_;
    Ptr<ID2D1Bitmap1> target_, bitmap_;
    Ptr<ID2D1SolidColorBrush> brush_;
    std::shared_ptr<contour::RasterScene> scene_;
    contour::Image frame_;
    std::shared_ptr<const contour::Image> image_;
    std::shared_ptr<const contour::Result> result_;
    float width_{1}, height_{1}, dpi_{1}, zoom_{1}, offsetX_{}, offsetY_{};
    bool fitted_{};
    float scale() const;
};
void previewPng(const contour::Image &, const contour::Result &, const std::wstring &path,
                ViewMode mode = ViewMode::Overlay);
} // namespace app
