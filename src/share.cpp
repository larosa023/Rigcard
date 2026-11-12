// Rigcard - sharing (see share.h)
#include "share.h"
#include "deflate.h"
#include "util.h"
#include "net.h"
#include "config.h"
#include <thread>
#include <atomic>
#include <ctime>
#include <cmath>

namespace share {

using json::Value;
static std::atomic<int> s_busy{0};
bool Busy() { return s_busy > 0; }

static std::wstring Hex(uint32_t c) {
    wchar_t b[16];
    swprintf(b, 16, L"#%06X", c & 0xFFFFFF);
    return b;
}

static Value Num1(float v) {
    if (std::isnan(v)) return Value();
    return Value::Num(std::round(v * 10) / 10);
}

Value LiveJson(const SensorSnapshot& s) {
    Value v = Value::MakeObject();
    v.Set(L"cpuT", Num1(s.cpuTemp));
    v.Set(L"gpuT", Num1(s.gpuTemp));
    v.Set(L"cpuW", Num1(s.cpuPower));
    v.Set(L"gpuW", Num1(s.gpuPower));
    v.Set(L"cpuL", Num1(s.cpuLoad));
    v.Set(L"gpuL", Num1(s.gpuLoad));
    v.Set(L"cpuMhz", Num1(s.cpuMhz));
    v.Set(L"gpuMhz", Num1(s.gpuMhz));
    v.Set(L"mem", Num1(s.memUsedPct));
    v.Set(L"up", Value::Num((double)s.uptimeSec));
    v.Set(L"at", Value::Num((double)(s.at ? s.at : time(nullptr))));
    return v;
}

Value CardJson(const Build& b, const SensorSnapshot& live, bool liveRunning, const std::vector<BenchResult>& bench, bool withPhoto) {
    Value v = Value::MakeObject();
    v.Set(L"v", Value::Num(1));
    v.Set(L"gen", Value::Str(std::wstring(L"Rigcard ") + RIGCARD_VERSION));
    v.Set(L"name", Value::Str(Trim(b.name).empty() ? L"My Build" : b.name));
    v.Set(L"sub", Value::Str(LocationLine(b)));     // location · OS, with privacy applied
    const CardStyle& s = b.style;
    Value st = Value::MakeObject();
    st.Set(L"id", Value::Str(s.id));
    st.Set(L"bg", Value::Str(s.background));
    st.Set(L"bg1", Value::Str(Hex(s.bg1)));
    st.Set(L"bg2", Value::Str(Hex(s.bg2)));
    st.Set(L"angle", Value::Num(s.angle));
    st.Set(L"panel", Value::Str(Hex(s.panel)));
    st.Set(L"panelOp", Value::Num(s.panelOpacity));
    st.Set(L"text", Value::Str(Hex(s.text)));
    st.Set(L"sub", Value::Str(Hex(s.subtext)));
    st.Set(L"acc", Value::Str(Hex(s.accent)));
    st.Set(L"acc2", Value::Str(Hex(s.accent2)));
    st.Set(L"font", Value::Str(s.font));
    st.Set(L"head", Value::Str(s.headFont));
    st.Set(L"r", Value::Num(s.radius));
    if (s.glow) st.Set(L"glow", Value::Boolean(true));
    if (!s.border) st.Set(L"border", Value::Boolean(false));
    st.Set(L"layout", Value::Str(s.layout));
    v.Set(L"style", st);
    Value sec = Value::MakeObject();
    sec.Set(L"parts", Value::Boolean(b.showParts));
    sec.Set(L"live", Value::Boolean(b.showLive));
    sec.Set(L"bench", Value::Boolean(b.showBench));
    sec.Set(L"photo", Value::Boolean(b.showPhoto && withPhoto));
    v.Set(L"show", sec);
    if (b.showParts) {
        Value parts = Value::MakeArray();
        for (auto& p : b.parts) {
            if (p.hidden || p.name.empty() || p.kind == L"os") continue;
            if (IsPeripheral(p.kind) && !b.showPeripherals) continue;
            Value o = Value::MakeObject();
            o.Set(L"k", Value::Str(p.kind));
            o.Set(L"l", Value::Str(KindLabel(p.kind)));
            o.Set(L"n", Value::Str(p.name));
            std::wstring d = PartDetailForCard(p);
            if (!d.empty()) o.Set(L"d", Value::Str(d));
            parts.arr.push_back(o);
        }
        v.Set(L"parts", parts);
    }
    if (b.showLive && live.at) {
        Value l = LiveJson(live);
        l.Set(L"mode", Value::Str(b.liveOnShare && g_cfg.liveShare ? L"live" : L"snapshot"));
        v.Set(L"live", l);
    }
    if (b.showBench && !bench.empty()) {
        Value list = Value::MakeArray();
        for (auto& r : bench) {
            Value o = Value::MakeObject();
            o.Set(L"n", Value::Str(r.name));
            o.Set(L"s", Value::Num(std::round(r.score * 10) / 10));
            o.Set(L"f", Value::Str(bench::FormatScore(r)));
            o.Set(L"src", Value::Str(r.source == L"Rigcard" ? r.detail : r.source));
            list.arr.push_back(o);
            if (list.arr.size() >= 8) break;
        }
        v.Set(L"bench", list);
    }
    if (withPhoto) {
        Value ph = Value::MakeObject();
        ph.Set(L"x", Value::Num(std::round(b.photoX * 1000) / 1000));
        ph.Set(L"y", Value::Num(std::round(b.photoY * 1000) / 1000));
        ph.Set(L"zoom", Value::Num(std::round(b.photoZoom * 100) / 100));
        v.Set(L"photo", ph);
    }
    v.Set(L"updated", Value::Num((double)time(nullptr)));
    (void)liveRunning;
    return v;
}

std::wstring QuickLink(const Build& b, const SensorSnapshot& live, const std::vector<BenchResult>& bench) {
    std::wstring text;
    json::WriteCompact(CardJson(b, live, false, bench, false), text);
    std::string packed = deflate::Compress(WideToUtf8(text));
    return std::wstring(RIGCARD_SITE) + L"#d=" + Utf8ToWide(deflate::Base64(packed, true));
}

// ---------------------------------------------------------------- GitHub
static std::wstring Headers(const std::wstring& token) {
    return L"Authorization: Bearer " + token + L"\r\nAccept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\nContent-Type: application/json\r\n";
}

static void Post(HWND notify, ShareResult* r) {
    if (!PostMessageW(notify, WM_RC_SHARE, 0, (LPARAM)r)) delete r;
}

static std::wstring ApiError(const net::Response& r, const wchar_t* what) {
    if (!r.error.empty()) return r.error;
    if (r.status == 401) return L"GitHub didn't accept your token. Connect GitHub again in Settings → Sharing.";
    if (r.status == 403 || r.status == 429) return L"GitHub refused (rate limit or missing \"gist\" permission on the token).";
    return std::wstring(what) + L" failed (HTTP " + std::to_wstring(r.status) + L").";
}

void PublishAsync(HWND notify, const Build& b, const SensorSnapshot& live, const std::vector<BenchResult>& bench, const std::string& photoJpeg) {
    std::wstring token = GetToken();
    if (token.empty()) {
        auto* r = new ShareResult;
        r->error = L"Connect GitHub in Settings → Sharing first (it's free; the card is stored in a secret gist you own).";
        Post(notify, r);
        return;
    }
    bool withPhoto = !photoJpeg.empty() && b.showPhoto;
    std::wstring cardText;
    json::Write(CardJson(b, live, true, bench, withPhoto), cardText);
    std::wstring liveText;
    json::WriteCompact(LiveJson(live), liveText);
    std::string photoB64 = withPhoto ? "data:image/jpeg;base64," + deflate::Base64(photoJpeg, false) : "";
    std::wstring gistId = b.gistId;
    std::wstring title = L"Rigcard · " + (Trim(b.name).empty() ? std::wstring(L"My Build") : b.name);
    ++s_busy;
    std::thread([=] {
        auto build = [&](bool create, bool dropPhoto = true) {
            Value files = Value::MakeObject();
            Value f1 = Value::MakeObject();
            f1.Set(L"content", Value::Str(cardText));
            files.Set(L"rigcard.json", f1);
            Value f2 = Value::MakeObject();
            f2.Set(L"content", Value::Str(liveText));
            files.Set(L"live.json", f2);
            if (withPhoto) {
                Value f3 = Value::MakeObject();
                f3.Set(L"content", Value::Str(Utf8ToWide(photoB64)));
                files.Set(L"photo.txt", f3);
            } else if (!create && dropPhoto) {
                files.Set(L"photo.txt", Value());   // removes it if it was there
            }
            Value body = Value::MakeObject();
            body.Set(L"description", Value::Str(title + L" — made with Rigcard (" + RIGCARD_SITE + L")"));
            if (create) body.Set(L"public", Value::Boolean(false));
            body.Set(L"files", files);
            std::wstring s;
            json::WriteCompact(body, s);
            return WideToUtf8(s);
        };
        auto* res = new ShareResult;
        net::Response r;
        bool created = gistId.empty();
        if (!created) {
            net::Send(L"PATCH", net::ApiBase() + L"/gists/" + gistId, build(false), Headers(token), r);
            if (r.status == 404) { created = true; r = net::Response{}; }   // you deleted the gist: make a new one
            else if (r.status == 422) {
                // removing a photo.txt that isn't there: try again without that change
                r = net::Response{};
                std::string body = build(false, false);
                net::Send(L"PATCH", net::ApiBase() + L"/gists/" + gistId, body, Headers(token), r);
            }
        }
        if (created) net::Send(L"POST", net::ApiBase() + L"/gists", build(true), Headers(token), r);
        json::Value v;
        if ((r.status == 200 || r.status == 201) && json::Parser(Utf8ToWide(r.body)).Parse(v)) {
            res->kind = ShareResult::Published;
            res->gistId = v.GetStr(L"id");
            if (const json::Value* o = v.Get(L"owner")) res->owner = o->GetStr(L"login");
            res->url = HostedUrl(res->gistId);
        } else {
            res->error = ApiError(r, created ? L"Creating the share page" : L"Updating the share page");
            Log(L"share publish: HTTP %d %ls", r.status, r.error.c_str());
        }
        --s_busy;
        Post(notify, res);
    }).detach();
}

void PushLiveAsync(HWND notify, const std::wstring& gistId, const SensorSnapshot& live) {
    std::wstring token = GetToken();
    if (token.empty() || gistId.empty() || s_busy) return;
    std::wstring liveText;
    json::WriteCompact(LiveJson(live), liveText);
    ++s_busy;
    std::thread([=] {
        Value files = Value::MakeObject();
        Value f = Value::MakeObject();
        f.Set(L"content", Value::Str(liveText));
        files.Set(L"live.json", f);
        Value body = Value::MakeObject();
        body.Set(L"files", files);
        std::wstring s;
        json::WriteCompact(body, s);
        net::Response r;
        net::Send(L"PATCH", net::ApiBase() + L"/gists/" + gistId, WideToUtf8(s), Headers(token), r);
        auto* res = new ShareResult;
        res->quiet = true;
        if (r.status == 200) res->kind = ShareResult::LivePushed;
        else res->error = ApiError(r, L"Updating live stats");
        --s_busy;
        Post(notify, res);
    }).detach();
}

void CheckTokenAsync(HWND notify, const std::wstring& token) {
    ++s_busy;
    std::thread([=] {
        net::Response r;
        net::Send(L"GET", net::ApiBase() + L"/user", "", Headers(token), r);
        auto* res = new ShareResult;
        json::Value v;
        if (r.status == 200 && json::Parser(Utf8ToWide(r.body)).Parse(v)) {
            res->kind = ShareResult::TokenChecked;
            res->login = v.GetStr(L"login");
            // classic tokens list their scopes; fine-grained tokens don't, so only warn on classic ones without "gist"
            (void)0;
        } else {
            res->error = r.status == 401 ? L"That token doesn't work. Make sure you copied all of it." : ApiError(r, L"Checking the token");
        }
        --s_busy;
        Post(notify, res);
    }).detach();
}

void Unpublish(const std::wstring& gistId) {
    std::wstring token = GetToken();
    if (token.empty() || gistId.empty()) return;
    std::thread([=] {
        net::Response r;
        net::Send(L"DELETE", net::ApiBase() + L"/gists/" + gistId, "", Headers(token), r);
        Log(L"unpublish %ls: HTTP %d", gistId.c_str(), r.status);
    }).detach();
}

} // namespace share
