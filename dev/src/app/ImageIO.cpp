#include "ImageIO.h"
#include <Windows.h>
#include <stdexcept>
#include <wincodec.h>
#include <wrl/client.h>
namespace app {
using Microsoft::WRL::ComPtr;
void checked(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error("Windows image decoder failed (unsupported or damaged image)");
}
void savePng(const contour::Image &image, const std::wstring &path) {
    ComPtr<IWICImagingFactory> factory;
    checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICStream> stream;
    checked(factory->CreateStream(&stream));
    checked(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    checked(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    checked(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    checked(encoder->CreateNewFrame(&frame, nullptr));
    checked(frame->Initialize(nullptr));
    checked(frame->SetSize(image.width, image.height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    checked(frame->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat32bppBGRA)
        throw std::runtime_error("PNG encoder rejected BGRA");
    checked(frame->WritePixels(image.height, image.width * 4, UINT(image.pixels.size() * 4),
                               reinterpret_cast<BYTE *>(const_cast<contour::Pixel *>(image.pixels.data()))));
    checked(frame->Commit());
    checked(encoder->Commit());
}
contour::Image loadImage(const std::wstring &path) {
    ComPtr<IWICImagingFactory> factory;
    checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmapDecoder> decoder;
    checked(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                               WICDecodeMetadataCacheOnDemand, &decoder));
    ComPtr<IWICBitmapFrameDecode> frame;
    checked(decoder->GetFrame(0, &frame));
    UINT w, h;
    checked(frame->GetSize(&w, &h));
    if (!w || !h || uint64_t(w) * h > 67108864)
        throw std::runtime_error("Image exceeds 64 megapixels (8K supported)");
    ComPtr<IWICFormatConverter> converter;
    checked(factory->CreateFormatConverter(&converter));
    checked(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr,
                                  0, WICBitmapPaletteTypeCustom));
    contour::Image image{w, h, std::vector<contour::Pixel>(size_t(w) * h)};
    checked(converter->CopyPixels(nullptr, w * 4, UINT(image.pixels.size() * 4),
                                  reinterpret_cast<BYTE *>(image.pixels.data())));
    return image;
}
} // namespace app
