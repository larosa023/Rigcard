// Rigcard - live stats (see sensors.h for where each number comes from)
#include "sensors.h"
#include "util.h"
#include "config.h"
#include <intrin.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <shellapi.h>
#include <powerbase.h>
#include <dxgi.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <ctime>
#include <algorithm>

static const wchar_t* kShmName = L"Local\\Rigcard.larosa023.Sensors";
static const wchar_t* kWantedName = L"Local\\Rigcard.larosa023.SensorsWanted";
static const wchar_t* kMainMutex = L"Local\\Rigcard.larosa023.SingleInstance";
static const wchar_t* kTaskName = L"Rigcard Sensors";

std::wstring FormatUptime(uint64_t s) {
    wchar_t b[64];
    uint64_t d = s / 86400, h = (s / 3600) % 24, m = (s / 60) % 60;
    if (d) swprintf(b, 64, L"%llud %lluh", (unsigned long long)d, (unsigned long long)h);
    else if (h) swprintf(b, 64, L"%lluh %llum", (unsigned long long)h, (unsigned long long)m);
    else swprintf(b, 64, L"%llum", (unsigned long long)m);
    return b;
}

namespace sensors {

// ================================================================ PawnIO (needs admin)
class PawnIo {
public:
    ~PawnIo() { Close(); }
    bool Open(int resourceId) {
        Close();
        h_ = CreateFileW(L"\\\\?\\GLOBALROOT\\Device\\PawnIO", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h_ == INVALID_HANDLE_VALUE) { h_ = nullptr; return false; }
        HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), (LPCWSTR)RT_RCDATA);
        HGLOBAL g = r ? LoadResource(nullptr, r) : nullptr;
        DWORD n = r ? SizeofResource(nullptr, r) : 0;
        void* p = g ? LockResource(g) : nullptr;
        DWORD ret = 0;
        if (!p || !n || !DeviceIoControl(h_, kLoad, p, n, nullptr, 0, &ret, nullptr)) { Close(); return false; }
        return true;
    }
    void Close() { if (h_) CloseHandle(h_); h_ = nullptr; }
    bool Ok() const { return h_ != nullptr; }
    bool Exec(const char* fn, uint64_t in, uint64_t& out) {
        if (!h_) return false;
        unsigned char buf[32 + 8] = {0};
        strncpy((char*)buf, fn, 31);
        memcpy(buf + 32, &in, 8);
        uint64_t o = 0;
        DWORD ret = 0;
        if (!DeviceIoControl(h_, kExec, buf, sizeof(buf), &o, sizeof(o), &ret, nullptr) || ret < 8) return false;
        out = o;
        return true;
    }
private:
    static constexpr DWORD kDevice = 41394u << 16;
    static constexpr DWORD kExec = kDevice | (0x841 << 2);
    static constexpr DWORD kLoad = kDevice | (0x821 << 2);
    HANDLE h_ = nullptr;
};

// CPU temperature and package power through PawnIO
class CpuProbe {
public:
    bool Init(const std::wstring& vendor, int family) {
        amd_ = IContains(vendor, L"AMD");
        intel_ = IContains(vendor, L"Intel");
        if (amd_ && family >= 0x17) ok_ = pio_.Open(201);
        else if (intel_) ok_ = pio_.Open(202);
        if (!ok_) { note_ = (amd_ && family < 0x17) ? L"This AMD CPU is older than Ryzen; CPU temperature isn't supported." : L"PawnIO isn't available."; return false; }
        uint64_t v = 0;
        if (intel_ && pio_.Exec("ioctl_read_msr", 0x1A2, v)) tjMax_ = (int)((v >> 16) & 0xFF);
        if (tjMax_ < 50 || tjMax_ > 130) tjMax_ = 100;
        uint64_t unit = 0;
        if (pio_.Exec("ioctl_read_msr", amd_ ? 0xC0010299 : 0x606, unit)) energyUnit_ = std::pow(0.5, (double)((unit >> 8) & 0x1F));
        return true;
    }
    bool Read(float& temp, float& power) {
        if (!ok_) return false;
        uint64_t v = 0;
        temp = NAN;
        power = NAN;
        if (amd_) {
            if (pio_.Exec("ioctl_read_smn", 0x00059800, v)) {
                uint32_t raw = (uint32_t)v;
                bool offset = (raw & 0x80000) != 0 || (raw & 0x30000) == 0x30000;
                float t = ((raw >> 21) * 125) * 0.001f;
                if (offset) t -= 49.0f;
                if (t > 0 && t < 125) temp = t;
            }
        } else if (intel_) {
            if (pio_.Exec("ioctl_read_msr", 0x1B1, v) && (v & 0x80000000ull)) temp = (float)(tjMax_ - (int)((v >> 16) & 0x7F));
        }
        uint64_t e = 0;
        if (energyUnit_ > 0 && pio_.Exec("ioctl_read_msr", amd_ ? 0xC001029B : 0x611, e)) {
            uint32_t now = (uint32_t)e;
            LARGE_INTEGER t, f;
            QueryPerformanceCounter(&t);
            QueryPerformanceFrequency(&f);
            if (lastTime_) {
                double dt = (double)(t.QuadPart - lastTime_) / f.QuadPart;
                uint32_t d = now - lastEnergy_;   // wraps cleanly
                if (dt > 0.05) {
                    double w = d * energyUnit_ / dt;
                    if (w >= 0 && w < 1500) power = (float)w;
                }
            }
            lastEnergy_ = now;
            lastTime_ = t.QuadPart;
        }
        return !std::isnan(temp) || !std::isnan(power);
    }
    const std::wstring& Note() const { return note_; }
private:
    PawnIo pio_;
    bool ok_ = false, amd_ = false, intel_ = false;
    int tjMax_ = 100;
    double energyUnit_ = 0;
    uint32_t lastEnergy_ = 0;
    long long lastTime_ = 0;
    std::wstring note_;
};

// ================================================================ helper shared memory
#pragma pack(push, 4)
struct HelperShm {
    uint32_t magic;          // 'RCSN'
    uint32_t version;
    volatile LONG seq;
    uint32_t status;         // 1 ok · 2 no PawnIO · 3 unsupported CPU
    uint64_t tick;           // GetTickCount64 of the last sample
    float cpuTemp, cpuPower;
    wchar_t note[96];
};
#pragma pack(pop)
static const uint32_t kMagic = 0x4E534352;

bool IsElevated() {
    HANDLE tok = nullptr;
    TOKEN_ELEVATION e{};
    DWORD n = 0;
    bool r = false;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        if (GetTokenInformation(tok, TokenElevation, &e, sizeof(e), &n)) r = e.TokenIsElevated != 0;
        CloseHandle(tok);
    }
    return r;
}

bool PawnIoInstalled() {
    HKEY k;
    for (REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PawnIO", 0, KEY_READ | view, &k) == ERROR_SUCCESS) {
            RegCloseKey(k);
            return true;
        }
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\PawnIO", 0, KEY_READ, &k) == ERROR_SUCCESS) { RegCloseKey(k); return true; }
    return false;
}

static std::wstring g_vendor;
static int g_family = 0;

int RunHelper() {
    // the main window creates the shared memory and the "wanted" event; we only fill them in
    HANDLE map = OpenFileMappingW(FILE_MAP_WRITE | FILE_MAP_READ, FALSE, kShmName);
    HANDLE wanted = OpenEventW(SYNCHRONIZE, FALSE, kWantedName);
    if (!map || !wanted) return 2;
    auto* shm = (HelperShm*)MapViewOfFile(map, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, sizeof(HelperShm));
    if (!shm) return 3;
    // which CPU? (CPUID)
    int v[4] = {0};
    __cpuid(v, 0);
    char vendor[13] = {0};
    memcpy(vendor, &v[1], 4); memcpy(vendor + 4, &v[3], 4); memcpy(vendor + 8, &v[2], 4);
    int i1[4] = {0};
    __cpuid(i1, 1);
    int fam = (i1[0] >> 8) & 0xF;
    if (fam == 0xF) fam += (i1[0] >> 20) & 0xFF;
    CpuProbe probe;
    bool ok = probe.Init(Utf8ToWide(vendor), fam);
    shm->magic = kMagic;
    shm->version = 1;
    shm->status = ok ? 1 : (PawnIoInstalled() ? 3 : 2);
    wcsncpy(shm->note, probe.Note().c_str(), 95);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    int missing = 0;
    while (true) {
        // quit when Rigcard is gone
        HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, kMainMutex);
        if (m) { CloseHandle(m); missing = 0; } else if (++missing > 3) break;
        // sleep (no CPU) until the window wants numbers
        if (WaitForSingleObject(wanted, 2000) != WAIT_OBJECT_0) continue;
        float t = NAN, p = NAN;
        if (ok) probe.Read(t, p);
        InterlockedIncrement(&shm->seq);
        shm->cpuTemp = t;
        shm->cpuPower = p;
        shm->tick = GetTickCount64();
        InterlockedIncrement(&shm->seq);
        Sleep(900);
    }
    UnmapViewOfFile(shm);
    CloseHandle(map);
    CloseHandle(wanted);
    return 0;
}

static bool RunHidden(const std::wstring& exe, const std::wstring& args, DWORD timeout, DWORD* code) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    bool done = WaitForSingleObject(pi.hProcess, timeout) == WAIT_OBJECT_0;
    DWORD c = 1;
    GetExitCodeProcess(pi.hProcess, &c);
    CloseHandle(pi.hProcess);
    if (code) *code = c;
    return done;
}

static std::wstring SysExe(const wchar_t* name) {
    wchar_t dir[MAX_PATH];
    GetSystemDirectoryW(dir, MAX_PATH);
    return std::wstring(dir) + L"\\" + name;
}

static bool TaskExists() {
    DWORD code = 1;
    return RunHidden(SysExe(L"schtasks.exe"), std::wstring(L"/Query /TN \"") + kTaskName + L"\"", 8000, &code) && code == 0;
}

int RunSetup() {
    if (!IsElevated()) return 10;
    if (!PawnIoInstalled()) {
        // winget is the simplest signed install; same package Ember uses
        DWORD code = 1;
        wchar_t local[MAX_PATH];
        ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\Microsoft\\WindowsApps\\winget.exe", local, MAX_PATH);
        std::wstring winget = FileExists(local) ? local : L"winget.exe";
        RunHidden(winget, L"install --id namazso.PawnIO --exact --silent --accept-package-agreements --accept-source-agreements", 5 * 60 * 1000, &code);
        if (!PawnIoInstalled()) return 11;
    }
    // a task with no trigger, run on demand at the highest level: no UAC prompt afterwards
    std::wstring xml =
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
        L"  <RegistrationInfo><Description>Reads CPU temperature and power for Rigcard while its window is open.</Description></RegistrationInfo>\r\n"
        L"  <Principals><Principal id=\"Author\"><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>\r\n"
        L"  <Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy><DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
        L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries><ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Hidden>true</Hidden>"
        L"<Priority>7</Priority><AllowStartOnDemand>true</AllowStartOnDemand></Settings>\r\n"
        L"  <Actions Context=\"Author\"><Exec><Command>" + ExePath() + L"</Command><Arguments>--sensor-helper</Arguments></Exec></Actions>\r\n"
        L"</Task>\r\n";
    std::wstring path = DataDir() + L"\\sensor-task.xml";
    std::string bytes("\xFF\xFE", 2);
    bytes.append((const char*)xml.data(), xml.size() * sizeof(wchar_t));
    if (!WriteFileAtomic(path, bytes)) return 12;
    DWORD code = 1;
    RunHidden(SysExe(L"schtasks.exe"), std::wstring(L"/Create /TN \"") + kTaskName + L"\" /XML \"" + path + L"\" /F", 20000, &code);
    DeleteFileW(path.c_str());
    return code == 0 ? 0 : 13;
}

bool SetupHelper(HWND owner, std::wstring* message) {
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = owner;
    sei.lpVerb = L"runas";
    std::wstring exe = ExePath();
    sei.lpFile = exe.c_str();
    sei.lpParameters = L"--setup-sensors";
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        *message = L"CPU temperature needs admin permission once. It wasn't given, so nothing changed.";
        return false;
    }
    WaitForSingleObject(sei.hProcess, 6 * 60 * 1000);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    switch (code) {
        case 0: *message = L"CPU temperature and power are on."; return true;
        case 11: *message = L"Couldn't install the PawnIO sensor driver. Install it from github.com/namazso/PawnIO.Setup, then try again."; return false;
        default: *message = L"Couldn't set up the sensor helper (code " + std::to_wstring(code) + L"). Details are in rigcard.log."; return false;
    }
}

void RemoveHelperTask() {
    DWORD code = 0;
    RunHidden(SysExe(L"schtasks.exe"), std::wstring(L"/Delete /TN \"") + kTaskName + L"\" /F", 10000, &code);
}

// ================================================================ NVML (NVIDIA)
struct NvUtil { unsigned gpu, memory; };
struct NvMem { unsigned long long total, free, used; };
typedef struct nvmlDevice_st* nvmlDevice_t;
class Nvml {
public:
    bool Init() {
        lib_ = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!lib_) {
            wchar_t p[MAX_PATH];
            ExpandEnvironmentStringsW(L"%ProgramFiles%\\NVIDIA Corporation\\NVSMI\\nvml.dll", p, MAX_PATH);
            lib_ = LoadLibraryW(p);
        }
        if (!lib_) return false;
        auto init = (int (*)())GetProcAddress(lib_, "nvmlInit_v2");
        auto byIndex = (int (*)(unsigned, nvmlDevice_t*))GetProcAddress(lib_, "nvmlDeviceGetHandleByIndex_v2");
        temp_ = (int (*)(nvmlDevice_t, int, unsigned*))GetProcAddress(lib_, "nvmlDeviceGetTemperature");
        power_ = (int (*)(nvmlDevice_t, unsigned*))GetProcAddress(lib_, "nvmlDeviceGetPowerUsage");
        clock_ = (int (*)(nvmlDevice_t, int, unsigned*))GetProcAddress(lib_, "nvmlDeviceGetClockInfo");
        util_ = (int (*)(nvmlDevice_t, NvUtil*))GetProcAddress(lib_, "nvmlDeviceGetUtilizationRates");
        mem_ = (int (*)(nvmlDevice_t, NvMem*))GetProcAddress(lib_, "nvmlDeviceGetMemoryInfo");
        shutdown_ = (int (*)())GetProcAddress(lib_, "nvmlShutdown");
        if (!init || !byIndex || !temp_ || init() != 0) { FreeLibrary(lib_); lib_ = nullptr; return false; }
        if (byIndex(0, &dev_) != 0) { Shutdown(); return false; }
        return true;
    }
    void Read(SensorSnapshot& s) {
        if (!dev_) return;
        unsigned v = 0;
        if (temp_(dev_, 0, &v) == 0) s.gpuTemp = (float)v;
        if (power_ && power_(dev_, &v) == 0) s.gpuPower = v / 1000.0f;
        if (clock_ && clock_(dev_, 0, &v) == 0) s.gpuMhz = (float)v;
        NvUtil u{};
        if (util_ && util_(dev_, &u) == 0) s.gpuLoad = (float)u.gpu;
        NvMem m{};
        if (mem_ && mem_(dev_, &m) == 0) { s.gpuMemUsedMb = m.used / 1048576.0f; s.gpuMemTotalMb = m.total / 1048576.0f; }
        s.gpuSource = L"NVML";
    }
    void Shutdown() {
        if (shutdown_ && dev_) shutdown_();
        dev_ = nullptr;
        if (lib_) FreeLibrary(lib_);
        lib_ = nullptr;
    }
    bool Ok() const { return dev_ != nullptr; }
private:
    HMODULE lib_ = nullptr;
    nvmlDevice_t dev_ = nullptr;
    int (*temp_)(nvmlDevice_t, int, unsigned*) = nullptr;
    int (*power_)(nvmlDevice_t, unsigned*) = nullptr;
    int (*clock_)(nvmlDevice_t, int, unsigned*) = nullptr;
    int (*util_)(nvmlDevice_t, NvUtil*) = nullptr;
    int (*mem_)(nvmlDevice_t, NvMem*) = nullptr;
    int (*shutdown_)() = nullptr;
};

// ================================================================ D3DKMT (any GPU, WDDM 2.4+)
struct KmtOpen { LUID luid; UINT hAdapter; };
struct KmtClose { UINT hAdapter; };
struct KmtQuery { UINT hAdapter; int type; void* data; UINT size; };
struct KmtPerf {
    UINT32 PhysicalAdapterIndex;
    ULONGLONG MemoryFrequency, MaxMemoryFrequency, MaxMemoryFrequencyOC, MemoryBandwidth, PCIEBandwidth;
    ULONG FanRPM, Power, Temperature;
    UCHAR PowerStateOverride;
};
class Kmt {
public:
    bool Init() {
        HMODULE g = GetModuleHandleW(L"gdi32.dll");
        if (!g) g = LoadLibraryW(L"gdi32.dll");
        open_ = (LONG(WINAPI*)(KmtOpen*))GetProcAddress(g, "D3DKMTOpenAdapterFromLuid");
        query_ = (LONG(WINAPI*)(KmtQuery*))GetProcAddress(g, "D3DKMTQueryAdapterInfo");
        close_ = (LONG(WINAPI*)(KmtClose*))GetProcAddress(g, "D3DKMTCloseAdapter");
        if (!open_ || !query_ || !close_) return false;
        IDXGIFactory1* f = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f))) return false;
        IDXGIAdapter1* a = nullptr;
        SIZE_T best = 0;
        LUID luid{};
        bool any = false;
        for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 d;
            if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.VendorId != 0x1414 && (d.DedicatedVideoMemory >= best || !any)) {
                best = d.DedicatedVideoMemory; luid = d.AdapterLuid; any = true;
            }
            a->Release();
        }
        f->Release();
        if (!any) return false;
        KmtOpen o{luid, 0};
        if (open_(&o) != 0) return false;
        adapter_ = o.hAdapter;
        KmtPerf p{};
        KmtQuery q{adapter_, 62 /*KMTQAITYPE_ADAPTERPERFDATA*/, &p, sizeof(p)};
        if (query_(&q) != 0 || p.Temperature == 0) { Close(); return false; }
        return true;
    }
    void Read(SensorSnapshot& s) {
        if (!adapter_) return;
        KmtPerf p{};
        KmtQuery q{adapter_, 62, &p, sizeof(p)};
        if (query_(&q) != 0) return;
        if (std::isnan(s.gpuTemp) && p.Temperature > 0 && p.Temperature < 1500) s.gpuTemp = p.Temperature / 10.0f;
        if (s.gpuSource.empty()) s.gpuSource = L"Windows";
    }
    void Close() { if (adapter_ && close_) { KmtClose c{adapter_}; close_(&c); } adapter_ = 0; }
    bool Ok() const { return adapter_ != 0; }
private:
    LONG(WINAPI* open_)(KmtOpen*) = nullptr;
    LONG(WINAPI* query_)(KmtQuery*) = nullptr;
    LONG(WINAPI* close_)(KmtClose*) = nullptr;
    UINT adapter_ = 0;
};

// ================================================================ HWiNFO shared memory (when it's running)
#pragma pack(push, 1)
struct HwiHeader {
    DWORD sig, version, revision;
    long long pollTime;
    DWORD sensorOff, sensorSize, sensorCount, readingOff, readingSize, readingCount;
};
struct HwiReading {
    DWORD type, sensorIndex, id;
    char labelOrig[128], labelUser[128], unit[16];
    double value, vmin, vmax, vavg;
};
#pragma pack(pop)

static bool ReadHwinfo(SensorSnapshot& s) {
    HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Global\\HWiNFO_SENS_SM2");
    if (!m) m = OpenFileMappingW(FILE_MAP_READ, FALSE, L"HWiNFO_SENS_SM2");
    if (!m) return false;
    bool got = false;
    if (auto* base = (const BYTE*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0)) {
        auto* h = (const HwiHeader*)base;
        MEMORY_BASIC_INFORMATION mbi{};
        VirtualQuery(base, &mbi, sizeof(mbi));
        if (h->sig == 0x53695748 /*'HWiS'*/ && h->readingSize >= sizeof(HwiReading)) {
            for (DWORD i = 0; i < h->readingCount; ++i) {
                size_t off = h->readingOff + (size_t)i * h->readingSize;
                if (off + sizeof(HwiReading) > mbi.RegionSize) break;
                auto* r = (const HwiReading*)(base + off);
                std::string l = r->labelOrig;
                if (r->type == 1 && std::isnan(s.cpuTemp) && (l == "CPU (Tctl/Tdie)" || l == "CPU Package" || l == "Core Max")) { s.cpuTemp = (float)r->value; got = true; }
                if (r->type == 5 && std::isnan(s.cpuPower) && (l == "CPU Package Power" || l == "CPU Package Power (SMU)")) { s.cpuPower = (float)r->value; got = true; }
                if (r->type == 1 && std::isnan(s.gpuTemp) && l == "GPU Temperature") s.gpuTemp = (float)r->value;
                if (r->type == 5 && std::isnan(s.gpuPower) && (l == "GPU Power" || l == "Total Board Power (TBP)" || l == "GPU ASIC Power")) s.gpuPower = (float)r->value;
            }
        }
        UnmapViewOfFile(base);
    }
    CloseHandle(m);
    return got;
}

// ================================================================ polling
static HWND s_notify = nullptr;
static std::mutex s_lock;
static SensorSnapshot s_latest;
static SensorStatus s_status;
static std::atomic<bool> s_active{false}, s_quit{false};
static HANDLE s_wake = nullptr;           // signals the poll thread
static HANDLE s_wanted = nullptr;         // tells the helper we want numbers
static HANDLE s_map = nullptr;
static HelperShm* s_shm = nullptr;
static std::thread s_thread;
static int s_baseMhz = 0;
static DWORD s_lastHelperRun = 0;

class Pdh {
public:
    bool Init() {
        if (PdhOpenQueryW(nullptr, 0, &q_) != ERROR_SUCCESS) return false;
        PdhAddEnglishCounterW(q_, L"\\Processor Information(_Total)\\% Processor Utility", 0, &cpuUtil_);
        if (!cpuUtil_) PdhAddEnglishCounterW(q_, L"\\Processor Information(_Total)\\% Processor Time", 0, &cpuUtil_);
        PdhAddEnglishCounterW(q_, L"\\Processor Information(_Total)\\% Processor Performance", 0, &cpuPerf_);
        PdhAddEnglishCounterW(q_, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &gpu3d_);
        PdhCollectQueryData(q_);
        return true;
    }
    void Read(SensorSnapshot& s, int baseMhz) {
        if (!q_ || PdhCollectQueryData(q_) != ERROR_SUCCESS) return;
        PDH_FMT_COUNTERVALUE v;
        if (cpuUtil_ && PdhGetFormattedCounterValue(cpuUtil_, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &v) == ERROR_SUCCESS && v.CStatus == 0)
            s.cpuLoad = (float)std::clamp(v.doubleValue, 0.0, 100.0);
        if (cpuPerf_ && baseMhz && PdhGetFormattedCounterValue(cpuPerf_, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &v) == ERROR_SUCCESS && v.CStatus == 0)
            s.cpuMhz = (float)(baseMhz * v.doubleValue / 100.0);
        if (gpu3d_ && std::isnan(s.gpuLoad)) {
            DWORD size = 0, count = 0;
            if (PdhGetFormattedCounterArrayW(gpu3d_, PDH_FMT_DOUBLE, &size, &count, nullptr) == (LONG)PDH_MORE_DATA && size) {
                std::vector<BYTE> buf(size);
                auto* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
                if (PdhGetFormattedCounterArrayW(gpu3d_, PDH_FMT_DOUBLE, &size, &count, items) == ERROR_SUCCESS) {
                    double sum = 0;
                    for (DWORD i = 0; i < count; ++i) if (items[i].FmtValue.CStatus == 0) sum += items[i].FmtValue.doubleValue;
                    s.gpuLoad = (float)std::clamp(sum, 0.0, 100.0);
                }
            }
        }
    }
    void Close() { if (q_) PdhCloseQuery(q_); q_ = nullptr; }
private:
    PDH_HQUERY q_ = nullptr;
    PDH_HCOUNTER cpuUtil_ = nullptr, cpuPerf_ = nullptr, gpu3d_ = nullptr;
};

static void HelperEnsureRunning() {
    if (!s_shm || !s_status.helperTask) return;
    bool fresh = s_shm->magic == kMagic && GetTickCount64() - s_shm->tick < 4000;
    s_status.helperRunning = fresh;
    if (fresh) return;
    DWORD now = GetTickCount();
    if (s_lastHelperRun && now - s_lastHelperRun < 30000) return;
    s_lastHelperRun = now;
    RunHidden(SysExe(L"schtasks.exe"), std::wstring(L"/Run /TN \"") + kTaskName + L"\"", 8000, nullptr);
}

static void PollThread() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Pdh pdh;
    pdh.Init();
    Nvml nvml;
    Kmt kmt;
    bool gpuInit = false;
    CpuProbe local;
    bool localOk = false, localTried = false;
    while (!s_quit) {
        if (!s_active) {
            if (s_wanted) ResetEvent(s_wanted);
            WaitForSingleObject(s_wake, INFINITE);
            continue;
        }
        if (!gpuInit) {
            gpuInit = true;
            bool nv = nvml.Init();
            bool km = kmt.Init();
            std::lock_guard<std::mutex> g(s_lock);
            s_status.nvml = nv;
            s_status.d3dkmt = km;
        }
        if (!localTried) {
            localTried = true;
            if (IsElevated()) localOk = local.Init(g_vendor, g_family);
        }
        if (s_wanted) SetEvent(s_wanted);
        SensorSnapshot s;
        pdh.Read(s, s_baseMhz);
        if (nvml.Ok()) nvml.Read(s);
        kmt.Read(s);
        // CPU temperature: this process (if elevated) → the helper → HWiNFO
        if (localOk) {
            local.Read(s.cpuTemp, s.cpuPower);
            s.cpuSource = L"PawnIO";
        } else if (s_shm && s_shm->magic == kMagic && GetTickCount64() - s_shm->tick < 4000) {
            LONG a = s_shm->seq;
            float t = s_shm->cpuTemp, p = s_shm->cpuPower;
            if (a == s_shm->seq && !(a & 1)) { s.cpuTemp = t; s.cpuPower = p; s.cpuSource = L"PawnIO"; }
        }
        bool hw = false;
        if (std::isnan(s.cpuTemp) || std::isnan(s.gpuPower)) hw = ReadHwinfo(s);
        if (hw && s.cpuSource.empty()) s.cpuSource = L"HWiNFO";
        MEMORYSTATUSEX ms{sizeof(ms)};
        if (GlobalMemoryStatusEx(&ms)) s.memUsedPct = (float)ms.dwMemoryLoad;
        s.uptimeSec = GetTickCount64() / 1000;
        s.at = (long long)time(nullptr);
        {
            std::lock_guard<std::mutex> g(s_lock);
            s_latest = s;
            s_status.hwinfo = hw;
            HelperEnsureRunning();
            if (!localOk && s.cpuSource.empty()) {
                if (!s_status.pawnioInstalled) s_status.cpuNote = L"CPU temperature needs the free PawnIO sensor driver.";
                else if (!s_status.helperTask) s_status.cpuNote = L"Turn on CPU temperature in Settings → Sensors (asks for admin once).";
                else if (s_shm && s_shm->magic == kMagic && s_shm->status != 1) s_status.cpuNote = s_shm->note;
                else s_status.cpuNote = L"Starting the sensor helper…";
            } else {
                s_status.cpuNote.clear();
            }
        }
        if (s_notify) {
            auto* copy = new SensorSnapshot(s);
            if (!PostMessageW(s_notify, WM_RC_SENSORS, 0, (LPARAM)copy)) delete copy;
        }
        WaitForSingleObject(s_wake, (DWORD)g_cfg.pollMs);
    }
    nvml.Shutdown();
    kmt.Close();
    pdh.Close();
    CoUninitialize();
}

void Init(HWND notify, const std::wstring& cpuVendor, int cpuFamily, int baseMhz) {
    s_notify = notify;
    g_vendor = cpuVendor;
    g_family = cpuFamily;
    s_baseMhz = baseMhz;
    if (!s_baseMhz) {
        struct PPI { ULONG Number, MaxMhz, CurrentMhz, MhzLimit, MaxIdleState, CurrentIdleState; } ppi[64] = {};
        if (CallNtPowerInformation(ProcessorInformation, nullptr, 0, ppi, sizeof(ppi)) == 0) s_baseMhz = (int)ppi[0].MaxMhz;
    }
    if (!s_wake) s_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!s_wanted) s_wanted = CreateEventW(nullptr, TRUE, FALSE, kWantedName);
    if (!s_map) {
        s_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(HelperShm), kShmName);
        if (s_map) s_shm = (HelperShm*)MapViewOfFile(s_map, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(HelperShm));
    }
    {
        std::lock_guard<std::mutex> g(s_lock);
        s_status.elevated = IsElevated();
        s_status.pawnioInstalled = PawnIoInstalled();
    }
    if (!s_thread.joinable()) {
        s_quit = false;
        s_thread = std::thread(PollThread);
    }
    // checking for the task starts schtasks.exe; keep it off the UI thread
    std::thread([] {
        bool exists = TaskExists();
        std::lock_guard<std::mutex> g(s_lock);
        s_status.helperTask = exists;
    }).detach();
}

void SetActive(bool active) {
    if (s_active.exchange(active) == active) return;
    if (!active && s_wanted) ResetEvent(s_wanted);
    if (s_wake) SetEvent(s_wake);
}

bool Active() { return s_active; }

SensorSnapshot Latest() {
    std::lock_guard<std::mutex> g(s_lock);
    return s_latest;
}

SensorStatus Status() {
    std::lock_guard<std::mutex> g(s_lock);
    return s_status;
}

void RefreshStatus() {
    std::thread([] {
        bool exists = TaskExists(), pawn = PawnIoInstalled();
        std::lock_guard<std::mutex> g(s_lock);
        s_status.helperTask = exists;
        s_status.pawnioInstalled = pawn;
        s_lastHelperRun = 0;
    }).detach();
}

void Shutdown() {
    s_quit = true;
    s_active = false;
    if (s_wake) SetEvent(s_wake);
    if (s_thread.joinable()) s_thread.join();
    if (s_wanted) { ResetEvent(s_wanted); CloseHandle(s_wanted); s_wanted = nullptr; }
    if (s_shm) UnmapViewOfFile(s_shm);
    s_shm = nullptr;
    if (s_map) CloseHandle(s_map);
    s_map = nullptr;
}

} // namespace sensors
