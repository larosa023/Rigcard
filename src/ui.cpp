// Rigcard - main window: frame, widgets, input, timers and background-result handling
#include "ui.h"
#include "ui_internal.h"
#include "util.h"
#include "json.h"
#include "hardware.h"
#include "share.h"
#include "hangar_sdk.h"
#include "net.h"
#include "partsdb.h"
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <algorithm>
#include <memory>
#include <cmath>
#include <ctime>

using namespace gfx;

// ---------------------------------------------------------------- state
HWND g_hwnd;
ID2D1HwndRenderTarget* g_rt;
static ID2D1SolidColorBrush* g_br;
Canvas g_c;
Theme g_t;
float g_W = 0, g_H = 0, g_scale = 1;
Page g_page = Page::Card;
std::vector<HotRect> g_hot;
SensorSnapshot g_live;
bool g_liveRunning = false;
Image g_photo, g_photoBlur;
int g_photoVersion = 0;
SelfUpdateState g_self;
std::wstring g_hwChanges;
BenchProgress g_benchProgress;
bool g_benching = false;
std::wstring g_shareStatus;
std::vector<CardStyle> g_savedStyles;
std::vector<ThemeDef> g_customThemes;
std::wstring g_tokenEdit;
std::wstring g_aspect = L"wide";
float g_pulse = 0;
DWORD g_lastLivePush = 0;
float g_scroll = 0, g_scrollTarget = 0, g_contentH = 0, g_viewH = 0;
float g_panelScroll = 0, g_panelScrollTarget = 0, g_panelContentH = 0, g_panelViewH = 0;

static HotRect g_hover{{0, 0, 0, 0}, Hit::None, 0, 0, false, false};
static HotRect g_pressed{{0, 0, 0, 0}, Hit::None, 0, 0, false, false};
static bool g_tracking = false;
static std::wstring g_toast;
static bool g_toastBad = false;
static DWORD g_toastAt = 0;
static float g_toastAnim = 0;
static bool g_animTimer = false;
static float g_pageAnim = 1;
static DWORD g_lastInput = 0;
static selfupdate::AfterUpdate g_after;
static std::wstring g_whatsNew, g_whatsNewVersion;
static bool g_showNotes = false;           // the update notification panel
static DWORD g_selfCheckedTick = 0;
static DWORD g_lastPublish = 0;
static bool g_publishPending = false;
static bool g_highContrast = false;
static float g_lastMouseX = 0, g_lastMouseY = 0;
static int g_kbFocus = -1;                 // index into the focusable hot list (Tab navigation)
static std::pair<int, int> g_kbFocusKey{0, 0};
static bool g_keyboardMode = false;
static std::vector<D2D1_RECT_F> g_focusRings;
static std::map<std::pair<int, int>, SliderFn> g_sliders;
static std::map<std::pair<int, int>, float> g_sliderVal;   // 0-1 position of each slider as last drawn
static struct { bool active = false; int id = 0, arg = 0; D2D1_RECT_F r{}; float lo = 0, hi = 1; } g_sliderDrag;
static struct { bool active = false; float x = 0, y = 0; } g_photoDrag;

enum : UINT_PTR { TIMER_ANIM = 1, TIMER_CARET, TIMER_SELF, TIMER_HEALTHY, TIMER_SAVE, TIMER_LIVEPUSH, TIMER_HANGAR_HEALTH, TIMER_TOAST, TIMER_PUBLISH };

const float kTopBar = 60;
static const float kTop = kTopBar;

void Invalidate() { if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE); }

static bool ReduceMotion() {
    BOOL anim = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0);
    return !anim || g_cfg.animSpeed <= 0.01f;
}

void StartAnim() {
    if (!g_animTimer && g_hwnd) { SetTimer(g_hwnd, TIMER_ANIM, 15, nullptr); g_animTimer = true; }
    Invalidate();
}

void Toast(const std::wstring& text, bool bad) {
    g_toast = text;
    g_toastBad = bad;
    g_toastAt = GetTickCount();
    SetTimer(g_hwnd, TIMER_TOAST, bad ? 7000 : 4000, nullptr);
    StartAnim();
}

void OpenUrl(const std::wstring& url) { ShellExecuteW(g_hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

float F(float size) { return size * g_cfg.fontScale; }
float D(float px) { return px * (g_cfg.compact ? 0.84f : 1.0f); }
float R() { return g_cfg.radius; }
IDWriteTextFormat* Txt(float size, DWRITE_FONT_WEIGHT w, DWRITE_TEXT_ALIGNMENT a, bool wrap) { return Format(F(size), w, a, wrap); }
IDWriteTextFormat* Icon(float size, DWRITE_TEXT_ALIGNMENT a) { return Format(F(size), DWRITE_FONT_WEIGHT_NORMAL, a, false, true); }

// ---------------------------------------------------------------- theme
void ApplyAppTheme() {
    HIGHCONTRASTW hc{sizeof(hc)};
    g_highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
    ThemeDef def = FindTheme(g_cfg.theme, g_customThemes);
    if (g_cfg.backdrop != L"theme") def.backdrop = g_cfg.backdrop;
    if (g_highContrast) {
        def = FindTheme(SystemUsesLightTheme() ? L"light" : L"midnight", g_customThemes);
        def.backdrop = L"none";
    }
    g_t = MakeTheme(def, g_highContrast ? L"windows" : g_cfg.accent);
    if (g_highContrast) { g_t.subtext = g_t.text; g_t.faint = g_t.subtext; g_t.divider = Alpha(g_t.text, 0.5f); }
    g_t.radius = g_cfg.radius;
    if (g_hwnd) ApplyWindowTheme(g_hwnd, g_t);
    Invalidate();
}

// ---------------------------------------------------------------- dialogs and clipboard
bool PickColor(uint32_t& rgb) {
    static COLORREF custom[16] = {};
    CHOOSECOLORW cc{sizeof(cc)};
    cc.hwndOwner = g_hwnd;
    cc.rgbResult = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return false;
    rgb = (GetRValue(cc.rgbResult) << 16) | (GetGValue(cc.rgbResult) << 8) | GetBValue(cc.rgbResult);
    return true;
}

std::wstring PickOpenFile(const wchar_t* filter, const wchar_t* title) {
    wchar_t file[MAX_PATH * 2] = {0};
    OPENFILENAMEW of{sizeof(of)};
    of.hwndOwner = g_hwnd;
    of.lpstrFilter = filter;
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH * 2;
    of.lpstrTitle = title;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&of) ? file : L"";
}

std::wstring PickSaveFile(const wchar_t* filter, const wchar_t* defName, const wchar_t* defExt) {
    wchar_t file[MAX_PATH * 2] = {0};
    wcsncpy(file, defName, MAX_PATH);
    OPENFILENAMEW of{sizeof(of)};
    of.hwndOwner = g_hwnd;
    of.lpstrFilter = filter;
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH * 2;
    of.lpstrDefExt = defExt;
    std::wstring pics = KnownFolder(FOLDERID_Pictures);
    of.lpstrInitialDir = pics.c_str();
    of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    return GetSaveFileNameW(&of) ? file : L"";
}

void CopyText(const std::wstring& text) {
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        memcpy(GlobalLock(g), text.c_str(), bytes);
        GlobalUnlock(g);
        if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
    }
    CloseClipboard();
}

std::wstring ClipboardText() {
    std::wstring s;
    if (!OpenClipboard(g_hwnd)) return s;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) { s = p; GlobalUnlock(h); }
    }
    CloseClipboard();
    return s;
}

// ---------------------------------------------------------------- build helpers
std::vector<BenchResult> CardBench() {
    std::vector<BenchResult> out;
    for (auto& r : bench::Latest()) {
        if (std::find(g_build.hiddenBench.begin(), g_build.hiddenBench.end(), r.id) != g_build.hiddenBench.end()) continue;
        out.push_back(r);
    }
    return out;
}

std::wstring ShareLink() {
    if (!g_build.gistId.empty()) return HostedUrl(g_build.gistId);
    return share::QuickLink(g_build, g_live, CardBench());
}

CardInput MakeCardInput() {
    CardInput in;
    in.build = &g_build;
    in.live = g_live;
    in.liveRunning = g_liveRunning;
    in.bench = CardBench();
    in.history = bench::History();
    in.photo = g_photo.Empty() ? nullptr : &g_photo;
    in.photoBlur = g_photoBlur.Empty() ? nullptr : &g_photoBlur;
    in.photoVersion = g_photoVersion;
    in.footerUrl = g_build.gistId.empty() ? std::wstring(RIGCARD_SITE) : HostedUrl(g_build.gistId);
    in.pulse = g_pulse;
    return in;
}

static void LoadPhotoFromBuild() {
    g_photo = Image{};
    g_photoBlur = Image{};
    if (!g_build.photo.empty()) {
        std::wstring err;
        if (image::Load(g_build.photo, 2000, g_photo, &err)) g_photoBlur = image::Blurred(g_photo, 200);
        else Log(L"photo: %ls", err.c_str());
    }
    ++g_photoVersion;
}

void SetPhotoFromFile(const std::wstring& path) {
    Image img;
    std::wstring err;
    if (!image::Load(path, 2400, img, &err)) { Toast(err, true); return; }
    std::string jpeg;
    std::wstring dest = DataDir() + L"\\photo.jpg";
    if (!image::EncodeJpeg(img, 0.9f, jpeg) || !image::SaveBytes(dest, jpeg)) { Toast(L"Couldn't save the photo.", true); return; }
    g_build.photo = dest;
    g_build.photoX = g_build.photoY = 0.5f;
    g_build.photoZoom = 1.0f;
    g_build.showPhoto = true;
    LoadPhotoFromBuild();
    SaveBuildSoon();
    Toast(L"Photo added. Drag it on the card to position it; scroll to zoom.");
}

void RemovePhoto() {
    if (!g_build.photo.empty()) DeleteFileW(g_build.photo.c_str());
    g_build.photo.clear();
    if (g_build.style.background == L"photo") g_build.style.background = L"gradient";
    LoadPhotoFromBuild();
    SaveBuildSoon();
}

void SaveBuildSoon() {
    SetTimer(g_hwnd, TIMER_SAVE, 700, nullptr);
    Invalidate();
}

void Rescan() {
    hardware::ScanAsync(g_hwnd);
    Toast(L"Scanning your hardware…");
}

void SetPage(Page p) {
    if (g_page == p) return;
    BlurField();
    g_page = p;
    g_cfg.page = p == Page::Card ? L"card" : p == Page::Parts ? L"parts" : p == Page::Bench ? L"bench" : L"settings";
    g_scroll = g_scrollTarget = 0;
    g_pageAnim = ReduceMotion() ? 1.0f : 0.0f;
    g_kbFocus = -1;
    StartAnim();
}

// ---------------------------------------------------------------- sensors, benchmarks, sharing
static void UpdateSensorActivity() {
    bool visible = IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd);
    bool live = g_cfg.liveShare && !g_build.gistId.empty();
    bool want = visible || live;
    sensors::SetActive(want);
    g_liveRunning = want;
    if (live) SetTimer(g_hwnd, TIMER_LIVEPUSH, (UINT)g_cfg.liveSeconds * 1000, nullptr);
    else KillTimer(g_hwnd, TIMER_LIVEPUSH);
}

void StartBenchmarks(const std::vector<std::wstring>& tests) {
    if (bench::Running() || tests.empty()) return;
    g_benching = true;
    g_benchProgress = BenchProgress{};
    g_benchProgress.label = L"Starting…";
    hangar::SendBusy(true, L"benchmark");
    bench::Start(g_hwnd, tests, g_cfg.benchDrive);
    if (g_page != Page::Bench) SetPage(Page::Bench);
    Invalidate();
}

void PublishShare(bool userAsked) {
    if (share::Busy()) { if (userAsked) Toast(L"Already updating the share page…"); return; }
    if (GetToken().empty()) {
        if (userAsked) {
            Toast(L"Connect GitHub in Settings → Sharing to get a link that updates. A quick link was copied instead.");
            CopyText(share::QuickLink(g_build, g_live, CardBench()));
        }
        return;
    }
    std::string jpeg;
    if (!g_photo.Empty() && g_build.showPhoto) {
        UINT maxDim = 1400;
        Image small = g_photo;
        if (std::max(g_photo.w, g_photo.h) > maxDim) {
            float k = (float)maxDim / std::max(g_photo.w, g_photo.h);
            small = image::Resize(g_photo, (UINT)(g_photo.w * k), (UINT)(g_photo.h * k));
        }
        image::EncodeJpeg(small, 0.82f, jpeg);
    }
    g_lastPublish = GetTickCount();
    g_publishPending = false;
    g_shareStatus = L"Updating the share page…";
    share::PublishAsync(g_hwnd, g_build, g_live, CardBench(), jpeg);
    if (userAsked) Toast(g_build.gistId.empty() ? L"Creating your share page…" : L"Updating your share page…");
    Invalidate();
}

// ---------------------------------------------------------------- widgets
void AddHot(const D2D1_RECT_F& r, Hit kind, int id, int arg, bool enabled, bool focusable) {
    g_hot.push_back({r, kind, id, arg, enabled, focusable && enabled});
    if (g_keyboardMode && focusable && enabled && g_kbFocusKey.first == id && g_kbFocusKey.second == arg) g_focusRings.push_back(r);
}
void ClipHot(size_t from, const D2D1_RECT_F& clip) {
    std::vector<HotRect> keep(g_hot.begin(), g_hot.begin() + std::min(from, g_hot.size()));
    for (size_t i = from; i < g_hot.size(); ++i) {
        HotRect h = g_hot[i];
        h.r.left = std::max(h.r.left, clip.left);
        h.r.top = std::max(h.r.top, clip.top);
        h.r.right = std::min(h.r.right, clip.right);
        h.r.bottom = std::min(h.r.bottom, clip.bottom);
        if (h.r.right > h.r.left && h.r.bottom > h.r.top) keep.push_back(h);
    }
    g_hot.swap(keep);
}
bool IsHover(int id, int arg) { return g_hover.kind != Hit::None && g_hover.id == id && g_hover.arg == arg; }
bool IsPressed(int id, int arg) { return g_pressed.kind != Hit::None && g_pressed.id == id && g_pressed.arg == arg && IsHover(id, arg); }

float ButtonWidth(const std::wstring& label, wchar_t glyph) {
    float w = MeasureWidth(label, Txt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD)) + 28;
    if (glyph) w += label.empty() ? 6 : 22;
    return std::max(w, label.empty() ? 34.0f : 72.0f);
}

void Button(const D2D1_RECT_F& r, const std::wstring& label, wchar_t glyph, Btn style, int id, int arg, bool enabled) {
    bool hot = enabled && IsHover(id, arg);
    bool down = enabled && IsPressed(id, arg);
    D2D1_COLOR_F bg, fg;
    switch (style) {
        case Btn::Primary: bg = hot ? g_t.accentHover : g_t.accent; fg = g_t.onAccent; break;
        case Btn::Danger: bg = hot ? Alpha(g_t.bad, 0.22f) : Alpha(g_t.bad, 0.12f); fg = g_t.bad; break;
        case Btn::Subtle: bg = hot ? g_t.surfaceHover : Alpha(g_t.surface, 0.0f); fg = g_t.text; break;
        default: bg = hot ? g_t.surfaceStrong : Mix(g_t.surface, g_t.surfaceStrong, 0.55f); fg = g_t.text; break;
    }
    if (!enabled) { bg = Alpha(g_t.surfaceStrong, 0.5f); fg = g_t.faint; }
    if (down) bg = Mix(bg, g_t.dark ? Rgb(0x000000) : Rgb(0xFFFFFF), 0.12f);
    float rad = std::min(R(), (r.bottom - r.top) / 2);
    g_c.FillRound(r, rad, bg);
    if (style == Btn::Secondary && enabled) g_c.StrokeRound(r, rad, g_t.divider);
    float w = r.right - r.left;
    auto* tf = Txt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    if (glyph && label.empty()) {
        g_c.Glyph(glyph, Icon(14), r, fg);
    } else if (glyph) {
        float tw = MeasureWidth(label, Txt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD)) + 22;
        float x = r.left + std::max(8.0f, (w - tw) / 2);
        g_c.Glyph(glyph, Icon(13, DWRITE_TEXT_ALIGNMENT_LEADING), D2D1::RectF(x, r.top, x + 18, r.bottom), fg);
        g_c.Text(label, Txt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(x + 22, r.top, r.right - 4, r.bottom), fg);
    } else {
        g_c.Text(label, tf, r, fg);
    }
    AddHot(r, Hit::Button, id, arg, enabled);
}

void Toggle(float x, float y, bool on, int id, int arg, bool enabled) {
    D2D1_RECT_F r = D2D1::RectF(x, y, x + 40, y + 22);
    bool hot = IsHover(id, arg) && enabled;
    D2D1_COLOR_F track = on ? (hot ? g_t.accentHover : g_t.accent) : (hot ? g_t.surfaceStrong : Mix(g_t.surfaceStrong, g_t.surface, 0.2f));
    if (!enabled) track = Alpha(track, 0.4f);
    g_c.FillRound(r, 11, track);
    if (!on) g_c.StrokeRound(r, 11, Alpha(g_t.text, 0.25f));
    float kx = on ? x + 29 : x + 11;
    g_c.Circle(kx, y + 11, 7, on ? g_t.onAccent : Alpha(g_t.text, 0.8f));
    AddHot(D2D1::RectF(x - 4, y - 4, x + 44, y + 26), Hit::Toggle, id, arg, enabled);
}

float ToggleRow(float x, float y, float w, const std::wstring& label, const std::wstring& sub, bool on, int id, int arg, bool enabled) {
    float h = sub.empty() ? D(38) : D(52);
    g_c.Text(label, Txt(13.5f), D2D1::RectF(x, y, x + w - 56, y + (sub.empty() ? h : D(28))), enabled ? g_t.text : g_t.faint);
    if (!sub.empty()) g_c.Text(sub, Txt(12), D2D1::RectF(x, y + D(24), x + w - 56, y + D(46)), g_t.subtext);
    Toggle(x + w - 42, y + (h - 22) / 2, on, id, arg, enabled);
    // the whole row toggles
    g_hot.push_back({D2D1::RectF(x, y, x + w - 50, y + h), Hit::Toggle, id, arg, enabled, false});
    return h;
}

float Segmented(float x, float y, const std::vector<std::wstring>& opts, int sel, int id, float maxW) {
    auto* f = Txt(12.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    float total = 0;
    std::vector<float> ws;
    for (auto& o : opts) { ws.push_back(MeasureWidth(o, f) + 22); total += ws.back() + 4; }
    float k = (maxW > 0 && total > maxW) ? maxW / total : 1.0f;
    if (maxW > 0 && total < maxW) k = maxW / total;   // fill the width evenly
    float cx = x, h = D(30);
    for (size_t i = 0; i < opts.size(); ++i) {
        float w = ws[i] * k;
        D2D1_RECT_F r = D2D1::RectF(cx, y, cx + w, y + h);
        bool s = (int)i == sel;
        float rad = std::min(R(), h / 2);
        g_c.FillRound(r, rad, s ? Alpha(g_t.accent, 0.20f) : (IsHover(id, (int)i) ? g_t.surfaceHover : Mix(g_t.surface, g_t.surfaceStrong, 0.35f)));
        if (s) g_c.StrokeRound(r, rad, Alpha(g_t.accent, 0.7f));
        g_c.Text(opts[i], Txt(12.5f, s ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER), r, s ? g_t.text : g_t.subtext);
        AddHot(r, Hit::Segment, id, (int)i);
        cx += w + 4 * k;
    }
    return h;
}

void SliderBind(int id, int arg, SliderFn fn) { g_sliders[{id, arg}] = std::move(fn); }

void Slider(const D2D1_RECT_F& r, float value, float lo, float hi, int id, int arg) {
    float t = hi > lo ? std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f) : 0;
    float cy = (r.top + r.bottom) / 2;
    g_c.FillRound(D2D1::RectF(r.left, cy - 2, r.right, cy + 2), 2, g_t.surfaceStrong);
    g_c.FillRound(D2D1::RectF(r.left, cy - 2, r.left + (r.right - r.left) * t, cy + 2), 2, g_t.accent);
    float kx = r.left + (r.right - r.left) * t;
    bool hot = IsHover(id, arg) || (g_sliderDrag.active && g_sliderDrag.id == id && g_sliderDrag.arg == arg);
    g_c.Circle(kx, cy, hot ? 8.0f : 7.0f, g_t.accent);
    g_c.Circle(kx, cy, 3, g_t.onAccent);
    g_hot.push_back({D2D1::RectF(r.left - 8, r.top, r.right + 8, r.bottom), Hit::Slider, id, arg, true, true});
    if (g_keyboardMode && g_kbFocusKey.first == id && g_kbFocusKey.second == arg) g_focusRings.push_back(r);
    g_sliderVal[{id, arg}] = t;
}


void ColorSwatch(const D2D1_RECT_F& r, uint32_t rgb, int id, int arg, bool selected) {
    float rad = std::min(R(), (r.bottom - r.top) / 2);
    g_c.FillRound(r, rad, Rgb(rgb));
    g_c.StrokeRound(r, rad, selected ? g_t.text : Alpha(g_t.text, IsHover(id, arg) ? 0.5f : 0.18f), selected ? 2.0f : 1.0f);
    AddHot(r, Hit::Swatch, id, arg);
}

void Card(const D2D1_RECT_F& r) {
    g_c.FillRound(r, R() + 2, g_t.surface);
    if (!g_t.dark || g_t.backdrop) g_c.StrokeRound(r, R() + 2, g_t.divider);
}

void ProgressBar(const D2D1_RECT_F& r, float pct, D2D1_COLOR_F color) {
    float h = r.bottom - r.top;
    g_c.FillRound(r, h / 2, g_t.surfaceStrong);
    D2D1_RECT_F s = r;
    s.right = r.left + (r.right - r.left) * std::clamp(pct / 100.0f, 0.0f, 1.0f);
    if (s.right - s.left > 1) g_c.FillRound(s, h / 2, color);
}

float Label(float x, float y, float w, const std::wstring& text, float size, D2D1_COLOR_F color, DWRITE_FONT_WEIGHT weight, bool wrap) {
    auto* f = Txt(size, weight, DWRITE_TEXT_ALIGNMENT_LEADING, wrap);
    float h = wrap ? MeasureHeight(text, f, w) + 2 : F(size) * 1.6f;
    g_c.Text(text, f, D2D1::RectF(x, y, x + w, y + h), color);
    return h;
}

float SectionHeader(float x, float y, float w, const std::wstring& title, const std::wstring& sub) {
    g_c.Text(title, Txt(15, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(x, y, x + w, y + D(26)), g_t.text);
    float h = D(28);
    if (!sub.empty()) h += Label(x, y + D(24), w, sub, 12, g_t.subtext, DWRITE_FONT_WEIGHT_NORMAL, true);
    return h + D(8);
}

// ---------------------------------------------------------------- text fields
static struct EditState {
    int id = -1, arg = -1;
    std::wstring* target = nullptr;
    size_t caret = 0, anchor = 0;
    std::wstring kind;
    bool password = false;
    D2D1_RECT_F rect{};
    std::vector<std::wstring> sugg;
    int sel = -1;
    bool caretOn = true;
    float scrollX = 0;
} g_edit;

bool FieldFocused(int id, int arg) { return g_edit.id == id && (arg < 0 || g_edit.arg == arg); }

static void FieldChanged(bool commit) {
    int id = g_edit.id, arg = g_edit.arg;
    if (id >= W_BUILD_NAME && id < W_RESCAN) CardFieldChanged(id, arg, commit);
    else if (id >= W_RESCAN && id < W_BENCH_RUN) PartsFieldChanged(id, arg, commit);
    else if (id >= W_THEME) SettingsFieldChanged(id, arg, commit);
}

void BlurField() {
    if (g_edit.id < 0) return;
    FieldChanged(true);
    g_edit = EditState{};
    KillTimer(g_hwnd, TIMER_CARET);
    Invalidate();
}

static void RefreshSuggestions() {
    g_edit.sugg.clear();
    g_edit.sel = -1;
    if (!g_edit.kind.empty() && g_edit.target) {
        g_edit.sugg = partsdb::Search(g_edit.kind, *g_edit.target, 7);
        if (g_edit.sugg.size() == 1 && IEquals(g_edit.sugg[0], *g_edit.target)) g_edit.sugg.clear();
    }
}

static std::wstring Shown(const std::wstring& s, bool password) { return password ? std::wstring(s.size(), L'•') : s; }

static size_t CaretFromX(const std::wstring& text, IDWriteTextFormat* f, float x) {
    if (text.empty() || x <= 0) return 0;
    IDWriteTextLayout* l = nullptr;
    size_t pos = text.size();
    if (SUCCEEDED(dw->CreateTextLayout(text.c_str(), (UINT32)text.size(), f, 10000, 100, &l))) {
        BOOL trailing = FALSE, inside = FALSE;
        DWRITE_HIT_TEST_METRICS m;
        if (SUCCEEDED(l->HitTestPoint(x, 10, &trailing, &inside, &m))) pos = m.textPosition + (trailing ? m.length : 0);
        l->Release();
    }
    return std::min(pos, text.size());
}

void Field(const D2D1_RECT_F& r, int id, int arg, std::wstring* value, const std::wstring& placeholder, const std::wstring& pickerKind, bool password) {
    bool focused = g_edit.id == id && g_edit.arg == arg;
    if (focused) { g_edit.target = value; g_edit.rect = r; g_edit.kind = pickerKind; g_edit.password = password; }
    float rad = std::min(R(), (r.bottom - r.top) / 2);
    bool hot = IsHover(id, arg);
    g_c.FillRound(r, rad, focused ? Mix(g_t.surface, g_t.bg, 0.3f) : hot ? g_t.surfaceHover : Mix(g_t.surface, g_t.surfaceStrong, 0.35f));
    g_c.StrokeRound(r, rad, focused ? g_t.accent : g_t.divider, focused ? 1.5f : 1.0f);
    if (focused) g_c.Fill(D2D1::RectF(r.left + rad, r.bottom - 2, r.right - rad, r.bottom), g_t.accent);
    auto* f = Txt(13.5f);
    D2D1_RECT_F inner = D2D1::RectF(r.left + 10, r.top, r.right - 10, r.bottom);
    std::wstring shown = Shown(*value, password);
    if (shown.empty() && !focused) {
        g_c.Text(placeholder, f, inner, g_t.faint);
    } else {
        // keep the caret visible: scroll the text left when it's longer than the box
        float caretX = 0;
        if (focused) {
            caretX = MeasureWidth(shown.substr(0, std::min(g_edit.caret, shown.size())), f);
            float vis = inner.right - inner.left - 4;
            if (caretX - g_edit.scrollX > vis) g_edit.scrollX = caretX - vis;
            if (caretX - g_edit.scrollX < 0) g_edit.scrollX = caretX;
        }
        float sx = focused ? g_edit.scrollX : 0;
        g_c.rt->PushAxisAlignedClip(D2D1::RectF(inner.left - 6, r.top, inner.right + 4, r.bottom), D2D1_ANTIALIAS_MODE_ALIASED);
        if (focused && g_edit.anchor != g_edit.caret) {
            size_t a = std::min(g_edit.anchor, g_edit.caret), b = std::max(g_edit.anchor, g_edit.caret);
            float x1 = MeasureWidth(shown.substr(0, a), f), x2 = MeasureWidth(shown.substr(0, b), f);
            g_c.Fill(D2D1::RectF(inner.left + x1 - sx, r.top + 6, inner.left + x2 - sx, r.bottom - 6), Alpha(g_t.accent, 0.35f));
        }
        auto* nf = Format(F(13.5f), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false);
        g_c.br->SetColor(g_t.text);
        g_c.rt->DrawText(shown.c_str(), (UINT32)shown.size(), nf, D2D1::RectF(inner.left - sx, r.top, inner.left - sx + 10000, r.bottom), g_c.br);
        if (focused && g_edit.caretOn) g_c.Fill(D2D1::RectF(inner.left + caretX - sx, r.top + 7, inner.left + caretX - sx + 1.5f, r.bottom - 7), g_t.text);
        g_c.rt->PopAxisAlignedClip();
        if (shown.empty() && focused) g_c.Text(placeholder, f, D2D1::RectF(inner.left + 4, r.top, inner.right, r.bottom), Alpha(g_t.faint, 0.6f));
    }
    AddHot(r, Hit::Field, id, arg);
}

static void FocusField(const HotRect& h, float clickX) {
    if (g_edit.id == h.id && g_edit.arg == h.arg) {
        if (g_edit.target) {
            g_edit.caret = g_edit.anchor = CaretFromX(Shown(*g_edit.target, g_edit.password), Txt(13.5f), clickX - h.r.left - 10 + g_edit.scrollX);
        }
        return;
    }
    BlurField();
    g_edit.id = h.id;
    g_edit.arg = h.arg;
    g_edit.rect = h.r;
    g_edit.target = nullptr;
    g_edit.scrollX = 0;
    g_edit.caretOn = true;
    SetTimer(g_hwnd, TIMER_CARET, 530, nullptr);
    // the target pointer is bound on the next paint; place the caret at the end then
    g_edit.caret = g_edit.anchor = (size_t)-1;
    Invalidate();
}

static void EditInsert(const std::wstring& s) {
    if (!g_edit.target) return;
    std::wstring& t = *g_edit.target;
    size_t a = std::min(g_edit.anchor, g_edit.caret), b = std::max(g_edit.anchor, g_edit.caret);
    a = std::min(a, t.size()); b = std::min(b, t.size());
    std::wstring clean;
    for (wchar_t c : s) if (c >= 0x20 && c != 0x7F) clean += c;
    t.replace(a, b - a, clean);
    g_edit.caret = g_edit.anchor = a + clean.size();
    if (t.size() > 300) { t.resize(300); g_edit.caret = g_edit.anchor = std::min(g_edit.caret, t.size()); }
    RefreshSuggestions();
    FieldChanged(false);
    g_edit.caretOn = true;
    Invalidate();
}

static bool EditKey(WPARAM vk) {
    if (g_edit.id < 0 || !g_edit.target) return false;
    std::wstring& t = *g_edit.target;
    bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    size_t& c = g_edit.caret;
    size_t& a = g_edit.anchor;
    auto move = [&](size_t to) { c = std::min(to, t.size()); if (!shift) a = c; };
    auto wordLeft = [&](size_t p) { while (p > 0 && t[p - 1] == L' ') --p; while (p > 0 && t[p - 1] != L' ') --p; return p; };
    auto wordRight = [&](size_t p) { while (p < t.size() && t[p] != L' ') ++p; while (p < t.size() && t[p] == L' ') ++p; return p; };
    switch (vk) {
        case VK_LEFT: move(a != c && !shift ? std::min(a, c) : ctrl ? wordLeft(c) : (c ? c - 1 : 0)); break;
        case VK_RIGHT: move(a != c && !shift ? std::max(a, c) : ctrl ? wordRight(c) : c + 1); break;
        case VK_HOME: move(0); break;
        case VK_END: move(t.size()); break;
        case VK_BACK:
            if (a == c && c > 0) a = ctrl ? wordLeft(c) : c - 1;
            EditInsert(L"");
            return true;
        case VK_DELETE:
            if (a == c && c < t.size()) a = ctrl ? wordRight(c) : c + 1;
            EditInsert(L"");
            return true;
        case VK_UP:
            if (!g_edit.sugg.empty()) { g_edit.sel = std::max(-1, g_edit.sel - 1); break; }
            return true;
        case VK_DOWN:
            if (!g_edit.sugg.empty()) { g_edit.sel = std::min((int)g_edit.sugg.size() - 1, g_edit.sel + 1); break; }
            return true;
        case VK_RETURN:
            if (g_edit.sel >= 0 && g_edit.sel < (int)g_edit.sugg.size()) { t = g_edit.sugg[g_edit.sel]; c = a = t.size(); }
            BlurField();
            return true;
        case VK_ESCAPE:
            if (!g_edit.sugg.empty()) { g_edit.sugg.clear(); break; }
            BlurField();
            return true;
        case VK_TAB:
            BlurField();
            return false;   // let Tab move focus
        default:
            if (ctrl && vk == 'A') { a = 0; c = t.size(); break; }
            if (ctrl && (vk == 'C' || vk == 'X')) {
                if (a != c && !g_edit.password) CopyText(t.substr(std::min(a, c), (a > c ? a - c : c - a)));
                if (vk == 'X') EditInsert(L"");
                return true;
            }
            if (ctrl && vk == 'V') { std::wstring s = ClipboardText(); std::replace(s.begin(), s.end(), L'\n', L' '); std::replace(s.begin(), s.end(), L'\r', L' '); EditInsert(s); return true; }
            return true;   // swallow other keys while typing
    }
    g_edit.caretOn = true;
    Invalidate();
    return true;
}

static void DrawSuggestions() {
    if (g_edit.id < 0 || g_edit.sugg.empty()) return;
    D2D1_RECT_F r = g_edit.rect;
    float rowH = D(32);
    float h = rowH * g_edit.sugg.size() + 8;
    float top = r.bottom + 4;
    if (top + h > g_H - 8) top = r.top - 4 - h;
    D2D1_RECT_F box = D2D1::RectF(r.left, top, r.right, top + h);
    g_c.FillRound(D2D1::RectF(box.left + 2, box.top + 4, box.right + 2, box.bottom + 6), R() + 2, Rgb(0x000000, 0.25f));
    g_c.FillRound(box, R() + 2, Mix(g_t.surface, g_t.surfaceStrong, 0.4f));
    g_c.StrokeRound(box, R() + 2, g_t.divider);
    for (size_t i = 0; i < g_edit.sugg.size(); ++i) {
        D2D1_RECT_F row = D2D1::RectF(box.left + 4, box.top + 4 + i * rowH, box.right - 4, box.top + 4 + (i + 1) * rowH);
        bool sel = (int)i == g_edit.sel || IsHover(9000, (int)i);
        if (sel) g_c.FillRound(row, R(), Alpha(g_t.accent, 0.18f));
        g_c.Text(g_edit.sugg[i], Txt(13), D2D1::RectF(row.left + 10, row.top, row.right - 8, row.bottom), g_t.text);
        g_hot.push_back({row, Hit::Suggest, 9000, (int)i, true, false});
    }
}

// ---------------------------------------------------------------- popup menu
static struct MenuState {
    bool open = false;
    D2D1_RECT_F anchor{};
    std::vector<std::wstring> items;
    int selected = -1;
    std::function<void(int)> onPick;
} g_menu;

void OpenMenu(const D2D1_RECT_F& anchor, const std::vector<std::wstring>& items, int selected, std::function<void(int)> onPick) {
    BlurField();
    g_menu.open = true;
    g_menu.anchor = anchor;
    g_menu.items = items;
    g_menu.selected = selected;
    g_menu.onPick = std::move(onPick);
    Invalidate();
}

static void DrawMenu() {
    if (!g_menu.open) return;
    g_hot.push_back({D2D1::RectF(0, 0, g_W, g_H), Hit::MenuOutside, 9100, 0, true, false});
    auto* f = Txt(13);
    float w = g_menu.anchor.right - g_menu.anchor.left;
    for (auto& it : g_menu.items) w = std::max(w, MeasureWidth(it, f) + 48);
    float rowH = D(34), h = rowH * g_menu.items.size() + 8;
    float x = std::min(g_menu.anchor.left, g_W - w - 8), y = g_menu.anchor.bottom + 4;
    if (y + h > g_H - 8) y = std::max(8.0f, g_menu.anchor.top - 4 - h);
    D2D1_RECT_F box = D2D1::RectF(x, y, x + w, y + h);
    g_c.FillRound(D2D1::RectF(box.left + 2, box.top + 4, box.right + 2, box.bottom + 8), R() + 2, Rgb(0x000000, 0.28f));
    g_c.FillRound(box, R() + 2, Mix(g_t.surface, g_t.surfaceStrong, 0.45f));
    g_c.StrokeRound(box, R() + 2, g_t.divider);
    for (size_t i = 0; i < g_menu.items.size(); ++i) {
        D2D1_RECT_F row = D2D1::RectF(box.left + 4, box.top + 4 + i * rowH, box.right - 4, box.top + 4 + (i + 1) * rowH);
        if (IsHover(9101, (int)i)) g_c.FillRound(row, R(), g_t.surfaceHover);
        if ((int)i == g_menu.selected) g_c.Glyph(icon::Check, Icon(12), D2D1::RectF(row.left + 4, row.top, row.left + 26, row.bottom), g_t.accent);
        g_c.Text(g_menu.items[i], f, D2D1::RectF(row.left + 30, row.top, row.right - 8, row.bottom), g_t.text);
        g_hot.push_back({row, Hit::MenuItem, 9101, (int)i, true, true});
    }
}

// ---------------------------------------------------------------- frame
static void DiscardDevice() {
    cardrender::ReleaseDeviceResources();
    SafeRelease(g_br);
    SafeRelease(g_rt);
}

static bool EnsureDevice() {
    if (g_rt) return true;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    UINT dpi = GetDpiForWindow(g_hwnd);
    props.dpiX = props.dpiY = (float)dpi;
    if (FAILED(d2d->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(g_hwnd, D2D1::SizeU(std::max<LONG>(1, rc.right), std::max<LONG>(1, rc.bottom))), &g_rt))) return false;
    g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &g_br);
    return true;
}

static const wchar_t kNavGlyph[] = {0xEB9F, 0xE950, 0xEC4A, 0xE713};
static const wchar_t* kNavLabel[] = {L"Card", L"Parts", L"Benchmarks", L"Settings"};

static void DrawTopBar() {
    g_c.Fill(D2D1::RectF(0, 0, g_W, kTop), Alpha(g_t.sidebar, g_t.backdrop ? 0.55f : 1.0f));
    g_c.Line(0, kTop - 0.5f, g_W, kTop - 0.5f, g_t.divider);
    // wordmark
    auto* wm = Format(F(19), DWRITE_FONT_WEIGHT_BOLD);
    float ww = MeasureWidth(L"rigcard", wm);
    g_c.Text(L"rigcard", wm, D2D1::RectF(22, 0, 22 + ww + 4, kTop), g_t.text);
    g_c.FillRound(D2D1::RectF(22 + ww + 3, kTop / 2 + 5, 22 + ww + 9, kTop / 2 + 11), 3, g_t.accent);
    float x = 22 + ww + 34;
    for (int i = 0; i < 4; ++i) {
        auto* f = Txt(13.5f, (int)g_page == i ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
        float w = MeasureWidth(kNavLabel[i], f) + 50;
        D2D1_RECT_F r = D2D1::RectF(x, 12, x + w, kTop - 12);
        bool sel = (int)g_page == i;
        if (sel) g_c.FillRound(r, std::min(R(), 18.0f), Alpha(g_t.accent, 0.16f));
        else if (IsHover(W_NAV, i)) g_c.FillRound(r, std::min(R(), 18.0f), g_t.surfaceHover);
        g_c.Glyph(kNavGlyph[i], Icon(14), D2D1::RectF(r.left + 10, r.top, r.left + 32, r.bottom), sel ? g_t.accent : g_t.subtext);
        g_c.Text(kNavLabel[i], f, D2D1::RectF(r.left + 34, r.top, r.right - 6, r.bottom), sel ? g_t.text : g_t.subtext);
        AddHot(r, Hit::Nav, W_NAV, i);
        x += w + 4;
    }
    // right side: update badge, sensors, benchmark state
    float rx = g_W - 18;
    if (g_self.kind == SelfUpdateState::Ready || g_self.kind == SelfUpdateState::Downloading) {
        std::wstring t = g_self.kind == SelfUpdateState::Ready ? L"Update " + g_self.version + L" ready" : L"Downloading " + g_self.version + L"…";
        auto* f = Txt(12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        float w = MeasureWidth(t, f) + 44;
        D2D1_RECT_F r = D2D1::RectF(rx - w, 14, rx, kTop - 14);
        g_c.FillRound(r, (r.bottom - r.top) / 2, IsHover(W_UPDATE_BADGE) ? g_t.accentHover : g_t.accent);
        g_c.Glyph(icon::Update, Icon(12), D2D1::RectF(r.left + 10, r.top, r.left + 28, r.bottom), g_t.onAccent);
        g_c.Text(t, f, D2D1::RectF(r.left + 30, r.top, r.right - 8, r.bottom), g_t.onAccent);
        AddHot(r, Hit::UpdateBadge, W_UPDATE_BADGE, 0, g_self.kind == SelfUpdateState::Ready);
        rx = r.left - 14;
    }
    std::wstring status;
    D2D1_COLOR_F dot = g_t.faint;
    if (g_benching) { status = L"Benchmarking · " + std::to_wstring((int)g_benchProgress.pct) + L"%"; dot = g_t.warn; }
    else if (g_liveRunning && g_live.at) { status = g_cfg.liveShare && !g_build.gistId.empty() ? L"Live · sharing" : L"Live"; dot = g_t.good; }
    if (!status.empty()) {
        auto* f = Txt(12.5f);
        float w = MeasureWidth(status, f);
        g_c.Text(status, f, D2D1::RectF(rx - w, 0, rx, kTop), g_t.subtext);
        g_c.Circle(rx - w - 10, kTop / 2, 4, dot);
    }
}

static void DrawToast() {
    if (g_toastAnim <= 0.01f || g_toast.empty()) return;
    auto* f = Txt(13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true);
    float maxW = std::min(560.0f, g_W - 80);
    float h = std::max(D(44), MeasureHeight(g_toast, f, maxW - 56) + 24);
    float w = std::min(maxW, MeasureWidth(g_toast, Txt(13)) + 64);
    float y = g_H - 24 - h + (1 - g_toastAnim) * 20;
    D2D1_RECT_F r = D2D1::RectF((g_W - w) / 2, y, (g_W + w) / 2, y + h);
    D2D1_COLOR_F bg = Mix(g_t.surfaceStrong, g_t.dark ? Rgb(0x000000) : Rgb(0xFFFFFF), 0.25f);
    g_c.FillRound(D2D1::RectF(r.left + 2, r.top + 4, r.right + 2, r.bottom + 6), R() + 4, Rgb(0x000000, 0.25f * g_toastAnim));
    g_c.FillRound(r, R() + 4, Alpha(bg, g_toastAnim));
    g_c.Glyph(g_toastBad ? icon::Warning : icon::Info, Icon(14), D2D1::RectF(r.left + 12, r.top, r.left + 36, r.bottom), Alpha(g_toastBad ? g_t.bad : g_t.accent, g_toastAnim));
    g_c.Text(g_toast, f, D2D1::RectF(r.left + 42, r.top + 12, r.right - 14, r.bottom - 10), Alpha(g_t.text, g_toastAnim));
}

static std::wstring CleanNotes(const std::wstring& md) {
    std::wstring out;
    size_t i = 0;
    while (i < md.size()) {
        size_t e = md.find(L'\n', i);
        if (e == std::wstring::npos) e = md.size();
        std::wstring line = Trim(md.substr(i, e - i));
        i = e + 1;
        if (line.empty()) continue;
        while (!line.empty() && line[0] == L'#') line.erase(0, 1);
        line = Trim(line);
        if (IStartsWith(line, L"- ") || IStartsWith(line, L"* ")) line = L"•  " + line.substr(2);
        std::wstring clean;
        for (wchar_t c : line) if (c != L'*' && c != L'`') clean += c;
        out += clean + L"\n";
    }
    return Trim(out);
}

static void DrawNotesPanel() {
    bool whatsNew = !g_whatsNew.empty();
    if (!whatsNew && !g_showNotes) return;
    std::wstring title = whatsNew ? L"What's new in Rigcard " + g_whatsNewVersion : L"Rigcard " + g_self.version + L" is ready";
    std::wstring body = whatsNew ? g_whatsNew : (CleanNotes(g_self.notes).empty() ? L"Bug fixes and improvements." : CleanNotes(g_self.notes));
    if (body.size() > 1400) body = body.substr(0, 1400) + L"…";
    float w = std::min(520.0f, g_W - 60);
    auto* bf = Txt(13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true);
    float bh = std::min(g_H - 260, MeasureHeight(body, bf, w - 48) + 8);
    float h = bh + 150;
    D2D1_RECT_F r = D2D1::RectF((g_W - w) / 2, (g_H - h) / 2, (g_W + w) / 2, (g_H + h) / 2);
    g_c.Fill(D2D1::RectF(0, 0, g_W, g_H), Rgb(0x000000, 0.45f));
    g_hot.push_back({D2D1::RectF(0, 0, g_W, g_H), Hit::MenuOutside, 9200, 0, true, false});
    g_c.FillRound(r, R() + 6, Mix(g_t.surface, g_t.bg, 0.2f));
    g_c.StrokeRound(r, R() + 6, g_t.divider);
    g_c.Text(title, Txt(18, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(r.left + 24, r.top + 18, r.right - 24, r.top + 52), g_t.text);
    g_c.Text(body, bf, D2D1::RectF(r.left + 24, r.top + 66, r.right - 24, r.top + 66 + bh), g_t.subtext);
    float by = r.bottom - 58;
    if (whatsNew) {
        Button(D2D1::RectF(r.right - 24 - 110, by, r.right - 24, by + 36), L"Got it", 0, Btn::Primary, W_UPD_NOTES, 0);
    } else {
        float bw = ButtonWidth(L"Restart to update", icon::Update);
        Button(D2D1::RectF(r.right - 24 - bw, by, r.right - 24, by + 36), L"Restart to update", icon::Update, Btn::Primary, W_UPD_RESTART, 0, !g_benching);
        Button(D2D1::RectF(r.right - 24 - bw - 10 - 90, by, r.right - 24 - bw - 10, by + 36), L"Later", 0, Btn::Secondary, W_UPD_NOTES, 0);
        if (g_benching) g_c.Text(L"Waiting for the benchmark to finish", Txt(12), D2D1::RectF(r.left + 24, by, r.right - bw - 140, by + 36), g_t.faint);
    }
}

static void Paint() {
    if (!EnsureDevice()) return;
    D2D1_SIZE_F sz = g_rt->GetSize();
    g_W = sz.width;
    g_H = sz.height;
    g_c.rt = g_rt;
    g_c.br = g_br;
    g_hot.clear();
    g_focusRings.clear();
    g_rt->BeginDraw();
    g_rt->SetTransform(D2D1::Matrix3x2F::Identity());
    g_rt->Clear(g_t.bg);
    float slide = (1.0f - g_pageAnim) * 12;
    if (slide > 0.05f) g_rt->SetTransform(D2D1::Matrix3x2F::Translation(0, slide));
    switch (g_page) {
        case Page::Card: DrawCardPage(0, kTop); break;
        case Page::Parts: DrawPartsPage(0, kTop); break;
        case Page::Bench: DrawBenchPage(0, kTop); break;
        case Page::Settings: DrawSettingsPage(0, kTop); break;
    }
    g_rt->SetTransform(D2D1::Matrix3x2F::Identity());
    if (g_pageAnim < 0.999f) g_c.Fill(D2D1::RectF(0, kTop, g_W, g_H), Alpha(g_t.bg, (1 - g_pageAnim) * 0.8f));
    DrawTopBar();
    DrawSuggestions();
    DrawMenu();
    DrawNotesPanel();
    DrawToast();
    for (auto& r : g_focusRings) g_c.StrokeRound(D2D1::RectF(r.left - 3, r.top - 3, r.right + 3, r.bottom + 3), R() + 3, g_t.accent, 2.0f);
    HRESULT hr = g_rt->EndDraw();
    if (hr == (HRESULT)D2DERR_RECREATE_TARGET) DiscardDevice();
    // place the caret at the end the first time a field is drawn after focusing
    if (g_edit.id >= 0 && g_edit.target && g_edit.caret == (size_t)-1) {
        g_edit.caret = g_edit.target->size();
        g_edit.anchor = 0;   // select all, so typing replaces it
        RefreshSuggestions();
        Invalidate();
    }
}

static bool Animate() {
    bool more = false;
    float k = ReduceMotion() ? 1.0f : std::min(1.0f, 0.28f * std::max(0.25f, g_cfg.animSpeed));
    auto approach = [&](float& v, float target) {
        if (std::fabs(target - v) > 0.5f) { v += (target - v) * k; more = true; } else v = target;
    };
    approach(g_scroll, g_scrollTarget);
    approach(g_panelScroll, g_panelScrollTarget);
    if (g_pageAnim < 1.0f) { g_pageAnim = std::min(1.0f, g_pageAnim + 0.12f * std::max(0.25f, g_cfg.animSpeed)); more = true; }
    float toastTarget = (!g_toast.empty() && g_toastAt) ? 1.0f : 0.0f;
    if (ReduceMotion()) g_toastAnim = toastTarget;
    else if (std::fabs(g_toastAnim - toastTarget) > 0.01f) { g_toastAnim += (toastTarget - g_toastAnim) * 0.25f; more = true; }
    else { g_toastAnim = toastTarget; if (toastTarget == 0) g_toast.clear(); }
    return more;
}

static HotRect HitAt(float x, float y) {
    for (auto it = g_hot.rbegin(); it != g_hot.rend(); ++it)
        if (x >= it->r.left && x < it->r.right && y >= it->r.top && y < it->r.bottom) return *it;
    return {{0, 0, 0, 0}, Hit::None, 0, 0, false, false};
}

// ---------------------------------------------------------------- state for live updates / self-updates
static std::wstring RestoreJson() {
    json::Value v = json::Value::MakeObject();
    v.Set(L"page", json::Value::Str(g_cfg.page));
    v.Set(L"aspect", json::Value::Str(g_aspect));
    v.Set(L"scroll", json::Value::Num(g_scrollTarget));
    v.Set(L"panelScroll", json::Value::Num(g_panelScrollTarget));
    v.Set(L"minimized", json::Value::Boolean(IsIconic(g_hwnd) != 0));
    v.Set(L"background", json::Value::Boolean(GetForegroundWindow() != g_hwnd));
    std::wstring out;
    json::Write(v, out);
    return out;
}

static void ApplyRestore(const std::wstring& text, int* show) {
    json::Value v;
    if (text.empty() || !json::Parser(text).Parse(v) || v.type != json::Value::Object) return;
    std::wstring page = v.GetStr(L"page", L"card");
    g_page = page == L"parts" ? Page::Parts : page == L"bench" ? Page::Bench : page == L"settings" ? Page::Settings : Page::Card;
    g_aspect = v.GetStr(L"aspect", g_aspect);
    g_scroll = g_scrollTarget = (float)v.GetNum(L"scroll", 0);
    g_panelScroll = g_panelScrollTarget = (float)v.GetNum(L"panelScroll", 0);
    if (show) {
        if (v.GetBool(L"minimized", false)) *show = SW_SHOWMINNOACTIVE;
        else if (v.GetBool(L"background", false)) *show = SW_SHOWNOACTIVATE;
    }
}

static void SaveWindowPlacement() {
    WINDOWPLACEMENT wp{sizeof(wp)};
    if (!GetWindowPlacement(g_hwnd, &wp)) return;
    g_cfg.maximized = wp.showCmd == SW_SHOWMAXIMIZED;
    RECT r = wp.rcNormalPosition;
    g_cfg.windowX = r.left;
    g_cfg.windowY = r.top;
    g_cfg.windowW = r.right - r.left;
    g_cfg.windowH = r.bottom - r.top;
}

static void ApplySelf(bool userAsked) {
    if (g_self.kind != SelfUpdateState::Ready) return;
    if (bench::Running()) { if (userAsked) Toast(L"Rigcard will update after the benchmark finishes."); return; }
    BlurField();
    BuildSave();
    SaveWindowPlacement();
    ConfigSave();
    selfupdate::SaveRestoreState(RestoreJson());
    std::wstring err;
    hangar::Stop();
    if (selfupdate::Apply(g_hwnd, g_self.staged, g_self.version, &err)) {
        sensors::Shutdown();
        DestroyWindow(g_hwnd);
        return;
    }
    hangar::Start(g_hwnd, RIGCARD_APP_ID, RIGCARD_VERSION);
    SkipVersion(g_self.version);
    ConfigSave();
    g_self = SelfUpdateState{};
    g_showNotes = false;
    Toast(err, true);
}

static void MaybeAutoApplySelf() {
    if (!g_cfg.autoUpdate || g_self.kind != SelfUpdateState::Ready || bench::Running() || share::Busy()) return;
    bool away = IsIconic(g_hwnd) || !IsWindowVisible(g_hwnd) || GetTickCount() - g_lastInput > 2 * 60 * 1000;
    LASTINPUTINFO li{sizeof(li)};
    if (GetLastInputInfo(&li) && GetTickCount() - li.dwTime < 60 * 1000 && !IsIconic(g_hwnd)) away = false;
    if (away) ApplySelf(false);
}

static void CheckSelf(bool force) {
    g_selfCheckedTick = GetTickCount();
    selfupdate::Check(g_hwnd, force);
}

// public helpers used by the settings page
void UiCheckUpdates() { CheckSelf(true); }
void UiApplyUpdate() { ApplySelf(true); }
void UiShowNotes() { g_showNotes = true; Invalidate(); }

// ---------------------------------------------------------------- input
static void Click(const HotRect& h, float x) {
    if (h.kind == Hit::MenuOutside && h.id == 9100) { g_menu.open = false; Invalidate(); return; }
    if (h.kind == Hit::MenuOutside && h.id == 9200) { if (g_whatsNew.empty()) g_showNotes = false; Invalidate(); return; }
    if (h.kind == Hit::MenuItem) {
        auto fn = g_menu.onPick;
        g_menu.open = false;
        if (fn) fn(h.arg);
        Invalidate();
        return;
    }
    if (h.kind == Hit::Suggest) {
        if (g_edit.target && h.arg < (int)g_edit.sugg.size()) *g_edit.target = g_edit.sugg[h.arg];
        BlurField();
        return;
    }
    if (h.kind == Hit::Field) { FocusField(h, x); return; }
    if (g_edit.id >= 0) BlurField();
    if (h.kind == Hit::Nav) { SetPage((Page)h.arg); return; }
    if (h.kind == Hit::UpdateBadge) { g_showNotes = true; Invalidate(); return; }
    if (h.id == W_UPD_NOTES && h.arg == 1) { g_showNotes = true; Invalidate(); return; }
    if (h.id == W_UPD_NOTES) {
        if (!g_whatsNew.empty()) { g_whatsNew.clear(); g_whatsNewVersion.clear(); }
        g_showNotes = false;
        Invalidate();
        return;
    }
    if (h.id == W_UPD_RESTART) { ApplySelf(true); return; }
    bool handled = false;
    switch (g_page) {
        case Page::Card: handled = ClickCardPage(h); break;
        case Page::Parts: handled = ClickPartsPage(h); break;
        case Page::Bench: handled = ClickBenchPage(h); break;
        case Page::Settings: handled = ClickSettingsPage(h); break;
    }
    (void)handled;
    Invalidate();
}

static void SliderTo(float x) {
    auto it = g_sliders.find({g_sliderDrag.id, g_sliderDrag.arg});
    if (it == g_sliders.end()) return;
    float t = std::clamp((x - g_sliderDrag.r.left) / std::max(1.0f, g_sliderDrag.r.right - g_sliderDrag.r.left), 0.0f, 1.0f);
    it->second(t);   // pages map 0-1 to their range
    Invalidate();
}

static void Scroll(float delta, float x, float y) {
    if (g_menu.open || g_showNotes || !g_whatsNew.empty()) return;
    if (g_page == Page::Card && WheelCardPage(x, y, delta)) { StartAnim(); return; }
    g_scrollTarget = std::clamp(g_scrollTarget - delta, 0.0f, std::max(0.0f, g_contentH - g_viewH));
    StartAnim();
}

static void MoveKbFocus(bool back) {
    std::vector<const HotRect*> list;
    for (auto& h : g_hot) if (h.focusable) list.push_back(&h);
    if (list.empty()) return;
    int cur = -1;
    for (size_t i = 0; i < list.size(); ++i) if (list[i]->id == g_kbFocusKey.first && list[i]->arg == g_kbFocusKey.second) cur = (int)i;
    int n = (int)list.size();
    int next = cur < 0 ? (back ? n - 1 : 0) : (cur + (back ? n - 1 : 1)) % n;
    g_kbFocusKey = {list[next]->id, list[next]->arg};
    g_keyboardMode = true;
    // scroll into view on scrolling pages
    const D2D1_RECT_F& r = list[next]->r;
    if (g_page != Page::Card) {
        if (r.top < kTop + 10) g_scrollTarget = std::max(0.0f, g_scrollTarget - (kTop + 20 - r.top));
        else if (r.bottom > g_H - 10) g_scrollTarget += r.bottom - g_H + 30;
    }
    StartAnim();
}

static void ActivateKbFocus() {
    for (auto& h : g_hot) {
        if (!h.focusable || h.id != g_kbFocusKey.first || h.arg != g_kbFocusKey.second) continue;
        if (h.kind == Hit::Field) { FocusField(h, h.r.right); return; }
        if (h.kind == Hit::Slider) return;
        Click(h, (h.r.left + h.r.right) / 2);
        return;
    }
}

static void OnKey(WPARAM vk) {
    g_lastInput = GetTickCount();
    if (EditKey(vk)) return;
    bool ctrl = GetKeyState(VK_CONTROL) < 0;
    if (vk == VK_ESCAPE) {
        if (g_menu.open) { g_menu.open = false; Invalidate(); return; }
        if (g_showNotes) { g_showNotes = false; Invalidate(); return; }
        if (!g_whatsNew.empty()) { g_whatsNew.clear(); Invalidate(); return; }
        if (g_keyboardMode) { g_keyboardMode = false; Invalidate(); return; }
        return;
    }
    if (vk == VK_TAB) { MoveKbFocus(GetKeyState(VK_SHIFT) < 0); return; }
    if ((vk == VK_RETURN || vk == VK_SPACE) && g_keyboardMode) { ActivateKbFocus(); return; }
    if (g_keyboardMode && (vk == VK_LEFT || vk == VK_RIGHT)) {
        auto it = g_sliders.find(g_kbFocusKey);
        auto v = g_sliderVal.find(g_kbFocusKey);
        if (it != g_sliders.end() && v != g_sliderVal.end()) {
            it->second(std::clamp(v->second + (vk == VK_RIGHT ? 0.05f : -0.05f), 0.0f, 1.0f));
            SaveBuildSoon();
            ConfigSave();
            Invalidate();
            return;
        }
    }
    if (ctrl && vk >= '1' && vk <= '4') { SetPage((Page)(vk - '1')); return; }
    if (ctrl && vk == 'C' && g_page == Page::Card) { CopyText(ShareLink()); Toast(L"Share link copied."); return; }
    if (ctrl && vk == 'S' && g_page == Page::Card) { HotRect h{{}, Hit::Button, W_EXPORT_PNG, 0, true, true}; ClickCardPage(h); return; }
    if (vk == VK_F5) { Rescan(); return; }
    if (ctrl && vk == VK_OEM_COMMA) { SetPage(Page::Settings); return; }
    if (vk == VK_PRIOR || vk == VK_NEXT) { Scroll(vk == VK_PRIOR ? g_viewH * 0.8f : -g_viewH * 0.8f, 0, kTop + 10); return; }
}

static void OnChar(wchar_t c) {
    if (g_edit.id >= 0 && g_edit.target && c >= 0x20) { EditInsert(std::wstring(1, c)); }
}

// ---------------------------------------------------------------- background results
static void OnHardware(HwResult* r) {
    bool first = !g_build.scanned;
    auto changes = hardware::Merge(g_build, *r);
    if (!r->error.empty()) Toast(r->error, true);
    sensors::Init(g_hwnd, r->cpuVendor, r->cpuFamily, r->baseMhz);
    UpdateSensorActivity();
    if (!changes.empty()) {
        g_hwChanges.clear();
        for (auto& c : changes) g_hwChanges += (g_hwChanges.empty() ? L"" : L"\n") + c;
        Toast(changes.size() == 1 ? L"Hardware changed: " + changes[0] : std::to_wstring(changes.size()) + L" hardware changes. See Parts.");
    } else if (!first) {
        Toast(L"No hardware changes found.");
    }
    BuildSave();
    if (!changes.empty() && !g_build.gistId.empty()) { g_publishPending = true; SetTimer(g_hwnd, TIMER_PUBLISH, 4000, nullptr); }
    Invalidate();
}

static void OnBench(BenchProgress* p) {
    if (p->done) {
        g_benching = false;
        g_benchProgress = *p;
        hangar::SendBusy(false, L"");
        if (!p->error.empty()) Toast(p->error);
        else Toast(L"Benchmark finished: " + std::to_wstring(p->results.size()) + L" result" + (p->results.size() == 1 ? L"" : L"s") + L" saved.");
        if (!g_build.gistId.empty()) { g_publishPending = true; SetTimer(g_hwnd, TIMER_PUBLISH, 2000, nullptr); }
        MaybeAutoApplySelf();
    } else {
        g_benchProgress = *p;
    }
    Invalidate();
}

static void OnShare(ShareResult* r) {
    switch (r->kind) {
        case ShareResult::Published: {
            bool first = g_build.gistId.empty() || g_build.gistId != r->gistId;
            g_build.gistId = r->gistId;
            g_build.gistOwner = r->owner;
            g_build.sharedAt = (long long)time(nullptr);
            BuildSave();
            g_shareStatus = L"Share page up to date";
            if (first) { CopyText(r->url); Toast(L"Your share page is ready. Link copied."); }
            UpdateSensorActivity();
            break;
        }
        case ShareResult::LivePushed:
            g_lastLivePush = GetTickCount();
            break;
        case ShareResult::TokenChecked:
            SetToken(g_tokenEdit);
            g_cfg.githubUser = r->login;
            g_tokenEdit.clear();
            ConfigSave();
            Toast(L"Connected to GitHub as " + r->login + L".");
            break;
        case ShareResult::Error:
            g_shareStatus = r->error;
            if (!r->quiet) Toast(r->error, true);
            else Log(L"live share: %ls", r->error.c_str());
            break;
    }
    Invalidate();
}

static void OnImport(ImportResult* r) {
    if (!r->error.empty() && r->results.empty()) { Toast(r->error, true); return; }
    for (auto& x : r->results) bench::Add(x);
    Toast(L"Imported " + std::to_wstring(r->results.size()) + L" score" + (r->results.size() == 1 ? L"" : L"s") + L".");
    if (!g_build.gistId.empty()) { g_publishPending = true; SetTimer(g_hwnd, TIMER_PUBLISH, 3000, nullptr); }
    Invalidate();
}

static void OnHangar(HangarCommand* cmd) {
    using json::Value;
    if (cmd->type == L"status") {
        if (bench::Running()) {
            cmd->reply.Set(L"state", Value::Str(L"busy"));
            cmd->reply.Set(L"reason", Value::Str(L"benchmark"));
        } else if (share::Busy()) {
            cmd->reply.Set(L"state", Value::Str(L"busy"));
            cmd->reply.Set(L"reason", Value::Str(L"updating the share page"));
        } else {
            cmd->reply.Set(L"state", Value::Str(L"idle"));
        }
    } else if (cmd->type == L"prepare-update") {
        if (bench::Running()) {
            cmd->reply.Set(L"ok", Value::Boolean(false));
            cmd->reply.Set(L"reason", Value::Str(L"benchmark running"));
            return;
        }
        BlurField();
        BuildSave();
        SaveWindowPlacement();
        ConfigSave();
        std::wstring file = DataDir() + L"\\hangar-state.json";
        WriteTextFileAtomic(file, RestoreJson());
        cmd->reply.Set(L"ok", Value::Boolean(true));
        cmd->reply.Set(L"stateFile", Value::Str(file));
    } else if (cmd->type == L"cancel-update") {
        cmd->reply.Set(L"ok", Value::Boolean(true));
    } else if (cmd->type == L"apply-theme") {
        const Value* th = cmd->request.Get(L"theme");
        if (!g_cfg.followHangar || !th) {
            cmd->reply.Set(L"ok", Value::Boolean(false));
            cmd->reply.Set(L"reason", Value::Str(g_cfg.followHangar ? L"no theme" : L"theme sync is off in Rigcard"));
            return;
        }
        std::wstring text, err;
        json::Write(*th, text);
        ThemeDef def;
        if (!ParseThemeJson(text, def, &err)) {
            cmd->reply.Set(L"ok", Value::Boolean(false));
            cmd->reply.Set(L"reason", Value::Str(err));
            return;
        }
        // keep it as a theme file so it survives restarts, and select it
        WriteTextFileAtomic(ThemesDir() + L"\\hangar.json", text);
        g_customThemes = LoadCustomThemes();
        g_cfg.theme = def.id;
        if (std::wstring a = th->Get(L"colors") ? th->Get(L"colors")->GetStr(L"accent") : L""; !a.empty()) g_cfg.accent = a == L"windows" ? L"windows" : L"theme";
        ConfigSave();
        ApplyAppTheme();
        cmd->reply.Set(L"ok", Value::Boolean(true));
    } else if (cmd->type == L"apply-settings") {
        ConfigLoad();
        BuildLoad();
        LoadPhotoFromBuild();
        ApplyAppTheme();
        UpdateSensorActivity();
        cmd->reply.Set(L"ok", Value::Boolean(true));
        cmd->reply.Set(L"restartRequired", Value::Boolean(false));
    } else if (cmd->type == L"export-settings") {
        // the build, the settings and benchmark history in one file (the token stays out: it's tied to this PC)
        Value v = Value::MakeObject();
        v.Set(L"app", Value::Str(RIGCARD_APP_ID));
        v.Set(L"version", Value::Str(RIGCARD_VERSION));
        std::wstring s;
        if (ReadTextFile(DataDir() + L"\\settings.json", s)) { Value c; if (json::Parser(s).Parse(c)) { c.Set(L"token", Value::Str(L"")); v.Set(L"settings", c); } }
        v.Set(L"build", BuildToJson(g_build));
        if (ReadTextFile(DataDir() + L"\\benchmarks.json", s)) { Value b; if (json::Parser(s).Parse(b)) v.Set(L"benchmarks", b); }
        std::wstring out, file = DataDir() + L"\\rigcard-backup.json";
        json::Write(v, out);
        WriteTextFileAtomic(file, out);
        cmd->reply.Set(L"file", Value::Str(file));
    }
}

static void OnDrop(HDROP drop) {
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; ++i) {
        wchar_t path[MAX_PATH * 2];
        DragQueryFileW(drop, i, path, MAX_PATH * 2);
        std::wstring p = Lower(path);
        auto ends = [&](const wchar_t* e) { size_t l = wcslen(e); return p.size() > l && p.compare(p.size() - l, l, e) == 0; };
        if (ends(L".jpg") || ends(L".jpeg") || ends(L".png") || ends(L".webp") || ends(L".heic") || ends(L".bmp") || ends(L".gif") || ends(L".tif") || ends(L".tiff")) {
            SetPhotoFromFile(path);
            if (g_page != Page::Card) SetPage(Page::Card);
        } else if (ends(L".json")) {
            CardStyle s;
            std::wstring err;
            if (LoadStyleFile(path, s, &err)) {
                EnsureDir(StylesDir());
                SaveStyleFile(s, StylesDir() + L"\\" + PathName(path));
                g_savedStyles = LoadSavedStyles();
                g_build.style = s;
                SaveBuildSoon();
                Toast(L"Card style \"" + s.name + L"\" imported.");
            } else {
                Toast(err, true);
            }
        } else if (ends(L".3dmark-result") || ends(L".txt") || ends(L".xml")) {
            bench::ImportFileAsync(g_hwnd, path);
        }
    }
    DragFinish(drop);
}

// ---------------------------------------------------------------- window procedure
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g_hwnd = hwnd;
            g_scale = GetDpiForWindow(hwnd) / 96.0f;
            ApplyAppTheme();
            DragAcceptFiles(hwnd, TRUE);
            // allow drops from Explorer even if Rigcard was started elevated
            ChangeWindowMessageFilterEx(hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
            ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
            ChangeWindowMessageFilterEx(hwnd, 0x0049 /*WM_COPYGLOBALDATA*/, MSGFLT_ALLOW, nullptr);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            Paint();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            if (g_rt) g_rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
            UpdateSensorActivity();
            if (wp == SIZE_MINIMIZED) MaybeAutoApplySelf();
            Invalidate();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = (MINMAXINFO*)lp;
            float s = GetDpiForWindow(hwnd) / 96.0f;
            mm->ptMinTrackSize.x = (LONG)(900 * s);
            mm->ptMinTrackSize.y = (LONG)(600 * s);
            return 0;
        }
        case WM_DPICHANGED: {
            g_scale = HIWORD(wp) / 96.0f;
            RECT* r = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (g_rt) g_rt->SetDpi((float)HIWORD(wp), (float)HIWORD(wp));
            Invalidate();
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (lp && (wcscmp((const wchar_t*)lp, L"ImmersiveColorSet") == 0 || wcscmp((const wchar_t*)lp, L"WindowsThemeElement") == 0)) ApplyAppTheme();
            if (wp == SPI_SETHIGHCONTRAST || wp == SPI_SETCLIENTAREAANIMATION) ApplyAppTheme();
            return 0;
        case WM_DWMCOLORIZATIONCOLORCHANGED:
            ApplyAppTheme();
            return 0;
        case WM_MOUSEMOVE: {
            g_lastInput = GetTickCount();
            float x = GET_X_LPARAM(lp) / g_scale, y = GET_Y_LPARAM(lp) / g_scale;
            if (!g_tracking) {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&t);
                g_tracking = true;
            }
            if (g_sliderDrag.active) { SliderTo(x); return 0; }
            if (g_photoDrag.active) {
                DragCardPage(x - g_photoDrag.x, y - g_photoDrag.y, false, false, x, y);
                g_photoDrag.x = x;
                g_photoDrag.y = y;
                return 0;
            }
            HotRect h = HitAt(x, y);
            if (h.kind != g_hover.kind || h.id != g_hover.id || h.arg != g_hover.arg) {
                g_hover = h;
                Invalidate();
            }
            if (g_keyboardMode && (std::fabs(x - g_lastMouseX) > 3 || std::fabs(y - g_lastMouseY) > 3)) { g_keyboardMode = false; Invalidate(); }
            g_lastMouseX = x;
            g_lastMouseY = y;
            LPCWSTR cur = IDC_ARROW;
            if (h.kind == Hit::Field) cur = IDC_IBEAM;
            else if (h.kind == Hit::PhotoArea) cur = IDC_SIZEALL;
            else if (h.kind != Hit::None && h.enabled && h.kind != Hit::MenuOutside) cur = IDC_HAND;
            SetCursor(LoadCursorW(nullptr, cur));
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) return TRUE;
            break;
        case WM_MOUSELEAVE:
            g_tracking = false;
            g_hover = {{0, 0, 0, 0}, Hit::None, 0, 0, false, false};
            Invalidate();
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            SetFocus(hwnd);
            float x = GET_X_LPARAM(lp) / g_scale, y = GET_Y_LPARAM(lp) / g_scale;
            g_pressed = HitAt(x, y);
            SetCapture(hwnd);
            if (g_pressed.kind == Hit::Slider) {
                BlurField();
                g_sliderDrag = {true, g_pressed.id, g_pressed.arg, D2D1::RectF(g_pressed.r.left + 8, g_pressed.r.top, g_pressed.r.right - 8, g_pressed.r.bottom), 0, 1};
                SliderTo(x);
            } else if (g_pressed.kind == Hit::PhotoArea) {
                BlurField();
                if (msg == WM_LBUTTONDBLCLK) { DragCardPage(0, 0, true, true, x, y); }   // double-click: reset position
                else { g_photoDrag = {true, x, y}; DragCardPage(0, 0, true, false, x, y); }
            } else if (g_pressed.kind == Hit::Field) {
                Click(g_pressed, x);
                g_pressed = {{0, 0, 0, 0}, Hit::None, 0, 0, false, false};
            } else if (g_pressed.kind == Hit::None && g_edit.id >= 0) {
                BlurField();
            }
            Invalidate();
            return 0;
        }
        case WM_LBUTTONUP: {
            ReleaseCapture();
            float x = GET_X_LPARAM(lp) / g_scale, y = GET_Y_LPARAM(lp) / g_scale;
            if (g_sliderDrag.active) { g_sliderDrag.active = false; SaveBuildSoon(); ConfigSave(); g_pressed = {}; Invalidate(); return 0; }
            if (g_photoDrag.active) { g_photoDrag.active = false; DragCardPage(0, 0, false, true, x, y); g_pressed = {}; Invalidate(); return 0; }
            HotRect up = HitAt(x, y);
            HotRect down = g_pressed;
            g_pressed = {{0, 0, 0, 0}, Hit::None, 0, 0, false, false};
            if (up.kind == down.kind && up.id == down.id && up.arg == down.arg && up.kind != Hit::None && up.enabled) Click(up, x);
            Invalidate();
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            Scroll(GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * 96.0f, pt.x / g_scale, pt.y / g_scale);
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (msg == WM_SYSKEYDOWN) break;
            OnKey(wp);
            return 0;
        case WM_CHAR:
            OnChar((wchar_t)wp);
            return 0;
        case WM_DROPFILES:
            OnDrop((HDROP)wp);
            return 0;
        case WM_TIMER:
            switch (wp) {
                case TIMER_ANIM:
                    if (!Animate()) { KillTimer(hwnd, TIMER_ANIM); g_animTimer = false; }
                    Invalidate();
                    break;
                case TIMER_CARET:
                    if (g_edit.id < 0 || GetFocus() != hwnd) { KillTimer(hwnd, TIMER_CARET); g_edit.caretOn = false; }
                    else g_edit.caretOn = !g_edit.caretOn;
                    Invalidate();
                    break;
                case TIMER_SAVE:
                    KillTimer(hwnd, TIMER_SAVE);
                    BuildSave();
                    if (!g_build.gistId.empty()) { g_publishPending = true; SetTimer(hwnd, TIMER_PUBLISH, 5000, nullptr); }
                    break;
                case TIMER_PUBLISH:
                    // updating the build updates the shared page (at most every 20 s)
                    KillTimer(hwnd, TIMER_PUBLISH);
                    if (g_publishPending && !g_build.gistId.empty() && !GetToken().empty()) {
                        if (GetTickCount() - g_lastPublish < 20000 || share::Busy()) SetTimer(hwnd, TIMER_PUBLISH, 8000, nullptr);
                        else PublishShare(false);
                    }
                    break;
                case TIMER_LIVEPUSH:
                    if (g_cfg.liveShare && !g_build.gistId.empty() && g_live.at) share::PushLiveAsync(hwnd, g_build.gistId, g_live);
                    break;
                case TIMER_SELF:
                    SetTimer(hwnd, TIMER_SELF, 60 * 1000, nullptr);
                    if (!g_selfCheckedTick || GetTickCount() - g_selfCheckedTick > (DWORD)g_cfg.checkHours * 3600 * 1000) CheckSelf(false);
                    MaybeAutoApplySelf();
                    break;
                case TIMER_HEALTHY:
                    KillTimer(hwnd, TIMER_HEALTHY);
                    selfupdate::SignalHealthy(g_after);
                    selfupdate::FinishAfterUpdate(g_after);
                    break;
                case TIMER_HANGAR_HEALTH:
                    KillTimer(hwnd, TIMER_HANGAR_HEALTH);
                    hangar::SendHealth();
                    break;
                case TIMER_TOAST:
                    KillTimer(hwnd, TIMER_TOAST);
                    g_toastAt = 0;
                    StartAnim();
                    break;
            }
            return 0;
        case WM_KILLFOCUS:
            g_edit.caretOn = false;
            Invalidate();
            return 0;
        case WM_RC_HARDWARE: { std::unique_ptr<HwResult> r((HwResult*)lp); OnHardware(r.get()); return 0; }
        case WM_RC_SENSORS: {
            std::unique_ptr<SensorSnapshot> s((SensorSnapshot*)lp);
            g_live = *s;
            g_pulse = g_pulse < 0.5f ? 0.5f : 0.0f;
            if (IsWindowVisible(hwnd) && !IsIconic(hwnd) && (g_page == Page::Card || g_page == Page::Settings)) Invalidate();
            return 0;
        }
        case WM_RC_BENCH: { std::unique_ptr<BenchProgress> p((BenchProgress*)lp); OnBench(p.get()); return 0; }
        case WM_RC_SHARE: { std::unique_ptr<ShareResult> r((ShareResult*)lp); OnShare(r.get()); return 0; }
        case WM_RC_IMPORT: { std::unique_ptr<ImportResult> r((ImportResult*)lp); OnImport(r.get()); return 0; }
        case WM_RC_HANGAR: OnHangar((HangarCommand*)lp); return 0;
        case WM_RC_SELF: {
            std::unique_ptr<SelfUpdateState> r((SelfUpdateState*)lp);
            bool wasReady = g_self.kind == SelfUpdateState::Ready;
            if (!(wasReady && (r->kind == SelfUpdateState::Error || r->kind == SelfUpdateState::Checking))) g_self = *r;
            if (r->kind == SelfUpdateState::Ready && !wasReady) {
                if (!g_cfg.autoUpdate) Toast(L"Rigcard " + r->version + L" is ready. Click the update button to see what's new.");
                MaybeAutoApplySelf();
            }
            Invalidate();
            return 0;
        }
        case WM_RC_ACTIVATE:
            if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            return 0;
        case WM_CLOSE:
            if (bench::Running()) {
                if (MessageBoxW(hwnd, L"A benchmark is running. Quit anyway and cancel it?", L"Rigcard", MB_OKCANCEL | MB_ICONWARNING) != IDOK) return 0;
                bench::Cancel();
                for (int i = 0; i < 50 && bench::Running(); ++i) Sleep(100);
            }
            BlurField();
            BuildSave();
            SaveWindowPlacement();
            ConfigSave();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            hangar::Stop();
            sensors::Shutdown();
            DiscardDevice();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND UiCreate(HINSTANCE inst, int show, const selfupdate::AfterUpdate& after, const std::wstring& restoreStateFile) {
    g_after = after;
    if (!gfx::Init()) return nullptr;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszClassName = RIGCARD_WINDOW_CLASS;
    RegisterClassExW(&wc);

    g_customThemes = LoadCustomThemes();
    g_savedStyles = LoadSavedStyles();
    BuildLoad();
    bench::Load();
    LoadPhotoFromBuild();
    std::wstring p = g_cfg.page;
    g_page = p == L"parts" ? Page::Parts : p == L"bench" ? Page::Bench : p == L"settings" ? Page::Settings : Page::Card;
    PruneSkippedVersions(RIGCARD_VERSION);

    UINT dpi = GetDpiForSystem();
    float s = dpi / 96.0f;
    int w = g_cfg.windowW > 0 ? g_cfg.windowW : (int)(1360 * s);
    int h = g_cfg.windowH > 0 ? g_cfg.windowH : (int)(860 * s);
    int x = g_cfg.windowX, y = g_cfg.windowY;
    if (x != CW_USEDEFAULT) {
        RECT r{x, y, x + w, y + h};
        if (!MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) x = y = CW_USEDEFAULT;
    }
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Rigcard", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return nullptr;

    std::wstring restore;
    if (after.active) restore = selfupdate::TakeRestoreState();
    if (!restoreStateFile.empty() && ReadTextFile(restoreStateFile, restore)) DeleteFileW(restoreStateFile.c_str());
    ApplyRestore(restore, &show);
    if (after.active) {
        if (CompareVersions(after.fromVersion, RIGCARD_VERSION) < 0) {
            std::wstring notes;
            LoadResourceText(101, notes);
            g_whatsNew = CleanNotes(notes);
            if (g_whatsNew.empty()) g_whatsNew = L"See the release notes on GitHub.";
            g_whatsNewVersion = RIGCARD_VERSION;
        } else {
            Toast(std::wstring(L"Rolled back to Rigcard ") + RIGCARD_VERSION + L".");
        }
    }
    bool maximize = g_cfg.maximized && (show == SW_SHOWNORMAL || show == SW_SHOWDEFAULT || show == SW_SHOWNOACTIVATE);
    ShowWindow(hwnd, maximize ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(hwnd);
    if (after.active) SetTimer(hwnd, TIMER_HEALTHY, 400, nullptr);

    // hardware scan (fast; the card shows the last scan meanwhile), sensors, Hangar
    hardware::ScanAsync(hwnd);
    hangar::Start(hwnd, RIGCARD_APP_ID, RIGCARD_VERSION);
    SetTimer(hwnd, TIMER_HANGAR_HEALTH, 2000, nullptr);
    if (!restoreStateFile.empty()) hangar::SendRestored(L"reopened on " + g_cfg.page);
    SetTimer(hwnd, TIMER_SELF, 20 * 1000, nullptr);   // first update check ~20 s after start
    return hwnd;
}
