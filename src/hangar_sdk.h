// Rigcard - the Hangar SDK (C++ port of docs/IPC.md in larosa023/Hangar, protocol v1).
//
// Rigcard is a named-pipe server at \\.\pipe\larosa023.rigcard.<sessionId>, announces itself in
// %LOCALAPPDATA%\Hangar\run\rigcard.json, sends Hangar a "health" ping after it starts, says it's
// "busy" during benchmarks (so Hangar never restarts it mid-run), saves and restores its state across
// live updates (--restore-state <file>) and applies themes Hangar pushes, live.
// This file has no Rigcard-specific code apart from the app id; other C++ suite apps can copy it.
#pragma once
#include "common.h"
#include "json.h"

// Sent (SendMessage) from the pipe thread to the window; the window fills in `reply`.
struct HangarCommand {
    std::wstring type;          // status · prepare-update · restart · cancel-update · apply-theme · apply-settings · export-settings
    json::Value request;
    json::Value reply;
};

namespace hangar {
void Start(HWND window, const std::wstring& appId, const std::wstring& version);
void Stop();
void SendHealth();                                 // call once the window is up (~2 s after start)
void SendRestored(const std::wstring& summary);
void SendBusy(bool busy, const std::wstring& reason);
std::wstring RestoreStateArg(const wchar_t* cmdLine);   // --restore-state <file>, or ""
bool HangarInstalled();
}
