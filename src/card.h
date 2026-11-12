// Rigcard - the build (parts, photo, card style, sections) and the JSON that gets shared.
#pragma once
#include "common.h"
#include "json.h"

struct Part {
    std::wstring key;               // stable id: "cpu", "gpu:0", "storage:1", "manual:3"
    std::wstring kind;              // cpu gpu motherboard ram storage cooler psu case fans monitor keyboard mouse headset mic os other
    std::wstring name;              // shown on the card (you can rename it)
    std::wstring detail;            // second line ("12 cores · 24 threads")
    std::wstring detected;          // what detection found ("" for parts you added)
    std::wstring detectedDetail;
    std::wstring serial;            // never shown unless you allow it
    bool manual = false;
    bool hidden = false;            // hidden from the card
};

struct CardStyle {
    std::wstring id = L"midnight", name = L"Midnight";
    std::wstring background = L"gradient";   // gradient | solid | photo (blurred build photo)
    uint32_t bg1 = 0x0B0D14, bg2 = 0x1A1F33;
    float angle = 135;                        // gradient direction, degrees
    uint32_t panel = 0xFFFFFF;
    float panelOpacity = 0.05f;
    uint32_t text = 0xF4F6FB, subtext = 0x9AA3B8, accent = 0x6C8CFF, accent2 = 0xB06CFF;
    std::wstring font = L"Segoe UI Variable Text", headFont = L"Segoe UI Variable Display";
    float radius = 18;
    bool glow = false;                        // neon glow on accents
    bool border = true;                       // hairline border on panels
    std::wstring layout = L"classic";         // classic | showcase (bigger photo) | minimal (no panels)
    bool custom = false;
};

const std::vector<CardStyle>& CardPresets();
json::Value StyleToJson(const CardStyle& s);
bool StyleFromJson(const json::Value& v, CardStyle& s);
std::wstring StylesDir();                         // DataDir()\styles: saved card styles (JSON)
std::vector<CardStyle> LoadSavedStyles();
bool SaveStyleFile(const CardStyle& s, const std::wstring& path);
bool LoadStyleFile(const std::wstring& path, CardStyle& s, std::wstring* error);

struct Build {
    std::wstring name = L"My Build";
    std::wstring location;
    std::wstring photo;                           // full path of the card photo (DataDir()\photo.jpg) or ""
    float photoX = 0.5f, photoY = 0.5f;           // focus point, 0-1
    float photoZoom = 1.0f;                       // 1-4
    CardStyle style;
    bool showParts = true, showLive = true, showBench = true, showPhoto = true, showPeripherals = false;
    bool liveOnShare = true;                      // shared page: live stats (true) or a snapshot (false)
    std::vector<Part> parts;
    std::vector<std::wstring> hiddenBench;        // benchmark ids hidden from the card
    // sharing
    std::wstring gistId, gistOwner;
    long long sharedAt = 0;
    // facts that are private unless you allow them in Settings → Privacy
    std::wstring computerName, user, ip;
    bool scanned = false;                          // the first hardware scan has run
};

extern Build g_build;
void BuildLoad();
void BuildSave();
json::Value BuildToJson(const Build& b);
void BuildFromJson(const json::Value& v, Build& b);

// Kinds in card order, labels and grouping
const std::vector<std::wstring>& PartKinds();
std::wstring KindLabel(const std::wstring& kind);
bool IsPeripheral(const std::wstring& kind);
bool IsDetectable(const std::wstring& kind);   // Windows can find it (no picker needed)

// What the card shows for a part, with privacy applied
std::wstring PartDetailForCard(const Part& p);

// Share URL helpers
std::wstring HostedUrl(const std::wstring& gistId);
std::wstring LocationLine(const Build& b);      // location · OS, honoring "hide location"
