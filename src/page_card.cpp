// Rigcard - Card page: the live preview and the "Customize card" panel
#include "ui_internal.h"
#include "util.h"
#include "share.h"
#include <algorithm>
#include <cmath>

using namespace gfx;

static CardHit s_hit;
static const float kPanelW = 372;
static D2D1_RECT_F s_previewArea{};
static D2D1_RECT_F s_panelRect{};
static D2D1_RECT_F s_exportAnchor{}, s_fontAnchor{};

static const std::vector<std::wstring>& Aspects() { static std::vector<std::wstring> a = {L"wide", L"square", L"portrait", L"phone"}; return a; }
static void AspectSize(UINT& w, UINT& h) {
    if (g_aspect == L"square") { w = 1080; h = 1080; }
    else if (g_aspect == L"portrait") { w = 1080; h = 1350; }
    else if (g_aspect == L"phone") { w = 1170; h = 2532; }
    else { w = 1920; h = 1080; }
}

static const std::vector<std::pair<std::wstring, std::wstring>>& Fonts() {
    static std::vector<std::pair<std::wstring, std::wstring>> f = {
        {L"Segoe UI Variable", L"Segoe UI Variable Text"}, {L"Bahnschrift", L"Bahnschrift"}, {L"Segoe UI", L"Segoe UI"},
        {L"Cascadia Code", L"Cascadia Code"}, {L"Consolas", L"Consolas"}, {L"Georgia", L"Georgia"}, {L"Verdana", L"Verdana"},
        {L"Trebuchet MS", L"Trebuchet MS"}, {L"Arial", L"Arial"}, {L"Impact", L"Impact"},
    };
    return f;
}
static std::wstring FontLabel(const std::wstring& family) {
    for (auto& f : Fonts()) if (f.second == family || (f.first == L"Segoe UI Variable" && IStartsWith(family, L"Segoe UI Variable"))) return f.first;
    return family;
}

// ---------------------------------------------------------------- preview
static void DrawPreview(float x0, float y0, float x1, float y1) {
    float pad = 26;
    // aspect selector
    std::vector<std::wstring> labels = {L"Wide", L"Square", L"Portrait", L"Phone"};
    int sel = 0;
    for (size_t i = 0; i < Aspects().size(); ++i) if (Aspects()[i] == g_aspect) sel = (int)i;
    Segmented(x0 + pad, y0 + 16, labels, sel, W_ASPECT);
    std::wstring hint = g_photo.Empty() ? L"Drop a build photo anywhere" : L"Drag the photo to position it · scroll to zoom · double-click to reset";
    g_c.Text(hint, Txt(12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING), D2D1::RectF(x0 + 340, y0 + 16, x1 - pad, y0 + 46), g_t.faint);
    s_previewArea = D2D1::RectF(x0 + pad, y0 + 62, x1 - pad, y1 - pad);
    UINT w, h;
    AspectSize(w, h);
    CardInput in = MakeCardInput();
    // soft shadow
    float aw = s_previewArea.right - s_previewArea.left, ah = s_previewArea.bottom - s_previewArea.top;
    float k = std::min(aw / w, ah / h);
    float cw = w * k, ch = h * k, cx = s_previewArea.left + (aw - cw) / 2, cy = s_previewArea.top + (ah - ch) / 2;
    for (int i = 0; i < 4; ++i) {
        float s = 4.0f + i * 5;
        g_c.FillRound(D2D1::RectF(cx - s * 0.4f, cy + s * 0.3f, cx + cw + s * 0.4f, cy + ch + s), 14 + s, Rgb(0x000000, g_t.dark ? 0.09f : 0.035f));
    }
    s_hit = cardrender::Draw(g_rt, s_previewArea, w, h, in);
    if (s_hit.photo.right > s_hit.photo.left) AddHot(s_hit.photo, Hit::PhotoArea, W_PHOTO_AREA, 0, true, false);
    if (g_photo.Empty() && g_build.showPhoto) {
        std::wstring t = L"Add a photo of your build";
        float bw = ButtonWidth(t, 0xEB9F);
        D2D1_RECT_F r = D2D1::RectF(s_hit.card.left + 16, s_hit.card.bottom - 52, s_hit.card.left + 16 + bw, s_hit.card.bottom - 16);
        Button(r, t, 0xEB9F, Btn::Secondary, W_PHOTO_CHOOSE, 0);
    }
}

// ---------------------------------------------------------------- panel
static float PresetTile(float x, float y, float w, const CardStyle& s, int id, int arg, bool selected) {
    float h = D(64);
    D2D1_RECT_F r = D2D1::RectF(x, y, x + w, y + h);
    // mini gradient
    ID2D1GradientStopCollection* stops = nullptr;
    D2D1_GRADIENT_STOP gs[2] = {{0, Rgb(s.bg1)}, {1, Rgb(s.bg2)}};
    g_rt->CreateGradientStopCollection(gs, 2, &stops);
    ID2D1LinearGradientBrush* lg = nullptr;
    if (stops) g_rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.left, r.top), D2D1::Point2F(r.right, r.bottom)), stops, &lg);
    float rad = std::min(R() + 2, 14.0f);
    if (lg) g_rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), lg);
    SafeRelease(lg);
    SafeRelease(stops);
    // accent dots and name in the style's own colors
    g_c.Circle(r.left + 16, r.top + 18, 5, Rgb(s.accent));
    g_c.Circle(r.left + 30, r.top + 18, 5, Rgb(s.accent2));
    g_c.Text(s.name, Format(F(13), DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(r.left + 12, r.top + 30, r.right - 8, r.bottom - 6), Rgb(s.text));
    g_c.StrokeRound(r, rad, selected ? g_t.accent : (IsHover(id, arg) ? Alpha(g_t.text, 0.5f) : Alpha(g_t.text, 0.12f)), selected ? 2.5f : 1.0f);
    AddHot(r, Hit::Preset, id, arg);
    return h;
}

static void DrawPanel(float x0, float y0, float x1, float y1) {
    s_panelRect = D2D1::RectF(x0, y0, x1, y1);
    g_c.Fill(s_panelRect, Alpha(g_t.sidebar, g_t.backdrop ? 0.5f : 1.0f));
    g_c.Line(x0 + 0.5f, y0, x0 + 0.5f, y1, g_t.divider);
    size_t hotFrom = g_hot.size();
    g_rt->PushAxisAlignedClip(s_panelRect, D2D1_ANTIALIAS_MODE_ALIASED);
    float pad = 22, x = x0 + pad, w = x1 - x0 - 2 * pad;
    float y = y0 + 18 - g_panelScroll;
    Build& b = g_build;
    CardStyle& st = b.style;

    g_c.Text(L"Customize card", Txt(18, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(x, y, x + w, y + 30), g_t.text);
    y += D(40);
    // primary actions
    float bw = (w - 8) / 2;
    Button(D2D1::RectF(x, y, x + bw, y + D(36)), L"Copy share link", icon::Link, Btn::Primary, W_SHARE_COPY);
    s_exportAnchor = D2D1::RectF(x + bw + 8, y, x + w, y + D(36));
    Button(s_exportAnchor, L"Export PNG", icon::Download, Btn::Secondary, W_EXPORT_PNG);
    y += D(44);
    Button(D2D1::RectF(x, y, x + bw, y + D(36)), L"Copy image", 0xE8C8, Btn::Secondary, W_COPY_IMAGE);
    Button(D2D1::RectF(x + bw + 8, y, x + w, y + D(36)), g_benching ? L"Benchmarking…" : L"Run benchmark", icon::Play, Btn::Secondary, W_RUN_BENCH, 0, !g_benching);
    y += D(56);

    // build
    y += SectionHeader(x, y, w, L"Build");
    Field(D2D1::RectF(x, y, x + w, y + D(36)), W_BUILD_NAME, 0, &b.name, L"Build name");
    y += D(44);
    Field(D2D1::RectF(x, y, x + w, y + D(36)), W_BUILD_LOCATION, 0, &b.location, g_cfg.hideLocation ? L"Location (hidden — see Settings)" : L"Location (optional)");
    y += D(56);

    // style presets
    y += SectionHeader(x, y, w, L"Card style");
    float tw = (w - 10) / 2;
    const auto& presets = CardPresets();
    for (size_t i = 0; i < presets.size(); ++i) {
        float tx = x + (i % 2) * (tw + 10);
        float th = PresetTile(tx, y, tw, presets[i], W_PRESET, (int)i, !st.custom && st.id == presets[i].id);
        if (i % 2 == 1 || i + 1 == presets.size()) y += th + 10;
    }
    if (!g_savedStyles.empty()) {
        y += 4;
        g_c.Text(L"Saved styles", Txt(12, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(x, y, x + w, y + 22), g_t.subtext);
        y += 26;
        for (size_t i = 0; i < g_savedStyles.size(); ++i) {
            float tx = x + (i % 2) * (tw + 10);
            float th = PresetTile(tx, y, tw, g_savedStyles[i], W_SAVED_STYLE, (int)i, st.custom && st.id == g_savedStyles[i].id);
            D2D1_RECT_F del = D2D1::RectF(tx + tw - 26, y + 4, tx + tw - 4, y + 26);
            if (IsHover(W_SAVED_STYLE, (int)i) || IsHover(W_STYLE_DELETE, (int)i)) {
                g_c.FillRound(del, 11, Rgb(0x000000, 0.45f));
                g_c.Glyph(icon::Close, Icon(9), del, Rgb(0xFFFFFF));
                AddHot(del, Hit::Button, W_STYLE_DELETE, (int)i, true, false);
            }
            if (i % 2 == 1 || i + 1 == g_savedStyles.size()) y += th + 10;
        }
    }
    float b3 = (w - 16) / 3;
    Button(D2D1::RectF(x, y, x + b3, y + D(32)), L"Save", 0xE74E, Btn::Secondary, W_STYLE_SAVE);
    Button(D2D1::RectF(x + b3 + 8, y, x + 2 * b3 + 8, y + D(32)), L"Import", 0xE8B5, Btn::Secondary, W_STYLE_IMPORT);
    Button(D2D1::RectF(x + 2 * b3 + 16, y, x + w, y + D(32)), L"Export", 0xEDE1, Btn::Secondary, W_STYLE_EXPORT);
    y += D(52);

    // colors & type
    y += SectionHeader(x, y, w, L"Colors and type");
    const wchar_t* colorNames[] = {L"Accent", L"Accent 2", L"Background", L"Gradient", L"Text"};
    uint32_t cols[] = {st.accent, st.accent2, st.bg1, st.bg2, st.text};
    float sw = (w - 4 * 10) / 5;
    for (int i = 0; i < 5; ++i) {
        float sx = x + i * (sw + 10);
        ColorSwatch(D2D1::RectF(sx, y, sx + sw, y + D(34)), cols[i], W_COLOR, i, false);
        g_c.Text(colorNames[i], Txt(11, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER), D2D1::RectF(sx - 6, y + D(36), sx + sw + 6, y + D(54)), g_t.subtext);
    }
    y += D(64);
    g_c.Text(L"Background", Txt(12.5f), D2D1::RectF(x, y, x + w, y + 20), g_t.subtext);
    y += 24;
    int bgSel = st.background == L"solid" ? 1 : st.background == L"photo" ? 2 : 0;
    Segmented(x, y, {L"Gradient", L"Solid", L"Blurred photo"}, bgSel, W_BGMODE, w);
    y += D(42);
    if (st.background == L"gradient") {
        g_c.Text(L"Gradient angle", Txt(12.5f), D2D1::RectF(x, y, x + w, y + 20), g_t.subtext);
        y += 22;
        Slider(D2D1::RectF(x, y, x + w, y + 22), st.angle, 0, 360, W_ANGLE);
        SliderBind(W_ANGLE, 0, [](float t) { g_build.style.angle = std::round(t * 360); g_build.style.custom = true; });
        y += D(34);
    }
    g_c.Text(L"Layout", Txt(12.5f), D2D1::RectF(x, y, x + w, y + 20), g_t.subtext);
    y += 24;
    int lay = st.layout == L"showcase" ? 1 : st.layout == L"minimal" ? 2 : 0;
    Segmented(x, y, {L"Classic", L"Showcase", L"Minimal"}, lay, W_LAYOUT, w);
    y += D(42);
    g_c.Text(L"Font", Txt(12.5f), D2D1::RectF(x, y, x + w, y + 20), g_t.subtext);
    y += 24;
    s_fontAnchor = D2D1::RectF(x, y, x + w, y + D(34));
    {
        bool hot = IsHover(W_FONT);
        g_c.FillRound(s_fontAnchor, std::min(R(), 17.0f), hot ? g_t.surfaceHover : Mix(g_t.surface, g_t.surfaceStrong, 0.35f));
        g_c.StrokeRound(s_fontAnchor, std::min(R(), 17.0f), g_t.divider);
        std::wstring label = FontLabel(st.font);
        IDWriteTextFormat* ff = nullptr;
        dw->CreateTextFormat(st.font.c_str(), nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, F(14), L"en-us", &ff);
        if (ff) { ff->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); g_c.Text(label, ff, D2D1::RectF(s_fontAnchor.left + 12, s_fontAnchor.top, s_fontAnchor.right - 30, s_fontAnchor.bottom), g_t.text); ff->Release(); }
        g_c.Glyph(0xE70D, Icon(11), D2D1::RectF(s_fontAnchor.right - 30, s_fontAnchor.top, s_fontAnchor.right - 8, s_fontAnchor.bottom), g_t.subtext);
        AddHot(s_fontAnchor, Hit::Button, W_FONT, 0);
    }
    y += D(46);
    g_c.Text(L"Corner radius", Txt(12.5f), D2D1::RectF(x, y, x + w, y + 20), g_t.subtext);
    y += 22;
    Slider(D2D1::RectF(x, y, x + w, y + 22), st.radius, 0, 40, W_RADIUS);
    SliderBind(W_RADIUS, 0, [](float t) { g_build.style.radius = std::round(t * 40); g_build.style.custom = true; });
    y += D(32);
    g_c.Text(L"Panel opacity", Txt(12.5f), D2D1::RectF(x, y, x + w, y + 20), g_t.subtext);
    y += 22;
    Slider(D2D1::RectF(x, y, x + w, y + 22), st.panelOpacity, 0, 1, W_PANELOP);
    SliderBind(W_PANELOP, 0, [](float t) { g_build.style.panelOpacity = std::round(t * 100) / 100; g_build.style.custom = true; });
    y += D(34);
    y += ToggleRow(x, y, w, L"Neon glow", L"", st.glow, W_GLOW);
    y += ToggleRow(x, y, w, L"Panel borders", L"", st.border, W_BORDER);
    y += D(18);

    // sections
    y += SectionHeader(x, y, w, L"Show on card");
    y += ToggleRow(x, y, w, L"Parts list", L"", b.showParts, W_SECTION, 0);
    y += ToggleRow(x, y, w, L"Live stats", L"CPU and GPU temperature, power, uptime", b.showLive, W_SECTION, 1);
    y += ToggleRow(x, y, w, L"Benchmarks", L"", b.showBench, W_SECTION, 2);
    y += ToggleRow(x, y, w, L"Photo", L"", b.showPhoto, W_SECTION, 3);
    y += ToggleRow(x, y, w, L"Peripherals", L"Monitors, keyboard, mouse, audio", b.showPeripherals, W_SECTION, 4);
    y += 6;
    g_c.Text(L"Stats on the shared page", Txt(12.5f), D2D1::RectF(x, y, x + w, y + 20), g_t.subtext);
    y += 24;
    Segmented(x, y, {L"Live (while Rigcard runs)", L"Snapshot"}, b.liveOnShare ? 0 : 1, W_LIVEMODE, w);
    y += D(52);

    // photo
    y += SectionHeader(x, y, w, L"Photo", g_photo.Empty() ? L"Drag and drop a photo anywhere in the window, or choose one." : L"");
    if (!g_photo.Empty()) {
        Button(D2D1::RectF(x, y, x + bw, y + D(34)), L"Replace…", 0xEB9F, Btn::Secondary, W_PHOTO_CHOOSE);
        Button(D2D1::RectF(x + bw + 8, y, x + w, y + D(34)), L"Remove", icon::Delete, Btn::Secondary, W_PHOTO_REMOVE);
        y += D(46);
        g_c.Text(L"Zoom", Txt(12.5f), D2D1::RectF(x, y, x + w, y + 20), g_t.subtext);
        y += 22;
        Slider(D2D1::RectF(x, y, x + w - 92, y + 22), b.photoZoom, 1, 4, W_PHOTO_ZOOM);
        SliderBind(W_PHOTO_ZOOM, 0, [](float t) { g_build.photoZoom = 1 + t * 3; });
        Button(D2D1::RectF(x + w - 80, y - 4, x + w, y + 26), L"Reset", 0, Btn::Subtle, W_PHOTO_RESET);
        y += D(36);
        y += ToggleRow(x, y, w, L"Blurred photo background", L"", st.background == L"photo", W_PHOTO_BLURBG);
    } else {
        Button(D2D1::RectF(x, y, x + w, y + D(34)), L"Choose photo…", 0xEB9F, Btn::Secondary, W_PHOTO_CHOOSE);
        y += D(44);
    }
    y += D(18);

    // sharing
    bool hosted = !b.gistId.empty();
    std::wstring shareSub = hosted ? L"Your card has its own page. Changes you make here update it."
                                   : (GetToken().empty() ? L"Copy a quick link now, or connect GitHub (free) in Settings for a link that stays up to date."
                                                         : L"Publish to get a page that updates when your build does.");
    y += SectionHeader(x, y, w, L"Share", shareSub);
    std::wstring link = ShareLink();
    {
        D2D1_RECT_F lr = D2D1::RectF(x, y, x + w, y + D(34));
        g_c.FillRound(lr, std::min(R(), 17.0f), Mix(g_t.surface, g_t.bg, 0.3f));
        std::wstring shown = link.size() > 70 ? link.substr(0, 68) + L"…" : link;
        g_c.Text(shown, Txt(12), D2D1::RectF(lr.left + 12, lr.top, lr.right - 10, lr.bottom), hosted ? g_t.accent : g_t.subtext);
        y += D(42);
    }
    Button(D2D1::RectF(x, y, x + bw, y + D(34)), L"Copy link", icon::Link, Btn::Secondary, W_SHARE_COPY, 1);
    Button(D2D1::RectF(x + bw + 8, y, x + w, y + D(34)), L"Open in browser", 0xE8A7, Btn::Secondary, W_SHARE_OPEN);
    y += D(42);
    if (!GetToken().empty()) {
        Button(D2D1::RectF(x, y, x + w, y + D(34)), hosted ? L"Update share page now" : L"Publish share page", 0xE898, hosted ? Btn::Secondary : Btn::Primary, W_SHARE_PUBLISH, 0, !share::Busy());
        y += D(42);
        if (hosted) {
            y += ToggleRow(x, y, w, L"Live stats on the shared page", L"Pushes temperatures every " + std::to_wstring(g_cfg.liveSeconds) + L" s while Rigcard runs", g_cfg.liveShare, W_SHARE_LIVE);
            Button(D2D1::RectF(x, y + 4, x + w, y + 4 + D(32)), L"Stop sharing (delete the page)", icon::Delete, Btn::Subtle, W_SHARE_UNPUBLISH);
            y += D(44);
        }
    } else {
        Button(D2D1::RectF(x, y, x + w, y + D(34)), L"Connect GitHub for a hosted link…", 0xE71B, Btn::Subtle, W_SHARE_CONNECT);
        y += D(42);
    }
    if (!g_shareStatus.empty()) y += Label(x, y, w, g_shareStatus, 12, g_t.faint, DWRITE_FONT_WEIGHT_NORMAL, true);
    y += 8;
    Button(D2D1::RectF(x, y, x + w, y + D(32)), L"Copy quick link (no account, snapshot)", 0, Btn::Subtle, W_SHARE_QUICK);
    y += D(48);

    g_panelContentH = y + g_panelScroll - y0;
    g_panelViewH = y1 - y0;
    g_rt->PopAxisAlignedClip();
    ClipHot(hotFrom, s_panelRect);
    // scrollbar
    if (g_panelContentH > g_panelViewH) {
        float track = g_panelViewH - 8;
        float th = std::max(30.0f, track * g_panelViewH / g_panelContentH);
        float ty = y0 + 4 + (track - th) * (g_panelScroll / std::max(1.0f, g_panelContentH - g_panelViewH));
        g_c.FillRound(D2D1::RectF(x1 - 6, ty, x1 - 3, ty + th), 1.5f, Alpha(g_t.text, 0.18f));
    }
}

void DrawCardPage(float x0, float y0) {
    float split = g_W - kPanelW;
    size_t hotFrom = g_hot.size();
    DrawPreview(x0, y0, split, g_H);
    ClipHot(hotFrom, D2D1::RectF(x0, y0, split, g_H));
    DrawPanel(split, y0, g_W, g_H);
}

// ---------------------------------------------------------------- input
static void ExportPngSize(size_t i) {
    const auto& sizes = ExportSizes();
    if (i >= sizes.size()) return;
    std::wstring name = (Trim(g_build.name).empty() ? std::wstring(L"Rigcard") : Trim(g_build.name)) + L" - " + sizes[i].id + L".png";
    for (auto& c : name) if (wcschr(L"\\/:*?\"<>|", c)) c = L'-';
    std::wstring path = PickSaveFile(L"PNG image\0*.png\0", name.c_str(), L"png");
    if (path.empty()) return;
    std::wstring err;
    if (cardrender::ExportPng(path, sizes[i].w, sizes[i].h, MakeCardInput(), &err)) Toast(L"Saved " + PathName(path) + L".");
    else Toast(err, true);
}

static void ApplyColor(int which) {
    CardStyle& st = g_build.style;
    uint32_t* target[] = {&st.accent, &st.accent2, &st.bg1, &st.bg2, &st.text};
    if (which < 0 || which > 4) return;
    uint32_t c = *target[which];
    if (!PickColor(c)) return;
    *target[which] = c;
    if (which == 4) {
        // derive the secondary text color from the new text color
        D2D1_COLOR_F t = Rgb(c), bg = Rgb(st.bg1);
        D2D1_COLOR_F m = Mix(t, bg, 0.42f);
        st.subtext = ((uint32_t)(m.r * 255) << 16) | ((uint32_t)(m.g * 255) << 8) | (uint32_t)(m.b * 255);
    }
    if (which == 2 && st.background == L"solid") st.bg2 = c;
    st.custom = true;
    if (st.name.find(L"(edited)") == std::wstring::npos && !st.name.empty()) st.name += L" (edited)";
    SaveBuildSoon();
}

bool ClickCardPage(const HotRect& h) {
    Build& b = g_build;
    CardStyle& st = b.style;
    switch (h.id) {
        case W_ASPECT: g_aspect = Aspects()[std::clamp(h.arg, 0, 3)]; return true;
        case W_PRESET: {
            const auto& p = CardPresets();
            if (h.arg >= 0 && h.arg < (int)p.size()) { st = p[h.arg]; SaveBuildSoon(); }
            return true;
        }
        case W_SAVED_STYLE:
            if (h.arg >= 0 && h.arg < (int)g_savedStyles.size()) { st = g_savedStyles[h.arg]; SaveBuildSoon(); }
            return true;
        case W_STYLE_DELETE:
            if (h.arg >= 0 && h.arg < (int)g_savedStyles.size()) {
                std::wstring file = g_savedStyles[h.arg].id.substr(6);   // "saved:<file>"
                DeleteFileW((StylesDir() + L"\\" + file).c_str());
                g_savedStyles = LoadSavedStyles();
            }
            return true;
        case W_STYLE_SAVE: {
            CardStyle s = st;
            std::wstring base = s.name;
            size_t e = base.find(L" (edited)");
            if (e != std::wstring::npos) base.erase(e);
            if (!s.custom || base.empty()) base = st.name + L" copy";
            s.name = base;
            std::wstring file = bench::Slug(base);
            if (file.empty()) file = L"style";
            EnsureDir(StylesDir());
            std::wstring path = StylesDir() + L"\\" + file + L".json";
            for (int n = 2; FileExists(path); ++n) path = StylesDir() + L"\\" + file + L"-" + std::to_wstring(n) + L".json";
            if (SaveStyleFile(s, path)) {
                g_savedStyles = LoadSavedStyles();
                st.custom = true;
                st.id = L"saved:" + PathName(path);
                st.name = base;
                SaveBuildSoon();
                Toast(L"Saved \"" + base + L"\". Export it to share it as a JSON file.");
            }
            return true;
        }
        case W_STYLE_IMPORT: {
            std::wstring path = PickOpenFile(L"Card style (*.json)\0*.json\0", L"Import a card style");
            if (path.empty()) return true;
            CardStyle s;
            std::wstring err;
            if (!LoadStyleFile(path, s, &err)) { Toast(err, true); return true; }
            EnsureDir(StylesDir());
            SaveStyleFile(s, StylesDir() + L"\\" + PathName(path));
            g_savedStyles = LoadSavedStyles();
            s.id = L"saved:" + PathName(path);
            st = s;
            SaveBuildSoon();
            Toast(L"Imported \"" + s.name + L"\".");
            return true;
        }
        case W_STYLE_EXPORT: {
            std::wstring name = bench::Slug(st.name) + L".json";
            std::wstring path = PickSaveFile(L"Card style (*.json)\0*.json\0", name.c_str(), L"json");
            if (!path.empty()) {
                if (SaveStyleFile(st, path)) Toast(L"Exported " + PathName(path) + L". Anyone with Rigcard can import it.");
                else Toast(L"Couldn't save the file.", true);
            }
            return true;
        }
        case W_COLOR: ApplyColor(h.arg); return true;
        case W_BGMODE: {
            const wchar_t* modes[] = {L"gradient", L"solid", L"photo"};
            if (h.arg == 2 && g_photo.Empty()) { Toast(L"Add a photo first; the background uses a blurred copy of it."); return true; }
            st.background = modes[std::clamp(h.arg, 0, 2)];
            if (h.arg == 1) st.bg2 = st.bg1;
            st.custom = true;
            SaveBuildSoon();
            return true;
        }
        case W_LAYOUT: {
            const wchar_t* l[] = {L"classic", L"showcase", L"minimal"};
            st.layout = l[std::clamp(h.arg, 0, 2)];
            st.custom = true;
            SaveBuildSoon();
            return true;
        }
        case W_FONT: {
            std::vector<std::wstring> items;
            int sel = -1;
            for (size_t i = 0; i < Fonts().size(); ++i) { items.push_back(Fonts()[i].first); if (FontLabel(st.font) == Fonts()[i].first) sel = (int)i; }
            OpenMenu(s_fontAnchor, items, sel, [](int i) {
                CardStyle& s = g_build.style;
                const auto& f = Fonts()[i];
                s.font = f.second;
                s.headFont = f.first == L"Segoe UI Variable" ? L"Segoe UI Variable Display" : f.second;
                s.custom = true;
                SaveBuildSoon();
            });
            return true;
        }
        case W_GLOW: st.glow = !st.glow; st.custom = true; SaveBuildSoon(); return true;
        case W_BORDER: st.border = !st.border; st.custom = true; SaveBuildSoon(); return true;
        case W_SECTION: {
            bool* f[] = {&b.showParts, &b.showLive, &b.showBench, &b.showPhoto, &b.showPeripherals};
            if (h.arg >= 0 && h.arg < 5) *f[h.arg] = !*f[h.arg];
            SaveBuildSoon();
            return true;
        }
        case W_LIVEMODE: b.liveOnShare = h.arg == 0; SaveBuildSoon(); return true;
        case W_PHOTO_CHOOSE: {
            std::wstring path = PickOpenFile(L"Images\0*.jpg;*.jpeg;*.png;*.webp;*.heic;*.bmp;*.gif;*.tif;*.tiff\0All files\0*.*\0", L"Choose a photo of your build");
            if (!path.empty()) SetPhotoFromFile(path);
            return true;
        }
        case W_PHOTO_REMOVE: RemovePhoto(); return true;
        case W_PHOTO_RESET: b.photoX = b.photoY = 0.5f; b.photoZoom = 1; SaveBuildSoon(); return true;
        case W_PHOTO_BLURBG:
            if (st.background == L"photo") st.background = L"gradient";
            else st.background = L"photo";
            st.custom = true;
            SaveBuildSoon();
            return true;
        case W_SHARE_COPY: {
            std::wstring link = ShareLink();
            CopyText(link);
            if (!b.gistId.empty()) Toast(L"Share link copied.");
            else if (!GetToken().empty()) { Toast(L"Quick link copied. Publishing your share page for a link that updates…"); PublishShare(false); }
            else Toast(L"Quick link copied (a snapshot, no photo). Connect GitHub in Settings for a link that updates.");
            return true;
        }
        case W_SHARE_QUICK:
            CopyText(share::QuickLink(g_build, g_live, CardBench()));
            Toast(L"Quick link copied. It's a snapshot of the card as it is now, without the photo.");
            return true;
        case W_SHARE_OPEN: OpenUrl(ShareLink()); return true;
        case W_SHARE_PUBLISH: PublishShare(true); return true;
        case W_SHARE_LIVE: g_cfg.liveShare = !g_cfg.liveShare; ConfigSave(); SaveBuildSoon(); return true;
        case W_SHARE_CONNECT: SetPage(Page::Settings); g_scrollTarget = 900; StartAnim(); return true;
        case W_SHARE_UNPUBLISH:
            if (MessageBoxW(g_hwnd, L"Delete your share page? The link will stop working. You can publish a new one any time.", L"Rigcard", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
                share::Unpublish(b.gistId);
                b.gistId.clear();
                b.gistOwner.clear();
                g_cfg.liveShare = false;
                ConfigSave();
                BuildSave();
                g_shareStatus.clear();
                Toast(L"Share page deleted.");
            }
            return true;
        case W_EXPORT_PNG: {
            std::vector<std::wstring> items;
            for (auto& s : ExportSizes()) items.push_back(s.name);
            OpenMenu(s_exportAnchor, items, -1, [](int i) { ExportPngSize((size_t)i); });
            return true;
        }
        case W_COPY_IMAGE: {
            UINT w, hgt;
            AspectSize(w, hgt);
            if (IWICBitmap* bmp = cardrender::RenderBitmap(w, hgt, MakeCardInput())) {
                bool ok = image::CopyToClipboard(g_hwnd, bmp);
                bmp->Release();
                Toast(ok ? L"Card image copied. Paste it into Discord, Reddit or anywhere." : L"Couldn't copy the image.", !ok);
            }
            return true;
        }
        case W_RUN_BENCH: StartBenchmarks(bench::BuiltinTests()); return true;
    }
    return false;
}

bool WheelCardPage(float x, float y, float delta) {
    if (x >= s_panelRect.left && y >= s_panelRect.top) {
        g_panelScrollTarget = std::clamp(g_panelScrollTarget - delta, 0.0f, std::max(0.0f, g_panelContentH - g_panelViewH));
        return true;
    }
    D2D1_RECT_F p = s_hit.photo;
    if (x >= p.left && x < p.right && y >= p.top && y < p.bottom && !g_photo.Empty()) {
        g_build.photoZoom = std::clamp(g_build.photoZoom * (delta > 0 ? 1.08f : 1 / 1.08f), 1.0f, 4.0f);
        SaveBuildSoon();
        return true;
    }
    return true;
}

void DragCardPage(float dx, float dy, bool start, bool end, float, float) {
    if (g_photo.Empty()) return;
    Build& b = g_build;
    if (start && end) { b.photoX = b.photoY = 0.5f; b.photoZoom = 1; SaveBuildSoon(); return; }
    if (end) { SaveBuildSoon(); return; }
    if (start) return;
    D2D1_RECT_F r = s_hit.photo;
    float rw = r.right - r.left, rh = r.bottom - r.top;
    if (rw < 2 || rh < 2) return;
    float k = std::max(rw / g_photo.w, rh / g_photo.h) * b.photoZoom;
    float ow = g_photo.w * k - rw, oh = g_photo.h * k - rh;   // how far the photo overflows the frame
    if (ow > 1) b.photoX = std::clamp(b.photoX - dx / ow, 0.0f, 1.0f);
    if (oh > 1) b.photoY = std::clamp(b.photoY - dy / oh, 0.0f, 1.0f);
    Invalidate();
}

void CardFieldChanged(int id, int, bool commit) {
    if (id == W_BUILD_NAME || id == W_BUILD_LOCATION) {
        if (commit) SaveBuildSoon();
        Invalidate();
    }
}
