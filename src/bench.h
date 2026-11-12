// Rigcard - built-in benchmarks, imported scores and the history of both.
#pragma once
#include "common.h"

struct BenchResult {
    std::wstring id;          // cpu-single cpu-multi gpu mem-bw mem-lat disk-read disk-write disk-4k, or "imp:<slug>"
    std::wstring name;        // "CPU multi-core", "Cinebench 2024 Multi"
    double score = 0;
    std::wstring unit;        // "pts", "GFLOPS", "GB/s", "ns", "MB/s"
    std::wstring source;      // "Rigcard", "3DMark", "Geekbench", "CrystalDiskMark", "Manual"
    std::wstring detail;      // "C: · 1 GiB file", "24 threads"
    long long when = 0;       // unix seconds
    bool lowerIsBetter = false;
};

struct BenchProgress {
    bool done = false;
    std::wstring test;        // id of the running test
    std::wstring label;       // "CPU multi-core · 24 threads"
    float pct = 0;            // 0-100 overall
    std::vector<BenchResult> results;   // finished results (on done)
    std::wstring error;
};

struct ImportResult {
    std::vector<BenchResult> results;
    std::wstring error;
};

namespace bench {
const std::vector<std::wstring>& BuiltinTests();     // in run order
std::wstring TestName(const std::wstring& id);

bool Running();
void Start(HWND notify, const std::vector<std::wstring>& tests, const std::wstring& drive);
void Cancel();

void Load();
void Save();
const std::vector<BenchResult>& History();          // oldest first
void Add(const BenchResult& r);
void Remove(size_t index);
std::vector<BenchResult> Latest();                   // newest result per id, in display order
std::vector<BenchResult> Series(const std::wstring& id);   // every run of one test, oldest first

std::wstring FormatScore(const BenchResult& r);      // "23,456 pts", "41.2 TFLOPS", "72.4 GB/s"
std::wstring FormatNumber(double v, int decimals = 0);

// Imports
void ImportFileAsync(HWND notify, const std::wstring& path);       // .3dmark-result, CrystalDiskMark .txt, any text
void ImportUrlAsync(HWND notify, const std::wstring& url);         // a Geekbench result link
std::vector<BenchResult> ParseText(const std::wstring& text);      // pasted results
std::wstring Slug(const std::wstring& s);
}
