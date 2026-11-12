// Rigcard - built-in catalog for the parts picker
#pragma once
#include "common.h"

namespace partsdb {
const std::vector<std::wstring>& Catalog(const std::wstring& kind);
std::vector<std::wstring> Search(const std::wstring& kind, const std::wstring& query, size_t max = 8);
}
