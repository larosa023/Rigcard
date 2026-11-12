// Rigcard - GitHub releases (same conventions as Hangar: tag v1.2.3, pre-releases = Beta channel)
#pragma once
#include "common.h"

struct Asset {
    std::wstring name, url, sha256;   // sha256 from GitHub's asset digest (may be empty)
    uint64_t size = 0;
};

struct ReleaseInfo {
    std::wstring tag, version, title, notes, published, page;
    bool prerelease = false;
    std::vector<Asset> assets;
};

namespace releases {
// Newest first. Uses Hangar's shared release index (no API limit) and falls back to the GitHub API.
bool Fetch(const std::wstring& repo, std::vector<ReleaseInfo>& out, std::wstring* error);
bool Parse(const std::string& body, std::vector<ReleaseInfo>& out, std::wstring* error);
const Asset* FindAsset(const ReleaseInfo& r, const std::wstring& name);
std::wstring ExpectedHashFromSums(const std::string& sums, const std::wstring& assetName);
}
