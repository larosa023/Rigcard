// Rigcard - app settings (DataDir()\settings.json). The build itself lives in build.json (card.h).
#pragma once
#include "common.h"

struct Config {
    // appearance (the app, not the card)
    std::wstring theme = L"dark";       // built-in theme id (dark, light, system, midnight, glass…) or custom:<id>
    std::wstring accent = L"theme";     // theme | windows | #RRGGBB
    std::wstring backdrop = L"theme";   // theme | none | mica | acrylic
    float fontScale = 1.0f;             // 0.9 · 1.0 · 1.1 · 1.25
    bool compact = false;               // density
    float radius = 8.0f;                // corner radius, 0-16
    float animSpeed = 1.0f;             // 0 (off) · 0.5 · 1 · 2
    bool followHangar = true;           // accept live theme changes pushed from Hangar
    // window
    int windowX = CW_USEDEFAULT, windowY = CW_USEDEFAULT, windowW = 0, windowH = 0;
    bool maximized = false;
    std::wstring page = L"card";
    // sensors
    int pollMs = 1000;
    bool sensorHelper = false;          // the elevated helper task is set up (CPU temperature + power)
    // sharing
    std::wstring tokenBlob;             // GitHub token, DPAPI-encrypted, hex
    std::wstring githubUser;
    bool liveShare = false;             // keep the shared page's live stats updated while Rigcard runs
    int liveSeconds = 60;
    // privacy (what the card and the shared page may show)
    bool showSerials = false, showIp = false, showUser = false, hideLocation = false;
    // updates
    bool beta = false;
    bool autoUpdate = true;             // install updates by itself when it won't interrupt
    int checkHours = 3;
    std::wstring skipVersion;           // versions that failed to start or were rolled back (comma-separated)
    // benchmarks
    std::wstring benchDrive;            // "C:" by default
};

extern Config g_cfg;
void ConfigLoad();
void ConfigSave();
bool IsSkippedVersion(const std::wstring& v);
void SkipVersion(const std::wstring& v);
void PruneSkippedVersions(const std::wstring& current);

// GitHub token, kept encrypted with Windows DPAPI (only this Windows user can read it)
std::wstring GetToken();
void SetToken(const std::wstring& token);
