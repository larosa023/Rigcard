// Rigcard - UI internals shared by the window (ui.cpp) and the pages (page_*.cpp).
// The UI is immediate-mode Direct2D: every paint rebuilds a list of hot rectangles that input is
// matched against, the same way Hangar's UI works.
#pragma once
#include "common.h"
#include "gfx.h"
#include "themes.h"
#include "config.h"
#include "card.h"
#include "sensors.h"
#include "bench.h"
#include "image.h"
#include "cardrender.h"
#include "selfupdate.h"
#include <functional>
#include <map>

enum class Page { Card, Parts, Bench, Settings };

// What a hot rectangle does. Pages own ranges; arg/index carry the details.
enum class Hit : int {
    None = 0,
    Nav, UpdateBadge, ToastAction, WhatsNewClose, WhatsNewRestart,
    Field, Suggest, MenuItem, MenuOutside, Slider, Toggle, Segment, Button, Swatch, Preset, PhotoArea, Scroll,
};

struct HotRect {
    D2D1_RECT_F r;
    Hit kind;
    int id;          // widget id (page-specific enums)
    int arg;
    bool enabled;
    bool focusable;
};

// ids for widgets: one flat namespace so keyboard focus and hover can match them
enum Wid : int {
    W_NONE = 0,
    // top bar
    W_NAV = 10, W_UPDATE_BADGE,
    // card page
    W_BUILD_NAME = 100, W_BUILD_LOCATION, W_PRESET, W_SAVED_STYLE, W_STYLE_SAVE, W_STYLE_IMPORT, W_STYLE_EXPORT, W_STYLE_DELETE,
    W_COLOR, W_BGMODE, W_FONT, W_LAYOUT, W_RADIUS, W_PANELOP, W_GLOW, W_BORDER, W_ANGLE,
    W_SECTION, W_LIVEMODE, W_PHOTO_CHOOSE, W_PHOTO_REMOVE, W_PHOTO_ZOOM, W_PHOTO_RESET, W_PHOTO_BLURBG,
    W_SHARE_COPY, W_SHARE_QUICK, W_SHARE_OPEN, W_SHARE_PUBLISH, W_EXPORT_PNG, W_COPY_IMAGE, W_RUN_BENCH, W_ASPECT, W_PHOTO_AREA,
    W_SHARE_LIVE, W_SHARE_CONNECT, W_SHARE_UNPUBLISH,
    // parts page
    W_RESCAN = 300, W_PART_NAME, W_PART_DETAIL, W_PART_HIDE, W_PART_RESET, W_PART_DELETE, W_PART_ADD, W_SLOT, W_CHANGES_CLOSE,
    // bench page
    W_BENCH_RUN = 400, W_BENCH_CANCEL, W_BENCH_TEST, W_BENCH_DRIVE, W_BENCH_PIN, W_BENCH_DEL, W_BENCH_IMPORT_FILE, W_BENCH_PASTE,
    W_BENCH_GB_URL, W_BENCH_GB_IMPORT, W_BENCH_M_NAME, W_BENCH_M_SCORE, W_BENCH_M_UNIT, W_BENCH_M_ADD, W_BENCH_RUN_ONE, W_BENCH_SHOW_HISTORY,
    // settings
    W_THEME = 500, W_ACCENT, W_ACCENT_CUSTOM, W_BACKDROP, W_FONTSCALE, W_DENSITY, W_UIRADIUS, W_ANIM, W_FOLLOW_HANGAR, W_THEME_SAVE, W_THEME_FOLDER,
    W_SENSOR_SETUP, W_SENSOR_POLL, W_PAWNIO_PAGE,
    W_GH_CREATE_TOKEN, W_GH_TOKEN, W_GH_CONNECT, W_GH_DISCONNECT, W_LIVE_SHARE, W_LIVE_INTERVAL,
    W_PRIV_SERIALS, W_PRIV_IP, W_PRIV_USER, W_PRIV_LOCATION,
    W_UPD_CHANNEL, W_UPD_AUTO, W_UPD_HOURS, W_UPD_CHECK, W_UPD_RESTART, W_UPD_ROLLBACK, W_UPD_NOTES,
    W_OPEN_DATA, W_OPEN_LOG, W_OPEN_REPO, W_OPEN_SITE,
};

// ---------------------------------------------------------------- shared state (ui.cpp)
extern HWND g_hwnd;
extern ID2D1HwndRenderTarget* g_rt;
extern gfx::Canvas g_c;
extern gfx::Theme g_t;
extern float g_W, g_H, g_scale;
extern Page g_page;
extern std::vector<HotRect> g_hot;
extern SensorSnapshot g_live;
extern bool g_liveRunning;
extern Image g_photo, g_photoBlur;
extern int g_photoVersion;
extern SelfUpdateState g_self;
extern std::wstring g_hwChanges;          // last rescan's changes ("" when none)
extern BenchProgress g_benchProgress;
extern bool g_benching;
extern std::wstring g_shareStatus;
extern std::vector<CardStyle> g_savedStyles;
extern std::vector<ThemeDef> g_customThemes;
extern std::wstring g_tokenEdit;
extern std::wstring g_aspect;              // wide | square | portrait | phone
extern float g_pulse;
extern DWORD g_lastLivePush;

// ---------------------------------------------------------------- helpers (ui.cpp)
void Invalidate();
void StartAnim();
void Toast(const std::wstring& text, bool bad = false);
void OpenUrl(const std::wstring& url);
void SetPage(Page p);
void SaveBuildSoon();                      // debounced build.json save + share update
void ApplyAppTheme();
bool PickColor(uint32_t& rgb);
std::wstring PickOpenFile(const wchar_t* filter, const wchar_t* title);
std::wstring PickSaveFile(const wchar_t* filter, const wchar_t* defName, const wchar_t* defExt);
void CopyText(const std::wstring& text);
std::wstring ClipboardText();
CardInput MakeCardInput();
std::vector<BenchResult> CardBench();      // latest per test minus hidden
void SetPhotoFromFile(const std::wstring& path);
void RemovePhoto();
void StartBenchmarks(const std::vector<std::wstring>& tests);
void PublishShare(bool userAsked);
std::wstring ShareLink();                  // hosted if published, else a quick link
void Rescan();

// layout units
float F(float size);                       // font size with the font-size setting
float D(float px);                         // spacing with the density setting
IDWriteTextFormat* Txt(float size, DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_LEADING, bool wrap = false);
IDWriteTextFormat* Icon(float size, DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_CENTER);
float R();                                 // corner radius setting

// ---------------------------------------------------------------- widgets (ui.cpp)
void AddHot(const D2D1_RECT_F& r, Hit kind, int id, int arg = 0, bool enabled = true, bool focusable = true);
void ClipHot(size_t from, const D2D1_RECT_F& clip);   // hot rects added since `from` only count inside clip
bool IsHover(int id, int arg = 0);
bool IsPressed(int id, int arg = 0);
enum class Btn { Primary, Secondary, Subtle, Danger };
float ButtonWidth(const std::wstring& label, wchar_t glyph);
void Button(const D2D1_RECT_F& r, const std::wstring& label, wchar_t glyph, Btn style, int id, int arg = 0, bool enabled = true);
void Toggle(float x, float y, bool on, int id, int arg = 0, bool enabled = true);     // 40×22 switch
float ToggleRow(float x, float y, float w, const std::wstring& label, const std::wstring& sub, bool on, int id, int arg = 0, bool enabled = true);
float Segmented(float x, float y, const std::vector<std::wstring>& opts, int sel, int id, float maxW = 0);
void Slider(const D2D1_RECT_F& r, float value, float lo, float hi, int id, int arg = 0);
void ColorSwatch(const D2D1_RECT_F& r, uint32_t rgb, int id, int arg, bool selected);
// single-line text field; picker: kind for suggestions ("" for none)
void Field(const D2D1_RECT_F& r, int id, int arg, std::wstring* value, const std::wstring& placeholder, const std::wstring& pickerKind = L"", bool password = false);
float SectionHeader(float x, float y, float w, const std::wstring& title, const std::wstring& sub = L"");
void Card(const D2D1_RECT_F& r);                   // a surface panel
void ProgressBar(const D2D1_RECT_F& r, float pct, D2D1_COLOR_F color);
float Label(float x, float y, float w, const std::wstring& text, float size, D2D1_COLOR_F color, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL, bool wrap = false);
// popup menu anchored under a rect; onPick(index)
void OpenMenu(const D2D1_RECT_F& anchor, const std::vector<std::wstring>& items, int selected, std::function<void(int)> onPick);
bool FieldFocused(int id, int arg = -1);
void BlurField();

// slider callback: pages set values here
using SliderFn = std::function<void(float)>;
void SliderBind(int id, int arg, SliderFn fn);

// ---------------------------------------------------------------- pages
void DrawCardPage(float x0, float y0);
bool ClickCardPage(const HotRect& h);
bool WheelCardPage(float x, float y, float delta);
void DragCardPage(float dx, float dy, bool start, bool end, float x, float y);

void DrawPartsPage(float x0, float y0);
bool ClickPartsPage(const HotRect& h);
void PartsFieldChanged(int id, int arg, bool commit);

void DrawBenchPage(float x0, float y0);
bool ClickBenchPage(const HotRect& h);

void DrawSettingsPage(float x0, float y0);
bool ClickSettingsPage(const HotRect& h);
void SettingsFieldChanged(int id, int arg, bool commit);

void CardFieldChanged(int id, int arg, bool commit);
void UiCheckUpdates();
void UiApplyUpdate();
void UiShowNotes();
extern const float kTopBar;

// page scroll (Parts, Benchmarks, Settings, and the card page's right panel)
extern float g_scroll, g_scrollTarget, g_contentH, g_viewH;
extern float g_panelScroll, g_panelScrollTarget, g_panelContentH, g_panelViewH;
