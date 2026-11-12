// Rigcard - benchmark kernels (portable C++, also compiled natively by tools/calibrate.cpp)
#pragma once
#include <cstdint>
#include <cmath>

namespace kernels {

// One "unit" of mixed CPU work: integer hashing, dependent floating point, a branchy
// Mandelbrot sample and a little sort. Returns a value so nothing is optimized away.
inline uint64_t CpuUnit(uint64_t seed) {
    uint64_t x = seed * 0x9E3779B97F4A7C15ull + 1;
    double a = 1.0 + (seed & 7) * 1e-3, b = 0.5;
    for (int i = 0; i < 2048; ++i) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        a = a * 1.0000001 + (double)(x & 1023) * 1e-9;
        b = b * 0.99991 + a * 1e-6;
    }
    // Mandelbrot: data-dependent branches
    int iters = 0;
    for (int p = 0; p < 24; ++p) {
        double cr = -0.75 + ((x >> (p & 31)) & 255) / 512.0, ci = 0.1 + ((x >> ((p + 7) & 31)) & 127) / 256.0;
        double zr = 0, zi = 0;
        int n = 0;
        while (n < 96 && zr * zr + zi * zi < 4.0) { double t = zr * zr - zi * zi + cr; zi = 2 * zr * zi + ci; zr = t; ++n; }
        iters += n;
    }
    // insertion sort of 48 keys
    uint32_t k[48];
    for (int i = 0; i < 48; ++i) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; k[i] = (uint32_t)x; }
    for (int i = 1; i < 48; ++i) { uint32_t v = k[i]; int j = i - 1; while (j >= 0 && k[j] > v) { k[j + 1] = k[j]; --j; } k[j + 1] = v; }
    return x ^ (uint64_t)(a * 1e6) ^ (uint64_t)(b * 1e9) ^ (uint64_t)iters ^ k[7] ^ k[40];
}

// Points per unit/second. Chosen so a 2023 desktop core (Ryzen 9 7900X, Core i7-13700K P-core)
// lands near 2,000 single-core points. Only comparisons between runs matter.
constexpr double kPointsPerUnitPerSec = 0.01;

} // namespace kernels
