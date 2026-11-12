// Rigcard - hardware detection: WMI, DXGI, the display config API, HID and the registry.
#pragma once
#include "common.h"
#include "card.h"

struct HwResult {
    std::vector<Part> parts;             // detected parts (manual=false, key set)
    std::wstring computerName, user, ip;
    std::wstring error;
    // extra facts for sensors and benchmarks
    int cores = 0, threads = 0, baseMhz = 0;
    std::wstring cpuVendor;              // "AuthenticAMD" / "GenuineIntel"
    int cpuFamily = 0, cpuModel = 0;
};

namespace hardware {
// Scans on a background thread and posts WM_RC_HARDWARE with a HwResult*.
void ScanAsync(HWND notify);
HwResult ScanNow();                      // synchronous (used by the scan thread and tests)

// Merges a scan into the build. Keeps your renames and hidden flags.
// Returns human-readable changes ("GPU: RTX 3080 → RTX 4090") when this isn't the first scan.
std::vector<std::wstring> Merge(Build& b, const HwResult& r);

// Name cleanups shared with the parts picker ("AMD Ryzen 9 7900X 12-Core Processor" → "AMD Ryzen 9 7900X")
std::wstring CleanCpuName(const std::wstring& s);
std::wstring CleanVendor(const std::wstring& s);
std::wstring FormatCapacity(uint64_t bytes);   // decimal like drive labels: "2 TB", "512 GB"
}
