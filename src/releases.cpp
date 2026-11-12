// Rigcard - GitHub releases
#include "releases.h"
#include "util.h"
#include "net.h"
#include "json.h"
#include <algorithm>
#include <ctime>

namespace releases {

bool Parse(const std::string& body, std::vector<ReleaseInfo>& out, std::wstring* error) {
    json::Value root;
    if (!json::Parser(Utf8ToWide(body)).Parse(root) || root.type != json::Value::Array) {
        if (error) *error = L"GitHub sent an unexpected reply.";
        return false;
    }
    out.clear();
    for (const json::Value& r : root.arr) {
        if (r.GetBool(L"draft", false)) continue;
        ReleaseInfo info;
        info.tag = r.GetStr(L"tag_name");
        std::wstring v = NormalizeVersion(info.tag);
        if (v.empty() || !iswdigit(v[0])) continue;
        info.version = v;
        info.title = r.GetStr(L"name", info.tag);
        info.notes = r.GetStr(L"body");
        info.published = r.GetStr(L"published_at");
        info.page = r.GetStr(L"html_url");
        info.prerelease = r.GetBool(L"prerelease", false) || v.find(L'-') != std::wstring::npos;
        if (const json::Value* assets = r.Get(L"assets"); assets && assets->type == json::Value::Array) {
            for (const json::Value& a : assets->arr) {
                Asset as;
                as.name = a.GetStr(L"name");
                as.url = a.GetStr(L"browser_download_url");
                as.size = (uint64_t)a.GetNum(L"size", 0);
                std::wstring digest = a.GetStr(L"digest");
                if (IStartsWith(digest, L"sha256:")) as.sha256 = Lower(digest.substr(7));
                if (!as.name.empty() && !as.url.empty()) info.assets.push_back(as);
            }
        }
        out.push_back(std::move(info));
    }
    std::stable_sort(out.begin(), out.end(), [](const ReleaseInfo& a, const ReleaseInfo& b) { return CompareVersions(a.version, b.version) > 0; });
    return true;
}

const Asset* FindAsset(const ReleaseInfo& r, const std::wstring& name) {
    for (auto& a : r.assets) if (IEquals(a.name, name)) return &a;
    return nullptr;
}

std::wstring ExpectedHashFromSums(const std::string& sums, const std::wstring& assetName) {
    std::wstring text = Utf8ToWide(sums);
    size_t i = 0;
    std::wstring single;
    int lines = 0;
    while (i < text.size()) {
        size_t e = text.find(L'\n', i);
        if (e == std::wstring::npos) e = text.size();
        std::wstring line = Trim(text.substr(i, e - i));
        i = e + 1;
        if (line.empty()) continue;
        ++lines;
        size_t sp = line.find_first_of(L" \t");
        std::wstring hash = Lower(line.substr(0, sp));
        if (hash.size() != 64 || hash.find_first_not_of(L"0123456789abcdef") != std::wstring::npos) continue;
        std::wstring name = sp == std::wstring::npos ? L"" : Trim(line.substr(sp));
        if (!name.empty() && name[0] == L'*') name.erase(0, 1);
        if (IEquals(name, assetName)) return hash;
        if (name.empty()) single = hash;
    }
    return lines == 1 ? single : L"";
}

// Hangar's release index: every suite app's releases in one file, refreshed every 10 minutes
static bool FromIndex(const std::wstring& repo, std::string& body) {
    net::Response r;
    if (!net::Get(L"https://raw.githubusercontent.com/larosa023/Hangar/index/releases.json", r) || r.status != 200) return false;
    json::Value v;
    if (!json::Parser(Utf8ToWide(r.body)).Parse(v) || v.type != json::Value::Object) return false;
    long long generated = (long long)v.GetNum(L"generated", 0);
    if (generated && (long long)time(nullptr) - generated > 24 * 3600) return false;   // stale index
    const json::Value* repos = v.Get(L"repos");
    if (!repos) repos = &v;
    const json::Value* list = repos->Get(Lower(repo));
    if (!list || list->type != json::Value::Array) return false;
    std::wstring out;
    json::WriteCompact(*list, out);
    body = WideToUtf8(out);
    return true;
}

bool Fetch(const std::wstring& repo, std::vector<ReleaseInfo>& out, std::wstring* error) {
    std::string body;
    if (FromIndex(repo, body) && Parse(body, out, nullptr) && !out.empty()) return true;
    net::Response r;
    if (!net::Get(net::ApiBase() + L"/repos/" + repo + L"/releases?per_page=10", r, L"", true)) { if (error) *error = r.error; return false; }
    if (r.status == 403 || r.status == 429) { if (error) *error = L"GitHub's hourly limit was reached; Rigcard will try again later."; return false; }
    if (r.status != 200) { if (error) *error = L"GitHub answered with HTTP " + std::to_wstring(r.status) + L"."; return false; }
    return Parse(r.body, out, error);
}

} // namespace releases
