// Rigcard - the card renderer (Direct2D, design units scaled to fit)
#include "cardrender.h"
#include "gfx.h"
#include "util.h"
#include "config.h"
#include <map>
#include <tuple>
#include <algorithm>
#include <ctime>

using namespace gfx;

const std::vector<ExportSize>& ExportSizes() {
    static std::vector<ExportSize> s = {
        {L"wide", L"Wide 1920 × 1080 · Discord, Reddit", 1920, 1080},
        {L"square", L"Square 1080 × 1080 · social post", 1080, 1080},
        {L"portrait", L"Portrait 1080 × 1350 · Instagram", 1080, 1350},
        {L"phone", L"Phone wallpaper 1170 × 2532", 1170, 2532},
        {L"4k", L"4K 3840 × 2160", 3840, 2160},
    };
    return s;
}

namespace cardrender {

// ---------------------------------------------------------------- resources
static std::wstring FontOr(const std::wstring& want, bool head) {
    static std::map<std::wstring, bool> known;
    auto exists = [](const std::wstring& n) {
        auto it = known.find(n);
        if (it != known.end()) return it->second;
        IDWriteFontCollection* sys = nullptr;
        dw->GetSystemFontCollection(&sys, FALSE);
        UINT32 idx;
        BOOL e = FALSE;
        if (sys) { sys->FindFamilyName(n.c_str(), &idx, &e); sys->Release(); }
        known[n] = e != FALSE;
        return e != FALSE;
    };
    if (!want.empty() && exists(want)) return want;
    for (const wchar_t* f : {head ? L"Segoe UI Variable Display" : L"Segoe UI Variable Text", L"Segoe UI", L"Tahoma", L"DejaVu Sans", L"Arial"})
        if (exists(f)) return f;
    return L"Segoe UI";
}

static IDWriteTextFormat* Fmt(const std::wstring& family, float size, DWRITE_FONT_WEIGHT w, DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_LEADING, bool wrap = false) {
    static std::map<std::tuple<std::wstring, int, int, int, bool>, IDWriteTextFormat*> cache;
    auto key = std::make_tuple(family, (int)(size * 4), (int)w, (int)a, wrap);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    IDWriteTextFormat* f = nullptr;
    dw->CreateTextFormat(family.c_str(), nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, std::max(1.0f, size), L"en-us", &f);
    if (f) {
        f->SetTextAlignment(a);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        if (!wrap) {
            IDWriteInlineObject* ell = nullptr;
            dw->CreateEllipsisTrimmingSign(f, &ell);
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            f->SetTrimming(&trim, ell);
            if (ell) ell->Release();
        }
    }
    if (cache.size() > 600) cache.clear();   // the old formats leak a little; fonts change rarely
    cache[key] = f;
    return f;
}

struct BitmapCache {
    ID2D1RenderTarget* rt = nullptr;
    int version = -1;
    ID2D1Bitmap* photo = nullptr;
    ID2D1Bitmap* blur = nullptr;
};
static BitmapCache s_window;

void ReleaseDeviceResources() {
    SafeRelease(s_window.photo);
    SafeRelease(s_window.blur);
    s_window.rt = nullptr;
    s_window.version = -1;
}

static ID2D1Bitmap* MakeBitmap(ID2D1RenderTarget* rt, const Image* im) {
    if (!im || im->Empty()) return nullptr;
    ID2D1Bitmap* b = nullptr;
    D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    rt->CreateBitmap(D2D1::SizeU(im->w, im->h), im->px.data(), im->w * 4, bp, &b);
    return b;
}

// ---------------------------------------------------------------- drawing context
struct Ctx {
    ID2D1RenderTarget* rt;
    ID2D1SolidColorBrush* br;
    const CardInput& in;
    const Build& b;
    const CardStyle& st;
    std::wstring body, head;
    ID2D1Bitmap* photo = nullptr;
    ID2D1Bitmap* blur = nullptr;
    float W, H;
    D2D1_RECT_F photoRect{0, 0, 0, 0};

    D2D1_COLOR_F C(uint32_t rgb, float a = 1) const { return Rgb(rgb, a); }
    D2D1_COLOR_F Text() const { return C(st.text); }
    D2D1_COLOR_F Sub() const { return C(st.subtext); }
    D2D1_COLOR_F Acc() const { return C(st.accent); }
    D2D1_COLOR_F Acc2() const { return C(st.accent2); }
    bool Light() const { return Luma(C(st.text)) < 0.45f; }   // dark text → light card

    void Fill(const D2D1_RECT_F& r, float rad, D2D1_COLOR_F c) {
        br->SetColor(c);
        if (rad <= 0.01f) rt->FillRectangle(r, br); else rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), br);
    }
    void Stroke(const D2D1_RECT_F& r, float rad, D2D1_COLOR_F c, float w) {
        br->SetColor(c);
        D2D1_RECT_F rr = D2D1::RectF(r.left + w / 2, r.top + w / 2, r.right - w / 2, r.bottom - w / 2);
        if (rad <= 0.01f) rt->DrawRectangle(rr, br, w); else rt->DrawRoundedRectangle(D2D1::RoundedRect(rr, rad, rad), br, w);
    }
    void Txt(const std::wstring& s, IDWriteTextFormat* f, const D2D1_RECT_F& r, D2D1_COLOR_F c, bool glow = false) {
        if (!f || s.empty()) return;
        if (glow && st.glow) {
            for (int k = 0; k < 6; ++k) {
                float dx = (k % 3 - 1) * 2.0f, dy = (k / 3 == 0 ? -1.5f : 1.5f);
                br->SetColor(Alpha(c, 0.10f));
                D2D1_RECT_F g = D2D1::RectF(r.left + dx, r.top + dy, r.right + dx, r.bottom + dy);
                rt->DrawText(s.c_str(), (UINT32)s.size(), f, g, br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
        br->SetColor(c);
        rt->DrawText(s.c_str(), (UINT32)s.size(), f, r, br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    float Width(const std::wstring& s, IDWriteTextFormat* f) { return MeasureWidth(s, f); }
    IDWriteTextFormat* B(float size, DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_LEADING) { return Fmt(body, size, w, a); }
    IDWriteTextFormat* Hd(float size, DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_LEADING) { return Fmt(head, size, w, a); }

    void Panel(const D2D1_RECT_F& r, float rad) {
        if (st.layout == L"minimal") return;
        Fill(r, rad, C(st.panel, st.panelOpacity));
        if (st.border) Stroke(r, rad, st.glow ? Alpha(Acc(), 0.35f) : (Light() ? C(0x000000, 0.08f) : C(0xFFFFFF, 0.08f)), 1.2f);
    }

    // Photo, cover-fit, with your zoom and focus point, clipped to a rounded rect
    void Photo(ID2D1Bitmap* bmp, const D2D1_RECT_F& r, float rad, float opacity = 1.0f, bool useFocus = true) {
        if (!bmp) return;
        D2D1_SIZE_F s = bmp->GetSize();
        float rw = r.right - r.left, rh = r.bottom - r.top;
        if (rw <= 1 || rh <= 1 || s.width < 1 || s.height < 1) return;
        float zoom = useFocus ? b.photoZoom : 1.0f;
        float k = std::max(rw / s.width, rh / s.height) * zoom;
        float dw = s.width * k, dh = s.height * k;
        float fx = useFocus ? b.photoX : 0.5f, fy = useFocus ? b.photoY : 0.5f;
        float left = r.left + (rw - dw) * fx, top = r.top + (rh - dh) * fy;
        ID2D1RoundedRectangleGeometry* g = nullptr;
        d2d->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r, rad, rad), &g);
        ID2D1Layer* layer = nullptr;
        rt->CreateLayer(nullptr, &layer);
        rt->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (g && layer) rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), g, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE), layer);
        rt->DrawBitmap(bmp, D2D1::RectF(left, top, left + dw, top + dh), opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        if (g && layer) rt->PopLayer();
        rt->PopAxisAlignedClip();
        SafeRelease(layer);
        SafeRelease(g);
    }
};

std::wstring ShortName(const Part& p) {
    std::wstring n = p.name;
    auto strip = [&](const wchar_t* pre) { if (IStartsWith(n, pre)) n = n.substr(wcslen(pre)); };
    if (p.kind == L"cpu") { strip(L"AMD "); strip(L"Intel(R) "); strip(L"Intel "); }
    if (p.kind == L"gpu") { strip(L"NVIDIA "); strip(L"GeForce "); strip(L"AMD "); strip(L"Radeon(TM) "); strip(L"Intel(R) "); strip(L"Intel "); }
    return Trim(n);
}

static const Part* First(const Build& b, const wchar_t* kind) {
    for (auto& p : b.parts) if (p.kind == kind && !p.hidden) return &p;
    return nullptr;
}

static std::vector<const Part*> CardParts(const Build& b) {
    std::vector<const Part*> out;
    for (auto& p : b.parts) {
        if (p.hidden || p.name.empty() || p.kind == L"os") continue;
        if (IsPeripheral(p.kind) && !b.showPeripherals) continue;
        out.push_back(&p);
    }
    return out;
}

static std::wstring Upper(std::wstring s) { if (!s.empty()) CharUpperBuffW(&s[0], (DWORD)s.size()); return s; }

// ---------------------------------------------------------------- sections
static float SectionTitle(Ctx& c, float x, float y, float w, const std::wstring& title, float f, const std::wstring& badge = L"", D2D1_COLOR_F badgeColor = {}) {
    float h = 30 * f;
    c.Fill(D2D1::RectF(x, y + 7 * f, x + 4 * f, y + h - 7 * f), 2 * f, c.Acc());
    c.Txt(Upper(title), c.B(14.5f * f, DWRITE_FONT_WEIGHT_BOLD), D2D1::RectF(x + 14 * f, y, x + w, y + h), c.Sub());
    if (!badge.empty()) {
        auto* bf = c.B(13 * f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        float bw = c.Width(badge, bf) + (badge == L"LIVE" ? 34 : 24) * f;
        D2D1_RECT_F br = D2D1::RectF(x + w - bw, y + 3 * f, x + w, y + h - 3 * f);
        c.Fill(br, (h - 6 * f) / 2, Alpha(badgeColor, 0.16f));
        float off = badge == L"LIVE" ? 14 * f : 0;
        c.Txt(badge, bf, D2D1::RectF(br.left + off, br.top, br.right, br.bottom), badgeColor);
        if (badge == L"LIVE") {
            float pulse = 0.55f + 0.45f * std::sin(c.in.pulse * 6.2832f);
            c.br->SetColor(Alpha(badgeColor, c.in.liveRunning ? pulse : 1.0f));
            c.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(br.left + 11 * f, (br.top + br.bottom) / 2), 3.5f * f, 3.5f * f), c.br);
        }
    }
    return h + 12 * f;
}

static float Parts(Ctx& c, const D2D1_RECT_F& r, float f, bool draw, int cols) {
    auto parts = CardParts(c.b);
    float pad = 26 * f, y = r.top + pad;
    float x = r.left + pad, w = r.right - r.left - 2 * pad;
    if (draw) y += SectionTitle(c, x, y, w, L"Parts", f);
    else y += 42 * f;
    cols = std::max(1, cols);
    float colW = (w - (cols - 1) * 28 * f) / cols;
    float labelW = std::min(150.0f * f, colW * 0.30f);
    float rowY[2] = {y, y};
    size_t shown = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        const Part& p = *parts[i];
        int col = cols == 2 ? (int)(i % 2) : 0;
        std::wstring detail = PartDetailForCard(p);
        float rh = (detail.empty() ? 40 : 60) * f;
        float ry = rowY[col];
        if (draw) {
            if (ry + rh > r.bottom - pad + 10 * f) { break; }
            float cx = x + col * (colW + 28 * f);
            c.Txt(Upper(KindLabel(p.kind)), c.B(12.5f * f, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(cx, ry, cx + labelW - 8 * f, ry + 26 * f), c.Sub());
            c.Txt(p.name, c.B(20 * f, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(cx + labelW, ry, cx + colW, ry + 28 * f), c.Text());
            if (!detail.empty()) c.Txt(detail, c.B(15 * f), D2D1::RectF(cx + labelW, ry + 27 * f, cx + colW, ry + 50 * f), c.Sub());
        }
        rowY[col] += rh;
        ++shown;
    }
    if (draw && shown < parts.size()) {
        std::wstring more = L"+" + std::to_wstring(parts.size() - shown) + L" more";
        c.Txt(more, c.B(14 * f, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(x + labelW, r.bottom - pad - 18 * f, x + w, r.bottom - pad + 4 * f), c.Acc());
    }
    if (parts.empty() && draw)
        c.Txt(L"Scanning your hardware…", c.B(17 * f), D2D1::RectF(x, y, x + w, y + 30 * f), c.Sub());
    return std::max(rowY[0], rowY[1]) - r.top + pad - 6 * f;
}

static std::wstring Val(float v, int dec = 0) {
    if (std::isnan(v)) return L"—";
    wchar_t b[32];
    swprintf(b, 32, L"%.*f", dec, v);
    return b;
}

struct Tile { std::wstring label, value, unit, sub; float bar; D2D1_COLOR_F color; };

static std::vector<Tile> LiveTiles(Ctx& c) {
    const SensorSnapshot& s = c.in.live;
    auto heat = [&](float t) {
        if (std::isnan(t)) return c.Acc();
        if (t >= 85) return Rgb(0xFF5D5D);
        if (t >= 72) return Rgb(0xFFB347);
        return c.Acc();
    };
    std::vector<Tile> t;
    auto loadClock = [](float load, float mhz) {
        std::wstring x;
        if (!std::isnan(load)) x += Val(load) + L"% load";
        if (!std::isnan(mhz) && mhz > 0) {
            wchar_t b[32];
            swprintf(b, 32, L"%.2f GHz", mhz / 1000.0f);
            x += (x.empty() ? L"" : L" · ") + std::wstring(b);
        }
        return x;
    };
    t.push_back({L"CPU", Val(s.cpuTemp), std::isnan(s.cpuTemp) ? L"" : L"°C", loadClock(s.cpuLoad, s.cpuMhz),
                 std::isnan(s.cpuTemp) ? (std::isnan(s.cpuLoad) ? 0.0f : s.cpuLoad / 100) : s.cpuTemp / 100, heat(s.cpuTemp)});
    t.push_back({L"GPU", Val(s.gpuTemp), std::isnan(s.gpuTemp) ? L"" : L"°C", loadClock(s.gpuLoad, s.gpuMhz),
                 std::isnan(s.gpuTemp) ? (std::isnan(s.gpuLoad) ? 0.0f : s.gpuLoad / 100) : s.gpuTemp / 100, heat(s.gpuTemp)});
    float total = NAN;
    if (!std::isnan(s.cpuPower) || !std::isnan(s.gpuPower)) total = (std::isnan(s.cpuPower) ? 0 : s.cpuPower) + (std::isnan(s.gpuPower) ? 0 : s.gpuPower);
    std::wstring psub;
    if (!std::isnan(s.cpuPower)) psub += L"CPU " + Val(s.cpuPower) + L" W";
    if (!std::isnan(s.gpuPower)) psub += (psub.empty() ? L"" : L" · ") + std::wstring(L"GPU ") + Val(s.gpuPower) + L" W";
    t.push_back({L"Power", Val(total), std::isnan(total) ? L"" : L"W", psub, std::isnan(total) ? 0.0f : std::min(1.0f, total / 800), c.Acc2()});
    std::wstring up = s.uptimeSec ? FormatUptime(s.uptimeSec) : L"—";
    t.push_back({L"Uptime", up, L"", std::isnan(s.memUsedPct) ? L"" : L"RAM " + Val(s.memUsedPct) + L"% used",
                 std::isnan(s.memUsedPct) ? 0.0f : s.memUsedPct / 100, c.Acc2()});
    return t;
}

static float Live(Ctx& c, const D2D1_RECT_F& r, float f, bool draw, int perRow) {
    float pad = 26 * f, y = r.top + pad;
    float x = r.left + pad, w = r.right - r.left - 2 * pad;
    bool snapshot = !c.b.liveOnShare && !c.in.liveRunning;
    std::wstring badge = c.in.live.at ? (c.in.liveRunning || c.b.liveOnShare ? L"LIVE" : L"SNAPSHOT") : L"";
    if (draw) y += SectionTitle(c, x, y, w, L"Live", f, badge, snapshot ? c.Sub() : Rgb(0x3DDC84));
    else y += 42 * f;
    auto tiles = LiveTiles(c);
    perRow = std::max(1, perRow);
    float gap = 14 * f;
    float tw = (w - gap * (perRow - 1)) / perRow, th = 118 * f;
    for (size_t i = 0; i < tiles.size(); ++i) {
        int row = (int)i / perRow, col = (int)i % perRow;
        float tx = x + col * (tw + gap), ty = y + row * (th + gap);
        if (!draw) continue;
        D2D1_RECT_F tr = D2D1::RectF(tx, ty, tx + tw, ty + th);
        c.Fill(tr, std::max(4.0f, c.st.radius * 0.6f) * f, c.Light() ? Rgb(0x000000, 0.04f) : Rgb(0xFFFFFF, 0.045f));
        const Tile& t = tiles[i];
        float ip = 16 * f;
        c.Txt(Upper(t.label), c.B(12.5f * f, DWRITE_FONT_WEIGHT_BOLD), D2D1::RectF(tx + ip, ty + 10 * f, tx + tw - ip, ty + 32 * f), c.Sub());
        auto* vf = c.Hd(40 * f, DWRITE_FONT_WEIGHT_BOLD);
        float vw = c.Width(t.value, vf);
        c.Txt(t.value, vf, D2D1::RectF(tx + ip, ty + 30 * f, tx + tw - ip, ty + 78 * f), t.value == L"—" ? c.Sub() : c.Text(), true);
        if (!t.unit.empty()) c.Txt(t.unit, c.B(17 * f, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(tx + ip + vw + 3 * f, ty + 40 * f, tx + tw - ip, ty + 72 * f), t.color);
        c.Txt(t.sub, c.B(13 * f), D2D1::RectF(tx + ip, ty + 76 * f, tx + tw - ip, ty + 96 * f), c.Sub());
        D2D1_RECT_F bar = D2D1::RectF(tx + ip, ty + th - 14 * f, tx + tw - ip, ty + th - 10 * f);
        c.Fill(bar, 2 * f, c.Light() ? Rgb(0x000000, 0.08f) : Rgb(0xFFFFFF, 0.08f));
        float frac = std::clamp(t.bar, 0.0f, 1.0f);
        if (frac > 0.01f) c.Fill(D2D1::RectF(bar.left, bar.top, bar.left + (bar.right - bar.left) * frac, bar.bottom), 2 * f, t.color);
    }
    int rows = ((int)tiles.size() + perRow - 1) / perRow;
    y += rows * th + (rows - 1) * gap;
    if (draw && c.in.live.at && !c.in.liveRunning && !c.b.liveOnShare) {
        wchar_t when[64];
        time_t tt = (time_t)c.in.live.at;
        tm lt{};
        localtime_s(&lt, &tt);
        wcsftime(when, 64, L"Snapshot %b %d, %H:%M", &lt);
        c.Txt(when, c.B(12.5f * f), D2D1::RectF(x, y + 6 * f, x + w, y + 26 * f), c.Sub());
        y += 24 * f;
    }
    return y - r.top + pad;
}

static float Bench(Ctx& c, const D2D1_RECT_F& r, float f, bool draw) {
    float pad = 26 * f, y = r.top + pad;
    float x = r.left + pad, w = r.right - r.left - 2 * pad;
    if (draw) y += SectionTitle(c, x, y, w, L"Benchmarks", f);
    else y += 42 * f;
    const auto& list = c.in.bench;
    if (list.empty()) {
        if (draw) c.Txt(L"Run a benchmark to add scores", c.B(16 * f), D2D1::RectF(x, y, x + w, y + 28 * f), c.Sub());
        return y + 34 * f - r.top + pad;
    }
    float rh = 64 * f;
    for (auto& br : list) {
        if (draw && y + rh > r.bottom - pad + 12 * f) break;
        if (draw) {
            std::wstring score = bench::FormatScore(br);
            auto* sf = c.Hd(21 * f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING);
            float sw = c.Width(score, sf);
            c.Txt(br.name, c.B(16.5f * f, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(x, y, x + w - sw - 12 * f, y + 26 * f), c.Text());
            c.Txt(score, sf, D2D1::RectF(x + w - sw - 4 * f, y, x + w, y + 26 * f), c.Text(), true);
            std::wstring meta = br.source == L"Rigcard" ? br.detail : br.source;
            c.Txt(meta, c.B(12.5f * f), D2D1::RectF(x, y + 25 * f, x + w, y + 44 * f), c.Sub());
            // bar: this score against your best for the same test
            double best = br.score;
            for (auto& h : c.in.history) if (h.id == br.id) best = br.lowerIsBetter ? std::min(best, h.score) : std::max(best, h.score);
            float frac = best > 0 ? (float)(br.lowerIsBetter ? best / br.score : br.score / best) : 1.0f;
            D2D1_RECT_F bar = D2D1::RectF(x, y + 48 * f, x + w, y + 52 * f);
            c.Fill(bar, 2 * f, c.Light() ? Rgb(0x000000, 0.08f) : Rgb(0xFFFFFF, 0.08f));
            ID2D1GradientStopCollection* stops = nullptr;
            D2D1_GRADIENT_STOP gs[2] = {{0, c.Acc()}, {1, c.Acc2()}};
            c.rt->CreateGradientStopCollection(gs, 2, &stops);
            ID2D1LinearGradientBrush* lg = nullptr;
            if (stops) c.rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(bar.left, 0), D2D1::Point2F(bar.right, 0)), stops, &lg);
            D2D1_RECT_F fill = D2D1::RectF(bar.left, bar.top, bar.left + (bar.right - bar.left) * std::clamp(frac, 0.04f, 1.0f), bar.bottom);
            if (lg) c.rt->FillRoundedRectangle(D2D1::RoundedRect(fill, 2 * f, 2 * f), lg);
            SafeRelease(lg);
            SafeRelease(stops);
        }
        y += rh;
    }
    return y - r.top + pad - 8 * f;
}

// ---------------------------------------------------------------- header
static void Chips(Ctx& c, float x, float y, float maxX, float f) {
    std::vector<std::wstring> chips;
    for (const wchar_t* k : {L"cpu", L"gpu", L"ram"}) if (const Part* p = First(c.b, k)) chips.push_back(ShortName(*p));
    auto* cf = c.B(17 * f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    float cx = x;
    for (size_t i = 0; i < chips.size(); ++i) {
        float cw = c.Width(chips[i], cf) + 34 * f;
        if (cx + cw > maxX) break;
        D2D1_RECT_F r = D2D1::RectF(cx, y, cx + cw, y + 38 * f);
        D2D1_COLOR_F col = i == 1 ? c.Acc2() : c.Acc();
        c.Fill(r, 19 * f, Alpha(col, c.Light() ? 0.12f : 0.16f));
        if (c.st.glow) c.Stroke(r, 19 * f, Alpha(col, 0.6f), 1.2f);
        c.Txt(chips[i], cf, r, c.Light() ? Mix(col, Rgb(0x000000), 0.25f) : Mix(col, Rgb(0xFFFFFF), 0.35f));
        cx += cw + 10 * f;
    }
}

static void NameBlock(Ctx& c, float x, float top, float right, float bottom, float f, bool overPhoto) {
    float w = right - x;
    std::wstring name = Trim(c.b.name).empty() ? L"My Build" : c.b.name;
    float size = 66 * f;
    while (size > 34 * f && c.Width(name, c.Hd(size)) > w) size -= 2 * f;
    float avail = bottom - top;
    std::wstring loc = LocationLine(c.b);
    float blockH = size * 1.25f + (loc.empty() ? 0 : 36 * f) + 56 * f;
    float y = overPhoto ? bottom - blockH : top + std::max(0.0f, (avail - blockH) / 2 - 10 * f);
    D2D1_COLOR_F tc = overPhoto ? Rgb(0xFFFFFF) : c.Text(), sc = overPhoto ? Rgb(0xFFFFFF, 0.82f) : c.Sub();
    c.Txt(name, c.Hd(size), D2D1::RectF(x, y, right, y + size * 1.25f), tc, true);
    y += size * 1.25f;
    if (!loc.empty()) {
        c.Txt(loc, c.B(21 * f), D2D1::RectF(x + 2 * f, y, right, y + 32 * f), sc);
        y += 40 * f;
    } else {
        y += 6 * f;
    }
    Chips(c, x, y, right, f);
}

static void Background(Ctx& c) {
    D2D1_RECT_F all = D2D1::RectF(0, 0, c.W, c.H);
    const CardStyle& st = c.st;
    if (st.background == L"photo" && c.blur) {
        c.Fill(all, 0, c.C(st.bg1));
        c.Photo(c.blur, all, 0, 1.0f, false);
        c.Fill(all, 0, c.C(st.bg1, 0.55f));
        return;
    }
    if (st.background == L"solid" || st.bg1 == st.bg2) { c.Fill(all, 0, c.C(st.bg1)); return; }
    float a = st.angle * 3.14159265f / 180.0f;
    float cx = c.W / 2, cy = c.H / 2, half = (std::fabs(std::cos(a)) * c.W + std::fabs(std::sin(a)) * c.H) / 2;
    D2D1_POINT_2F p0 = D2D1::Point2F(cx - std::cos(a) * half, cy - std::sin(a) * half), p1 = D2D1::Point2F(cx + std::cos(a) * half, cy + std::sin(a) * half);
    ID2D1GradientStopCollection* stops = nullptr;
    D2D1_GRADIENT_STOP gs[2] = {{0, c.C(st.bg1)}, {1, c.C(st.bg2)}};
    c.rt->CreateGradientStopCollection(gs, 2, &stops);
    ID2D1LinearGradientBrush* lg = nullptr;
    if (stops) c.rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(p0, p1), stops, &lg);
    if (lg) c.rt->FillRectangle(all, lg); else c.Fill(all, 0, c.C(st.bg1));
    SafeRelease(lg);
    SafeRelease(stops);
    // a soft accent glow in the corner (Wine's radial brushes ignore stop alpha, so test runs skip it)
    static const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
    if (wine) return;
    ID2D1RadialGradientBrush* rg = nullptr;
    D2D1_GRADIENT_STOP gl[2] = {{0, Alpha(c.Acc(), st.glow ? 0.22f : 0.12f)}, {1, Alpha(c.Acc(), 0)}};
    c.rt->CreateGradientStopCollection(gl, 2, &stops);
    if (stops) c.rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(c.W * 0.92f, c.H * 0.05f), D2D1::Point2F(0, 0), c.W * 0.5f, c.W * 0.5f), stops, &rg);
    if (rg) c.rt->FillRectangle(all, rg);
    SafeRelease(rg);
    SafeRelease(stops);
}

static void Footer(Ctx& c, float y, float pad, float f) {
    std::wstring url = c.in.footerUrl;
    if (IStartsWith(url, L"https://")) url = url.substr(8);
    auto* ff = c.B(14 * f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    c.Txt(L"rigcard", c.Hd(16 * f, DWRITE_FONT_WEIGHT_BOLD), D2D1::RectF(pad, y, pad + 200 * f, y + 24 * f), Alpha(c.Sub(), 0.9f));
    c.Fill(D2D1::RectF(pad + c.Width(L"rigcard", c.Hd(16 * f, DWRITE_FONT_WEIGHT_BOLD)) + 4 * f, y + 15 * f, pad + c.Width(L"rigcard", c.Hd(16 * f, DWRITE_FONT_WEIGHT_BOLD)) + 9 * f, y + 20 * f), 2.5f * f, c.Acc());
    if (!url.empty()) c.Txt(url, Fmt(c.body, 14 * f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING), D2D1::RectF(pad + 220 * f, y, c.W - pad, y + 24 * f), Alpha(c.Sub(), 0.9f));
    (void)ff;
}

// ---------------------------------------------------------------- layouts
static void Layout(Ctx& c) {
    const Build& b = c.b;
    float W = c.W, H = c.H;
    float aspect = W / H;
    bool wide = aspect > 1.25f, tall = aspect < 0.85f;
    float s = wide ? W / 1920.0f : W / 1080.0f;          // design scale
    if (tall) s = W / 1080.0f;
    float pad = (wide ? 64 : 52) * s;
    float rad = c.st.radius * s;
    bool photo = b.showPhoto && c.photo;
    bool showcase = c.st.layout == L"showcase" && photo;
    float footerH = 40 * s;
    float topPad = (H / W > 2.0f) ? H * 0.17f : pad;      // phone wallpaper: leave room for the clock

    // ---------- header
    D2D1_RECT_F header;
    if (wide) {
        float hh = (photo ? (showcase ? 0.42f : 0.36f) : 0.25f) * H;
        header = D2D1::RectF(pad, topPad, W - pad, topPad + hh);
        if (showcase) {
            c.photoRect = header;
            c.Photo(c.photo, header, rad);
            // dark fade so the name reads over the photo
            ID2D1GradientStopCollection* stops = nullptr;
            D2D1_GRADIENT_STOP gs[2] = {{0, Rgb(0x000000, 0)}, {1, Rgb(0x000000, 0.72f)}};
            c.rt->CreateGradientStopCollection(gs, 2, &stops);
            ID2D1LinearGradientBrush* lg = nullptr;
            if (stops) c.rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, header.top + (header.bottom - header.top) * 0.35f), D2D1::Point2F(0, header.bottom)), stops, &lg);
            if (lg) c.rt->FillRoundedRectangle(D2D1::RoundedRect(header, rad, rad), lg);
            SafeRelease(lg);
            SafeRelease(stops);
            NameBlock(c, header.left + 40 * s, header.top, header.right - 40 * s, header.bottom - 30 * s, s, true);
        } else {
            float tx = header.left;
            if (photo) {
                float pw = std::min((header.bottom - header.top) * 1.55f, (W - 2 * pad) * 0.42f);
                c.photoRect = D2D1::RectF(header.left, header.top, header.left + pw, header.bottom);
                c.Photo(c.photo, c.photoRect, rad);
                tx = c.photoRect.right + 48 * s;
            }
            NameBlock(c, tx, header.top, header.right, header.bottom, s, false);
        }
    } else {
        float ph = photo ? (tall ? W * 0.56f : H * 0.34f) : 0;
        if (tall) {
            if (photo) { c.photoRect = D2D1::RectF(pad, topPad, W - pad, topPad + ph); c.Photo(c.photo, c.photoRect, rad); }
            float ny = topPad + ph + (photo ? 26 * s : 0);
            header = D2D1::RectF(pad, ny, W - pad, ny + 190 * s);
            NameBlock(c, pad, header.top, W - pad, header.bottom, s, false);
        } else {
            float hh = photo ? ph : 210 * s;
            header = D2D1::RectF(pad, topPad, W - pad, topPad + hh);
            float tx = pad;
            if (photo) {
                c.photoRect = D2D1::RectF(pad, topPad, pad + (W - 2 * pad) * 0.46f, topPad + hh);
                c.Photo(c.photo, c.photoRect, rad);
                tx = c.photoRect.right + 36 * s;
            }
            NameBlock(c, tx, header.top, W - pad, header.bottom, s * 0.86f, false);
        }
    }

    // ---------- body
    float top = header.bottom + 34 * s, bottom = H - pad - footerH;
    struct Sec { int kind; float weight; };   // 0 parts · 1 live · 2 bench
    std::vector<Sec> secs;
    if (b.showParts) secs.push_back({0, 0.43f});
    if (b.showLive) secs.push_back({1, 0.26f});
    if (b.showBench) secs.push_back({2, 0.31f});
    float gap = 24 * s;
    auto measure = [&](int kind, float w, float f, int partCols, int perRow) {
        D2D1_RECT_F r = D2D1::RectF(0, 0, w, 100000);
        if (kind == 0) return Parts(c, r, f, false, partCols);
        if (kind == 1) return Live(c, r, f, false, perRow);
        return Bench(c, r, f, false);
    };
    auto draw = [&](int kind, const D2D1_RECT_F& r, float f, int partCols, int perRow) {
        c.Panel(r, rad);
        if (kind == 0) Parts(c, r, f, true, partCols);
        else if (kind == 1) Live(c, r, f, true, perRow);
        else Bench(c, r, f, true);
    };
    if (!secs.empty() && bottom - top > 60 * s) {
        if (wide) {
            float total = 0;
            for (auto& x : secs) total += x.weight;
            float avail = W - 2 * pad - gap * (secs.size() - 1);
            float need = 0;
            std::vector<float> widths;
            for (auto& x : secs) widths.push_back(avail * x.weight / total);
            auto colsFor = [&](size_t i) { return secs[i].kind == 0 && widths[i] > 1100 * s ? 2 : 1; };
            auto perRowFor = [&](size_t i) { return widths[i] > 900 * s ? 4 : 2; };
            for (size_t i = 0; i < secs.size(); ++i) need = std::max(need, measure(secs[i].kind, widths[i], s, colsFor(i), perRowFor(i)));
            float f = std::clamp((bottom - top) / std::max(1.0f, need), 0.62f, 1.0f) * s;
            float x = pad;
            for (size_t i = 0; i < secs.size(); ++i) {
                draw(secs[i].kind, D2D1::RectF(x, top, x + widths[i], bottom), f, colsFor(i), perRowFor(i));
                x += widths[i] + gap;
            }
        } else if (tall) {
            float w = W - 2 * pad, need = 0;
            for (auto& x : secs) need += measure(x.kind, w, s, 1, 2);
            need += gap * (secs.size() - 1);
            float f = std::clamp((bottom - top) / std::max(1.0f, need), 0.55f, 1.15f) * s;
            float y = top, scale = f / s;
            for (size_t i = 0; i < secs.size(); ++i) {
                float h = measure(secs[i].kind, w, f, 1, 2);
                if (i + 1 == secs.size()) h = std::max(h, std::min(bottom - y, h * 1.0f));
                h = std::min(h, bottom - y);
                if (h < 60 * s) break;
                draw(secs[i].kind, D2D1::RectF(pad, y, W - pad, y + h), f, 1, 2);
                y += h + gap * scale;
            }
        } else {
            // square-ish: parts on the left, live and benchmarks stacked on the right
            bool hasParts = b.showParts;
            std::vector<Sec> right;
            for (auto& x : secs) if (x.kind != 0) right.push_back(x);
            float leftW = hasParts ? (right.empty() ? W - 2 * pad : (W - 2 * pad - gap) * 0.52f) : 0;
            float rightX = hasParts ? pad + leftW + gap : pad, rightW = W - pad - rightX;
            float needL = hasParts ? measure(0, leftW, s, 1, 2) : 0, needR = 0;
            for (auto& x : right) needR += measure(x.kind, rightW, s, 1, 2);
            needR += gap * (right.size() > 1 ? right.size() - 1 : 0);
            float need = std::max(needL, needR);
            float f = std::clamp((bottom - top) / std::max(1.0f, need), 0.55f, 1.0f) * s;
            if (hasParts) draw(0, D2D1::RectF(pad, top, pad + leftW, bottom), f, 1, 2);
            float y = top;
            for (size_t i = 0; i < right.size(); ++i) {
                float h = measure(right[i].kind, rightW, f, 1, 2);
                if (i + 1 == right.size()) h = bottom - y;
                if (h < 60 * s) break;
                draw(right[i].kind, D2D1::RectF(rightX, y, W - pad, y + h), f, 1, 2);
                y += h + gap;
            }
        }
    }
    Footer(c, H - pad - footerH + 14 * s, pad, s);
}

// ---------------------------------------------------------------- entry points
static CardHit DrawInto(ID2D1RenderTarget* rt, const D2D1_RECT_F& area, UINT w, UINT h, const CardInput& in, ID2D1Bitmap* photo, ID2D1Bitmap* blur, bool clipCorners) {
    CardHit hit;
    if (!in.build) return hit;
    float aw = area.right - area.left, ah = area.bottom - area.top;
    float k = std::min(aw / w, ah / h);
    float ox = area.left + (aw - w * k) / 2, oy = area.top + (ah - h * k) / 2;
    D2D1_MATRIX_3X2_F old;
    rt->GetTransform(&old);
    D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(ox, oy) * old;
    rt->SetTransform(m);
    ID2D1SolidColorBrush* br = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &br);
    Ctx c{rt, br, in, *in.build, in.build->style, FontOr(in.build->style.font, false), FontOr(in.build->style.headFont, true), photo, blur, (float)w, (float)h};
    // rounded corners for the preview; exports are square-cornered images
    ID2D1RoundedRectangleGeometry* g = nullptr;
    ID2D1Layer* layer = nullptr;
    if (clipCorners) {
        float rr = 14.0f / k;
        d2d->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(0, 0, (float)w, (float)h), rr, rr), &g);
        rt->CreateLayer(nullptr, &layer);
        if (g && layer) rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), g, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE), layer);
    }
    Background(c);
    Layout(c);
    if (g && layer) rt->PopLayer();
    SafeRelease(layer);
    SafeRelease(g);
    SafeRelease(br);
    rt->SetTransform(old);
    hit.card = D2D1::RectF(ox, oy, ox + w * k, oy + h * k);
    if (c.photoRect.right > c.photoRect.left)
        hit.photo = D2D1::RectF(ox + c.photoRect.left * k, oy + c.photoRect.top * k, ox + c.photoRect.right * k, oy + c.photoRect.bottom * k);
    return hit;
}

CardHit Draw(ID2D1RenderTarget* rt, const D2D1_RECT_F& area, UINT w, UINT h, const CardInput& in) {
    if (s_window.rt != rt || s_window.version != in.photoVersion) {
        SafeRelease(s_window.photo);
        SafeRelease(s_window.blur);
        s_window.rt = rt;
        s_window.version = in.photoVersion;
        s_window.photo = MakeBitmap(rt, in.photo);
        s_window.blur = MakeBitmap(rt, in.photoBlur);
    }
    return DrawInto(rt, area, w, h, in, s_window.photo, s_window.blur, true);
}

IWICBitmap* RenderBitmap(UINT w, UINT h, const CardInput& in) {
    IWICImagingFactory* f = image::Wic();
    if (!f) { Log(L"render: no WIC"); return nullptr; }
    IWICBitmap* bmp = nullptr;
    if (FAILED(f->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) { Log(L"render: WIC bitmap failed"); return nullptr; }
    ID2D1RenderTarget* rt = nullptr;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (FAILED(d2d->CreateWicBitmapRenderTarget(bmp, props, &rt))) {
        props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
        if (FAILED(d2d->CreateWicBitmapRenderTarget(bmp, props, &rt))) { Log(L"render: no WIC render target"); bmp->Release(); return nullptr; }
    }
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    ID2D1Bitmap* photo = MakeBitmap(rt, in.photo);
    ID2D1Bitmap* blur = MakeBitmap(rt, in.photoBlur);
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    DrawInto(rt, D2D1::RectF(0, 0, (float)w, (float)h), w, h, in, photo, blur, false);
    HRESULT hr = rt->EndDraw();
    SafeRelease(photo);
    SafeRelease(blur);
    rt->Release();
    if (FAILED(hr)) { Log(L"render: EndDraw 0x%08lx", (unsigned long)hr); bmp->Release(); return nullptr; }
    return bmp;
}

bool ExportPng(const std::wstring& path, UINT w, UINT h, const CardInput& in, std::wstring* error) {
    IWICBitmap* bmp = RenderBitmap(w, h, in);
    if (!bmp) { if (error) *error = L"Couldn't draw the card."; return false; }
    std::string png;
    bool ok = image::EncodePng(bmp, png) && image::SaveBytes(path, png);
    bmp->Release();
    if (!ok && error) *error = L"Couldn't save " + path + L".";
    return ok;
}

} // namespace cardrender
