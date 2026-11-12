// Rigcard - Settings page: appearance, sensors, sharing, privacy, updates, about
#include "ui_internal.h"
#include "util.h"
#include "share.h"
#include "hangar_sdk.h"
#include <shellapi.h>
#include <algorithm>

using namespace gfx;

static std::vector<ThemeDef> AllThemes() {
    std::vector<ThemeDef> all = BuiltinThemes();
    for (auto& t : g_customThemes) all.push_back(t);
    return all;
}

static std::wstring HexColor(uint32_t c) {
    wchar_t b[16];
    swprintf(b, 16, L"#%06X", c & 0xFFFFFF);
    return b;
}

static void ThemeTile(const ThemeDef& def0, const D2D1_RECT_F& r, int index, bool selected) {
    ThemeDef def = def0;
    if (def.followWindows) def = BuiltinThemes()[SystemUsesLightTheme() ? 1 : 0];
    float rad = std::min(R() + 2, 14.0f);
    g_c.FillRound(r, rad, Rgb(def.bg));
    D2D1_RECT_F side = D2D1::RectF(r.left, r.top, r.left + (r.right - r.left) * 0.28f, r.bottom);
    g_c.FillRound(side, rad, Rgb(def.sidebar));
    g_c.Fill(D2D1::RectF(side.right - rad, side.top, side.right, side.bottom), Rgb(def.sidebar));
    float cx = side.right + 8;
    D2D1_RECT_F card = D2D1::RectF(cx, r.top + 10, r.right - 10, r.top + 34);
    g_c.FillRound(card, 4, def.surfaceOpacity < 1 ? Mix(Rgb(def.bg), Rgb(def.surface), def.surfaceOpacity * 2) : Rgb(def.surface));
    uint32_t accent = def.accent ? def.accent : SystemAccent();
    g_c.FillRound(D2D1::RectF(cx + 6, card.top + 8, cx + 34, card.bottom - 8), 2, Rgb(accent));
    g_c.Text(def0.name, Format(F(12.5f), DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(cx, r.bottom - 26, r.right - 6, r.bottom - 6), Rgb(def.text));
    g_c.StrokeRound(r, rad, selected ? g_t.accent : (IsHover(W_THEME, index) ? Alpha(g_t.text, 0.5f) : Alpha(g_t.text, 0.14f)), selected ? 2.5f : 1.0f);
    AddHot(r, Hit::Preset, W_THEME, index);
}

static float Group(float x, float& y, float w, const std::wstring& title, const std::wstring& sub, float contentH) {
    float h = contentH;
    Card(D2D1::RectF(x, y, x + w, y + h));
    float yy = y + 18;
    g_c.Text(title, Txt(16, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(x + 22, yy, x + w - 22, yy + 26), g_t.text);
    yy += 28;
    if (!sub.empty()) yy += Label(x + 22, yy, w - 44, sub, 12.5f, g_t.subtext, DWRITE_FONT_WEIGHT_NORMAL, true);
    return yy + 8;
}

static float s_hAppearance = 600, s_hSensors = 300, s_hSharing = 300, s_hPrivacy = 260, s_hUpdates = 300, s_hAbout = 200;

void DrawSettingsPage(float x0, float y0) {
    size_t hotFrom = g_hot.size();
    float w = std::min(900.0f, g_W - x0 - 64);
    float x = x0 + (g_W - x0 - w) / 2;
    float y = y0 + 28 - g_scroll;
    g_c.Text(L"Settings", Txt(24, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(x, y, x + w, y + 36), g_t.text);
    y += 52;
    float px = x + 22, pw = w - 44;

    // ---------------- appearance
    float top = y;
    float py = Group(x, y, w, L"Appearance", L"How Rigcard itself looks. Card styles are separate (Card → Card style).", s_hAppearance);
    auto themes = AllThemes();
    int cols = std::max(3, (int)(pw / 150));
    float tw = (pw - (cols - 1) * 10) / cols, th = D(78);
    for (size_t i = 0; i < themes.size(); ++i) {
        float tx = px + (i % cols) * (tw + 10), ty = py + (i / cols) * (th + 10);
        ThemeTile(themes[i], D2D1::RectF(tx, ty, tx + tw, ty + th), (int)i, g_cfg.theme == themes[i].id);
    }
    py += ((themes.size() + cols - 1) / cols) * (th + 10) + 8;
    g_c.Text(L"Accent color", Txt(13), D2D1::RectF(px, py, px + 160, py + D(32)), g_t.text);
    float ax = px + 170;
    float segW = Segmented(ax, py, {L"Theme", L"Windows"}, g_cfg.accent == L"theme" ? 0 : g_cfg.accent == L"windows" ? 1 : -1, W_ACCENT);
    (void)segW;
    float sx = ax + 190;
    for (int i = 0; i < kAccentSwatchCount; ++i) {
        D2D1_RECT_F r = D2D1::RectF(sx + i * 30, py + 3, sx + i * 30 + 24, py + 27);
        ColorSwatch(r, kAccentSwatches[i], W_ACCENT, 10 + i, IEquals(g_cfg.accent, HexColor(kAccentSwatches[i])));
    }
    Button(D2D1::RectF(sx + kAccentSwatchCount * 30 + 6, py, sx + kAccentSwatchCount * 30 + 96, py + D(30)), L"Custom…", 0, Btn::Subtle, W_ACCENT_CUSTOM);
    py += D(44);
    g_c.Text(L"Window background", Txt(13), D2D1::RectF(px, py, px + 160, py + D(30)), g_t.text);
    int bd = g_cfg.backdrop == L"none" ? 1 : g_cfg.backdrop == L"mica" ? 2 : g_cfg.backdrop == L"acrylic" ? 3 : 0;
    Segmented(px + 170, py, {L"Theme default", L"Solid", L"Mica", L"Acrylic"}, bd, W_BACKDROP);
    if (!BackdropSupported()) g_c.Text(L"Mica and acrylic need Windows 11", Txt(11.5f), D2D1::RectF(px + 170, py + D(32), px + pw, py + D(50)), g_t.faint);
    py += D(54);
    g_c.Text(L"Text size", Txt(13), D2D1::RectF(px, py, px + 160, py + D(30)), g_t.text);
    const float scales[] = {0.9f, 1.0f, 1.1f, 1.25f};
    int fs = 1;
    for (int i = 0; i < 4; ++i) if (std::fabs(g_cfg.fontScale - scales[i]) < 0.02f) fs = i;
    Segmented(px + 170, py, {L"Small", L"Default", L"Large", L"Larger"}, fs, W_FONTSCALE);
    py += D(42);
    g_c.Text(L"Density", Txt(13), D2D1::RectF(px, py, px + 160, py + D(30)), g_t.text);
    Segmented(px + 170, py, {L"Comfortable", L"Compact"}, g_cfg.compact ? 1 : 0, W_DENSITY);
    py += D(42);
    g_c.Text(L"Corner radius", Txt(13), D2D1::RectF(px, py, px + 160, py + D(30)), g_t.text);
    Slider(D2D1::RectF(px + 178, py + 4, px + 178 + 220, py + 26), g_cfg.radius, 0, 16, W_UIRADIUS);
    SliderBind(W_UIRADIUS, 0, [](float t) { g_cfg.radius = std::round(t * 16); ApplyAppTheme(); });
    py += D(42);
    g_c.Text(L"Animations", Txt(13), D2D1::RectF(px, py, px + 160, py + D(30)), g_t.text);
    const float speeds[] = {0.0f, 0.5f, 1.0f, 2.0f};
    int an = 2;
    for (int i = 0; i < 4; ++i) if (std::fabs(g_cfg.animSpeed - speeds[i]) < 0.01f) an = i;
    Segmented(px + 170, py, {L"Off", L"Slow", L"Normal", L"Fast"}, an, W_ANIM);
    py += D(46);
    py += ToggleRow(px, py, pw, L"Follow theme changes from Hangar", hangar::HangarInstalled() ? L"Hangar can push a theme to every app in the suite, live." : L"Hangar isn't installed; this takes effect when it is.", g_cfg.followHangar, W_FOLLOW_HANGAR);
    py += 6;
    Button(D2D1::RectF(px, py, px + 170, py + D(32)), L"Save theme as file", 0xE74E, Btn::Secondary, W_THEME_SAVE);
    Button(D2D1::RectF(px + 180, py, px + 350, py + D(32)), L"Open themes folder", icon::Folder, Btn::Subtle, W_THEME_FOLDER);
    py += D(46);
    s_hAppearance = py - top;
    y = py + 18;

    // ---------------- sensors
    top = y;
    SensorStatus ss = sensors::Status();
    py = Group(x, y, w, L"Sensors", L"Live stats are read only while the window is open (or while live sharing is on).", s_hSensors);
    auto line = [&](const std::wstring& k, const std::wstring& v, D2D1_COLOR_F c) {
        g_c.Text(k, Txt(13), D2D1::RectF(px, py, px + 170, py + D(28)), g_t.subtext);
        g_c.Text(v, Txt(13), D2D1::RectF(px + 170, py, px + pw, py + D(28)), c);
        py += D(30);
    };
    std::wstring cpu = !std::isnan(g_live.cpuTemp) ? L"Working · " + g_live.cpuSource : (ss.cpuNote.empty() ? L"Waiting for the first reading…" : ss.cpuNote);
    line(L"CPU temperature", cpu, !std::isnan(g_live.cpuTemp) ? g_t.good : g_t.warn);
    std::wstring gpu = !std::isnan(g_live.gpuTemp) ? L"Working · " + (g_live.gpuSource.empty() ? L"Windows" : g_live.gpuSource) : L"Not available for this GPU";
    line(L"GPU temperature", gpu, !std::isnan(g_live.gpuTemp) ? g_t.good : g_t.faint);
    std::wstring pw2 = (!std::isnan(g_live.cpuPower) ? std::wstring(L"CPU ") : L"") + (!std::isnan(g_live.gpuPower) ? std::wstring(L"GPU") : L"");
    line(L"Power draw", pw2.empty() ? L"Not available yet" : L"Reading " + pw2, pw2.empty() ? g_t.faint : g_t.good);
    py += 6;
    if (std::isnan(g_live.cpuTemp) && !ss.elevated) {
        std::wstring label = ss.helperTask ? L"Repair CPU temperature setup" : (ss.pawnioInstalled ? L"Turn on CPU temperature" : L"Install driver and turn on CPU temperature");
        float bw = ButtonWidth(label, 0xE7EF);
        Button(D2D1::RectF(px, py, px + bw, py + D(34)), label, 0xE7EF, Btn::Primary, W_SENSOR_SETUP);
        Button(D2D1::RectF(px + bw + 10, py, px + bw + 160, py + D(34)), L"About PawnIO", 0xE8A7, Btn::Subtle, W_PAWNIO_PAGE);
        py += D(42);
        py += Label(px, py, pw, L"Windows only lets signed drivers read CPU temperature. Rigcard uses PawnIO, the same driver as Ember and LibreHardwareMonitor. You approve one admin prompt; after that a small helper reads the sensor while Rigcard is open.",
                    12, g_t.subtext, DWRITE_FONT_WEIGHT_NORMAL, true);
        py += 10;
    }
    g_c.Text(L"Update every", Txt(13), D2D1::RectF(px, py, px + 160, py + D(30)), g_t.text);
    const int polls[] = {500, 1000, 2000, 5000};
    int pi = 1;
    for (int i = 0; i < 4; ++i) if (g_cfg.pollMs == polls[i]) pi = i;
    Segmented(px + 170, py, {L"0.5 s", L"1 s", L"2 s", L"5 s"}, pi, W_SENSOR_POLL);
    py += D(48);
    s_hSensors = py - top;
    y = py + 18;

    // ---------------- sharing
    top = y;
    bool connected = !GetToken().empty();
    py = Group(x, y, w, L"Sharing", L"Share links open a page on " + std::wstring(RIGCARD_SITE).substr(8) +
                                     L". Quick links need nothing. For a link that updates (and shows your photo), connect GitHub: the card is stored in a secret gist on your account.", s_hSharing);
    if (connected) {
        g_c.Glyph(icon::Check, Icon(14), D2D1::RectF(px, py, px + 22, py + D(32)), g_t.good);
        g_c.Text(L"Connected to GitHub" + (g_cfg.githubUser.empty() ? L"" : L" as " + g_cfg.githubUser), Txt(13.5f), D2D1::RectF(px + 26, py, px + pw - 140, py + D(32)), g_t.text);
        Button(D2D1::RectF(px + pw - 130, py, px + pw, py + D(32)), L"Disconnect", 0, Btn::Secondary, W_GH_DISCONNECT);
        py += D(46);
        py += ToggleRow(px, py, pw, L"Live stats on the shared page", L"While Rigcard runs, temperatures and power are pushed to your page", g_cfg.liveShare, W_LIVE_SHARE, 0, !g_build.gistId.empty());
        g_c.Text(L"Push every", Txt(13), D2D1::RectF(px, py, px + 160, py + D(30)), g_t.text);
        const int ivs[] = {30, 60, 120, 300};
        int ii = 1;
        for (int i = 0; i < 4; ++i) if (g_cfg.liveSeconds == ivs[i]) ii = i;
        Segmented(px + 170, py, {L"30 s", L"1 min", L"2 min", L"5 min"}, ii, W_LIVE_INTERVAL);
        py += D(46);
    } else {
        g_c.Text(L"1.", Txt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(px, py, px + 20, py + D(34)), g_t.subtext);
        Button(D2D1::RectF(px + 24, py, px + 24 + ButtonWidth(L"Create a token on GitHub", 0xE8A7), py + D(34)), L"Create a token on GitHub", 0xE8A7, Btn::Secondary, W_GH_CREATE_TOKEN);
        g_c.Text(L"Only the \"gist\" permission is needed (it's pre-selected).", Txt(12), D2D1::RectF(px + 270, py, px + pw, py + D(34)), g_t.faint);
        py += D(44);
        g_c.Text(L"2.", Txt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(px, py, px + 20, py + D(34)), g_t.subtext);
        Field(D2D1::RectF(px + 24, py, px + pw - 120, py + D(34)), W_GH_TOKEN, 0, &g_tokenEdit, L"Paste the token here", L"", true);
        Button(D2D1::RectF(px + pw - 110, py, px + pw, py + D(34)), L"Connect", 0, Btn::Primary, W_GH_CONNECT, 0, !Trim(g_tokenEdit).empty() && !share::Busy());
        py += D(46);
        py += Label(px, py, pw, L"The token is encrypted with Windows (DPAPI) so only your Windows account can read it, and it's only ever sent to GitHub.", 12, g_t.faint, DWRITE_FONT_WEIGHT_NORMAL, true);
        py += 12;
    }
    s_hSharing = py - top;
    y = py + 18;

    // ---------------- privacy
    top = y;
    py = Group(x, y, w, L"Privacy", L"What may appear on your card, exported images and share pages. Off by default.", s_hPrivacy);
    py += ToggleRow(px, py, pw, L"Show serial numbers", L"Motherboard, memory and drive serials", g_cfg.showSerials, W_PRIV_SERIALS);
    py += ToggleRow(px, py, pw, L"Show IP address", g_build.ip.empty() ? L"" : L"Your local IP: " + g_build.ip, g_cfg.showIp, W_PRIV_IP);
    py += ToggleRow(px, py, pw, L"Show Windows user name", g_build.user.empty() ? L"" : L"Shown as " + g_build.user + L"@" + g_build.computerName, g_cfg.showUser, W_PRIV_USER);
    py += ToggleRow(px, py, pw, L"Hide location", L"Keeps the location you typed off the card", g_cfg.hideLocation, W_PRIV_LOCATION);
    py += 12;
    s_hPrivacy = py - top;
    y = py + 18;

    // ---------------- updates
    top = y;
    py = Group(x, y, w, L"Updates", std::wstring(L"You have Rigcard ") + RIGCARD_VERSION + (IsPortable() ? L" (portable)" : L"") + L". Downloads are checked against their SHA-256 hash; if a new version doesn't start, Rigcard puts the old one back.", s_hUpdates);
    g_c.Text(L"Channel", Txt(13), D2D1::RectF(px, py, px + 160, py + D(30)), g_t.text);
    Segmented(px + 170, py, {L"Stable", L"Beta"}, g_cfg.beta ? 1 : 0, W_UPD_CHANNEL);
    py += D(42);
    g_c.Text(L"Check every", Txt(13), D2D1::RectF(px, py, px + 160, py + D(30)), g_t.text);
    const int hours[] = {1, 3, 6, 12, 24};
    int hi = 1;
    for (int i = 0; i < 5; ++i) if (g_cfg.checkHours == hours[i]) hi = i;
    Segmented(px + 170, py, {L"1 h", L"3 h", L"6 h", L"12 h", L"24 h"}, hi, W_UPD_HOURS);
    py += D(42);
    py += ToggleRow(px, py, pw, L"Install updates automatically", L"Quietly, when Rigcard is minimized or you're away, never during a benchmark", g_cfg.autoUpdate, W_UPD_AUTO);
    py += 6;
    std::wstring st;
    switch (g_self.kind) {
        case SelfUpdateState::Checking: st = L"Checking…"; break;
        case SelfUpdateState::UpToDate: st = L"You're up to date."; break;
        case SelfUpdateState::Downloading: st = L"Downloading " + g_self.version + (g_self.percent >= 0 ? L" · " + std::to_wstring(g_self.percent) + L"%" : L""); break;
        case SelfUpdateState::Ready: st = L"Rigcard " + g_self.version + L" is downloaded and verified."; break;
        case SelfUpdateState::Error: st = g_self.message; break;
        default: break;
    }
    if (g_self.kind == SelfUpdateState::Ready) {
        float bw = ButtonWidth(L"Restart to update", icon::Update);
        Button(D2D1::RectF(px, py, px + bw, py + D(34)), L"Restart to update", icon::Update, Btn::Primary, W_UPD_RESTART, 0, !g_benching);
        Button(D2D1::RectF(px + bw + 10, py, px + bw + 130, py + D(34)), L"What's new", 0, Btn::Subtle, W_UPD_NOTES, 1);
    } else {
        Button(D2D1::RectF(px, py, px + 150, py + D(34)), L"Check now", icon::Refresh, Btn::Secondary, W_UPD_CHECK, 0, g_self.kind != SelfUpdateState::Checking && g_self.kind != SelfUpdateState::Downloading);
    }
    g_c.Text(st, Txt(12.5f), D2D1::RectF(px + 290, py, px + pw, py + D(34)), g_self.kind == SelfUpdateState::Error ? g_t.warn : g_t.subtext);
    py += D(46);
    auto prev = selfupdate::PreviousVersions();
    if (!prev.empty()) {
        g_c.Text(L"Roll back", Txt(13), D2D1::RectF(px, py, px + 160, py + D(32)), g_t.text);
        float bx = px + 170;
        for (size_t i = 0; i < prev.size() && i < 2; ++i) {
            std::wstring l = L"Rigcard " + prev[i].version;
            float bw = ButtonWidth(l, 0xE7A7);
            Button(D2D1::RectF(bx, py, bx + bw, py + D(32)), l, 0xE7A7, Btn::Secondary, W_UPD_ROLLBACK, (int)i, !g_benching);
            bx += bw + 8;
        }
        py += D(44);
    }
    s_hUpdates = py - top;
    y = py + 18;

    // ---------------- about
    top = y;
    py = Group(x, y, w, L"About", L"Rigcard is part of the larosa023 suite. Your data stays in " + DataDir() + L".", s_hAbout);
    float bx = px;
    struct { const wchar_t* label; wchar_t glyph; int id; } links[] = {
        {L"Open data folder", icon::Folder, W_OPEN_DATA}, {L"Open log", 0xE8A5, W_OPEN_LOG}, {L"GitHub", icon::Globe, W_OPEN_REPO}, {L"Share site", icon::Link, W_OPEN_SITE}};
    for (auto& l : links) {
        float bw = ButtonWidth(l.label, l.glyph);
        Button(D2D1::RectF(bx, py, bx + bw, py + D(32)), l.label, l.glyph, Btn::Secondary, l.id);
        bx += bw + 8;
    }
    py += D(44);
    py += Label(px, py, pw, L"Keyboard: Ctrl+1–4 switch pages · Tab moves between controls · Enter or Space activates · Ctrl+C copies the share link · Ctrl+S exports a PNG · F5 rescans hardware.",
                12, g_t.faint, DWRITE_FONT_WEIGHT_NORMAL, true);
    py += 14;
    s_hAbout = py - top;
    y = py + 40;
    g_contentH = y + g_scroll - y0;
    g_viewH = g_H - y0;
    ClipHot(hotFrom, D2D1::RectF(0, y0, g_W, g_H));
}

bool ClickSettingsPage(const HotRect& h) {
    switch (h.id) {
        case W_THEME: {
            auto all = AllThemes();
            if (h.arg >= 0 && h.arg < (int)all.size()) { g_cfg.theme = all[h.arg].id; ApplyAppTheme(); ConfigSave(); }
            return true;
        }
        case W_ACCENT:
            if (h.arg == 0) g_cfg.accent = L"theme";
            else if (h.arg == 1) g_cfg.accent = L"windows";
            else if (h.arg >= 10 && h.arg < 10 + kAccentSwatchCount) g_cfg.accent = HexColor(kAccentSwatches[h.arg - 10]);
            ApplyAppTheme();
            ConfigSave();
            return true;
        case W_ACCENT_CUSTOM: {
            uint32_t c = (uint32_t)((g_t.accent.r * 255)) << 16 | (uint32_t)(g_t.accent.g * 255) << 8 | (uint32_t)(g_t.accent.b * 255);
            if (PickColor(c)) { g_cfg.accent = HexColor(c); ApplyAppTheme(); ConfigSave(); }
            return true;
        }
        case W_BACKDROP: {
            const wchar_t* v[] = {L"theme", L"none", L"mica", L"acrylic"};
            g_cfg.backdrop = v[std::clamp(h.arg, 0, 3)];
            ApplyAppTheme();
            ConfigSave();
            return true;
        }
        case W_FONTSCALE: { const float s[] = {0.9f, 1.0f, 1.1f, 1.25f}; g_cfg.fontScale = s[std::clamp(h.arg, 0, 3)]; ConfigSave(); return true; }
        case W_DENSITY: g_cfg.compact = h.arg == 1; ConfigSave(); return true;
        case W_ANIM: { const float s[] = {0.0f, 0.5f, 1.0f, 2.0f}; g_cfg.animSpeed = s[std::clamp(h.arg, 0, 3)]; ConfigSave(); return true; }
        case W_FOLLOW_HANGAR: g_cfg.followHangar = !g_cfg.followHangar; ConfigSave(); return true;
        case W_THEME_SAVE: {
            ThemeDef def = FindTheme(g_cfg.theme, g_customThemes);
            std::wstring text = ThemeToJson(def, g_cfg.accent);
            EnsureDir(ThemesDir());
            std::wstring base = ThemesDir() + L"\\" + bench::Slug(def.name + L" copy");
            std::wstring path = base + L".json";
            for (int n = 2; FileExists(path); ++n) path = base + L"-" + std::to_wstring(n) + L".json";
            if (WriteTextFileAtomic(path, text)) {
                g_customThemes = LoadCustomThemes();
                Toast(L"Saved " + PathName(path) + L". Edit it in any text editor; the format is shared with Hangar and the rest of the suite.");
            }
            return true;
        }
        case W_THEME_FOLDER: EnsureDir(ThemesDir()); ShellExecuteW(g_hwnd, L"open", ThemesDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); g_customThemes = LoadCustomThemes(); return true;
        case W_SENSOR_SETUP: {
            std::wstring msg;
            bool ok = sensors::SetupHelper(g_hwnd, &msg);
            if (ok) { g_cfg.sensorHelper = true; ConfigSave(); }
            sensors::RefreshStatus();
            Toast(msg, !ok);
            return true;
        }
        case W_PAWNIO_PAGE: OpenUrl(L"https://github.com/namazso/PawnIO.Setup"); return true;
        case W_SENSOR_POLL: { const int p[] = {500, 1000, 2000, 5000}; g_cfg.pollMs = p[std::clamp(h.arg, 0, 3)]; ConfigSave(); return true; }
        case W_GH_CREATE_TOKEN: OpenUrl(L"https://github.com/settings/tokens/new?description=Rigcard%20share%20page&scopes=gist"); return true;
        case W_GH_CONNECT: {
            std::wstring t = Trim(g_tokenEdit);
            if (t.empty()) return true;
            g_tokenEdit = t;
            share::CheckTokenAsync(g_hwnd, t);
            Toast(L"Checking the token with GitHub…");
            return true;
        }
        case W_GH_DISCONNECT:
            if (MessageBoxW(g_hwnd, L"Disconnect GitHub? Your share page stays online, but Rigcard can't update it until you connect again.", L"Rigcard", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
                SetToken(L"");
                g_cfg.liveShare = false;
                ConfigSave();
            }
            return true;
        case W_LIVE_SHARE: g_cfg.liveShare = !g_cfg.liveShare; ConfigSave(); SaveBuildSoon(); return true;
        case W_LIVE_INTERVAL: { const int v[] = {30, 60, 120, 300}; g_cfg.liveSeconds = v[std::clamp(h.arg, 0, 3)]; ConfigSave(); SaveBuildSoon(); return true; }
        case W_PRIV_SERIALS: g_cfg.showSerials = !g_cfg.showSerials; ConfigSave(); SaveBuildSoon(); return true;
        case W_PRIV_IP: g_cfg.showIp = !g_cfg.showIp; ConfigSave(); SaveBuildSoon(); return true;
        case W_PRIV_USER: g_cfg.showUser = !g_cfg.showUser; ConfigSave(); SaveBuildSoon(); return true;
        case W_PRIV_LOCATION: g_cfg.hideLocation = !g_cfg.hideLocation; ConfigSave(); SaveBuildSoon(); return true;
        case W_UPD_CHANNEL: g_cfg.beta = h.arg == 1; ConfigSave(); UiCheckUpdates(); return true;
        case W_UPD_HOURS: { const int v[] = {1, 3, 6, 12, 24}; g_cfg.checkHours = v[std::clamp(h.arg, 0, 4)]; ConfigSave(); return true; }
        case W_UPD_AUTO: g_cfg.autoUpdate = !g_cfg.autoUpdate; ConfigSave(); return true;
        case W_UPD_CHECK: UiCheckUpdates(); return true;
        case W_UPD_NOTES: UiShowNotes(); return true;
        case W_UPD_ROLLBACK: {
            auto prev = selfupdate::PreviousVersions();
            if (h.arg < 0 || h.arg >= (int)prev.size()) return true;
            std::wstring m = L"Go back to Rigcard " + prev[h.arg].version + L"? Rigcard restarts, and won't offer " + RIGCARD_VERSION + L" again until a newer version comes out.";
            if (MessageBoxW(g_hwnd, m.c_str(), L"Rigcard", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return true;
            SkipVersion(RIGCARD_VERSION);
            ConfigSave();
            g_self.kind = SelfUpdateState::Ready;
            g_self.version = prev[h.arg].version;
            g_self.staged = prev[h.arg].exe;
            UiApplyUpdate();
            return true;
        }
        case W_OPEN_DATA: ShellExecuteW(g_hwnd, L"open", DataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); return true;
        case W_OPEN_LOG: ShellExecuteW(g_hwnd, L"open", LogPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL); return true;
        case W_OPEN_REPO: OpenUrl(L"https://github.com/larosa023/Rigcard"); return true;
        case W_OPEN_SITE: OpenUrl(RIGCARD_SITE); return true;
    }
    return false;
}

void SettingsFieldChanged(int id, int, bool commit) {
    if (id == W_GH_TOKEN && commit && !Trim(g_tokenEdit).empty() && GetToken().empty() && !share::Busy()) {
        // pressing Enter in the token box connects
    }
    Invalidate();
}
