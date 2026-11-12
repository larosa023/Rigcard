// Rigcard - your PC build as one clean, shareable card
//
//   Rigcard.exe                    open Rigcard (or bring the running one to the front)
//   Rigcard.exe --quit             close a running Rigcard the normal way (used by Hangar)
//   Rigcard.exe --uninstall        remove the sensor task, shortcuts and (optionally) your data
//   Rigcard.exe --version          exit with code 0 (smoke tests)
//   Rigcard.exe --sensor-helper    the elevated CPU-sensor helper (started by its scheduled task)
//   Rigcard.exe --setup-sensors    one-time elevated setup for the helper
//   Rigcard.exe --render-test <out.png> <w> <h> [build.json] [photo]   draw a card without a window (tests)
//   Rigcard.exe --restore-state <file>   reopen where you were (Hangar live updates)
#include "common.h"
#include "ui.h"
#include "util.h"
#include "config.h"
#include "net.h"
#include "card.h"
#include "bench.h"
#include "cardrender.h"
#include "sensors.h"
#include "gfx.h"
#include "json.h"
#include "hangar_sdk.h"
#include "selfupdate.h"
#include "share.h"
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>

static HANDLE g_mutex;
static const wchar_t* kMutexName = L"Local\\Rigcard.larosa023.SingleInstance";

void ReleaseInstanceMutex() {
    if (g_mutex) { ReleaseMutex(g_mutex); CloseHandle(g_mutex); g_mutex = nullptr; }
}
void ReacquireInstanceMutex() {
    if (!g_mutex) g_mutex = CreateMutexW(nullptr, TRUE, kMutexName);
}

static std::vector<std::wstring> Args() {
    std::vector<std::wstring> out;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) out.push_back(argv[i]);
        LocalFree(argv);
    }
    return out;
}

static bool Has(const std::vector<std::wstring>& a, const wchar_t* flag) {
    for (auto& s : a) if (s == flag) return true;
    return false;
}

// Headless card render, so the renderer can be tested without a window (and under Wine in CI)
static int RenderTest(const std::vector<std::wstring>& a) {
    size_t i = 0;
    while (i < a.size() && a[i] != L"--render-test") ++i;
    if (i + 3 >= a.size()) return 2;
    std::wstring out = a[i + 1];
    UINT w = (UINT)_wtoi(a[i + 2].c_str()), h = (UINT)_wtoi(a[i + 3].c_str());
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LogInit();
    if (!gfx::Init()) return 3;
    if (i + 4 < a.size()) {
        std::wstring text;
        json::Value v;
        if (ReadTextFile(a[i + 4], text) && json::Parser(text).Parse(v)) BuildFromJson(v, g_build);
    }
    Image photo, blur;
    if (i + 5 < a.size()) {
        std::wstring err;
        if (image::Load(a[i + 5], 2000, photo, &err)) blur = image::Blurred(photo, 200);
    }
    CardInput in;
    in.build = &g_build;
    in.live.cpuTemp = 61; in.live.gpuTemp = 54; in.live.cpuPower = 88; in.live.gpuPower = 312; in.live.cpuLoad = 23; in.live.gpuLoad = 97;
    in.live.cpuMhz = 5450; in.live.gpuMhz = 2730; in.live.memUsedPct = 38; in.live.uptimeSec = 3 * 86400 + 4 * 3600 + 120; in.live.at = 1;
    in.liveRunning = true;
    auto mk = [](const wchar_t* id, const wchar_t* name, double s, const wchar_t* unit, const wchar_t* src, const wchar_t* det) {
        BenchResult r; r.id = id; r.name = name; r.score = s; r.unit = unit; r.source = src; r.detail = det; return r;
    };
    in.bench = {mk(L"cpu-single", L"CPU single-core", 2140, L"pts", L"Rigcard", L"1 thread"), mk(L"cpu-multi", L"CPU multi-core", 31250, L"pts", L"Rigcard", L"24 threads"),
                mk(L"gpu", L"GPU compute", 61800, L"GFLOPS", L"Rigcard", L"RTX 4090 · FP32 FMA"), mk(L"disk-read", L"Storage read", 7012, L"MB/s", L"Rigcard", L"C: · 1 GiB sequential"),
                mk(L"imp:tsx", L"3DMark Time Spy", 30211, L"", L"3DMark", L"")};
    in.history = in.bench;
    in.history.push_back(mk(L"cpu-multi", L"CPU multi-core", 33000, L"pts", L"Rigcard", L""));
    in.photo = photo.Empty() ? nullptr : &photo;
    in.photoBlur = blur.Empty() ? nullptr : &blur;
    in.footerUrl = L"https://larosa023.github.io/Rigcard/#g=0123456789abcdef";
    std::wstring err;
    bool ok = cardrender::ExportPng(out, w, h, in, &err);
    if (!ok) Log(L"render-test: %ls", err.c_str());
    CoUninitialize();
    return ok ? 0 : 4;
}

// Runs benchmarks without a window and logs the results (tests)
static int BenchTest(const std::vector<std::wstring>& a) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LogInit();
    std::vector<std::wstring> tests;
    bool after = false;
    for (auto& s : a) { if (after) tests.push_back(s); if (s == L"--bench-test") after = true; }
    if (tests.empty()) tests = bench::BuiltinTests();
    HWND sink = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    bench::Start(sink, tests, L"");
    MSG m;
    int code = 1;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (m.message == WM_RC_BENCH) {
            auto* p = (BenchProgress*)m.lParam;
            if (p->done) {
                for (auto& r : p->results) Log(L"bench %ls = %ls (%ls)", r.id.c_str(), bench::FormatScore(r).c_str(), r.detail.c_str());
                code = p->results.empty() ? 1 : 0;
                delete p;
                break;
            }
            delete p;
            continue;
        }
        DispatchMessageW(&m);
    }
    for (int i = 0; i < 50 && bench::Running(); ++i) Sleep(100);
    return code;
}

static int Uninstall() {
    sensors::RemoveHelperTask();
    // shortcuts and the Apps & features entry Hangar or the installer may have made
    std::wstring programs = KnownFolder(FOLDERID_Programs);
    DeleteFileW((programs + L"\\Rigcard.lnk").c_str());
    DeleteFileW((KnownFolder(FOLDERID_Desktop) + L"\\Rigcard.lnk").c_str());
    if (MessageBoxW(nullptr, L"Also delete your Rigcard data (your build, photo, benchmark history and settings)?", L"Uninstall Rigcard",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
        std::wstring dir = DataDir();
        std::wstring from = dir + L'\0';
        SHFILEOPSTRUCTW op{};
        op.wFunc = FO_DELETE;
        op.pFrom = from.c_str();
        op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
        SHFileOperationW(&op);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int show) {
    auto args = Args();
    if (Has(args, L"--version")) return 0;
    if (Has(args, L"--sensor-helper")) return sensors::RunHelper();
    if (Has(args, L"--setup-sensors")) { LogInit(); int r = sensors::RunSetup(); Log(L"sensor setup: %d", r); return r; }
    if (Has(args, L"--render-test")) return RenderTest(args);
    if (Has(args, L"--bench-test")) return BenchTest(args);
    if (Has(args, L"--share-test")) {
        // writes the quick link for a build.json next to it (tests)
        size_t i = 0;
        while (i < args.size() && args[i] != L"--share-test") ++i;
        if (i + 2 >= args.size()) return 2;
        std::wstring text;
        json::Value v;
        if (!ReadTextFile(args[i + 1], text) || !json::Parser(text).Parse(v)) return 3;
        BuildFromJson(v, g_build);
        SensorSnapshot s;
        s.cpuTemp = 61; s.gpuTemp = 54; s.cpuPower = 88; s.gpuPower = 312; s.uptimeSec = 7200; s.at = 1762900000;
        BenchResult r; r.id = L"cpu-multi"; r.name = L"CPU multi-core"; r.score = 31250; r.unit = L"pts"; r.source = L"Rigcard"; r.detail = L"24 threads";
        return WriteTextFileAtomic(args[i + 2], share::QuickLink(g_build, s, {r})) ? 0 : 4;
    }
    if (Has(args, L"--quit")) {
        if (HWND w = FindWindowW(RIGCARD_WINDOW_CLASS, nullptr)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            PostMessageW(w, WM_CLOSE, 0, 0);
            if (HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid)) { WaitForSingleObject(p, 15000); CloseHandle(p); }
        }
        return 0;
    }
    if (Has(args, L"--uninstall")) {
        if (HWND w = FindWindowW(RIGCARD_WINDOW_CLASS, nullptr)) PostMessageW(w, WM_CLOSE, 0, 0);
        return Uninstall();
    }

    selfupdate::AfterUpdate after = selfupdate::ParseArgs(GetCommandLineW());
    std::wstring restore = hangar::RestoreStateArg(GetCommandLineW());
    g_mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS && (after.active || !restore.empty())) {
        // the copy we're replacing is letting go; give it a moment
        CloseHandle(g_mutex);
        g_mutex = nullptr;
        for (int i = 0; i < 100 && !g_mutex; ++i) {
            Sleep(100);
            HANDLE m = CreateMutexW(nullptr, TRUE, kMutexName);
            if (GetLastError() != ERROR_ALREADY_EXISTS) g_mutex = m; else CloseHandle(m);
        }
    } else if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(RIGCARD_WINDOW_CLASS, nullptr)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(existing, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(existing, WM_RC_ACTIVATE, 0, 0);
        }
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SetCurrentProcessExplicitAppUserModelID(L"larosa023.Rigcard");
    LogInit();
    Log(L"==== Rigcard %ls starting (data: %ls%ls)", RIGCARD_VERSION, DataDir().c_str(), IsPortable() ? L", portable" : L"");
    ConfigLoad();
    net::Init();

    HWND hwnd = UiCreate(inst, show, after, restore);
    if (!hwnd) {
        MessageBoxW(nullptr, L"Rigcard couldn't start. Details are in rigcard.log.", L"Rigcard", MB_ICONERROR);
        return 1;
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    net::Shutdown();
    Log(L"Rigcard exiting");
    CoUninitialize();
    if (g_mutex) CloseHandle(g_mutex);
    (void)cmdLine;
    return 0;
}
