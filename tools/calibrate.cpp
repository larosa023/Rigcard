// Native calibration of the CPU kernel: prints units/second on this machine.
// g++ -O2 -std=c++17 tools/calibrate.cpp -o /tmp/cal && /tmp/cal
#include "../src/bench_kernels.h"
#include <chrono>
#include <cstdio>
int main() {
    using clk = std::chrono::steady_clock;
    uint64_t acc = 0, n = 0;
    auto t0 = clk::now();
    while (std::chrono::duration<double>(clk::now() - t0).count() < 3.0) acc ^= kernels::CpuUnit(n++);
    double secs = std::chrono::duration<double>(clk::now() - t0).count();
    printf("units/s %.0f  points %.0f  (%llx)\n", n / secs, n / secs * kernels::kPointsPerUnitPerSec, (unsigned long long)acc);
}
