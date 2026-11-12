// Rigcard - live stats.
//
// Where each number comes from (best source first):
//   CPU temperature + package power  PawnIO driver (same signed driver Ember and LibreHardwareMonitor use).
//                                    It needs admin, so Rigcard runs a tiny elevated helper (a scheduled
//                                    task set up once) and reads its numbers through shared memory. The
//                                    window itself stays un-elevated (so drag-and-drop keeps working).
//                                    Falls back to HWiNFO's shared memory when HWiNFO is running.
//   GPU temp/power/clock/load        NVIDIA: NVML (ships with the driver). Others: Windows' own GPU
//                                    perf data (D3DKMT, the numbers Task Manager shows).
//   CPU load and clock               Windows performance counters (no admin).
//   Uptime, memory                   Windows.
// Polling only runs while the window is visible or live sharing is on.
#pragma once
#include "common.h"
#include <cmath>

struct SensorSnapshot {
    float cpuTemp = NAN, cpuLoad = NAN, cpuMhz = NAN, cpuPower = NAN;
    float gpuTemp = NAN, gpuLoad = NAN, gpuMhz = NAN, gpuPower = NAN, gpuMemUsedMb = NAN, gpuMemTotalMb = NAN;
    float memUsedPct = NAN;
    uint64_t uptimeSec = 0;
    std::wstring cpuSource, gpuSource;     // "PawnIO", "HWiNFO", "NVML", "Windows"
    long long at = 0;                      // unix seconds
};

struct SensorStatus {
    bool pawnioInstalled = false;
    bool elevated = false;
    bool helperTask = false;               // the scheduled task exists
    bool helperRunning = false;            // the helper is writing fresh numbers
    bool nvml = false, d3dkmt = false, hwinfo = false;
    std::wstring cpuNote;                  // why CPU temperature isn't available, if it isn't
};

namespace sensors {
void Init(HWND notify, const std::wstring& cpuVendor, int cpuFamily, int baseMhz);
void SetActive(bool active);               // start/stop polling
bool Active();
SensorSnapshot Latest();
SensorStatus Status();
void Shutdown();
void RefreshStatus();                      // re-check the driver and helper task (after setup)

// The elevated helper (Rigcard.exe --sensor-helper). Returns the process exit code.
int RunHelper();
// Elevated one-time setup (Rigcard.exe --setup-sensors): installs PawnIO if needed and creates the task.
int RunSetup();
// From the normal app: asks for admin once and runs the setup. Returns true when the helper is ready.
bool SetupHelper(HWND owner, std::wstring* message);
void RemoveHelperTask();                   // used by --uninstall
bool IsElevated();
bool PawnIoInstalled();
}

std::wstring FormatUptime(uint64_t secs);
