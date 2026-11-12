// Rigcard - images through WIC: load (with EXIF rotation), resize, blur, JPEG/PNG encode, clipboard.
#pragma once
#include "common.h"
#include <wincodec.h>

struct Image {
    UINT w = 0, h = 0;
    std::vector<BYTE> px;          // 32bpp premultiplied BGRA, stride w*4
    bool Empty() const { return px.empty(); }
};

namespace image {
IWICImagingFactory* Wic();
bool Load(const std::wstring& path, UINT maxDim, Image& out, std::wstring* error);
Image Resize(const Image& src, UINT w, UINT h);
Image Blurred(const Image& src, UINT targetW);           // small, soft copy for blurred backgrounds
bool EncodeJpeg(const Image& img, float quality, std::string& out);
bool EncodePng(IWICBitmapSource* src, std::string& out);
bool SaveBytes(const std::wstring& path, const std::string& data);
bool CopyToClipboard(HWND owner, IWICBitmap* bmp);       // as an image (DIB) and as PNG
}
