#include "Renderer.h"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <microsoft.ui.xaml.media.dxinterop.h>
#include <stdexcept>
#include <wincodec.h>
namespace app {
namespace {
template <class T> using Ptr = Microsoft::WRL::ComPtr<T>;
void hr(HRESULT code) {
    if (FAILED(code))
        throw std::runtime_error("Direct2D rendering failed");
}
} // namespace
void Renderer::initialize(IUnknown *panel) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    // WARP: software presentation, CPU-only processing and rendering in this first version.
    hr(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &d3d_,
                         nullptr, nullptr));
    Ptr<IDXGIDevice> dxgi;
    hr(d3d_.As(&dxgi));
    Ptr<IDXGIAdapter> adapter;
    hr(dxgi->GetAdapter(&adapter));
    Ptr<IDXGIFactory2> dxgiFactory;
    hr(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory)));
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 1;
    desc.Height = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    hr(dxgiFactory->CreateSwapChainForComposition(d3d_.Get(), &desc, nullptr, &swap_));
    Ptr<ISwapChainPanelNative> native;
    hr(panel->QueryInterface(IID_PPV_ARGS(&native)));
    hr(native->SetSwapChain(swap_.Get()));
    hr(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&factory_)));
    Ptr<ID2D1Device> device;
    hr(factory_->CreateDevice(dxgi.Get(), &device));
    hr(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context_));
    hr(context_->CreateSolidColorBrush(D2D1::ColorF(0x39e6b4), &brush_));
}
void Renderer::resize(float width, float height, float dpiScale) {
    width_ = std::max(1.f, width);
    height_ = std::max(1.f, height);
    dpi_ = std::max(1.f, dpiScale);
    context_->SetTarget(nullptr);
    target_.Reset();
    hr(swap_->ResizeBuffers(0, UINT(std::ceil(width_ * dpi_)), UINT(std::ceil(height_ * dpi_)),
                            DXGI_FORMAT_UNKNOWN, 0));
    Ptr<IDXGISurface> surface;
    hr(swap_->GetBuffer(0, IID_PPV_ARGS(&surface)));
    auto props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96 * dpi_, 96 * dpi_);
    hr(context_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &target_));
    context_->SetTarget(target_.Get());
    context_->SetDpi(96 * dpi_, 96 * dpi_);
    draw();
}
void Renderer::setImage(std::shared_ptr<const contour::Image> image) {
    image_ = std::move(image);
    result_.reset();
    scene_ = std::make_shared<contour::RasterScene>();
    scene_->prepare(image_, nullptr);
    fit();
}
void Renderer::setResult(std::shared_ptr<const contour::Result> result,
                         std::shared_ptr<contour::RasterScene> scene) {
    result_ = std::move(result);
    if (scene)
        scene_ = std::move(scene);
    else {
        scene_ = std::make_shared<contour::RasterScene>();
        scene_->prepare(image_, result_);
    }
    draw();
}
float Renderer::scale() const {
    if (!image_)
        return 1;
    float w = float(result_ ? result_->width : image_->width),
          h = float(result_ ? result_->height : image_->height);
    return std::min((width_ - 24) / w, (height_ - 24) / h) * zoom_;
}
void Renderer::fit() {
    zoom_ = 1;
    offsetX_ = offsetY_ = 0;
    fitted_ = true;
    draw();
}
void Renderer::zoomAt(float amount, float x, float y) {
    float old = zoom_;
    zoom_ = std::clamp(zoom_ * amount, .15f, 32.f);
    float ratio = zoom_ / old;
    offsetX_ = (offsetX_ - (x - width_ / 2)) * ratio + (x - width_ / 2);
    offsetY_ = (offsetY_ - (y - height_ / 2)) * ratio + (y - height_ / 2);
    draw();
}
void Renderer::pan(float dx, float dy) {
    offsetX_ += dx;
    offsetY_ += dy;
    draw();
}
uint32_t Renderer::regionAt(float x, float y) const {
    if (!result_)
        return UINT32_MAX;
    float s = scale(), tx = (width_ - result_->width * s) / 2 + offsetX_,
          ty = (height_ - result_->height * s) / 2 + offsetY_;
    int ix = int(std::floor((x - tx) / s)), iy = int(std::floor((y - ty) / s));
    if (ix < 0 || iy < 0 || ix >= int(result_->width) || iy >= int(result_->height))
        return UINT32_MAX;
    return scene_->regionAt(uint32_t(ix), uint32_t(iy));
}
void Renderer::draw() {
    if (!target_)
        return;
    const auto begin = std::chrono::steady_clock::now();
    float w = float(result_ ? result_->width : (image_ ? image_->width : 1)),
          h = float(result_ ? result_->height : (image_ ? image_->height : 1)), s = std::max(.0001f, scale());
    if (scene_)
        scene_->render(frame_, uint32_t(std::ceil(width_ * dpi_)), uint32_t(std::ceil(height_ * dpi_)),
                       s * dpi_, ((width_ - w * s) / 2 + offsetX_) * dpi_,
                       ((height_ - h * s) / 2 + offsetY_) * dpi_, contour::RasterMode(mode), opacity,
                       lineWidth * dpi_, true, outline);
    else {
        frame_.width = uint32_t(std::ceil(width_ * dpi_));
        frame_.height = uint32_t(std::ceil(height_ * dpi_));
        frame_.pixels.assign(size_t(frame_.width) * frame_.height, {32, 24, 21, 255});
    }
    if (!bitmap_ || bitmap_->GetPixelSize().width != frame_.width ||
        bitmap_->GetPixelSize().height != frame_.height) {
        bitmap_.Reset();
        auto props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96 * dpi_,
            96 * dpi_);
        hr(context_->CreateBitmap({frame_.width, frame_.height}, frame_.pixels.data(), frame_.width * 4,
                                  &props, &bitmap_));
    } else
        hr(bitmap_->CopyFromMemory(nullptr, frame_.pixels.data(), frame_.width * 4));
    context_->BeginDraw();
    context_->SetTransform(D2D1::Matrix3x2F::Identity());
    context_->DrawBitmap(bitmap_.Get(), D2D1::RectF(0, 0, width_, height_), 1,
                         D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    hr(context_->EndDraw());
    hr(swap_->Present(0, 0));
    lastRenderMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}
void previewPng(const contour::Image &source, const contour::Result &result, const std::wstring &path,
                ViewMode mode) {
    Ptr<IWICImagingFactory> wic;
    hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)));
    contour::RasterScene scene;
    scene.prepare(std::make_shared<contour::Image>(source), std::make_shared<contour::Result>(result));
    contour::Image pixels;
    scene.render(pixels, result.width, result.height, 1, 0, 0, contour::RasterMode(mode), .9f, .7f);
    Ptr<IWICStream> stream;
    hr(wic->CreateStream(&stream));
    hr(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    Ptr<IWICBitmapEncoder> encoder;
    hr(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    hr(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    Ptr<IWICBitmapFrameEncode> frame;
    hr(encoder->CreateNewFrame(&frame, nullptr));
    hr(frame->Initialize(nullptr));
    hr(frame->SetSize(result.width, result.height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    hr(frame->SetPixelFormat(&format));
    hr(frame->WritePixels(result.height, result.width * 4, UINT(pixels.pixels.size() * 4),
                          reinterpret_cast<BYTE *>(pixels.pixels.data())));
    hr(frame->Commit());
    hr(encoder->Commit());
}
} // namespace app
