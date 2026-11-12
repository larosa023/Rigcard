// Rigcard - draws the build card with Direct2D. The same code draws the live preview and the
// PNG exports, so what you see is exactly what gets shared.
#pragma once
#include "common.h"
#include "card.h"
#include "sensors.h"
#include "bench.h"
#include "image.h"
#include <d2d1.h>

struct ExportSize {
    const wchar_t* id;
    const wchar_t* name;
    UINT w, h;
};
const std::vector<ExportSize>& ExportSizes();   // wide (Discord/Reddit), square, phone wallpaper, 4K…

struct CardInput {
    const Build* build = nullptr;
    SensorSnapshot live;
    bool liveRunning = false;                   // the app is polling (shows a LIVE badge)
    std::vector<BenchResult> bench;             // latest result per test, already filtered
    std::vector<BenchResult> history;           // for the bars (best ever per test)
    const Image* photo = nullptr;
    const Image* photoBlur = nullptr;
    int photoVersion = 0;                       // bump when the photo changes
    std::wstring footerUrl;
    float pulse = 0;                            // 0-1, animates the LIVE dot in the app
};

struct CardHit {
    D2D1_RECT_F photo{0, 0, 0, 0};              // where the photo was drawn (in render-target DIPs)
    D2D1_RECT_F card{0, 0, 0, 0};
};

namespace cardrender {
// Draws a card of design size (w, h) scaled to fit `area` on `rt`. Returns where things landed.
CardHit Draw(ID2D1RenderTarget* rt, const D2D1_RECT_F& area, UINT w, UINT h, const CardInput& in);
// Renders to a WIC bitmap (caller releases). For PNG export and copy-to-clipboard.
IWICBitmap* RenderBitmap(UINT w, UINT h, const CardInput& in);
bool ExportPng(const std::wstring& path, UINT w, UINT h, const CardInput& in, std::wstring* error);
void ReleaseDeviceResources();                 // the window's render target was recreated
std::wstring ShortName(const Part& p);         // "Ryzen 9 7900X", "RTX 4090", "64 GB DDR5-6000"
}
