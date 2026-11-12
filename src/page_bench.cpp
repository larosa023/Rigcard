// Rigcard - Benchmarks page: run built-in tests, import scores, see history
#include "ui_internal.h"
#include "util.h"
#include <algorithm>
#include <set>
#include <ctime>

using namespace gfx;

static std::set<std::wstring> s_selected = {L"cpu-single", L"cpu-multi", L"gpu", L"mem-bw", L"mem-lat", L"disk-read", L"disk-write", L"disk-4k"};
static std::wstring s_gbUrl, s_mName, s_mScore, s_mUnit;
static bool s_allHistory = false;
static D2D1_RECT_F s_driveAnchor{};
static std::vector<BenchResult> s_latest;

static std::vector<std::wstring> FixedDrives() {
    std::vector<std::wstring> out;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        wchar_t root[4] = {(wchar_t)(L'A' + i), L':', L'\\', 0};
        if (GetDriveTypeW(root) == DRIVE_FIXED) out.push_back(std::wstring(root, 2));
    }
    return out;
}

static std::wstring DriveLabel(const std::wstring& d) {
    wchar_t name[MAX_PATH] = {0};
    std::wstring root = d + L"\\";
    GetVolumeInformationW(root.c_str(), name, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
    ULARGE_INTEGER freeBytes{}, total{};
    GetDiskFreeSpaceExW(root.c_str(), &freeBytes, &total, nullptr);
    std::wstring s = d;
    if (name[0]) s += L" " + std::wstring(name);
    if (total.QuadPart) s += L" · " + FormatBytes(freeBytes.QuadPart) + L" free";
    return s;
}

static std::wstring DateOf(long long t) {
    if (!t) return L"";
    time_t tt = (time_t)t;
    tm lt{};
    localtime_s(&lt, &tt);
    wchar_t b[64];
    wcsftime(b, 64, L"%b %d, %Y %H:%M", &lt);
    return b;
}

static void Sparkline(const D2D1_RECT_F& r, const std::vector<BenchResult>& series) {
    if (series.size() < 2) return;
    double lo = series[0].score, hi = series[0].score;
    for (auto& s : series) { lo = std::min(lo, s.score); hi = std::max(hi, s.score); }
    if (hi - lo < 1e-9) { lo -= 1; hi += 1; }
    size_t n = std::min<size_t>(series.size(), 16), start = series.size() - n;
    ID2D1PathGeometry* g = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    d2d->CreatePathGeometry(&g);
    if (!g || FAILED(g->Open(&sink))) { SafeRelease(g); return; }
    for (size_t i = 0; i < n; ++i) {
        float x = r.left + (r.right - r.left) * i / (n - 1);
        float y = r.bottom - (float)((series[start + i].score - lo) / (hi - lo)) * (r.bottom - r.top);
        if (i == 0) sink->BeginFigure(D2D1::Point2F(x, y), D2D1_FIGURE_BEGIN_HOLLOW);
        else sink->AddLine(D2D1::Point2F(x, y));
    }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    g_c.br->SetColor(g_t.accent);
    g_rt->DrawGeometry(g, g_c.br, 1.8f);
    SafeRelease(sink);
    SafeRelease(g);
    float lx = r.right, ly = r.bottom - (float)((series.back().score - lo) / (hi - lo)) * (r.bottom - r.top);
    g_c.Circle(lx, ly, 3, g_t.accent);
}

void DrawBenchPage(float x0, float y0) {
    size_t hotFrom = g_hot.size();
    float w = std::min(1000.0f, g_W - x0 - 64);
    float x = x0 + (g_W - x0 - w) / 2;
    float y = y0 + 28 - g_scroll;
    g_c.Text(L"Benchmarks", Txt(24, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(x, y, x + w, y + 36), g_t.text);
    y += 40;
    y += Label(x, y, w, L"Quick built-in tests for CPU, GPU, memory and storage, plus scores you import from other benchmarks. Every run is kept, so you can see if performance changes over time.",
               13, g_t.subtext, DWRITE_FONT_WEIGHT_NORMAL, true);
    y += 18;

    // ---- run
    // chips may wrap: count the rows first
    int chipRows = 1;
    {
        float cx = 0, avail = w - 44;
        for (auto& id : bench::BuiltinTests()) {
            bool on = s_selected.count(id) > 0;
            float cw = MeasureWidth(bench::TestName(id), Txt(12.5f, on ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL)) + (on ? 42 : 28);
            if (cx + cw > avail) { cx = 0; ++chipRows; }
            cx += cw + 8;
        }
    }
    float ch = g_benching ? D(150) : D(178) + (chipRows - 1) * D(38);
    Card(D2D1::RectF(x, y, x + w, y + ch));
    float px = x + 22, pw = w - 44, py = y + 18;
    if (g_benching) {
        g_c.Text(L"Running: " + g_benchProgress.label, Txt(15, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(px, py, px + pw - 120, py + 28), g_t.text);
        Button(D2D1::RectF(px + pw - 100, py, px + pw, py + 32), L"Cancel", icon::Cancel, Btn::Secondary, W_BENCH_CANCEL);
        py += 46;
        ProgressBar(D2D1::RectF(px, py, px + pw, py + 8), g_benchProgress.pct, g_t.accent);
        py += 22;
        Label(px, py, pw, L"Keep other apps closed for steady results. Rigcard won't update itself until this finishes.", 12.5f, g_t.subtext, DWRITE_FONT_WEIGHT_NORMAL, true);
    } else {
        g_c.Text(L"Run benchmarks", Txt(15, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(px, py, px + pw, py + 28), g_t.text);
        py += 36;
        float cx = px;
        for (size_t i = 0; i < bench::BuiltinTests().size(); ++i) {
            const std::wstring& id = bench::BuiltinTests()[i];
            std::wstring name = bench::TestName(id);
            bool on = s_selected.count(id) > 0;
            auto* f = Txt(12.5f, on ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
            float cw = MeasureWidth(name, f) + (on ? 42 : 28);
            if (cx + cw > px + pw) { cx = px; py += D(38); }
            D2D1_RECT_F r = D2D1::RectF(cx, py, cx + cw, py + D(30));
            g_c.FillRound(r, D(15), on ? Alpha(g_t.accent, 0.18f) : (IsHover(W_BENCH_TEST, (int)i) ? g_t.surfaceHover : Mix(g_t.surface, g_t.surfaceStrong, 0.4f)));
            g_c.StrokeRound(r, D(15), on ? Alpha(g_t.accent, 0.6f) : g_t.divider);
            if (on) g_c.Glyph(icon::Check, Icon(10), D2D1::RectF(r.left + 8, r.top, r.left + 24, r.bottom), g_t.accent);
            g_c.Text(name, f, D2D1::RectF(r.left + (on ? 16 : 0), r.top, r.right, r.bottom), on ? g_t.text : g_t.subtext);
            AddHot(r, Hit::Toggle, W_BENCH_TEST, (int)i);
            cx += cw + 8;
        }
        py += D(48);
        std::wstring drive = g_cfg.benchDrive.empty() ? L"C:" : g_cfg.benchDrive;
        std::wstring dl = L"Storage test drive: " + DriveLabel(drive);
        float dw = MeasureWidth(dl, Txt(12.5f)) + 44;
        s_driveAnchor = D2D1::RectF(px, py, px + std::min(dw, pw * 0.55f), py + D(34));
        Button(s_driveAnchor, dl, 0xEDA2, Btn::Subtle, W_BENCH_DRIVE);
        float rb = ButtonWidth(L"Run selected", icon::Play);
        Button(D2D1::RectF(px + pw - rb, py, px + pw, py + D(34)), L"Run selected", icon::Play, Btn::Primary, W_BENCH_RUN, 0, !s_selected.empty());
        Button(D2D1::RectF(px + pw - rb - 10 - 90, py, px + pw - rb - 10, py + D(34)), L"Run all", 0, Btn::Secondary, W_BENCH_RUN, 1);
        py += D(40);
        g_c.Text(L"About a minute for everything. The storage test writes and deletes a 1 GiB temporary file.", Txt(12), D2D1::RectF(px, py, px + pw, py + 20), g_t.faint);
    }
    y += ch + 18;

    // ---- results
    s_latest = bench::Latest();
    float rowH = D(64);
    float rh = 62 + std::max<size_t>(1, s_latest.size()) * rowH;
    Card(D2D1::RectF(x, y, x + w, y + rh));
    py = y + 18;
    g_c.Text(L"Latest results", Txt(15, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(px, py, px + pw, py + 28), g_t.text);
    g_c.Text(L"On card", Txt(12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING), D2D1::RectF(px, py, px + pw, py + 28), g_t.subtext);
    py += 40;
    if (s_latest.empty()) g_c.Text(L"No results yet. Run a benchmark or import scores below.", Txt(13), D2D1::RectF(px, py, px + pw, py + rowH * 0.6f), g_t.faint);
    for (size_t i = 0; i < s_latest.size(); ++i) {
        const BenchResult& r = s_latest[i];
        auto series = bench::Series(r.id);
        float ry = py + i * rowH;
        if (i) g_c.Line(px, ry, px + pw, ry, g_t.divider);
        g_c.Text(r.name, Txt(14, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(px, ry + 8, px + pw * 0.36f, ry + 32), g_t.text);
        std::wstring meta = (r.source == L"Rigcard" ? r.detail : r.source) + L" · " + DateOf(r.when);
        g_c.Text(meta, Txt(11.5f), D2D1::RectF(px, ry + 32, px + pw * 0.40f, ry + 54), g_t.subtext);
        std::wstring score = bench::FormatScore(r);
        g_c.Text(score, Txt(17, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING), D2D1::RectF(px + pw * 0.38f, ry + 8, px + pw * 0.62f, ry + 34), g_t.text);
        if (series.size() >= 2) {
            double prev = series[series.size() - 2].score, first = series.front().score;
            double change = prev > 0 ? (r.score - prev) / prev * 100 : 0;
            if (r.lowerIsBetter) change = -change;
            wchar_t b[64];
            swprintf(b, 64, L"%+.1f%% vs last run", change);
            D2D1_COLOR_F col = std::fabs(change) < 2 ? g_t.subtext : change > 0 ? g_t.good : g_t.bad;
            g_c.Text(b, Txt(11.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING), D2D1::RectF(px + pw * 0.38f, ry + 34, px + pw * 0.62f, ry + 54), col);
            (void)first;
            Sparkline(D2D1::RectF(px + pw * 0.66f, ry + 16, px + pw * 0.82f, ry + 46), series);
        } else {
            g_c.Text(L"first run", Txt(11.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING), D2D1::RectF(px + pw * 0.38f, ry + 34, px + pw * 0.62f, ry + 54), g_t.faint);
        }
        bool builtin = std::find(bench::BuiltinTests().begin(), bench::BuiltinTests().end(), r.id) != bench::BuiltinTests().end();
        if (builtin) Button(D2D1::RectF(px + pw - 96, ry + 16, px + pw - 62, ry + 48), L"", icon::Refresh, Btn::Subtle, W_BENCH_RUN_ONE, (int)i, !g_benching);
        bool shown = std::find(g_build.hiddenBench.begin(), g_build.hiddenBench.end(), r.id) == g_build.hiddenBench.end();
        Toggle(px + pw - 42, ry + 21, shown, W_BENCH_PIN, (int)i);
    }
    y += rh + 18;

    // ---- import
    float ih = D(250);
    Card(D2D1::RectF(x, y, x + w, y + ih));
    py = y + 18;
    g_c.Text(L"Add scores from other benchmarks", Txt(15, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(px, py, px + pw, py + 28), g_t.text);
    py += 30;
    g_c.Text(L"3DMark result files, CrystalDiskMark text, Geekbench links, or anything you paste (Cinebench, Time Spy, Port Royal…).", Txt(12.5f), D2D1::RectF(px, py, px + pw, py + 22), g_t.subtext);
    py += 34;
    float bw1 = ButtonWidth(L"Import file…", 0xE8B5), bw2 = ButtonWidth(L"Paste results", 0xE77F);
    Button(D2D1::RectF(px, py, px + bw1, py + D(34)), L"Import file…", 0xE8B5, Btn::Secondary, W_BENCH_IMPORT_FILE);
    Button(D2D1::RectF(px + bw1 + 8, py, px + bw1 + 8 + bw2, py + D(34)), L"Paste results", 0xE77F, Btn::Secondary, W_BENCH_PASTE);
    float gx = px + bw1 + bw2 + 24;
    Field(D2D1::RectF(gx, py, px + pw - 96, py + D(34)), W_BENCH_GB_URL, 0, &s_gbUrl, L"browser.geekbench.com/v6/cpu/…");
    Button(D2D1::RectF(px + pw - 88, py, px + pw, py + D(34)), L"Import", 0, Btn::Secondary, W_BENCH_GB_IMPORT, 0, !Trim(s_gbUrl).empty());
    py += D(54);
    g_c.Text(L"Or enter a score by hand", Txt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(px, py, px + pw, py + 24), g_t.text);
    py += 30;
    float nw = pw * 0.46f, sw = pw * 0.2f, uw = pw * 0.14f;
    Field(D2D1::RectF(px, py, px + nw, py + D(34)), W_BENCH_M_NAME, 0, &s_mName, L"Benchmark (e.g. Cinebench 2024 Multi)");
    Field(D2D1::RectF(px + nw + 8, py, px + nw + 8 + sw, py + D(34)), W_BENCH_M_SCORE, 0, &s_mScore, L"Score");
    Field(D2D1::RectF(px + nw + sw + 16, py, px + nw + sw + 16 + uw, py + D(34)), W_BENCH_M_UNIT, 0, &s_mUnit, L"Unit (pts)");
    Button(D2D1::RectF(px + pw - 88, py, px + pw, py + D(34)), L"Add", icon::Check, Btn::Primary, W_BENCH_M_ADD, 0, !Trim(s_mName).empty() && !Trim(s_mScore).empty());
    y += ih + 18;

    // ---- history
    const auto& hist = bench::History();
    size_t showN = s_allHistory ? hist.size() : std::min<size_t>(hist.size(), 10);
    float hh = 62 + std::max<size_t>(1, showN) * D(36) + (hist.size() > 10 ? 44 : 0);
    Card(D2D1::RectF(x, y, x + w, y + hh));
    py = y + 18;
    g_c.Text(L"History", Txt(15, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(px, py, px + pw, py + 28), g_t.text);
    g_c.Text(std::to_wstring(hist.size()) + L" results", Txt(12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING), D2D1::RectF(px, py, px + pw, py + 28), g_t.subtext);
    py += 40;
    if (hist.empty()) g_c.Text(L"Nothing yet.", Txt(13), D2D1::RectF(px, py, px + pw, py + D(36)), g_t.faint);
    for (size_t k = 0; k < showN; ++k) {
        size_t idx = hist.size() - 1 - k;
        const BenchResult& r = hist[idx];
        float ry = py + k * D(36);
        g_c.Text(DateOf(r.when), Txt(12.5f), D2D1::RectF(px, ry, px + 150, ry + D(36)), g_t.subtext);
        g_c.Text(r.name, Txt(13), D2D1::RectF(px + 160, ry, px + pw * 0.55f, ry + D(36)), g_t.text);
        g_c.Text(bench::FormatScore(r), Txt(13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING), D2D1::RectF(px + pw * 0.55f, ry, px + pw * 0.75f, ry + D(36)), g_t.text);
        g_c.Text(r.source, Txt(12), D2D1::RectF(px + pw * 0.78f, ry, px + pw - 44, ry + D(36)), g_t.faint);
        Button(D2D1::RectF(px + pw - 34, ry + 3, px + pw, ry + D(36) - 3), L"", icon::Delete, Btn::Subtle, W_BENCH_DEL, (int)idx);
    }
    if (hist.size() > 10) {
        float by = py + showN * D(36) + 6;
        Button(D2D1::RectF(px, by, px + 160, by + 32), s_allHistory ? L"Show fewer" : L"Show all " + std::to_wstring(hist.size()), 0, Btn::Subtle, W_BENCH_SHOW_HISTORY);
    }
    y += hh + 40;
    g_contentH = y + g_scroll - y0;
    g_viewH = g_H - y0;
    ClipHot(hotFrom, D2D1::RectF(0, y0, g_W, g_H));
}

bool ClickBenchPage(const HotRect& h) {
    switch (h.id) {
        case W_BENCH_TEST: {
            const auto& t = bench::BuiltinTests();
            if (h.arg >= 0 && h.arg < (int)t.size()) { if (s_selected.count(t[h.arg])) s_selected.erase(t[h.arg]); else s_selected.insert(t[h.arg]); }
            return true;
        }
        case W_BENCH_RUN: {
            std::vector<std::wstring> tests;
            for (auto& id : bench::BuiltinTests()) if (h.arg == 1 || s_selected.count(id)) tests.push_back(id);
            StartBenchmarks(tests);
            return true;
        }
        case W_BENCH_RUN_ONE:
            if (h.arg >= 0 && h.arg < (int)s_latest.size()) StartBenchmarks({s_latest[h.arg].id});
            return true;
        case W_BENCH_CANCEL: bench::Cancel(); return true;
        case W_BENCH_DRIVE: {
            auto drives = FixedDrives();
            std::vector<std::wstring> items;
            int sel = -1;
            for (size_t i = 0; i < drives.size(); ++i) { items.push_back(DriveLabel(drives[i])); if (IEquals(drives[i], g_cfg.benchDrive.empty() ? L"C:" : g_cfg.benchDrive)) sel = (int)i; }
            OpenMenu(s_driveAnchor, items, sel, [drives](int i) { g_cfg.benchDrive = drives[i]; ConfigSave(); });
            return true;
        }
        case W_BENCH_PIN: {
            if (h.arg < 0 || h.arg >= (int)s_latest.size()) return true;
            auto& hb = g_build.hiddenBench;
            const std::wstring& id = s_latest[h.arg].id;
            auto it = std::find(hb.begin(), hb.end(), id);
            if (it == hb.end()) hb.push_back(id); else hb.erase(it);
            SaveBuildSoon();
            return true;
        }
        case W_BENCH_DEL: bench::Remove((size_t)h.arg); SaveBuildSoon(); return true;
        case W_BENCH_SHOW_HISTORY: s_allHistory = !s_allHistory; return true;
        case W_BENCH_IMPORT_FILE: {
            std::wstring path = PickOpenFile(L"Benchmark results\0*.3dmark-result;*.txt;*.xml\0All files\0*.*\0", L"Import benchmark results");
            if (!path.empty()) bench::ImportFileAsync(g_hwnd, path);
            return true;
        }
        case W_BENCH_PASTE: {
            std::wstring text = ClipboardText();
            if (Trim(text).empty()) { Toast(L"Copy your results first (for example CrystalDiskMark's File → Copy), then click Paste.", true); return true; }
            if (IContains(text, L"browser.geekbench.com")) { bench::ImportUrlAsync(g_hwnd, Trim(text)); return true; }
            auto found = bench::ParseText(text);
            if (found.empty()) { Toast(L"No scores found in what you copied. You can add one by hand below.", true); return true; }
            for (auto& r : found) bench::Add(r);
            Toast(L"Added " + std::to_wstring(found.size()) + L" score" + (found.size() == 1 ? L"" : L"s") + L".");
            SaveBuildSoon();
            return true;
        }
        case W_BENCH_GB_IMPORT: {
            std::wstring url = Trim(s_gbUrl);
            if (!IStartsWith(url, L"http")) url = L"https://" + url;
            bench::ImportUrlAsync(g_hwnd, url);
            s_gbUrl.clear();
            return true;
        }
        case W_BENCH_M_ADD: {
            std::wstring digits;
            for (wchar_t c : s_mScore) if (iswdigit(c) || c == L'.') digits += c;
            double v = digits.empty() ? 0 : wcstod(digits.c_str(), nullptr);
            if (v <= 0 || Trim(s_mName).empty()) { Toast(L"Enter a name and a score.", true); return true; }
            BenchResult r;
            r.name = Trim(s_mName);
            r.id = L"imp:" + bench::Slug(r.name);
            r.score = v;
            r.unit = Trim(s_mUnit);
            r.source = L"Manual";
            bench::Add(r);
            s_mName.clear(); s_mScore.clear(); s_mUnit.clear();
            Toast(L"Added " + r.name + L".");
            SaveBuildSoon();
            return true;
        }
    }
    return false;
}
