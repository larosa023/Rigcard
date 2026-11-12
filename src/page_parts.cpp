// Rigcard - Parts page: detected parts, the parts Windows can't see, peripherals
#include "ui_internal.h"
#include "util.h"
#include "hardware.h"
#include <algorithm>
#include <map>

using namespace gfx;

static std::map<std::wstring, std::wstring> s_slot;     // text typed into an "add" slot, by kind
static const wchar_t* kSlotKinds[] = {L"cooler", L"psu", L"case", L"fans", L"keyboard", L"mouse", L"headset", L"mic", L"monitor", L"other"};
static int SlotIndex(const std::wstring& k) { for (int i = 0; i < 10; ++i) if (k == kSlotKinds[i]) return i; return -1; }
static bool PickerKind(const std::wstring& k) { return SlotIndex(k) >= 0; }

static float PartRow(float x, float y, float w, int i) {
    Part& p = g_build.parts[i];
    float h = D(44);
    float labelW = 118, gap = 10, btnW = 34;
    float avail = w - labelW - 2 * btnW - 3 * gap;
    float nameW = avail * 0.56f, detW = avail - nameW - gap;
    g_c.Text(KindLabel(p.kind), Txt(12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(x, y, x + labelW, y + h), p.hidden ? g_t.faint : g_t.subtext);
    if (!p.manual) g_c.Circle(x - 10, y + h / 2, 3, p.hidden ? g_t.faint : g_t.good);
    float fx = x + labelW;
    Field(D2D1::RectF(fx, y + 4, fx + nameW, y + h - 4), W_PART_NAME, i, &p.name, p.detected.empty() ? L"Name" : p.detected, PickerKind(p.kind) ? p.kind : L"");
    fx += nameW + gap;
    Field(D2D1::RectF(fx, y + 4, fx + detW, y + h - 4), W_PART_DETAIL, i, &p.detail, L"Details (optional)");
    fx += detW + gap;
    D2D1_RECT_F eye = D2D1::RectF(fx, y + 5, fx + btnW, y + h - 5);
    Button(eye, L"", p.hidden ? 0xED1A : 0xE7B3, Btn::Subtle, W_PART_HIDE, i);
    fx += btnW + gap;
    D2D1_RECT_F act = D2D1::RectF(fx, y + 5, fx + btnW, y + h - 5);
    if (p.manual) Button(act, L"", icon::Delete, Btn::Subtle, W_PART_DELETE, i);
    else if (p.name != p.detected || p.detail != p.detectedDetail) Button(act, L"", 0xE7A7, Btn::Subtle, W_PART_RESET, i);
    return h;
}

static float SlotRow(float x, float y, float w, const std::wstring& kind, const std::wstring& placeholder) {
    float h = D(44);
    float labelW = 118;
    g_c.Text(KindLabel(kind), Txt(12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(x, y, x + labelW, y + h), g_t.faint);
    std::wstring& text = s_slot[kind];
    float fw = (w - labelW - 2 * 34 - 3 * 10) * 0.56f;
    Field(D2D1::RectF(x + labelW, y + 4, x + labelW + fw, y + h - 4), W_SLOT, SlotIndex(kind), &text, placeholder, kind);
    if (!Trim(text).empty())
        Button(D2D1::RectF(x + labelW + fw + 10, y + 5, x + labelW + fw + 10 + 76, y + h - 5), L"Add", icon::Check, Btn::Primary, W_PART_ADD, SlotIndex(kind));
    return h;
}

static bool Has(const std::wstring& kind) {
    for (auto& p : g_build.parts) if (p.kind == kind) return true;
    return false;
}

void DrawPartsPage(float x0, float y0) {
    size_t hotFrom = g_hot.size();
    float maxW = 1000;
    float w = std::min(maxW, g_W - x0 - 64);
    float x = x0 + (g_W - x0 - w) / 2;
    float y = y0 + 28 - g_scroll;
    g_c.Text(L"Parts", Txt(24, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(x, y, x + w, y + 36), g_t.text);
    float rb = ButtonWidth(L"Rescan hardware", icon::Refresh);
    Button(D2D1::RectF(x + w - rb, y + 2, x + w, y + 36), L"Rescan hardware", icon::Refresh, Btn::Secondary, W_RESCAN);
    y += 40;
    y += Label(x, y, w - rb - 20, L"Rigcard finds your parts by itself. Rename anything, hide what you don't want on the card, and add the parts Windows can't see.", 13, g_t.subtext, DWRITE_FONT_WEIGHT_NORMAL, true);
    y += 18;

    if (!g_hwChanges.empty()) {
        auto* bf = Txt(13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true);
        float bh = MeasureHeight(g_hwChanges, bf, w - 80) + 52;
        D2D1_RECT_F r = D2D1::RectF(x, y, x + w, y + bh);
        g_c.FillRound(r, R() + 2, Alpha(g_t.info, 0.12f));
        g_c.StrokeRound(r, R() + 2, Alpha(g_t.info, 0.4f));
        g_c.Text(L"What changed since the last scan", Txt(13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(x + 18, y + 10, x + w - 60, y + 34), g_t.text);
        g_c.Text(g_hwChanges, bf, D2D1::RectF(x + 18, y + 36, x + w - 60, y + bh - 8), g_t.subtext);
        Button(D2D1::RectF(x + w - 44, y + 8, x + w - 10, y + 40), L"", icon::Close, Btn::Subtle, W_CHANGES_CLOSE);
        y += bh + 18;
    }

    float gx = x + 22, gw = w - 44;
    auto group = [&](const std::wstring& title, const std::wstring& sub, const std::function<void(float&)>& rows) {
        float top = y;
        // draw content first into place, then the panel behind would cover it; so draw the panel with a
        // pre-pass height: run rows in "measure" by drawing them, and paint the panel underneath via a layer
        float yy = top + 18;
        g_c.Text(title, Txt(15, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(gx, yy, gx + gw, yy + 26), g_t.text);
        yy += 26;
        if (!sub.empty()) yy += Label(gx, yy, gw, sub, 12, g_t.subtext, DWRITE_FONT_WEIGHT_NORMAL, true);
        yy += 8;
        rows(yy);
        y = yy + 14;
        return std::make_pair(top, y);
    };
    // group backgrounds are drawn by measuring first (cheap: rows are fixed height)
    auto groupHeight = [&](int rowsCount, const std::wstring& sub) {
        float subH = sub.empty() ? 0 : MeasureHeight(sub, Txt(12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true), gw) + 2;
        return 18 + 26 + subH + 8 + rowsCount * D(44) + 14;
    };

    std::vector<int> detected, mine, periph;
    for (int i = 0; i < (int)g_build.parts.size(); ++i) {
        const Part& p = g_build.parts[i];
        if (IsPeripheral(p.kind) || p.kind == L"other") periph.push_back(i);
        else if (p.manual || p.kind == L"cooler" || p.kind == L"psu" || p.kind == L"case" || p.kind == L"fans") mine.push_back(i);
        else detected.push_back(i);
    }
    std::vector<std::wstring> mySlots, periphSlots;
    for (const wchar_t* k : {L"cooler", L"psu", L"case", L"fans"}) if (!Has(k)) mySlots.push_back(k);
    for (const wchar_t* k : {L"keyboard", L"mouse", L"headset", L"mic", L"other"}) if (!Has(k)) periphSlots.push_back(k);
    if (Has(L"other")) periphSlots.push_back(L"other");
    if (!Has(L"monitor")) periphSlots.insert(periphSlots.begin(), L"monitor");

    y -= 0;
    const std::wstring sub1 = L"Found by Windows. A green dot means Rigcard keeps it in sync when you rescan.";
    const std::wstring sub2 = L"Windows can't detect these. Start typing to pick from popular parts, or type anything.";
    const std::wstring sub3 = L"Shown on the card when \"Peripherals\" is on (Card → Show on card).";
    float h1 = groupHeight((int)std::max<size_t>(1, detected.size()), sub1);
    Card(D2D1::RectF(x, y, x + w, y + h1));
    group(L"Detected", sub1, [&](float& yy) {
        if (detected.empty()) { g_c.Text(L"Scanning…", Txt(13), D2D1::RectF(gx, yy, gx + gw, yy + D(44)), g_t.faint); yy += D(44); }
        for (int i : detected) yy += PartRow(gx + 12, yy, gw - 12, i);
    });
    y += 16;
    float h2 = groupHeight((int)(mine.size() + mySlots.size()), sub2);
    Card(D2D1::RectF(x, y, x + w, y + h2));
    group(L"Cooling, power and case", sub2, [&](float& yy) {
        for (int i : mine) yy += PartRow(gx + 12, yy, gw - 12, i);
        for (auto& k : mySlots) {
            std::wstring ph = k == L"cooler" ? L"e.g. Noctua NH-D15 or Arctic Liquid Freezer III 360" : k == L"psu" ? L"e.g. Corsair RM1000x" : k == L"case" ? L"e.g. Lian Li O11 Dynamic EVO" : L"e.g. 6 × Arctic P12";
            yy += SlotRow(gx + 12, yy, gw - 12, k, ph);
        }
    });
    y += 16;
    float h3 = groupHeight((int)(periph.size() + periphSlots.size()), sub3);
    Card(D2D1::RectF(x, y, x + w, y + h3));
    group(L"Monitors and peripherals", sub3, [&](float& yy) {
        for (int i : periph) yy += PartRow(gx + 12, yy, gw - 12, i);
        for (auto& k : periphSlots) yy += SlotRow(gx + 12, yy, gw - 12, k, L"Add " + Lower(KindLabel(k)) + L"…");
    });
    y += 12;
    y += Label(x + 4, y, w, L"Serial numbers are found but never shown on your card or share page unless you allow it in Settings → Privacy.", 12, g_t.faint, DWRITE_FONT_WEIGHT_NORMAL, true);
    y += 40;
    g_contentH = y + g_scroll - y0;
    g_viewH = g_H - y0;
    ClipHot(hotFrom, D2D1::RectF(0, y0, g_W, g_H));
}

static void AddManual(int slot) {
    if (slot < 0 || slot >= 10) return;
    std::wstring kind = kSlotKinds[slot];
    std::wstring text = Trim(s_slot[kind]);
    if (text.empty()) return;
    int n = 0;
    for (auto& p : g_build.parts) if (IStartsWith(p.key, L"manual:")) n = std::max(n, _wtoi(p.key.c_str() + 7) + 1);
    Part p;
    p.key = L"manual:" + std::to_wstring(n);
    p.kind = kind;
    p.name = text;
    p.manual = true;
    // keep the card order: insert after the last part of an earlier-or-same kind
    auto order = [](const std::wstring& k) { auto& ks = PartKinds(); return (int)(std::find(ks.begin(), ks.end(), k) - ks.begin()); };
    auto it = g_build.parts.begin();
    while (it != g_build.parts.end() && order(it->kind) <= order(kind)) ++it;
    g_build.parts.insert(it, p);
    s_slot[kind].clear();
    if (IsPeripheral(kind) && !g_build.showPeripherals) Toast(KindLabel(kind) + L" added. Turn on Peripherals on the Card page to show it.");
    SaveBuildSoon();
}

bool ClickPartsPage(const HotRect& h) {
    auto& parts = g_build.parts;
    switch (h.id) {
        case W_RESCAN: Rescan(); return true;
        case W_CHANGES_CLOSE: g_hwChanges.clear(); return true;
        case W_PART_HIDE:
            if (h.arg >= 0 && h.arg < (int)parts.size()) { parts[h.arg].hidden = !parts[h.arg].hidden; SaveBuildSoon(); }
            return true;
        case W_PART_RESET:
            if (h.arg >= 0 && h.arg < (int)parts.size()) { parts[h.arg].name = parts[h.arg].detected; parts[h.arg].detail = parts[h.arg].detectedDetail; SaveBuildSoon(); }
            return true;
        case W_PART_DELETE:
            if (h.arg >= 0 && h.arg < (int)parts.size() && parts[h.arg].manual) { BlurField(); parts.erase(parts.begin() + h.arg); SaveBuildSoon(); }
            return true;
        case W_PART_ADD: BlurField(); AddManual(h.arg); return true;
    }
    return false;
}

void PartsFieldChanged(int id, int arg, bool commit) {
    if (id == W_SLOT) {
        // picking a suggestion or pressing Enter adds it right away
        if (commit && arg >= 0 && arg < 10 && !Trim(s_slot[kSlotKinds[arg]]).empty()) {
            int slot = arg;
            AddManual(slot);
        }
        return;
    }
    if ((id == W_PART_NAME || id == W_PART_DETAIL) && commit) {
        if (arg >= 0 && arg < (int)g_build.parts.size()) {
            Part& p = g_build.parts[arg];
            if (Trim(p.name).empty() && !p.detected.empty()) p.name = p.detected;   // an empty name goes back to the detected one
            p.name = Trim(p.name);
            p.detail = Trim(p.detail);
        }
        SaveBuildSoon();
    }
    Invalidate();
}
