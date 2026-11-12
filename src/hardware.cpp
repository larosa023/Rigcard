// Rigcard - hardware detection
#include "hardware.h"
#include "util.h"
#include <wbemidl.h>
#include <oleauto.h>
#include <dxgi1_2.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <intrin.h>
#include <thread>
#include <algorithm>
#include <map>
#include <set>

namespace hardware {

// ---------------------------------------------------------------- WMI
static const CLSID kClsidWbemLocator = {0x4590f811, 0x1d3a, 0x11d0, {0x89, 0x1f, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24}};
static const IID kIidWbemLocator = {0xdc12a687, 0x737f, 0x11cf, {0x88, 0x4d, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24}};

class Wmi {
public:
    ~Wmi() { if (svc_) svc_->Release(); if (loc_) loc_->Release(); }
    bool Connect(const wchar_t* ns) {
        if (FAILED(CoCreateInstance(kClsidWbemLocator, nullptr, CLSCTX_INPROC_SERVER, kIidWbemLocator, (void**)&loc_))) return false;
        BSTR b = SysAllocString(ns);
        HRESULT hr = loc_->ConnectServer(b, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc_);
        SysFreeString(b);
        if (FAILED(hr)) return false;
        CoSetProxyBlanket(svc_, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        return true;
    }
    template <class F> void Query(const wchar_t* wql, F&& each) {
        if (!svc_) return;
        IEnumWbemClassObject* en = nullptr;
        BSTR lang = SysAllocString(L"WQL"), q = SysAllocString(wql);
        HRESULT hr = svc_->ExecQuery(lang, q, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &en);
        SysFreeString(lang);
        SysFreeString(q);
        if (FAILED(hr) || !en) return;
        while (true) {
            IWbemClassObject* o = nullptr;
            ULONG n = 0;
            if (FAILED(en->Next(5000, 1, &o, &n)) || n == 0) break;
            each(o);
            o->Release();
        }
        en->Release();
    }
private:
    IWbemLocator* loc_ = nullptr;
    IWbemServices* svc_ = nullptr;
};

static std::wstring Str(IWbemClassObject* o, const wchar_t* name) {
    VARIANT v;
    VariantInit(&v);
    std::wstring s;
    if (SUCCEEDED(o->Get(name, 0, &v, nullptr, nullptr))) {
        if (v.vt == VT_BSTR && v.bstrVal) s = v.bstrVal;
        else if (v.vt == (VT_ARRAY | VT_I4) || v.vt == (VT_ARRAY | VT_UI2) || v.vt == (VT_ARRAY | VT_I2)) {
            // WmiMonitorID strings are arrays of character codes
            SAFEARRAY* a = v.parray;
            LONG lo = 0, hi = -1;
            SafeArrayGetLBound(a, 1, &lo);
            SafeArrayGetUBound(a, 1, &hi);
            for (LONG i = lo; i <= hi; ++i) {
                LONG c = 0;
                if (v.vt == (VT_ARRAY | VT_I4)) SafeArrayGetElement(a, &i, &c);
                else { short sc = 0; SafeArrayGetElement(a, &i, &sc); c = (unsigned short)sc; }
                if (c == 0) break;
                s += (wchar_t)c;
            }
        }
    }
    VariantClear(&v);
    return Trim(s);
}

static uint64_t Num(IWbemClassObject* o, const wchar_t* name) {
    VARIANT v;
    VariantInit(&v);
    uint64_t n = 0;
    if (SUCCEEDED(o->Get(name, 0, &v, nullptr, nullptr))) {
        switch (v.vt) {
            case VT_BSTR: n = v.bstrVal ? _wcstoui64(v.bstrVal, nullptr, 10) : 0; break;   // uint64 comes back as a string
            case VT_I4: n = (uint64_t)(uint32_t)v.lVal; break;
            case VT_UI4: n = v.ulVal; break;
            case VT_I2: n = (uint64_t)(uint16_t)v.iVal; break;
            case VT_UI2: n = v.uiVal; break;
            case VT_UI1: n = v.bVal; break;
            case VT_I8: n = (uint64_t)v.llVal; break;
            case VT_UI8: n = v.ullVal; break;
            case VT_BOOL: n = v.boolVal ? 1 : 0; break;
            default: break;
        }
    }
    VariantClear(&v);
    return n;
}

// ---------------------------------------------------------------- name helpers
static std::wstring Collapse(std::wstring s) {
    std::wstring out;
    bool sp = false;
    for (wchar_t c : s) {
        if (c == L' ' || c == L'\t') { sp = !out.empty(); continue; }
        if (sp) { out += L' '; sp = false; }
        out += c;
    }
    return out;
}

static void EraseI(std::wstring& s, const std::wstring& what) {
    std::wstring l = Lower(s), w = Lower(what);
    size_t i = l.find(w);
    if (i != std::wstring::npos) s.erase(i, what.size());
}

std::wstring CleanCpuName(const std::wstring& s0) {
    std::wstring s = Collapse(s0);
    for (const wchar_t* junk : {L"(R)", L"(TM)", L"(tm)", L"(r)", L" CPU", L" Processor", L" with Radeon Graphics", L" w/ Radeon Graphics"}) {
        while (true) {
            std::wstring before = s;
            EraseI(s, junk);
            if (before == s) break;
        }
    }
    // "AMD Ryzen 9 7900X 12-Core" → "AMD Ryzen 9 7900X"
    size_t core = Lower(s).find(L"-core");
    if (core != std::wstring::npos) {
        size_t sp = s.rfind(L' ', core);
        if (sp != std::wstring::npos) s.erase(sp);
    }
    size_t at = s.find(L" @ ");
    if (at != std::wstring::npos) s.erase(at);
    return Trim(Collapse(s));
}

std::wstring CleanVendor(const std::wstring& s0) {
    std::wstring s = Trim(s0), l = Lower(s);
    struct { const wchar_t* match; const wchar_t* name; } map[] = {
        {L"asustek", L"ASUS"}, {L"micro-star", L"MSI"}, {L"gigabyte", L"Gigabyte"}, {L"asrock", L"ASRock"}, {L"nzxt", L"NZXT"},
        {L"biostar", L"Biostar"}, {L"evga", L"EVGA"}, {L"dell", L"Dell"}, {L"hewlett", L"HP"}, {L"lenovo", L"Lenovo"},
        {L"microsoft", L"Microsoft"}, {L"intel corporation", L"Intel"}, {L"advanced micro", L"AMD"}, {L"nvidia", L"NVIDIA"},
        {L"g.skill", L"G.Skill"}, {L"g skill", L"G.Skill"}, {L"corsair", L"Corsair"}, {L"kingston", L"Kingston"},
        {L"crucial", L"Crucial"}, {L"micron", L"Micron"}, {L"samsung", L"Samsung"}, {L"sk hynix", L"SK hynix"}, {L"hynix", L"SK hynix"},
        {L"teamgroup", L"TeamGroup"}, {L"team group", L"TeamGroup"}, {L"patriot", L"Patriot"}, {L"adata", L"ADATA"},
        {L"04cd", L"G.Skill"}, {L"029e", L"Corsair"}, {L"0198", L"Kingston"}, {L"859b", L"Crucial"}, {L"00ce", L"Samsung"},
        {L"80ad", L"SK hynix"}, {L"04cb", L"ADATA"}, {L"0d1b", L"TeamGroup"},
    };
    for (auto& m : map) if (l.find(m.match) != std::wstring::npos) return m.name;
    if (l == L"to be filled by o.e.m." || l == L"default string" || l == L"system manufacturer" || l == L"unknown") return L"";
    return s;
}

std::wstring FormatCapacity(uint64_t bytes) {
    wchar_t b[64];
    double gb = bytes / 1e9;
    if (gb >= 999.5) {
        double tb = bytes / 1e12;
        // round to the marketed size: 1.0 TB, 2 TB, 4 TB…
        double r = tb >= 1.9 ? std::round(tb) : std::round(tb * 10) / 10;
        if (r == (long long)r) swprintf(b, 64, L"%lld TB", (long long)r); else swprintf(b, 64, L"%.1f TB", r);
    } else {
        static const int sizes[] = {32, 64, 120, 128, 240, 250, 256, 480, 500, 512, 960};
        int best = (int)std::round(gb);
        for (int s : sizes) if (std::fabs(gb - s) / s < 0.08) best = s;
        swprintf(b, 64, L"%d GB", best);
    }
    return b;
}

static std::wstring FormatGiB(uint64_t bytes) {
    wchar_t b[32];
    double g = bytes / (1024.0 * 1024 * 1024);
    if (g >= 1) swprintf(b, 32, L"%g GB", std::round(g * 10) / 10); else swprintf(b, 32, L"%.0f MB", bytes / (1024.0 * 1024));
    return b;
}

static std::wstring Ghz(int mhz) {
    wchar_t b[32];
    swprintf(b, 32, L"%.1f GHz", mhz / 1000.0);
    return b;
}

static std::wstring RegStr(HKEY root, const wchar_t* path, const wchar_t* name) {
    wchar_t buf[512];
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, path, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return L"";
    return Trim(buf);
}
static DWORD RegDword(HKEY root, const wchar_t* path, const wchar_t* name) {
    DWORD v = 0, size = sizeof(v);
    if (RegGetValueW(root, path, name, RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS) return 0;
    return v;
}

// "32.0.15.6636" → "566.36" for NVIDIA drivers
static std::wstring NvidiaDriver(const std::wstring& v) {
    std::wstring digits;
    for (wchar_t c : v) if (iswdigit(c)) digits += c;
    if (digits.size() < 5) return v;
    std::wstring last = digits.substr(digits.size() - 5);
    return last.substr(0, 3) + L"." + last.substr(3);
}

// ---------------------------------------------------------------- parts
static Part MakePart(const std::wstring& key, const std::wstring& kind, const std::wstring& name, const std::wstring& detail, const std::wstring& serial = L"") {
    Part p;
    p.key = key; p.kind = kind; p.name = name; p.detail = detail; p.detected = name; p.detectedDetail = detail;
    std::wstring s = Trim(serial);
    std::wstring ls = Lower(s);
    if (ls == L"to be filled by o.e.m." || ls == L"default string" || ls == L"none" || ls == L"0" || ls == L"00000000") s.clear();
    p.serial = s;
    return p;
}

static void DetectCpu(Wmi& w, HwResult& r) {
    w.Query(L"SELECT Name, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed, Manufacturer, ProcessorId FROM Win32_Processor",
            [&](IWbemClassObject* o) {
                if (!r.cpuVendor.empty()) { r.cores += (int)Num(o, L"NumberOfCores"); r.threads += (int)Num(o, L"NumberOfLogicalProcessors"); return; }
                r.cpuVendor = Str(o, L"Manufacturer");
                r.cores = (int)Num(o, L"NumberOfCores");
                r.threads = (int)Num(o, L"NumberOfLogicalProcessors");
                r.baseMhz = (int)Num(o, L"MaxClockSpeed");
                r.parts.push_back(MakePart(L"cpu", L"cpu", CleanCpuName(Str(o, L"Name")), L""));
            });
    // family/model for the sensor code (CPUID leaf 1)
    int info[4] = {0};
    __cpuid(info, 1);
    int fam = (info[0] >> 8) & 0xF, mod = (info[0] >> 4) & 0xF;
    if (fam == 0xF) fam += (info[0] >> 20) & 0xFF;
    if (fam >= 6) mod |= ((info[0] >> 16) & 0xF) << 4;
    r.cpuFamily = fam;
    r.cpuModel = mod;
    if (r.cpuVendor.empty()) {
        int v[4] = {0};
        __cpuid(v, 0);
        char vendor[13] = {0};
        memcpy(vendor, &v[1], 4); memcpy(vendor + 4, &v[3], 4); memcpy(vendor + 8, &v[2], 4);
        r.cpuVendor = Utf8ToWide(vendor);
    }
    for (auto& p : r.parts) if (p.key == L"cpu") {
        std::wstring d;
        if (r.cores) d += std::to_wstring(r.cores) + L" cores";
        if (r.threads) d += (d.empty() ? L"" : L" · ") + std::to_wstring(r.threads) + L" threads";
        if (r.baseMhz) d += (d.empty() ? L"" : L" · ") + Ghz(r.baseMhz) + L" base";
        p.detail = p.detectedDetail = d;
    }
}

struct GpuInfo { std::wstring name; uint64_t vram = 0; LUID luid{}; UINT vendor = 0; };

static std::vector<GpuInfo> DxgiAdapters() {
    std::vector<GpuInfo> out;
    IDXGIFactory1* f = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f))) return out;
    IDXGIAdapter1* a = nullptr;
    for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d;
        if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.VendorId != 0x1414 /*Microsoft*/) {
            bool dup = false;
            for (auto& g : out) dup |= g.luid.LowPart == d.AdapterLuid.LowPart && g.luid.HighPart == d.AdapterLuid.HighPart;
            if (!dup) out.push_back({Trim(d.Description), (uint64_t)d.DedicatedVideoMemory, d.AdapterLuid, d.VendorId});
        }
        a->Release();
    }
    f->Release();
    // discrete (most VRAM) first
    std::stable_sort(out.begin(), out.end(), [](const GpuInfo& x, const GpuInfo& y) { return x.vram > y.vram; });
    return out;
}

static void DetectGpu(Wmi& w, HwResult& r) {
    struct Wv { std::wstring name, driver; };
    std::vector<Wv> wmi;
    w.Query(L"SELECT Name, DriverVersion FROM Win32_VideoController", [&](IWbemClassObject* o) {
        wmi.push_back({Str(o, L"Name"), Str(o, L"DriverVersion")});
    });
    auto adapters = DxgiAdapters();
    int idx = 0;
    for (auto& g : adapters) {
        std::wstring driver;
        for (auto& x : wmi) if (IEquals(x.name, g.name)) driver = x.driver;
        std::wstring d;
        if (g.vram >= 512ull * 1024 * 1024) d = FormatGiB(g.vram);
        if (!driver.empty()) d += (d.empty() ? L"" : L" · ") + std::wstring(L"driver ") + (g.vendor == 0x10DE ? NvidiaDriver(driver) : driver);
        std::wstring name = Collapse(g.name);
        EraseI(name, L"(R)"); EraseI(name, L"(TM)");
        r.parts.push_back(MakePart(L"gpu:" + std::to_wstring(idx++), L"gpu", Trim(name), d));
    }
    if (adapters.empty()) {   // no DXGI (very old systems, some VMs): fall back to WMI
        for (auto& x : wmi) {
            if (IContains(x.name, L"Basic Display") || IContains(x.name, L"Virtual")) continue;
            r.parts.push_back(MakePart(L"gpu:" + std::to_wstring(idx++), L"gpu", x.name, x.driver.empty() ? L"" : L"driver " + x.driver));
        }
    }
}

static void DetectBoard(Wmi& w, HwResult& r) {
    std::wstring maker, product, serial, bios, biosDate;
    w.Query(L"SELECT Manufacturer, Product, SerialNumber FROM Win32_BaseBoard", [&](IWbemClassObject* o) {
        if (product.empty()) { maker = Str(o, L"Manufacturer"); product = Str(o, L"Product"); serial = Str(o, L"SerialNumber"); }
    });
    w.Query(L"SELECT SMBIOSBIOSVersion, ReleaseDate FROM Win32_BIOS", [&](IWbemClassObject* o) {
        bios = Str(o, L"SMBIOSBIOSVersion");
        std::wstring d = Str(o, L"ReleaseDate");   // 20240814000000.000000+000
        if (d.size() >= 8) biosDate = d.substr(0, 4) + L"-" + d.substr(4, 2) + L"-" + d.substr(6, 2);
    });
    std::wstring v = CleanVendor(maker);
    std::wstring name = Trim((v.empty() ? L"" : v + L" ") + product);
    if (name.empty()) return;
    std::wstring detail;
    if (!bios.empty()) detail = L"BIOS " + bios + (biosDate.empty() ? L"" : L" (" + biosDate + L")");
    r.parts.push_back(MakePart(L"motherboard", L"motherboard", name, detail, serial));
}

static void DetectRam(Wmi& w, HwResult& r) {
    uint64_t total = 0;
    int count = 0, speed = 0, type = 0;
    std::vector<uint64_t> sizes;
    std::wstring maker, partNo, serials;
    w.Query(L"SELECT Capacity, ConfiguredClockSpeed, Speed, Manufacturer, PartNumber, SMBIOSMemoryType, SerialNumber FROM Win32_PhysicalMemory",
            [&](IWbemClassObject* o) {
                uint64_t c = Num(o, L"Capacity");
                if (!c) return;
                total += c;
                ++count;
                sizes.push_back(c);
                int s = (int)Num(o, L"ConfiguredClockSpeed");
                if (!s) s = (int)Num(o, L"Speed");
                if (s > speed) speed = s;
                if (!type) type = (int)Num(o, L"SMBIOSMemoryType");
                if (maker.empty()) maker = CleanVendor(Str(o, L"Manufacturer"));
                if (partNo.empty()) partNo = Str(o, L"PartNumber");
                std::wstring sn = Str(o, L"SerialNumber");
                if (!sn.empty()) serials += (serials.empty() ? L"" : L", ") + sn;
            });
    if (!total) {
        MEMORYSTATUSEX ms{sizeof(ms)};
        GlobalMemoryStatusEx(&ms);
        total = ms.ullTotalPhys;
    }
    const wchar_t* t = type == 34 ? L"DDR5" : type == 26 ? L"DDR4" : type == 24 ? L"DDR3" : type == 35 ? L"LPDDR5" : type == 30 ? L"LPDDR4" : L"";
    std::wstring name = FormatGiB(total);
    if (*t) name += std::wstring(L" ") + t + (speed ? L"-" + std::to_wstring(speed) : L"");
    else if (speed) name += L" @ " + std::to_wstring(speed) + L" MT/s";
    std::wstring detail;
    if (count) {
        bool same = std::all_of(sizes.begin(), sizes.end(), [&](uint64_t s) { return s == sizes[0]; });
        detail = same ? std::to_wstring(count) + L" × " + FormatGiB(sizes[0]) : std::to_wstring(count) + L" modules";
        std::wstring brand = Trim(maker + L" " + partNo);
        if (!brand.empty()) detail += L" · " + brand;
    }
    r.parts.push_back(MakePart(L"ram", L"ram", name, detail, serials));
}

static void DetectStorage(HwResult& r) {
    Wmi s;
    int idx = 0;
    if (s.Connect(L"ROOT\\Microsoft\\Windows\\Storage")) {
        s.Query(L"SELECT FriendlyName, Size, MediaType, BusType, SerialNumber FROM MSFT_PhysicalDisk", [&](IWbemClassObject* o) {
            uint64_t size = Num(o, L"Size");
            int media = (int)Num(o, L"MediaType"), bus = (int)Num(o, L"BusType");
            if (bus == 7 /*USB*/ || size == 0) return;   // skip USB sticks and card readers
            std::wstring kind = bus == 17 ? L"NVMe SSD" : media == 4 ? L"SSD" : media == 3 ? L"HDD" : bus == 11 ? L"SATA" : L"";
            std::wstring detail = FormatCapacity(size) + (kind.empty() ? L"" : L" " + kind);
            r.parts.push_back(MakePart(L"storage:" + std::to_wstring(idx++), L"storage", Str(o, L"FriendlyName"), detail, Str(o, L"SerialNumber")));
        });
    }
    if (idx == 0) {
        Wmi c;
        if (c.Connect(L"ROOT\\CIMV2"))
            c.Query(L"SELECT Model, Size, InterfaceType, SerialNumber FROM Win32_DiskDrive", [&](IWbemClassObject* o) {
                if (IEquals(Str(o, L"InterfaceType"), L"USB")) return;
                r.parts.push_back(MakePart(L"storage:" + std::to_wstring(idx++), L"storage", Str(o, L"Model"), FormatCapacity(Num(o, L"Size")), Str(o, L"SerialNumber")));
            });
    }
}

static void DetectOs(HwResult& r) {
    const wchar_t* k = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    std::wstring product = RegStr(HKEY_LOCAL_MACHINE, k, L"ProductName");
    std::wstring display = RegStr(HKEY_LOCAL_MACHINE, k, L"DisplayVersion");
    std::wstring build = RegStr(HKEY_LOCAL_MACHINE, k, L"CurrentBuild");
    DWORD ubr = RegDword(HKEY_LOCAL_MACHINE, k, L"UBR");
    int b = _wtoi(build.c_str());
    // Windows 11 still says "Windows 10" in ProductName
    if (b >= 22000 && IStartsWith(product, L"Windows 10")) product = L"Windows 11" + product.substr(10);
    if (product.empty()) product = L"Windows";
    std::wstring detail = display;
    if (!build.empty()) detail += (detail.empty() ? L"" : L" · ") + std::wstring(L"build ") + build + (ubr ? L"." + std::to_wstring(ubr) : L"");
    r.parts.push_back(MakePart(L"os", L"os", product, detail));
}

static void DetectMonitors(HwResult& r) {
    UINT32 np = 0, nm = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) != ERROR_SUCCESS) return;
    int idx = 0;
    std::set<std::wstring> seen;
    for (UINT32 i = 0; i < np; ++i) {
        auto& p = paths[i];
        DISPLAYCONFIG_TARGET_DEVICE_NAME tn{};
        tn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        tn.header.size = sizeof(tn);
        tn.header.adapterId = p.targetInfo.adapterId;
        tn.header.id = p.targetInfo.id;
        std::wstring name;
        if (DisplayConfigGetDeviceInfo(&tn.header) == ERROR_SUCCESS) name = Trim(tn.monitorFriendlyDeviceName);
        if (seen.count(name + std::to_wstring(p.targetInfo.id))) continue;
        seen.insert(name + std::to_wstring(p.targetInfo.id));
        bool internal = p.targetInfo.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL ||
                        p.targetInfo.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED;
        if (name.empty()) name = internal ? L"Built-in display" : L"Display";
        std::wstring detail;
        UINT32 mi = p.sourceInfo.modeInfoIdx;
        if (mi < nm && modes[mi].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
            auto& sm = modes[mi].sourceMode;
            detail = std::to_wstring(sm.width) + L" × " + std::to_wstring(sm.height);
        }
        auto& rr = p.targetInfo.refreshRate;
        if (rr.Denominator) {
            double hz = (double)rr.Numerator / rr.Denominator;
            detail += (detail.empty() ? L"" : L" · ") + std::to_wstring((int)std::round(hz)) + L" Hz";
        }
        r.parts.push_back(MakePart(L"monitor:" + std::to_wstring(idx++), L"monitor", name, detail));
    }
}

// Keyboard and mouse names from their HID product strings ("G Pro X Superlight", "AULA F75")
static void DetectInputDevices(HwResult& r) {
    GUID hid;
    HidD_GetHidGuid(&hid);
    HDEVINFO set = SetupDiGetClassDevsW(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return;
    std::wstring keyboard, mouse;
    SP_DEVICE_INTERFACE_DATA did{sizeof(did)};
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hid, i, &did); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &did, nullptr, 0, &need, nullptr);
        if (!need) continue;
        std::vector<BYTE> buf(need);
        auto* det = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)buf.data();
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &did, det, need, nullptr, nullptr)) continue;
        HANDLE h = CreateFileW(det->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        PHIDP_PREPARSED_DATA pp = nullptr;
        HIDP_CAPS caps{};
        if (HidD_GetPreparsedData(h, &pp)) {
            HidP_GetCaps(pp, &caps);
            HidD_FreePreparsedData(pp);
        }
        wchar_t prod[128] = {0};
        bool hasName = HidD_GetProductString(h, prod, sizeof(prod));
        CloseHandle(h);
        if (!hasName || caps.UsagePage != 1) continue;
        std::wstring n = Trim(prod);
        if (n.empty() || IContains(n, L"receiver") || IContains(n, L"virtual") || IEquals(n, L"USB Keyboard") || IEquals(n, L"USB Mouse")) continue;
        if (caps.Usage == 6 && keyboard.empty()) keyboard = n;
        if (caps.Usage == 2 && mouse.empty()) mouse = n;
    }
    SetupDiDestroyDeviceInfoList(set);
    if (!keyboard.empty()) r.parts.push_back(MakePart(L"keyboard", L"keyboard", keyboard, L""));
    if (!mouse.empty()) r.parts.push_back(MakePart(L"mouse", L"mouse", mouse, L""));
}

static void DetectIdentity(HwResult& r) {
    wchar_t buf[256];
    DWORD n = 256;
    if (GetComputerNameW(buf, &n)) r.computerName = buf;
    n = 256;
    if (GetUserNameW(buf, &n)) r.user = buf;
    ULONG size = 32 * 1024;
    std::vector<BYTE> mem(size);
    auto* aa = (IP_ADAPTER_ADDRESSES*)mem.data();
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_MULTICAST, nullptr, aa, &size) == NO_ERROR) {
        for (auto* a = aa; a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || !a->FirstGatewayAddress) continue;
            for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
                if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
                auto* sin = (sockaddr_in*)u->Address.lpSockaddr;
                const BYTE* b = (const BYTE*)&sin->sin_addr;
                wchar_t ip[32];
                swprintf(ip, 32, L"%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
                r.ip = ip;
                return;
            }
        }
    }
}

HwResult ScanNow() {
    HwResult r;
    DWORD t0 = GetTickCount();
    Wmi w;
    if (!w.Connect(L"ROOT\\CIMV2")) r.error = L"Windows Management Instrumentation isn't available, so some parts couldn't be detected.";
    DetectCpu(w, r);
    DetectGpu(w, r);
    DetectBoard(w, r);
    DetectRam(w, r);
    DetectStorage(r);
    DetectOs(r);
    DetectMonitors(r);
    DetectInputDevices(r);
    DetectIdentity(r);
    Log(L"hardware scan: %zu parts in %lu ms", r.parts.size(), GetTickCount() - t0);
    return r;
}

void ScanAsync(HWND notify) {
    std::thread([notify] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        auto* r = new HwResult(ScanNow());
        CoUninitialize();
        if (!PostMessageW(notify, WM_RC_HARDWARE, 0, (LPARAM)r)) delete r;
    }).detach();
}

// ---------------------------------------------------------------- merge
static int KindOrder(const std::wstring& k) {
    auto& ks = PartKinds();
    for (size_t i = 0; i < ks.size(); ++i) if (ks[i] == k) return (int)i;
    return 99;
}

std::vector<std::wstring> Merge(Build& b, const HwResult& r) {
    std::vector<std::wstring> changes;
    bool first = !b.scanned;
    std::set<std::wstring> found;
    for (const Part& d : r.parts) {
        found.insert(d.key);
        auto it = std::find_if(b.parts.begin(), b.parts.end(), [&](const Part& p) { return !p.manual && p.key == d.key; });
        if (it == b.parts.end()) {
            Part p = d;
            if (IsPeripheral(p.kind) && p.kind != L"monitor") {
                // a keyboard or mouse you already typed in wins over the HID name
                bool typed = std::any_of(b.parts.begin(), b.parts.end(), [&](const Part& x) { return x.manual && x.kind == p.kind; });
                if (typed) continue;
            }
            b.parts.push_back(p);
            if (!first) changes.push_back(L"New " + KindLabel(d.kind) + L": " + d.name);
            continue;
        }
        Part& p = *it;
        if (!IEquals(p.detected, d.detected)) {
            if (!first) changes.push_back(KindLabel(d.kind) + L": " + p.detected + L" → " + d.detected);
            if (p.name == p.detected || p.name.empty()) p.name = d.name;   // keep a rename
        } else if (p.name.empty()) {
            p.name = d.name;
        }
        if (p.detail == p.detectedDetail || p.detail.empty()) {
            if (!first && p.detectedDetail != d.detectedDetail && !p.detectedDetail.empty() && (d.kind == L"ram" || d.kind == L"storage" || d.kind == L"gpu"))
                changes.push_back(KindLabel(d.kind) + L" " + d.name + L": " + p.detectedDetail + L" → " + d.detectedDetail);
            p.detail = d.detail;
        }
        p.detected = d.detected;
        p.detectedDetail = d.detectedDetail;
        p.serial = d.serial;
    }
    for (auto it = b.parts.begin(); it != b.parts.end();) {
        if (!it->manual && !found.count(it->key)) {
            // keyboards and mice come and go (wireless, asleep); only drop core parts loudly
            if (!IsPeripheral(it->kind) || it->kind == L"monitor") changes.push_back(KindLabel(it->kind) + L" removed: " + it->name);
            if (IsPeripheral(it->kind) && it->kind != L"monitor") { ++it; continue; }
            it = b.parts.erase(it);
        } else {
            ++it;
        }
    }
    std::stable_sort(b.parts.begin(), b.parts.end(), [](const Part& x, const Part& y) { return KindOrder(x.kind) < KindOrder(y.kind); });
    b.computerName = r.computerName;
    b.user = r.user;
    b.ip = r.ip;
    b.scanned = true;
    return changes;
}

} // namespace hardware
