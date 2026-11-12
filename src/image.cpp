// Rigcard - images through WIC
#include "image.h"
#include "util.h"
#include <propvarutil.h>
#include <algorithm>

namespace image {

IWICImagingFactory* Wic() {
    static IWICImagingFactory* f = nullptr;
    if (!f) CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
    return f;
}

template <class T> static void Rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

static int ExifOrientation(IWICBitmapFrameDecode* frame) {
    IWICMetadataQueryReader* q = nullptr;
    int o = 1;
    if (SUCCEEDED(frame->GetMetadataQueryReader(&q)) && q) {
        PROPVARIANT v;
        PropVariantInit(&v);
        for (const wchar_t* path : {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}"}) {
            if (SUCCEEDED(q->GetMetadataByName(path, &v))) {
                if (v.vt == VT_UI2) o = v.uiVal;
                PropVariantClear(&v);
                break;
            }
        }
        q->Release();
    }
    return o;
}

bool Load(const std::wstring& path, UINT maxDim, Image& out, std::wstring* error) {
    IWICImagingFactory* f = Wic();
    if (!f) { if (error) *error = L"Windows Imaging isn't available."; return false; }
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapFlipRotator* rot = nullptr;
    IWICBitmapScaler* sc = nullptr;
    IWICFormatConverter* conv = nullptr;
    bool ok = false;
    if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) ||
        FAILED(dec->GetFrame(0, &frame))) {
        if (error) *error = L"That file isn't an image Windows can open (try JPG, PNG, WebP or HEIC with the extension installed).";
    } else {
        IWICBitmapSource* src = frame;
        int o = ExifOrientation(frame);
        WICBitmapTransformOptions t = WICBitmapTransformRotate0;
        switch (o) {
            case 2: t = WICBitmapTransformFlipHorizontal; break;
            case 3: t = WICBitmapTransformRotate180; break;
            case 4: t = WICBitmapTransformFlipVertical; break;
            case 6: t = WICBitmapTransformRotate90; break;
            case 8: t = WICBitmapTransformRotate270; break;
            default: break;
        }
        if (t != WICBitmapTransformRotate0 && SUCCEEDED(f->CreateBitmapFlipRotator(&rot)) && SUCCEEDED(rot->Initialize(src, t))) src = rot;
        UINT w = 0, h = 0;
        src->GetSize(&w, &h);
        if (w && h && maxDim && std::max(w, h) > maxDim) {
            double k = (double)maxDim / std::max(w, h);
            UINT nw = std::max<UINT>(1, (UINT)(w * k)), nh = std::max<UINT>(1, (UINT)(h * k));
            if (SUCCEEDED(f->CreateBitmapScaler(&sc)) && SUCCEEDED(sc->Initialize(src, nw, nh, (WICBitmapInterpolationMode)4 /*HighQualityCubic*/))) {
                src = sc; w = nw; h = nh;
            }
        }
        if (SUCCEEDED(f->CreateFormatConverter(&conv)) &&
            SUCCEEDED(conv->Initialize(src, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
            out.w = w;
            out.h = h;
            out.px.resize((size_t)w * h * 4);
            ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 4, (UINT)out.px.size(), out.px.data()));
            if (!ok && error) *error = L"Couldn't read the image.";
        }
    }
    Rel(conv); Rel(sc); Rel(rot); Rel(frame); Rel(dec);
    return ok;
}

Image Resize(const Image& src, UINT w, UINT h) {
    Image out;
    IWICImagingFactory* f = Wic();
    IWICBitmap* b = nullptr;
    IWICBitmapScaler* sc = nullptr;
    if (f && !src.Empty() && SUCCEEDED(f->CreateBitmapFromMemory(src.w, src.h, GUID_WICPixelFormat32bppPBGRA, src.w * 4, (UINT)src.px.size(), (BYTE*)src.px.data(), &b)) &&
        SUCCEEDED(f->CreateBitmapScaler(&sc)) && SUCCEEDED(sc->Initialize(b, w, h, WICBitmapInterpolationModeFant))) {
        out.w = w; out.h = h;
        out.px.resize((size_t)w * h * 4);
        if (FAILED(sc->CopyPixels(nullptr, w * 4, (UINT)out.px.size(), out.px.data()))) out = Image{};
    }
    Rel(sc); Rel(b);
    return out;
}

// three box-blur passes ≈ a Gaussian
static void BoxBlur(Image& im, int r) {
    if (r < 1 || im.Empty()) return;
    std::vector<BYTE> tmp(im.px.size());
    int W = (int)im.w, H = (int)im.h;
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < H; ++y) {      // horizontal
            for (int c = 0; c < 4; ++c) {
                int sum = 0, n = 0;
                for (int x = -r; x <= r; ++x) { int xx = std::clamp(x, 0, W - 1); sum += im.px[(y * W + xx) * 4 + c]; ++n; }
                for (int x = 0; x < W; ++x) {
                    tmp[(y * W + x) * 4 + c] = (BYTE)(sum / n);
                    int out = std::clamp(x - r, 0, W - 1), in = std::clamp(x + r + 1, 0, W - 1);
                    sum += im.px[(y * W + in) * 4 + c] - im.px[(y * W + out) * 4 + c];
                }
            }
        }
        for (int x = 0; x < W; ++x) {      // vertical
            for (int c = 0; c < 4; ++c) {
                int sum = 0, n = 0;
                for (int y = -r; y <= r; ++y) { int yy = std::clamp(y, 0, H - 1); sum += tmp[(yy * W + x) * 4 + c]; ++n; }
                for (int y = 0; y < H; ++y) {
                    im.px[(y * W + x) * 4 + c] = (BYTE)(sum / n);
                    int out = std::clamp(y - r, 0, H - 1), in = std::clamp(y + r + 1, 0, H - 1);
                    sum += tmp[(in * W + x) * 4 + c] - tmp[(out * W + x) * 4 + c];
                }
            }
        }
    }
}

Image Blurred(const Image& src, UINT targetW) {
    if (src.Empty()) return {};
    UINT w = std::max<UINT>(8, std::min(targetW, src.w));
    UINT h = std::max<UINT>(8, (UINT)((double)src.h * w / src.w));
    Image small = Resize(src, w, h);
    BoxBlur(small, std::max(2, (int)w / 40));
    return small;
}

static bool Encode(IWICBitmapSource* src, const GUID& container, float quality, std::string& out) {
    IWICImagingFactory* f = Wic();
    if (!f || !src) return false;
    IStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* props = nullptr;
    bool ok = false;
    if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) && SUCCEEDED(f->CreateEncoder(container, nullptr, &enc)) &&
        SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&frame, &props))) {
        if (container == GUID_ContainerFormatJpeg && props) {
            PROPBAG2 opt{};
            opt.pstrName = (LPOLESTR)L"ImageQuality";
            VARIANT v;
            VariantInit(&v);
            v.vt = VT_R4;
            v.fltVal = quality;
            props->Write(1, &opt, &v);
        }
        UINT w = 0, h = 0;
        src->GetSize(&w, &h);
        WICPixelFormatGUID fmt = container == GUID_ContainerFormatJpeg ? GUID_WICPixelFormat24bppBGR : GUID_WICPixelFormat32bppBGRA;
        IWICFormatConverter* conv = nullptr;
        IWICBitmapSource* feed = src;
        if (SUCCEEDED(f->CreateFormatConverter(&conv)) &&
            SUCCEEDED(conv->Initialize(src, fmt, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) feed = conv;
        if (SUCCEEDED(frame->Initialize(props)) && SUCCEEDED(frame->SetSize(w, h)) && SUCCEEDED(frame->SetPixelFormat(&fmt)) &&
            SUCCEEDED(frame->WriteSource(feed, nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit())) {
            HGLOBAL g = nullptr;
            if (SUCCEEDED(GetHGlobalFromStream(stream, &g))) {
                STATSTG st{};
                stream->Stat(&st, STATFLAG_NONAME);
                const char* p = (const char*)GlobalLock(g);
                if (p) { out.assign(p, (size_t)st.cbSize.QuadPart); GlobalUnlock(g); ok = true; }
            }
        }
        Rel(conv);
    }
    Rel(props); Rel(frame); Rel(enc); Rel(stream);
    return ok;
}

bool EncodeJpeg(const Image& img, float quality, std::string& out) {
    IWICImagingFactory* f = Wic();
    IWICBitmap* b = nullptr;
    if (!f || img.Empty() || FAILED(f->CreateBitmapFromMemory(img.w, img.h, GUID_WICPixelFormat32bppPBGRA, img.w * 4, (UINT)img.px.size(), (BYTE*)img.px.data(), &b))) return false;
    bool ok = Encode(b, GUID_ContainerFormatJpeg, quality, out);
    b->Release();
    return ok;
}

bool EncodePng(IWICBitmapSource* src, std::string& out) { return Encode(src, GUID_ContainerFormatPng, 1.0f, out); }

bool SaveBytes(const std::wstring& path, const std::string& data) { return WriteFileAtomic(path, data); }

bool CopyToClipboard(HWND owner, IWICBitmap* bmp) {
    if (!bmp) return false;
    UINT w = 0, h = 0;
    bmp->GetSize(&w, &h);
    std::string png;
    EncodePng(bmp, png);
    // a bottom-up 32-bit DIB (premultiplied → straight alpha for apps that read it)
    std::vector<BYTE> px((size_t)w * h * 4);
    WICRect rc{0, 0, (INT)w, (INT)h};
    if (FAILED(bmp->CopyPixels(&rc, w * 4, (UINT)px.size(), px.data()))) return false;
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    BITMAPINFOHEADER bi{sizeof(bi)};
    bi.biWidth = (LONG)w;
    bi.biHeight = (LONG)h;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = w * h * 4;
    if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, sizeof(bi) + px.size())) {
        BYTE* p = (BYTE*)GlobalLock(g);
        memcpy(p, &bi, sizeof(bi));
        for (UINT y = 0; y < h; ++y) {
            BYTE* dst = p + sizeof(bi) + (size_t)(h - 1 - y) * w * 4;
            const BYTE* src = px.data() + (size_t)y * w * 4;
            for (UINT x = 0; x < w; ++x) {
                BYTE a = src[x * 4 + 3];
                for (int c = 0; c < 3; ++c) dst[x * 4 + c] = a ? (BYTE)std::min(255, src[x * 4 + c] * 255 / a) : 0;
                dst[x * 4 + 3] = a;
            }
        }
        GlobalUnlock(g);
        if (!SetClipboardData(CF_DIB, g)) GlobalFree(g);
    }
    if (!png.empty()) {
        UINT fmt = RegisterClipboardFormatW(L"PNG");
        if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, png.size())) {
            memcpy(GlobalLock(g), png.data(), png.size());
            GlobalUnlock(g);
            if (!SetClipboardData(fmt, g)) GlobalFree(g);
        }
    }
    CloseClipboard();
    return true;
}

} // namespace image
