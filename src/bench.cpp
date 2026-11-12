// Rigcard - benchmarks, imports and history
#include "bench.h"
#include "bench_kernels.h"
#include "util.h"
#include "net.h"
#include "json.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <ctime>
#include <random>
#include <algorithm>
#include <map>

namespace bench {

static std::atomic<bool> s_running{false}, s_cancel{false};
static std::vector<BenchResult> s_history;
static std::mutex s_lock;

const std::vector<std::wstring>& BuiltinTests() {
    static std::vector<std::wstring> t = {L"cpu-single", L"cpu-multi", L"gpu", L"mem-bw", L"mem-lat", L"disk-read", L"disk-write", L"disk-4k"};
    return t;
}

std::wstring TestName(const std::wstring& id) {
    static const wchar_t* m[][2] = {{L"cpu-single", L"CPU single-core"}, {L"cpu-multi", L"CPU multi-core"}, {L"gpu", L"GPU compute"},
                                    {L"mem-bw", L"Memory bandwidth"}, {L"mem-lat", L"Memory latency"}, {L"disk-read", L"Storage read"},
                                    {L"disk-write", L"Storage write"}, {L"disk-4k", L"Storage 4K random"}};
    for (auto& x : m) if (id == x[0]) return x[1];
    return id;
}

std::wstring FormatNumber(double v, int decimals) {
    wchar_t b[64];
    swprintf(b, 64, L"%.*f", decimals, v);
    std::wstring s = b;
    size_t dot = s.find(L'.');
    size_t end = dot == std::wstring::npos ? s.size() : dot;
    int start = (s[0] == L'-') ? 1 : 0;
    for (int i = (int)end - 3; i > start; i -= 3) s.insert(i, L",");
    return s;
}

std::wstring FormatScore(const BenchResult& r) {
    if (r.unit == L"GFLOPS" && r.score >= 1000) return FormatNumber(r.score / 1000, 1) + L" TFLOPS";
    if (r.unit == L"GB/s" || r.unit == L"GFLOPS") return FormatNumber(r.score, 1) + L" " + r.unit;
    if (r.unit == L"ns") return FormatNumber(r.score, 1) + L" ns";
    return FormatNumber(r.score, 0) + (r.unit.empty() ? L"" : L" " + r.unit);
}

std::wstring Slug(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : Lower(s)) {
        if (iswalnum(c)) out += c;
        else if (!out.empty() && out.back() != L'-') out += L'-';
    }
    while (!out.empty() && out.back() == L'-') out.pop_back();
    return out;
}

bool Running() { return s_running; }
void Cancel() { s_cancel = true; }

// ---------------------------------------------------------------- history
static std::wstring HistoryPath() { return DataDir() + L"\\benchmarks.json"; }

void Load() {
    std::lock_guard<std::mutex> g(s_lock);
    s_history.clear();
    std::wstring text;
    json::Value v;
    if (!ReadTextFile(HistoryPath(), text) || !json::Parser(text).Parse(v)) return;
    const json::Value* list = v.Get(L"results");
    if (!list || list->type != json::Value::Array) return;
    for (auto& o : list->arr) {
        BenchResult r;
        r.id = o.GetStr(L"id");
        r.name = o.GetStr(L"name");
        r.score = o.GetNum(L"score", 0);
        r.unit = o.GetStr(L"unit");
        r.source = o.GetStr(L"source");
        r.detail = o.GetStr(L"detail");
        r.when = (long long)o.GetNum(L"when", 0);
        r.lowerIsBetter = o.GetBool(L"lowerIsBetter", false);
        if (!r.id.empty()) s_history.push_back(r);
    }
}

static void SaveLocked() {
    using json::Value;
    Value v = Value::MakeObject();
    v.Set(L"schema", Value::Num(1));
    Value list = Value::MakeArray();
    for (auto& r : s_history) {
        Value o = Value::MakeObject();
        o.Set(L"id", Value::Str(r.id));
        o.Set(L"name", Value::Str(r.name));
        o.Set(L"score", Value::Num(r.score));
        o.Set(L"unit", Value::Str(r.unit));
        o.Set(L"source", Value::Str(r.source));
        o.Set(L"detail", Value::Str(r.detail));
        o.Set(L"when", Value::Num((double)r.when));
        if (r.lowerIsBetter) o.Set(L"lowerIsBetter", Value::Boolean(true));
        list.arr.push_back(o);
    }
    v.Set(L"results", list);
    std::wstring out;
    json::Write(v, out);
    WriteTextFileAtomic(HistoryPath(), out);
}

void Save() { std::lock_guard<std::mutex> g(s_lock); SaveLocked(); }

const std::vector<BenchResult>& History() { return s_history; }

void Add(const BenchResult& r0) {
    BenchResult r = r0;
    if (!r.when) r.when = (long long)time(nullptr);
    std::lock_guard<std::mutex> g(s_lock);
    s_history.push_back(r);
    SaveLocked();
}

void Remove(size_t i) {
    std::lock_guard<std::mutex> g(s_lock);
    if (i < s_history.size()) { s_history.erase(s_history.begin() + i); SaveLocked(); }
}

std::vector<BenchResult> Latest() {
    std::map<std::wstring, BenchResult> last;
    for (auto& r : s_history) last[r.id] = r;
    std::vector<BenchResult> out;
    for (auto& id : BuiltinTests()) if (last.count(id)) { out.push_back(last[id]); last.erase(id); }
    std::vector<BenchResult> rest;
    for (auto& kv : last) rest.push_back(kv.second);
    std::sort(rest.begin(), rest.end(), [](const BenchResult& a, const BenchResult& b) { return a.when > b.when; });
    out.insert(out.end(), rest.begin(), rest.end());
    return out;
}

std::vector<BenchResult> Series(const std::wstring& id) {
    std::vector<BenchResult> out;
    for (auto& r : s_history) if (r.id == id) out.push_back(r);
    return out;
}

// ---------------------------------------------------------------- runner
struct Runner {
    HWND notify;
    std::vector<std::wstring> tests;
    std::wstring drive;
    std::vector<BenchResult> results;
    size_t index = 0;

    void Progress(const std::wstring& id, const std::wstring& label, float within) {
        auto* p = new BenchProgress;
        p->test = id;
        p->label = label;
        p->pct = (index + std::clamp(within, 0.0f, 1.0f)) * 100.0f / std::max<size_t>(1, tests.size());
        if (!PostMessageW(notify, WM_RC_BENCH, 0, (LPARAM)p)) delete p;
    }
    BenchResult Make(const std::wstring& id, double score, const std::wstring& unit, const std::wstring& detail, bool lower = false) {
        BenchResult r;
        r.id = id; r.name = TestName(id); r.score = score; r.unit = unit; r.source = L"Rigcard"; r.detail = detail;
        r.when = (long long)time(nullptr); r.lowerIsBetter = lower;
        return r;
    }
    static double Now() {
        LARGE_INTEGER t, f;
        QueryPerformanceCounter(&t);
        QueryPerformanceFrequency(&f);
        return (double)t.QuadPart / f.QuadPart;
    }

    // ---- CPU
    bool Cpu(bool multi) {
        int threads = 1;
        if (multi) {
            DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
            threads = (int)std::max<DWORD>(1, n);
        }
        const double warm = 1.0, run = multi ? 8.0 : 6.0;
        std::atomic<uint64_t> units{0};
        std::atomic<bool> counting{false}, stop{false};
        std::vector<std::thread> pool;
        std::atomic<uint64_t> sink{0};
        for (int t = 0; t < threads; ++t) {
            pool.emplace_back([&, t] {
                if (multi) {
                    // spread across processor groups on >64-thread machines
                    WORD groups = GetActiveProcessorGroupCount();
                    if (groups > 1) {
                        int g = 0, idx = t;
                        while (g < groups && idx >= (int)GetActiveProcessorCount(g)) { idx -= GetActiveProcessorCount(g); ++g; }
                        GROUP_AFFINITY ga{};
                        ga.Group = (WORD)g;
                        ga.Mask = (KAFFINITY)1 << idx;
                        SetThreadGroupAffinity(GetCurrentThread(), &ga, nullptr);
                    }
                }
                uint64_t local = 0, acc = 0, seed = (uint64_t)t * 1000003;
                while (!stop) {
                    acc ^= kernels::CpuUnit(seed++);
                    if (counting) ++local;
                    if ((seed & 15) == 0 && local) { units += local; local = 0; }
                }
                units += local;
                sink ^= acc;
            });
        }
        double t0 = Now();
        std::wstring label = multi ? TestName(L"cpu-multi") + L" · " + std::to_wstring(threads) + L" threads" : TestName(L"cpu-single");
        while (Now() - t0 < warm && !s_cancel) { Progress(multi ? L"cpu-multi" : L"cpu-single", label, (float)((Now() - t0) / (warm + run))); Sleep(100); }
        units = 0;
        counting = true;
        double t1 = Now();
        while (Now() - t1 < run && !s_cancel) { Progress(multi ? L"cpu-multi" : L"cpu-single", label, (float)((Now() - t0) / (warm + run))); Sleep(150); }
        double secs = Now() - t1;
        stop = true;
        for (auto& th : pool) th.join();
        if (s_cancel) return false;
        double score = units / secs * kernels::kPointsPerUnitPerSec;
        results.push_back(Make(multi ? L"cpu-multi" : L"cpu-single", score, L"pts", multi ? std::to_wstring(threads) + L" threads" : L"1 thread"));
        return true;
    }

    // ---- memory
    bool MemBandwidth() {
        const size_t size = 256u * 1024 * 1024;
        char* a = (char*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        char* b = (char*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!a || !b) { if (a) VirtualFree(a, 0, MEM_RELEASE); if (b) VirtualFree(b, 0, MEM_RELEASE); return true; }
        memset(a, 1, size);
        memset(b, 2, size);
        // copy with several threads at once, like real workloads that saturate memory
        int threads = (int)std::min<DWORD>(8, std::max<DWORD>(1, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS) / 2));
        double best = 0;
        for (int pass = 0; pass < 6 && !s_cancel; ++pass) {
            Progress(L"mem-bw", TestName(L"mem-bw"), pass / 6.0f);
            std::vector<std::thread> pool;
            double t0 = Now();
            for (int t = 0; t < threads; ++t) {
                pool.emplace_back([=] {
                    size_t chunk = size / threads;
                    for (int rep = 0; rep < 2; ++rep) memcpy(b + t * chunk, a + t * chunk, chunk);
                });
            }
            for (auto& th : pool) th.join();
            double secs = Now() - t0;
            double gbs = (2.0 * 2 * size) / secs / 1e9;   // read + write, twice
            best = std::max(best, gbs);
        }
        VirtualFree(a, 0, MEM_RELEASE);
        VirtualFree(b, 0, MEM_RELEASE);
        if (s_cancel) return false;
        results.push_back(Make(L"mem-bw", best, L"GB/s", std::to_wstring(threads) + L" threads, copy"));
        return true;
    }

    bool MemLatency() {
        const size_t lines = 4u * 1024 * 1024;   // 256 MB of 64-byte lines: far bigger than any cache
        struct Line { size_t next; char pad[56]; };
        Line* m = (Line*)VirtualAlloc(nullptr, lines * sizeof(Line), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!m) return true;
        Progress(L"mem-lat", TestName(L"mem-lat"), 0.05f);
        // one random cycle through every line (Sattolo's algorithm)
        std::vector<uint32_t> order(lines);
        for (size_t i = 0; i < lines; ++i) order[i] = (uint32_t)i;
        std::mt19937_64 rng(42);
        for (size_t i = lines - 1; i > 0; --i) { size_t j = rng() % i; std::swap(order[i], order[j]); }
        for (size_t i = 0; i < lines; ++i) m[order[i]].next = order[(i + 1) % lines];
        Progress(L"mem-lat", TestName(L"mem-lat"), 0.3f);
        size_t p = 0;
        const size_t hops = 20u * 1000 * 1000;
        double t0 = Now();
        for (size_t i = 0; i < hops; ++i) {
            p = m[p].next;
            if ((i & 0xFFFFF) == 0) { if (s_cancel) break; Progress(L"mem-lat", TestName(L"mem-lat"), 0.3f + 0.7f * i / hops); }
        }
        double ns = (Now() - t0) / hops * 1e9;
        volatile size_t keep = p;
        (void)keep;
        VirtualFree(m, 0, MEM_RELEASE);
        if (s_cancel) return false;
        results.push_back(Make(L"mem-lat", ns, L"ns", L"random reads, 256 MB", true));
        return true;
    }

    // ---- storage
    std::wstring BenchFile() {
        std::wstring d = drive.empty() ? L"C:" : drive.substr(0, 2);
        wchar_t sys[MAX_PATH];
        GetWindowsDirectoryW(sys, MAX_PATH);
        if (_wcsnicmp(sys, d.c_str(), 2) == 0) {
            wchar_t tmp[MAX_PATH];
            GetTempPathW(MAX_PATH, tmp);
            return std::wstring(tmp) + L"rigcard-bench.tmp";
        }
        return d + L"\\rigcard-bench.tmp";
    }

    bool Disk(bool doRead, bool doWrite, bool do4k) {
        const DWORD block = 1u << 20;
        const int blocks = 1024;                  // 1 GiB
        std::wstring path = BenchFile();
        std::wstring label = (drive.empty() ? std::wstring(L"C:") : drive.substr(0, 2));
        BYTE* buf = (BYTE*)VirtualAlloc(nullptr, block, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!buf) return true;
        std::mt19937 rng(7);
        for (DWORD i = 0; i < block / 4; ++i) ((uint32_t*)buf)[i] = rng();   // incompressible
        // write (also creates the file the read tests use)
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH | FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (h == INVALID_HANDLE_VALUE) { Log(L"disk bench: can't write %ls (%lu)", path.c_str(), GetLastError()); VirtualFree(buf, 0, MEM_RELEASE); return true; }
        double t0 = Now();
        bool ok = true;
        for (int i = 0; i < blocks && ok; ++i) {
            DWORD w = 0;
            ok = WriteFile(h, buf, block, &w, nullptr) && w == block;
            if ((i & 31) == 0) { if (s_cancel) ok = false; Progress(L"disk-write", TestName(L"disk-write") + L" · " + label, 0.33f * i / blocks); }
        }
        double wsecs = Now() - t0;
        CloseHandle(h);
        if (ok && doWrite) results.push_back(Make(L"disk-write", blocks * (double)block / wsecs / 1e6, L"MB/s", label + L" · 1 GiB sequential"));
        if (ok && doRead) {
            h = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                t0 = Now();
                int i = 0;
                for (; i < blocks; ++i) {
                    DWORD r = 0;
                    if (!ReadFile(h, buf, block, &r, nullptr) || r != block) break;
                    if ((i & 31) == 0) { if (s_cancel) break; Progress(L"disk-read", TestName(L"disk-read") + L" · " + label, 0.33f + 0.33f * i / blocks); }
                }
                double rsecs = Now() - t0;
                CloseHandle(h);
                if (i == blocks) results.push_back(Make(L"disk-read", blocks * (double)block / rsecs / 1e6, L"MB/s", label + L" · 1 GiB sequential"));
            }
        }
        if (ok && do4k && !s_cancel) {
            h = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_RANDOM_ACCESS, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                const double run = 3.0;
                uint64_t ops = 0;
                t0 = Now();
                while (Now() - t0 < run && !s_cancel) {
                    LARGE_INTEGER off;
                    off.QuadPart = (int64_t)(rng() % (blocks * (block / 4096))) * 4096;
                    SetFilePointerEx(h, off, nullptr, FILE_BEGIN);
                    DWORD r = 0;
                    if (!ReadFile(h, buf, 4096, &r, nullptr)) break;
                    if ((++ops & 1023) == 0) Progress(L"disk-4k", TestName(L"disk-4k") + L" · " + label, 0.66f + 0.34f * (float)((Now() - t0) / run));
                }
                double secs = Now() - t0;
                CloseHandle(h);
                if (!s_cancel && ops) results.push_back(Make(L"disk-4k", ops * 4096.0 / secs / 1e6, L"MB/s",
                                                             label + L" · " + FormatNumber(ops / secs) + L" IOPS, QD1"));
            }
        }
        DeleteFileW(path.c_str());
        VirtualFree(buf, 0, MEM_RELEASE);
        return !s_cancel;
    }

    // ---- GPU (D3D11 compute)
    bool Gpu() {
        Progress(L"gpu", TestName(L"gpu"), 0.02f);
        HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
        auto compile = dc ? (decltype(&D3DCompile))GetProcAddress(dc, "D3DCompile") : nullptr;
        if (!compile) { Log(L"gpu bench: d3dcompiler_47.dll missing"); return true; }
        static const char* hlsl =
            "RWBuffer<float> o : register(u0);\n"
            "cbuffer C : register(b0) { float k; float3 pad; };\n"
            "[numthreads(256,1,1)] void main(uint3 id : SV_DispatchThreadID) {\n"
            "  float a = id.x * 1e-7 + k, b = 1.0001, c = 0.9999, d = a * 0.5;\n"
            "  [loop] for (int i = 0; i < 1024; i++) { a = a * b + c; d = d * c + b; a = a * c + d; d = d * b + a; }\n"
            "  o[id.x] = a + d;\n"
            "}\n";
        ID3DBlob* code = nullptr;
        ID3DBlob* err = nullptr;
        if (FAILED(compile(hlsl, strlen(hlsl), "rigcard", nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err))) {
            if (err) { Log(L"gpu bench: %hs", (const char*)err->GetBufferPointer()); err->Release(); }
            return true;
        }
        // the adapter with the most VRAM
        IDXGIFactory1* f = nullptr;
        IDXGIAdapter1* best = nullptr;
        std::wstring gpuName;
        if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f))) {
            IDXGIAdapter1* a = nullptr;
            SIZE_T vram = 0;
            for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_ADAPTER_DESC1 d;
                if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && (!best || d.DedicatedVideoMemory > vram)) {
                    if (best) best->Release();
                    best = a; vram = d.DedicatedVideoMemory; gpuName = d.Description;
                    continue;
                }
                a->Release();
            }
            f->Release();
        }
        ID3D11Device* dev = nullptr;
        ID3D11DeviceContext* ctx = nullptr;
        D3D_FEATURE_LEVEL fl;
        HRESULT hr = D3D11CreateDevice(best, best ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx);
        if (best) best->Release();
        if (FAILED(hr)) { code->Release(); return true; }
        ID3D11ComputeShader* cs = nullptr;
        dev->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &cs);
        code->Release();
        const UINT groups = 16384, threadsPer = 256, n = groups * threadsPer;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = n * 4;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        ID3D11Buffer* buf = nullptr;
        dev->CreateBuffer(&bd, nullptr, &buf);
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = DXGI_FORMAT_R32_FLOAT;
        ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = n;
        ID3D11UnorderedAccessView* uav = nullptr;
        if (buf) dev->CreateUnorderedAccessView(buf, &ud, &uav);
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = 16;
        cb.Usage = D3D11_USAGE_DEFAULT;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        float k[4] = {0.25f, 0, 0, 0};
        D3D11_SUBRESOURCE_DATA init{k, 0, 0};
        ID3D11Buffer* cbuf = nullptr;
        dev->CreateBuffer(&cb, &init, &cbuf);
        D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0};
        ID3D11Query* done = nullptr;
        dev->CreateQuery(&qd, &done);
        bool ok = cs && uav && cbuf && done;
        double gflops = 0;
        if (ok) {
            ctx->CSSetShader(cs, nullptr, 0);
            ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
            ctx->CSSetConstantBuffers(0, 1, &cbuf);
            auto runBatch = [&](int dispatches) {
                for (int i = 0; i < dispatches; ++i) ctx->Dispatch(groups, 1, 1);
                ctx->End(done);
                while (ctx->GetData(done, nullptr, 0, 0) == S_FALSE) { if (s_cancel) return false; Sleep(1); }
                return true;
            };
            runBatch(2);   // warm up and let clocks ramp
            // size a batch to ~0.25 s, then measure for ~4 s
            int batch = 1;
            double t0 = Now();
            runBatch(batch);
            double one = std::max(1e-4, Now() - t0);
            batch = std::clamp((int)(0.25 / one), 1, 4000);
            double flopsPerDispatch = (double)n * 1024 * 4 * 2;
            double total = 0, start = Now();
            int dispatches = 0;
            while (Now() - start < 4.0 && !s_cancel) {
                if (!runBatch(batch)) break;
                dispatches += batch;
                total = Now() - start;
                Progress(L"gpu", TestName(L"gpu"), 0.05f + 0.95f * (float)(total / 4.0));
            }
            if (dispatches && total > 0) gflops = dispatches * flopsPerDispatch / total / 1e9;
        }
        if (done) done->Release();
        if (cbuf) cbuf->Release();
        if (uav) uav->Release();
        if (buf) buf->Release();
        if (cs) cs->Release();
        ctx->Release();
        dev->Release();
        if (s_cancel) return false;
        if (gflops > 0) results.push_back(Make(L"gpu", gflops, L"GFLOPS", Trim(gpuName) + L" · FP32 FMA"));
        return true;
    }

    void Run() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        bool diskDone = false;
        for (index = 0; index < tests.size() && !s_cancel; ++index) {
            const std::wstring& t = tests[index];
            Progress(t, TestName(t), 0);
            if (t == L"cpu-single") Cpu(false);
            else if (t == L"cpu-multi") Cpu(true);
            else if (t == L"gpu") Gpu();
            else if (t == L"mem-bw") MemBandwidth();
            else if (t == L"mem-lat") MemLatency();
            else if ((t == L"disk-read" || t == L"disk-write" || t == L"disk-4k") && !diskDone) {
                bool r = std::find(tests.begin(), tests.end(), L"disk-read") != tests.end();
                bool w = std::find(tests.begin(), tests.end(), L"disk-write") != tests.end();
                bool k = std::find(tests.begin(), tests.end(), L"disk-4k") != tests.end();
                Disk(r, w, k);
                diskDone = true;
            }
        }
        auto* p = new BenchProgress;
        p->done = true;
        p->pct = 100;
        p->results = results;
        if (s_cancel) p->error = L"Benchmark cancelled.";
        if (!PostMessageW(notify, WM_RC_BENCH, 0, (LPARAM)p)) delete p;
    }
};

void Start(HWND notify, const std::vector<std::wstring>& tests, const std::wstring& drive) {
    if (s_running.exchange(true)) return;
    s_cancel = false;
    auto* r = new Runner{notify, tests, drive, {}, 0};
    std::thread([r] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        r->Run();
        for (auto& res : r->results) Add(res);
        delete r;
        s_running = false;
        CoUninitialize();
    }).detach();
}

// ---------------------------------------------------------------- imports
static BenchResult Imported(const std::wstring& name, double score, const std::wstring& unit, const std::wstring& source) {
    BenchResult r;
    r.id = L"imp:" + Slug(name);
    r.name = name;
    r.score = score;
    r.unit = unit;
    r.source = source;
    r.when = (long long)time(nullptr);
    return r;
}

static double ParseNum(const std::wstring& s) {
    std::wstring d;
    for (wchar_t c : s) {
        if (iswdigit(c) || c == L'.') d += c;
        else if (c == L',' || c == L' ' || c == 0xA0 || c == 0x202F) continue;
        else if (!d.empty()) break;
    }
    return d.empty() ? -1 : wcstod(d.c_str(), nullptr);
}

// "CamelCaseName" → "Camel Case Name"
static std::wstring SplitCamel(const std::wstring& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i && iswupper(s[i]) && (iswlower(s[i - 1]) || (i + 1 < s.size() && iswlower(s[i + 1]) && iswupper(s[i - 1])))) out += L' ';
        out += s[i];
    }
    return out;
}

static std::vector<BenchResult> Parse3DMarkXml(const std::wstring& xml) {
    // <TimeSpyPerformance3DMarkScore>17234</...>, <...GraphicsScore>, <...CPUScore>
    std::vector<BenchResult> out;
    std::map<std::wstring, bool> seen;
    size_t i = 0;
    while ((i = xml.find(L'<', i)) != std::wstring::npos) {
        size_t e = xml.find(L'>', i);
        if (e == std::wstring::npos) break;
        std::wstring tag = xml.substr(i + 1, e - i - 1);
        i = e + 1;
        if (tag.empty() || tag[0] == L'/' || tag.find(L' ') != std::wstring::npos) continue;
        bool overall = tag.size() > 11 && tag.compare(tag.size() - 11, 11, L"3DMarkScore") == 0;
        bool gfx = tag.size() > 13 && tag.compare(tag.size() - 13, 13, L"GraphicsScore") == 0;
        bool cpu = tag.size() > 8 && tag.compare(tag.size() - 8, 8, L"CPUScore") == 0;
        if (!overall && !gfx && !cpu) continue;
        size_t close = xml.find(L"</" + tag + L">", i);
        if (close == std::wstring::npos) continue;
        double v = ParseNum(xml.substr(i, close - i));
        if (v <= 0) continue;
        std::wstring test = tag.substr(0, tag.size() - (overall ? 11 : gfx ? 13 : 8));
        for (const wchar_t* fluff : {L"Performance", L"Extreme", L"Custom"}) {
            size_t f = test.find(fluff);
            if (f != std::wstring::npos && f > 0) { if (std::wstring(fluff) != L"Extreme") test.erase(f, wcslen(fluff)); }
        }
        std::wstring name = L"3DMark " + SplitCamel(test) + (overall ? L"" : gfx ? L" Graphics" : L" CPU");
        if (seen[name]) continue;
        seen[name] = true;
        out.push_back(Imported(name, v, L"", L"3DMark"));
    }
    return out;
}

std::vector<BenchResult> ParseText(const std::wstring& text0) {
    std::vector<BenchResult> out;
    std::wstring text = text0;
    std::vector<std::wstring> lines;
    for (size_t i = 0; i <= text.size();) {
        size_t e = text.find(L'\n', i);
        if (e == std::wstring::npos) e = text.size();
        lines.push_back(Trim(text.substr(i, e - i)));
        i = e + 1;
    }
    // CrystalDiskMark "Copy" text: [Read] / [Write] sections with "SEQ1M Q8T1 :  7084.613 MB/s"
    bool cdm = IContains(text, L"CrystalDiskMark");
    if (cdm) {
        std::wstring section;
        for (auto& l : lines) {
            if (IStartsWith(l, L"[Read]")) { section = L"Read"; continue; }
            if (IStartsWith(l, L"[Write]")) { section = L"Write"; continue; }
            if (IStartsWith(l, L"[Mix]")) { section.clear(); continue; }
            size_t colon = l.find(L':');
            if (section.empty() || colon == std::wstring::npos || !IContains(l, L"MB/s")) continue;
            std::wstring test = Trim(l.substr(0, colon));
            double v = ParseNum(l.substr(colon + 1));
            if (v > 0 && (IStartsWith(test, L"SEQ1M Q8T1") || IStartsWith(test, L"RND4K Q1T1")))
                out.push_back(Imported(L"CrystalDiskMark " + test + L" " + section, v, L"MB/s", L"CrystalDiskMark"));
        }
        return out;
    }
    // Known names followed by a number on the same or the next line
    struct Known { const wchar_t* match; const wchar_t* name; const wchar_t* unit; const wchar_t* source; };
    static const Known known[] = {
        {L"cinebench 2024 multi", L"Cinebench 2024 Multi", L"pts", L"Cinebench"}, {L"cinebench 2024 single", L"Cinebench 2024 Single", L"pts", L"Cinebench"},
        {L"cinebench r23 multi", L"Cinebench R23 Multi", L"pts", L"Cinebench"}, {L"cinebench r23 single", L"Cinebench R23 Single", L"pts", L"Cinebench"},
        {L"multi core", L"Cinebench Multi", L"pts", L"Cinebench"}, {L"single core", L"Cinebench Single", L"pts", L"Cinebench"},
        {L"multi-core score", L"Geekbench 6 Multi", L"", L"Geekbench"}, {L"single-core score", L"Geekbench 6 Single", L"", L"Geekbench"},
        {L"time spy extreme", L"3DMark Time Spy Extreme", L"", L"3DMark"}, {L"time spy", L"3DMark Time Spy", L"", L"3DMark"},
        {L"port royal", L"3DMark Port Royal", L"", L"3DMark"}, {L"steel nomad", L"3DMark Steel Nomad", L"", L"3DMark"},
        {L"speed way", L"3DMark Speed Way", L"", L"3DMark"}, {L"fire strike", L"3DMark Fire Strike", L"", L"3DMark"},
        {L"passmark", L"PassMark", L"", L"PassMark"}, {L"superposition", L"Unigine Superposition", L"", L"Unigine"},
    };
    std::map<std::wstring, bool> seen;
    bool isCinebench2024 = IContains(text, L"2024"), isR23 = IContains(text, L"R23");
    for (size_t li = 0; li < lines.size(); ++li) {
        std::wstring low = Lower(lines[li]);
        for (auto& k : known) {
            if (low.find(k.match) == std::wstring::npos) continue;
            std::wstring rest = lines[li].substr(low.find(k.match) + wcslen(k.match));
            double v = ParseNum(rest);
            if (v <= 0 && li + 1 < lines.size()) v = ParseNum(lines[li + 1]);
            if (v <= 0) continue;
            std::wstring name = k.name;
            if (name == L"Cinebench Multi" || name == L"Cinebench Single")
                name = std::wstring(L"Cinebench ") + (isCinebench2024 ? L"2024 " : isR23 ? L"R23 " : L"") + (name == L"Cinebench Multi" ? L"Multi" : L"Single");
            if (seen[name]) break;
            seen[name] = true;
            out.push_back(Imported(name, v, k.unit, k.source));
            break;
        }
    }
    return out;
}

static void PostImport(HWND notify, ImportResult* r) {
    if (!PostMessageW(notify, WM_RC_IMPORT, 0, (LPARAM)r)) delete r;
}

void ImportFileAsync(HWND notify, const std::wstring& path) {
    std::thread([notify, path] {
        auto* res = new ImportResult;
        std::wstring lower = Lower(path);
        if (lower.size() > 14 && lower.compare(lower.size() - 14, 14, L".3dmark-result") == 0) {
            // it's a zip: Windows 10+ ships tar.exe, which extracts zips
            wchar_t tmp[MAX_PATH];
            GetTempPathW(MAX_PATH, tmp);
            std::wstring dir = std::wstring(tmp) + L"rigcard-3dmark-" + std::to_wstring(GetTickCount());
            EnsureDir(dir);
            wchar_t sys[MAX_PATH];
            GetSystemDirectoryW(sys, MAX_PATH);
            DWORD code = 1;
            RunAndWait(std::wstring(sys) + L"\\tar.exe", L"-xf \"" + path + L"\" -C \"" + dir + L"\"", 30000, &code, true);
            WIN32_FIND_DATAW fd;
            HANDLE f = FindFirstFileW((dir + L"\\*.xml").c_str(), &fd);
            if (f != INVALID_HANDLE_VALUE) {
                do {
                    std::wstring xml;
                    if (ReadTextFile(dir + L"\\" + fd.cFileName, xml)) {
                        auto found = Parse3DMarkXml(xml);
                        for (auto& r : found) {
                            bool dup = false;
                            for (auto& x : res->results) dup |= x.id == r.id;
                            if (!dup) res->results.push_back(r);
                        }
                    }
                    DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
                } while (FindNextFileW(f, &fd));
                FindClose(f);
            }
            // tidy (the archive may hold other files)
            f = FindFirstFileW((dir + L"\\*").c_str(), &fd);
            if (f != INVALID_HANDLE_VALUE) {
                do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir + L"\\" + fd.cFileName).c_str()); } while (FindNextFileW(f, &fd));
                FindClose(f);
            }
            RemoveDirectoryW(dir.c_str());
            if (res->results.empty()) res->error = L"No scores found in that 3DMark result file.";
        } else {
            std::wstring text;
            if (!ReadTextFile(path, text)) res->error = L"Couldn't read that file.";
            else {
                res->results = lower.size() > 4 && lower.compare(lower.size() - 4, 4, L".xml") == 0 ? Parse3DMarkXml(text) : ParseText(text);
                if (res->results.empty()) res->error = L"No benchmark scores found in that file.";
            }
        }
        PostImport(notify, res);
    }).detach();
}

void ImportUrlAsync(HWND notify, const std::wstring& url) {
    std::thread([notify, url] {
        auto* res = new ImportResult;
        if (!IContains(url, L"browser.geekbench.com")) {
            res->error = L"Paste a Geekbench result link (browser.geekbench.com/v6/cpu/…).";
            PostImport(notify, res);
            return;
        }
        net::Response r;
        if (!net::Get(url, r) || r.status != 200) {
            res->error = r.error.empty() ? L"Geekbench answered with HTTP " + std::to_wstring(r.status) + L"." : r.error;
            PostImport(notify, res);
            return;
        }
        std::wstring html = Utf8ToWide(r.body);
        // the result page lists "<div class='score'>2345</div>" … "<div class='note'>Single-Core Score</div>"
        std::vector<double> scores;
        std::vector<std::wstring> notes;
        size_t i = 0;
        while (true) {
            size_t a = html.find(L"class='score'", i), b = html.find(L"class=\"score\"", i);
            i = std::min(a, b);
            if (i == std::wstring::npos) break;
            size_t gt = html.find(L'>', i);
            if (gt == std::wstring::npos) break;
            scores.push_back(ParseNum(html.substr(gt + 1, 20)));
            size_t n = html.find(L"class='note'", gt);
            if (n == std::wstring::npos) n = html.find(L"class=\"note\"", gt);
            if (n != std::wstring::npos) {
                size_t ngt = html.find(L'>', n), nend = html.find(L'<', ngt);
                notes.push_back(ngt != std::wstring::npos && nend != std::wstring::npos ? Trim(html.substr(ngt + 1, nend - ngt - 1)) : L"");
            } else {
                notes.push_back(L"");
            }
            i = gt;
        }
        std::wstring ver = IContains(url, L"/v5/") ? L"5" : L"6";
        bool gpu = IContains(url, L"/compute/");
        for (size_t k = 0; k < scores.size(); ++k) {
            if (scores[k] <= 0) continue;
            std::wstring note = notes[k];
            std::wstring name = L"Geekbench " + ver + L" " + (gpu ? (note.empty() ? L"GPU" : note) : (IContains(note, L"Multi") ? L"Multi" : IContains(note, L"Single") ? L"Single" : note));
            res->results.push_back(Imported(Trim(name), scores[k], L"", L"Geekbench"));
            if (res->results.size() >= 3) break;
        }
        if (res->results.empty()) res->error = L"Couldn't find scores on that Geekbench page.";
        PostImport(notify, res);
    }).detach();
}

} // namespace bench
