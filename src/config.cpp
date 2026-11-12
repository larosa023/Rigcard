// Rigcard - settings
#include "config.h"
#include "util.h"
#include "json.h"
#include <wincrypt.h>
#include <algorithm>

Config g_cfg;

static std::wstring ConfigPath() { return DataDir() + L"\\settings.json"; }

void ConfigLoad() {
    std::wstring text;
    if (!ReadTextFile(ConfigPath(), text)) return;
    json::Value v;
    if (!json::Parser(text).Parse(v) || v.type != json::Value::Object) { Log(L"settings.json is damaged; using defaults"); return; }
    Config c;
    c.theme = v.GetStr(L"theme", c.theme);
    c.accent = v.GetStr(L"accent", c.accent);
    c.backdrop = v.GetStr(L"backdrop", c.backdrop);
    c.fontScale = std::clamp((float)v.GetNum(L"fontScale", c.fontScale), 0.8f, 1.5f);
    c.compact = v.GetBool(L"compact", c.compact);
    c.radius = std::clamp((float)v.GetNum(L"radius", c.radius), 0.0f, 16.0f);
    c.animSpeed = std::clamp((float)v.GetNum(L"animSpeed", c.animSpeed), 0.0f, 2.0f);
    c.followHangar = v.GetBool(L"followHangar", c.followHangar);
    c.page = v.GetStr(L"page", c.page);
    c.pollMs = std::clamp((int)v.GetNum(L"pollMs", c.pollMs), 500, 5000);
    c.sensorHelper = v.GetBool(L"sensorHelper", c.sensorHelper);
    c.tokenBlob = v.GetStr(L"token");
    c.githubUser = v.GetStr(L"githubUser");
    c.liveShare = v.GetBool(L"liveShare", c.liveShare);
    c.liveSeconds = std::clamp((int)v.GetNum(L"liveSeconds", c.liveSeconds), 30, 600);
    c.showSerials = v.GetBool(L"showSerials", false);
    c.showIp = v.GetBool(L"showIp", false);
    c.showUser = v.GetBool(L"showUser", false);
    c.hideLocation = v.GetBool(L"hideLocation", false);
    c.beta = v.GetBool(L"beta", c.beta);
    c.autoUpdate = v.GetBool(L"autoUpdate", c.autoUpdate);
    c.checkHours = std::clamp((int)v.GetNum(L"checkHours", c.checkHours), 1, 48);
    c.skipVersion = v.GetStr(L"skipVersion");
    c.benchDrive = v.GetStr(L"benchDrive");
    if (const json::Value* w = v.Get(L"window"); w && w->type == json::Value::Object) {
        c.windowX = (int)w->GetNum(L"x", CW_USEDEFAULT);
        c.windowY = (int)w->GetNum(L"y", CW_USEDEFAULT);
        c.windowW = (int)w->GetNum(L"w", 0);
        c.windowH = (int)w->GetNum(L"h", 0);
        c.maximized = w->GetBool(L"maximized", false);
    }
    g_cfg = c;
}

void ConfigSave() {
    using json::Value;
    Value v = Value::MakeObject();
    v.Set(L"theme", Value::Str(g_cfg.theme));
    v.Set(L"accent", Value::Str(g_cfg.accent));
    v.Set(L"backdrop", Value::Str(g_cfg.backdrop));
    v.Set(L"fontScale", Value::Num(g_cfg.fontScale));
    v.Set(L"compact", Value::Boolean(g_cfg.compact));
    v.Set(L"radius", Value::Num(g_cfg.radius));
    v.Set(L"animSpeed", Value::Num(g_cfg.animSpeed));
    v.Set(L"followHangar", Value::Boolean(g_cfg.followHangar));
    v.Set(L"page", Value::Str(g_cfg.page));
    v.Set(L"pollMs", Value::Num(g_cfg.pollMs));
    v.Set(L"sensorHelper", Value::Boolean(g_cfg.sensorHelper));
    v.Set(L"token", Value::Str(g_cfg.tokenBlob));
    v.Set(L"githubUser", Value::Str(g_cfg.githubUser));
    v.Set(L"liveShare", Value::Boolean(g_cfg.liveShare));
    v.Set(L"liveSeconds", Value::Num(g_cfg.liveSeconds));
    v.Set(L"showSerials", Value::Boolean(g_cfg.showSerials));
    v.Set(L"showIp", Value::Boolean(g_cfg.showIp));
    v.Set(L"showUser", Value::Boolean(g_cfg.showUser));
    v.Set(L"hideLocation", Value::Boolean(g_cfg.hideLocation));
    v.Set(L"beta", Value::Boolean(g_cfg.beta));
    v.Set(L"autoUpdate", Value::Boolean(g_cfg.autoUpdate));
    v.Set(L"checkHours", Value::Num(g_cfg.checkHours));
    v.Set(L"skipVersion", Value::Str(g_cfg.skipVersion));
    v.Set(L"benchDrive", Value::Str(g_cfg.benchDrive));
    Value w = Value::MakeObject();
    w.Set(L"x", Value::Num(g_cfg.windowX));
    w.Set(L"y", Value::Num(g_cfg.windowY));
    w.Set(L"w", Value::Num(g_cfg.windowW));
    w.Set(L"h", Value::Num(g_cfg.windowH));
    w.Set(L"maximized", Value::Boolean(g_cfg.maximized));
    v.Set(L"window", w);
    std::wstring out;
    json::Write(v, out);
    if (!WriteTextFileAtomic(ConfigPath(), out)) Log(L"couldn't save settings.json");
}

static std::vector<std::wstring> SkipList() {
    std::vector<std::wstring> out;
    size_t i = 0;
    const std::wstring& s = g_cfg.skipVersion;
    while (i < s.size()) {
        size_t e = s.find(L',', i);
        if (e == std::wstring::npos) e = s.size();
        std::wstring v = Trim(s.substr(i, e - i));
        if (!v.empty()) out.push_back(v);
        i = e + 1;
    }
    return out;
}
static void SetSkipList(const std::vector<std::wstring>& list) {
    g_cfg.skipVersion.clear();
    for (auto& v : list) g_cfg.skipVersion += (g_cfg.skipVersion.empty() ? L"" : L",") + v;
}
bool IsSkippedVersion(const std::wstring& v) {
    for (auto& s : SkipList()) if (CompareVersions(s, v) == 0) return true;
    return false;
}
void SkipVersion(const std::wstring& v) {
    if (IsSkippedVersion(v)) return;
    auto list = SkipList();
    list.push_back(NormalizeVersion(v));
    SetSkipList(list);
}
void PruneSkippedVersions(const std::wstring& current) {
    std::vector<std::wstring> keep;
    for (auto& v : SkipList()) if (CompareVersions(v, current) > 0) keep.push_back(v);
    SetSkipList(keep);
}

// ---------------------------------------------------------------- token (DPAPI)
static std::wstring ToHex(const BYTE* p, DWORD n) {
    static const wchar_t* d = L"0123456789abcdef";
    std::wstring s;
    for (DWORD i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

std::wstring GetToken() {
    const std::wstring& h = g_cfg.tokenBlob;
    if (h.size() < 2 || h.size() % 2) return L"";
    std::vector<BYTE> blob(h.size() / 2);
    for (size_t i = 0; i < blob.size(); ++i) blob[i] = (BYTE)wcstoul(h.substr(i * 2, 2).c_str(), nullptr, 16);
    DATA_BLOB in{(DWORD)blob.size(), blob.data()}, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return L"";
    std::string s((const char*)out.pbData, out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return Utf8ToWide(s);
}

void SetToken(const std::wstring& token) {
    if (token.empty()) { g_cfg.tokenBlob.clear(); g_cfg.githubUser.clear(); return; }
    std::string u = WideToUtf8(token);
    DATA_BLOB in{(DWORD)u.size(), (BYTE*)u.data()}, out{};
    if (CryptProtectData(&in, L"Rigcard GitHub token", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        g_cfg.tokenBlob = ToHex(out.pbData, out.cbData);
        LocalFree(out.pbData);
    }
    SecureZeroMemory(&u[0], u.size());
}
