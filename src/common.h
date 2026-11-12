// Rigcard - your PC build as one clean, shareable card (part of the larosa023 suite)
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

#ifndef RIGCARD_VERSION
#define RIGCARD_VERSION L"1.0.0"
#endif
#define RIGCARD_REPO_OWNER L"larosa023"
#define RIGCARD_REPO_NAME L"Rigcard"
#define RIGCARD_APP_ID L"rigcard"
#define RIGCARD_SITE L"https://larosa023.github.io/Rigcard/"
#define RIGCARD_WINDOW_CLASS L"Rigcard.larosa023.Main"

// Messages posted from worker threads to the main window. lParam owns a heap object
// that the window procedure takes over and deletes.
enum : UINT {
    WM_RC_HARDWARE = WM_APP + 1,   // lParam: HwResult*
    WM_RC_SENSORS,                 // lParam: SensorSnapshot*
    WM_RC_BENCH,                   // lParam: BenchProgress*
    WM_RC_SHARE,                   // lParam: ShareResult*
    WM_RC_SELF,                    // lParam: SelfUpdateState*
    WM_RC_ACTIVATE,                // another Rigcard.exe asked us to come to the front
    WM_RC_HANGAR,                  // lParam: HangarCommand* (from the Hangar SDK pipe thread)
    WM_RC_IMPORT,                  // lParam: ImportResult*
};
