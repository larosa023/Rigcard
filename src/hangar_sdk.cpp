// Rigcard - Hangar SDK (named-pipe JSON lines; see hangar_sdk.h)
#include "hangar_sdk.h"
#include "util.h"
#include <shellapi.h>
#include <shlobj.h>
#include <thread>
#include <atomic>

namespace hangar {

static HWND s_window = nullptr;
static std::wstring s_appId, s_version, s_pipeName, s_runFile;
static std::atomic<bool> s_quit{false};
static std::thread s_thread;

static DWORD Session() {
    DWORD s = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &s);
    return s;
}

static std::wstring HangarDir() { return KnownFolder(FOLDERID_LocalAppData) + L"\\Hangar"; }

bool HangarInstalled() {
    return DirExists(HangarDir()) || FileExists(KnownFolder(FOLDERID_LocalAppData) + L"\\Programs\\Hangar\\Hangar.exe");
}

static bool WriteLine(HANDLE pipe, const json::Value& v) {
    std::wstring s;
    json::WriteCompact(v, s);
    std::string u = WideToUtf8(s) + "\n";
    DWORD w = 0;
    return WriteFile(pipe, u.data(), (DWORD)u.size(), &w, nullptr) && w == u.size();
}

static json::Value Handle(const json::Value& req) {
    using json::Value;
    std::wstring type = req.GetStr(L"type");
    Value reply = Value::MakeObject();
    if (req.Get(L"id")) reply.Set(L"id", *req.Get(L"id"));
    if (type == L"hello") {
        reply.Set(L"protocol", Value::Num(1));
        reply.Set(L"app", Value::Str(s_appId));
        reply.Set(L"version", Value::Str(s_version));
        Value caps = Value::MakeArray();
        for (const wchar_t* c : {L"state", L"theme", L"settings"}) caps.arr.push_back(Value::Str(c));
        reply.Set(L"capabilities", caps);
        return reply;
    }
    HangarCommand cmd;
    cmd.type = type;
    cmd.request = req;
    cmd.reply = Value::MakeObject();
    if (s_window) SendMessageTimeoutW(s_window, WM_RC_HANGAR, 0, (LPARAM)&cmd, SMTO_ABORTIFHUNG, 15000, nullptr);
    if (cmd.reply.obj.empty()) {
        cmd.reply.Set(L"ok", Value::Boolean(false));
        cmd.reply.Set(L"reason", Value::Str(L"unknown request"));
    }
    for (auto& kv : cmd.reply.obj) reply.Set(kv.first, kv.second);
    return reply;
}

static void Serve() {
    while (!s_quit) {
        HANDLE pipe = CreateNamedPipeW(s_pipeName.c_str(), PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                       1, 64 * 1024, 64 * 1024, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) { Log(L"hangar sdk: can't create pipe (%lu)", GetLastError()); return; }
        bool connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (s_quit) { CloseHandle(pipe); break; }
        std::string buf;
        bool restart = false;
        while (connected && !s_quit) {
            char chunk[4096];
            DWORD got = 0;
            if (!ReadFile(pipe, chunk, sizeof(chunk), &got, nullptr) || got == 0) break;
            buf.append(chunk, got);
            if (buf.size() > 1024 * 1024) break;
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                json::Value req;
                if (!json::Parser(Utf8ToWide(line)).Parse(req) || req.type != json::Value::Object) continue;
                json::Value reply = Handle(req);
                if (req.GetStr(L"type") == L"restart") { restart = true; break; }
                WriteLine(pipe, reply);
            }
            if (restart) break;
        }
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        if (restart) {
            // the window saved its state on prepare-update; now quit the normal way
            if (s_window) PostMessageW(s_window, WM_CLOSE, 0, 0);
            break;
        }
    }
}

void Start(HWND window, const std::wstring& appId, const std::wstring& version) {
    s_window = window;
    s_appId = appId;
    s_version = version;
    wchar_t name[128];
    swprintf(name, 128, L"\\\\.\\pipe\\larosa023.%ls.%lu", appId.c_str(), Session());
    s_pipeName = name;
    s_quit = false;
    s_thread = std::thread(Serve);
    // discovery file: Hangar reads it to know Rigcard speaks the protocol
    std::wstring run = HangarDir() + L"\\run";
    EnsureDir(run);
    s_runFile = run + L"\\" + appId + L".json";
    json::Value v = json::Value::MakeObject();
    v.Set(L"pid", json::Value::Num(GetCurrentProcessId()));
    v.Set(L"pipe", json::Value::Str(s_pipeName));
    v.Set(L"version", json::Value::Str(version));
    v.Set(L"protocol", json::Value::Num(1));
    v.Set(L"exe", json::Value::Str(ExePath()));
    std::wstring out;
    json::Write(v, out);
    WriteTextFileAtomic(s_runFile, out);
}

void Stop() {
    if (!s_thread.joinable()) return;
    s_quit = true;
    // unblock ConnectNamedPipe
    HANDLE h = CreateFileW(s_pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    if (s_thread.get_id() == std::this_thread::get_id()) s_thread.detach(); else s_thread.join();
    if (!s_runFile.empty()) DeleteFileW(s_runFile.c_str());
}

static void ToHangar(const json::Value& v) {
    wchar_t name[128];
    swprintf(name, 128, L"\\\\.\\pipe\\larosa023.hangar.%lu", Session());
    std::wstring pipeName = name;
    std::thread([pipeName, v] {
        if (!WaitNamedPipeW(pipeName.c_str(), 1500)) return;   // Hangar isn't running: nothing to tell
        HANDLE h = CreateFileW(pipeName.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        WriteLine(h, v);
        CloseHandle(h);
    }).detach();
}

void SendHealth() {
    json::Value v = json::Value::MakeObject();
    v.Set(L"type", json::Value::Str(L"health"));
    v.Set(L"app", json::Value::Str(s_appId));
    v.Set(L"version", json::Value::Str(s_version));
    v.Set(L"pid", json::Value::Num(GetCurrentProcessId()));
    ToHangar(v);
}

void SendRestored(const std::wstring& summary) {
    json::Value v = json::Value::MakeObject();
    v.Set(L"type", json::Value::Str(L"restored"));
    v.Set(L"app", json::Value::Str(s_appId));
    v.Set(L"summary", json::Value::Str(summary));
    ToHangar(v);
}

void SendBusy(bool busy, const std::wstring& reason) {
    json::Value v = json::Value::MakeObject();
    v.Set(L"type", json::Value::Str(busy ? L"busy" : L"idle"));
    v.Set(L"app", json::Value::Str(s_appId));
    if (busy) v.Set(L"reason", json::Value::Str(reason));
    ToHangar(v);
}

std::wstring RestoreStateArg(const wchar_t* cmdLine) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine && *cmdLine ? cmdLine : L"x", &argc);
    std::wstring out;
    if (argv) {
        for (int i = 0; i + 1 < argc; ++i) if (wcscmp(argv[i], L"--restore-state") == 0) out = argv[i + 1];
        LocalFree(argv);
    }
    return out;
}

} // namespace hangar
