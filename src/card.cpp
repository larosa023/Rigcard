// Rigcard - the build model, card styles and their JSON
#include "card.h"
#include "util.h"
#include "config.h"
#include <algorithm>

Build g_build;

// ---------------------------------------------------------------- styles
static CardStyle P(const wchar_t* id, const wchar_t* name, const wchar_t* bgMode, uint32_t bg1, uint32_t bg2, float angle,
                   uint32_t panel, float panelOp, uint32_t text, uint32_t sub, uint32_t acc, uint32_t acc2,
                   const wchar_t* font, const wchar_t* head, float radius, bool glow, bool border, const wchar_t* layout) {
    CardStyle s;
    s.id = id; s.name = name; s.background = bgMode; s.bg1 = bg1; s.bg2 = bg2; s.angle = angle;
    s.panel = panel; s.panelOpacity = panelOp; s.text = text; s.subtext = sub; s.accent = acc; s.accent2 = acc2;
    s.font = font; s.headFont = head; s.radius = radius; s.glow = glow; s.border = border; s.layout = layout;
    return s;
}

const std::vector<CardStyle>& CardPresets() {
    static std::vector<CardStyle> list;
    if (!list.empty()) return list;
    const wchar_t* ui = L"Segoe UI Variable Text";
    const wchar_t* disp = L"Segoe UI Variable Display";
    list.push_back(P(L"midnight", L"Midnight", L"gradient", 0x0B0D14, 0x1A1F33, 135, 0xFFFFFF, 0.05f, 0xF4F6FB, 0x9AA3B8, 0x6C8CFF, 0xB06CFF, ui, disp, 18, false, true, L"classic"));
    list.push_back(P(L"light", L"Light", L"gradient", 0xF7F8FC, 0xE6EAF4, 135, 0xFFFFFF, 0.88f, 0x15171F, 0x5B6275, 0x3B6CF6, 0x8B5CF6, ui, disp, 18, false, true, L"classic"));
    list.push_back(P(L"neon", L"Neon", L"gradient", 0x07020F, 0x1C0733, 160, 0xFFFFFF, 0.04f, 0xF7F2FF, 0xA99BC6, 0x00F0FF, 0xFF2BD6, L"Bahnschrift", L"Bahnschrift", 14, true, true, L"classic"));
    list.push_back(P(L"aurora", L"Aurora", L"gradient", 0x07161D, 0x15304A, 120, 0xFFFFFF, 0.05f, 0xEEF7F7, 0x9DB7BF, 0x4CF2B4, 0x7AA2FF, ui, disp, 22, false, true, L"classic"));
    list.push_back(P(L"carbon", L"Carbon", L"gradient", 0x0F0F10, 0x222224, 90, 0xFFFFFF, 0.04f, 0xF2F2F2, 0x9C9CA2, 0xFF4655, 0xFF8A4C, L"Bahnschrift", L"Bahnschrift", 6, false, true, L"classic"));
    list.push_back(P(L"sunset", L"Sunset", L"gradient", 0x24101C, 0x5A2131, 140, 0xFFFFFF, 0.06f, 0xFFF3EC, 0xD9B3A8, 0xFF9A3C, 0xFF4F81, ui, disp, 20, false, false, L"classic"));
    list.push_back(P(L"showcase", L"Showcase", L"photo", 0x101014, 0x1C1C24, 135, 0x000000, 0.42f, 0xFFFFFF, 0xC9CBD3, 0x7AA2FF, 0xFFFFFF, ui, disp, 20, false, true, L"showcase"));
    list.push_back(P(L"mono", L"Mono", L"solid", 0x000000, 0x000000, 0, 0xFFFFFF, 0.06f, 0xFFFFFF, 0x8E8E93, 0xFFFFFF, 0x8E8E93, ui, disp, 12, false, true, L"minimal"));
    list.push_back(P(L"paper", L"Paper", L"solid", 0xF3EEE3, 0xF3EEE3, 0, 0xFFFFFF, 0.55f, 0x2B241B, 0x6B5F50, 0xB5651D, 0x2F6F5E, L"Georgia", L"Georgia", 10, false, true, L"classic"));
    list.push_back(P(L"ocean", L"Ocean", L"gradient", 0x03121B, 0x0A3142, 160, 0xFFFFFF, 0.05f, 0xE6F4F8, 0x93B3C0, 0x3CC8C8, 0x4F8CFF, ui, disp, 18, false, true, L"classic"));
    return list;
}

static std::wstring Hex(uint32_t c) {
    wchar_t b[16];
    swprintf(b, 16, L"#%06X", c & 0xFFFFFF);
    return b;
}
static bool ParseHex(const std::wstring& s0, uint32_t& out) {
    std::wstring s = Trim(s0);
    if (!s.empty() && s[0] == L'#') s.erase(0, 1);
    if (s.size() == 3) s = std::wstring{s[0], s[0], s[1], s[1], s[2], s[2]};
    if (s.size() != 6 || s.find_first_not_of(L"0123456789abcdefABCDEF") != std::wstring::npos) return false;
    out = (uint32_t)wcstoul(s.c_str(), nullptr, 16);
    return true;
}

json::Value StyleToJson(const CardStyle& s) {
    using json::Value;
    Value v = Value::MakeObject();
    v.Set(L"schema", Value::Num(1));
    v.Set(L"kind", Value::Str(L"rigcard-style"));
    v.Set(L"id", Value::Str(s.id));
    v.Set(L"name", Value::Str(s.name));
    v.Set(L"background", Value::Str(s.background));
    v.Set(L"bg1", Value::Str(Hex(s.bg1)));
    v.Set(L"bg2", Value::Str(Hex(s.bg2)));
    v.Set(L"angle", Value::Num(s.angle));
    v.Set(L"panel", Value::Str(Hex(s.panel)));
    v.Set(L"panelOpacity", Value::Num(s.panelOpacity));
    v.Set(L"text", Value::Str(Hex(s.text)));
    v.Set(L"subtext", Value::Str(Hex(s.subtext)));
    v.Set(L"accent", Value::Str(Hex(s.accent)));
    v.Set(L"accent2", Value::Str(Hex(s.accent2)));
    v.Set(L"font", Value::Str(s.font));
    v.Set(L"headFont", Value::Str(s.headFont));
    v.Set(L"radius", Value::Num(s.radius));
    v.Set(L"glow", Value::Boolean(s.glow));
    v.Set(L"border", Value::Boolean(s.border));
    v.Set(L"layout", Value::Str(s.layout));
    return v;
}

bool StyleFromJson(const json::Value& v, CardStyle& s) {
    if (v.type != json::Value::Object) return false;
    CardStyle d = s;
    d.id = v.GetStr(L"id", d.id);
    d.name = v.GetStr(L"name", d.name);
    d.background = v.GetStr(L"background", d.background);
    if (d.background != L"gradient" && d.background != L"solid" && d.background != L"photo") d.background = L"gradient";
    struct { const wchar_t* k; uint32_t* f; } cols[] = {{L"bg1", &d.bg1}, {L"bg2", &d.bg2}, {L"panel", &d.panel}, {L"text", &d.text},
                                                        {L"subtext", &d.subtext}, {L"accent", &d.accent}, {L"accent2", &d.accent2}};
    for (auto& c : cols) {
        std::wstring h = v.GetStr(c.k);
        if (!h.empty() && !ParseHex(h, *c.f)) return false;
    }
    d.angle = (float)v.GetNum(L"angle", d.angle);
    d.panelOpacity = std::clamp((float)v.GetNum(L"panelOpacity", d.panelOpacity), 0.0f, 1.0f);
    d.font = v.GetStr(L"font", d.font);
    d.headFont = v.GetStr(L"headFont", d.headFont);
    d.radius = std::clamp((float)v.GetNum(L"radius", d.radius), 0.0f, 40.0f);
    d.glow = v.GetBool(L"glow", d.glow);
    d.border = v.GetBool(L"border", d.border);
    d.layout = v.GetStr(L"layout", d.layout);
    if (d.layout != L"classic" && d.layout != L"showcase" && d.layout != L"minimal") d.layout = L"classic";
    s = d;
    return true;
}

std::wstring StylesDir() { return DataDir() + L"\\styles"; }

bool SaveStyleFile(const CardStyle& s, const std::wstring& path) {
    std::wstring out;
    json::Write(StyleToJson(s), out);
    return WriteTextFileAtomic(path, out);
}

bool LoadStyleFile(const std::wstring& path, CardStyle& s, std::wstring* error) {
    std::wstring text;
    if (!ReadTextFile(path, text)) { if (error) *error = L"Couldn't read the file."; return false; }
    json::Value v;
    if (!json::Parser(text).Parse(v)) { if (error) *error = L"That file isn't valid JSON."; return false; }
    // a full share/build JSON carries its style under "style"
    if (const json::Value* st = v.Get(L"style"); st && st->type == json::Value::Object) v = *st;
    CardStyle c = CardPresets()[0];
    if (!StyleFromJson(v, c)) { if (error) *error = L"That file isn't a Rigcard card style."; return false; }
    if (Trim(c.name).empty()) c.name = PathName(path);
    c.custom = true;
    s = c;
    return true;
}

std::vector<CardStyle> LoadSavedStyles() {
    std::vector<CardStyle> out;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((StylesDir() + L"\\*.json").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return out;
    do {
        CardStyle s;
        if (LoadStyleFile(StylesDir() + L"\\" + fd.cFileName, s, nullptr)) {
            s.id = L"saved:" + std::wstring(fd.cFileName);
            out.push_back(s);
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    std::sort(out.begin(), out.end(), [](const CardStyle& a, const CardStyle& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    return out;
}

// ---------------------------------------------------------------- kinds
const std::vector<std::wstring>& PartKinds() {
    static std::vector<std::wstring> k = {L"cpu", L"gpu", L"motherboard", L"ram", L"storage", L"cooler", L"psu", L"case", L"fans",
                                          L"os", L"monitor", L"keyboard", L"mouse", L"headset", L"mic", L"other"};
    return k;
}

std::wstring KindLabel(const std::wstring& k) {
    static const wchar_t* map[][2] = {{L"cpu", L"CPU"}, {L"gpu", L"GPU"}, {L"motherboard", L"Motherboard"}, {L"ram", L"Memory"},
                                      {L"storage", L"Storage"}, {L"cooler", L"Cooler"}, {L"psu", L"Power supply"}, {L"case", L"Case"},
                                      {L"fans", L"Fans"}, {L"os", L"OS"}, {L"monitor", L"Monitor"}, {L"keyboard", L"Keyboard"},
                                      {L"mouse", L"Mouse"}, {L"headset", L"Audio"}, {L"mic", L"Microphone"}, {L"other", L"Other"}};
    for (auto& m : map) if (k == m[0]) return m[1];
    return k;
}

bool IsPeripheral(const std::wstring& k) {
    return k == L"monitor" || k == L"keyboard" || k == L"mouse" || k == L"headset" || k == L"mic";
}
bool IsDetectable(const std::wstring& k) {
    return k == L"cpu" || k == L"gpu" || k == L"motherboard" || k == L"ram" || k == L"storage" || k == L"os" || k == L"monitor";
}

std::wstring PartDetailForCard(const Part& p) {
    std::wstring d = p.detail;
    if (g_cfg.showSerials && !p.serial.empty()) d += (d.empty() ? L"" : L" · ") + std::wstring(L"S/N ") + p.serial;
    return d;
}

std::wstring HostedUrl(const std::wstring& gistId) { return gistId.empty() ? L"" : std::wstring(RIGCARD_SITE) + L"#g=" + gistId; }

std::wstring LocationLine(const Build& b) {
    std::vector<std::wstring> bits;
    if (!g_cfg.hideLocation && !Trim(b.location).empty()) bits.push_back(Trim(b.location));
    for (auto& p : b.parts) if (p.kind == L"os" && !p.hidden && !p.name.empty()) { bits.push_back(p.name); break; }
    if (g_cfg.showUser && !b.user.empty()) bits.push_back(b.user + (b.computerName.empty() ? L"" : L"@" + b.computerName));
    if (g_cfg.showIp && !b.ip.empty()) bits.push_back(b.ip);
    std::wstring s;
    for (auto& x : bits) s += (s.empty() ? L"" : L"  ·  ") + x;
    return s;
}

// ---------------------------------------------------------------- build JSON
json::Value BuildToJson(const Build& b) {
    using json::Value;
    Value v = Value::MakeObject();
    v.Set(L"schema", Value::Num(1));
    v.Set(L"name", Value::Str(b.name));
    v.Set(L"location", Value::Str(b.location));
    v.Set(L"photo", Value::Str(b.photo));
    Value ph = Value::MakeObject();
    ph.Set(L"x", Value::Num(b.photoX));
    ph.Set(L"y", Value::Num(b.photoY));
    ph.Set(L"zoom", Value::Num(b.photoZoom));
    v.Set(L"photoPos", ph);
    v.Set(L"style", StyleToJson(b.style));
    Value sec = Value::MakeObject();
    sec.Set(L"parts", Value::Boolean(b.showParts));
    sec.Set(L"live", Value::Boolean(b.showLive));
    sec.Set(L"bench", Value::Boolean(b.showBench));
    sec.Set(L"photo", Value::Boolean(b.showPhoto));
    sec.Set(L"peripherals", Value::Boolean(b.showPeripherals));
    v.Set(L"sections", sec);
    v.Set(L"liveOnShare", Value::Boolean(b.liveOnShare));
    Value parts = Value::MakeArray();
    for (auto& p : b.parts) {
        Value o = Value::MakeObject();
        o.Set(L"key", Value::Str(p.key));
        o.Set(L"kind", Value::Str(p.kind));
        o.Set(L"name", Value::Str(p.name));
        o.Set(L"detail", Value::Str(p.detail));
        o.Set(L"detected", Value::Str(p.detected));
        o.Set(L"detectedDetail", Value::Str(p.detectedDetail));
        o.Set(L"serial", Value::Str(p.serial));
        o.Set(L"manual", Value::Boolean(p.manual));
        o.Set(L"hidden", Value::Boolean(p.hidden));
        parts.arr.push_back(o);
    }
    v.Set(L"parts", parts);
    Value hb = Value::MakeArray();
    for (auto& h : b.hiddenBench) hb.arr.push_back(Value::Str(h));
    v.Set(L"hiddenBench", hb);
    v.Set(L"gistId", Value::Str(b.gistId));
    v.Set(L"gistOwner", Value::Str(b.gistOwner));
    v.Set(L"sharedAt", Value::Num((double)b.sharedAt));
    v.Set(L"computerName", Value::Str(b.computerName));
    v.Set(L"user", Value::Str(b.user));
    v.Set(L"ip", Value::Str(b.ip));
    v.Set(L"scanned", Value::Boolean(b.scanned));
    return v;
}

void BuildFromJson(const json::Value& v, Build& b) {
    b = Build{};
    b.name = v.GetStr(L"name", b.name);
    b.location = v.GetStr(L"location");
    b.photo = v.GetStr(L"photo");
    if (const json::Value* ph = v.Get(L"photoPos")) {
        b.photoX = std::clamp((float)ph->GetNum(L"x", 0.5), 0.0f, 1.0f);
        b.photoY = std::clamp((float)ph->GetNum(L"y", 0.5), 0.0f, 1.0f);
        b.photoZoom = std::clamp((float)ph->GetNum(L"zoom", 1.0), 1.0f, 4.0f);
    }
    if (const json::Value* st = v.Get(L"style")) StyleFromJson(*st, b.style);
    if (const json::Value* s = v.Get(L"sections")) {
        b.showParts = s->GetBool(L"parts", true);
        b.showLive = s->GetBool(L"live", true);
        b.showBench = s->GetBool(L"bench", true);
        b.showPhoto = s->GetBool(L"photo", true);
        b.showPeripherals = s->GetBool(L"peripherals", false);
    }
    b.liveOnShare = v.GetBool(L"liveOnShare", true);
    if (const json::Value* parts = v.Get(L"parts"); parts && parts->type == json::Value::Array) {
        for (auto& o : parts->arr) {
            Part p;
            p.key = o.GetStr(L"key");
            p.kind = o.GetStr(L"kind", L"other");
            p.name = o.GetStr(L"name");
            p.detail = o.GetStr(L"detail");
            p.detected = o.GetStr(L"detected");
            p.detectedDetail = o.GetStr(L"detectedDetail");
            p.serial = o.GetStr(L"serial");
            p.manual = o.GetBool(L"manual", false);
            p.hidden = o.GetBool(L"hidden", false);
            if (!p.key.empty()) b.parts.push_back(p);
        }
    }
    if (const json::Value* hb = v.Get(L"hiddenBench"); hb && hb->type == json::Value::Array)
        for (auto& h : hb->arr) if (h.type == json::Value::String) b.hiddenBench.push_back(h.s);
    b.gistId = v.GetStr(L"gistId");
    b.gistOwner = v.GetStr(L"gistOwner");
    b.sharedAt = (long long)v.GetNum(L"sharedAt", 0);
    b.computerName = v.GetStr(L"computerName");
    b.user = v.GetStr(L"user");
    b.ip = v.GetStr(L"ip");
    b.scanned = v.GetBool(L"scanned", false);
}

static std::wstring BuildPath() { return DataDir() + L"\\build.json"; }

void BuildLoad() {
    std::wstring text;
    json::Value v;
    if (ReadTextFile(BuildPath(), text) && json::Parser(text).Parse(v) && v.type == json::Value::Object) BuildFromJson(v, g_build);
    else g_build = Build{};
    if (!g_build.photo.empty() && !FileExists(g_build.photo)) g_build.photo.clear();
}

void BuildSave() {
    std::wstring out;
    json::Write(BuildToJson(g_build), out);
    if (!WriteTextFileAtomic(BuildPath(), out)) Log(L"couldn't save build.json");
}
