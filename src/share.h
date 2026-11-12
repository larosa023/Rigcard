// Rigcard - sharing.
//
// Two kinds of link, both opening the same static viewer on GitHub Pages
// (https://larosa023.github.io/Rigcard/):
//   Quick link   #d=<deflated card JSON>   no account, works instantly, no photo, a snapshot.
//   Hosted link  #g=<gist id>              the card lives in a secret GitHub gist you own. Updating
//                                          the build in Rigcard updates the same link; live stats are
//                                          pushed to the gist while Rigcard runs (if you turn it on).
// Privacy: serial numbers, IP address, Windows user name and location only leave your PC when
// you allow them in Settings → Privacy.
#pragma once
#include "common.h"
#include "json.h"
#include "card.h"
#include "sensors.h"
#include "bench.h"

struct ShareResult {
    enum Kind { Published, LivePushed, TokenChecked, Error } kind = Error;
    std::wstring url, gistId, owner, login, error;
    bool quiet = false;     // background live push: no toast
};

namespace share {
json::Value CardJson(const Build& b, const SensorSnapshot& live, bool liveRunning, const std::vector<BenchResult>& bench, bool withPhoto);
json::Value LiveJson(const SensorSnapshot& s);
std::wstring QuickLink(const Build& b, const SensorSnapshot& live, const std::vector<BenchResult>& bench);

bool Busy();
// photoJpeg: encoded photo (empty: none). Creates the gist the first time, then updates it.
void PublishAsync(HWND notify, const Build& b, const SensorSnapshot& live, const std::vector<BenchResult>& bench, const std::string& photoJpeg);
void PushLiveAsync(HWND notify, const std::wstring& gistId, const SensorSnapshot& live);
void CheckTokenAsync(HWND notify, const std::wstring& token);
void Unpublish(const std::wstring& gistId);     // deletes the gist (background)
}
