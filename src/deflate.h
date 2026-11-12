// Rigcard - a small raw-DEFLATE compressor (RFC 1951, fixed Huffman codes + LZ77) and base64.
// Used for quick share links: the web viewer inflates them with the browser's DecompressionStream.
// Portable C++ (tests compile it natively).
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace deflate {

class BitWriter {
public:
    void Bits(uint32_t v, int n) {
        acc_ |= (uint64_t)v << n_;
        n_ += n;
        while (n_ >= 8) { out.push_back((char)(acc_ & 0xFF)); acc_ >>= 8; n_ -= 8; }
    }
    void Huff(uint32_t code, int len) {   // Huffman codes go most-significant bit first
        uint32_t r = 0;
        for (int i = 0; i < len; ++i) r |= ((code >> i) & 1u) << (len - 1 - i);
        Bits(r, len);
    }
    void Flush() { if (n_) { out.push_back((char)(acc_ & 0xFF)); acc_ = 0; n_ = 0; } }
    std::string out;
private:
    uint64_t acc_ = 0;
    int n_ = 0;
};

inline void Literal(BitWriter& w, int v) {
    if (v < 144) w.Huff(0x30 + v, 8);
    else if (v < 256) w.Huff(0x190 + (v - 144), 9);
    else if (v < 280) w.Huff(v - 256, 7);
    else w.Huff(0xC0 + (v - 280), 8);
}

inline void Match(BitWriter& w, int len, int dist) {
    static const int lbase[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const int lextra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const int dbase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                4097, 6145, 8193, 12289, 16385, 24577};
    static const int dextra[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    int li = 28;
    while (li > 0 && lbase[li] > len) --li;
    Literal(w, 257 + li);
    if (lextra[li]) w.Bits(len - lbase[li], lextra[li]);
    int di = 29;
    while (di > 0 && dbase[di] > dist) --di;
    w.Huff(di, 5);
    if (dextra[di]) w.Bits(dist - dbase[di], dextra[di]);
}

inline std::string Compress(const std::string& in) {
    const int kWin = 32768, kHash = 1 << 15, kChain = 96, kMax = 258;
    std::vector<int> head(kHash, -1), prev(in.size() > 0 ? in.size() : 1, -1);
    const unsigned char* p = (const unsigned char*)in.data();
    int n = (int)in.size();
    auto hash = [&](int i) { return ((p[i] << 10) ^ (p[i + 1] << 5) ^ p[i + 2]) & (kHash - 1); };
    BitWriter w;
    w.Bits(1, 1);   // BFINAL
    w.Bits(1, 2);   // BTYPE = fixed Huffman
    int i = 0;
    while (i < n) {
        int bestLen = 0, bestDist = 0;
        if (i + 2 < n) {
            int h = hash(i);
            int cand = head[h], chain = 0;
            while (cand >= 0 && i - cand <= kWin && chain++ < kChain) {
                int l = 0, maxl = std::min(kMax, n - i);
                while (l < maxl && p[cand + l] == p[i + l]) ++l;
                if (l > bestLen) { bestLen = l; bestDist = i - cand; if (l == maxl) break; }
                cand = prev[cand];
            }
            prev[i] = head[h];
            head[h] = i;
        }
        if (bestLen >= 3) {
            Match(w, bestLen, bestDist);
            for (int k = 1; k < bestLen; ++k) {
                int j = i + k;
                if (j + 2 < n) { int h = hash(j); prev[j] = head[h]; head[h] = j; }
            }
            i += bestLen;
        } else {
            Literal(w, p[i]);
            ++i;
        }
    }
    Literal(w, 256);   // end of block
    w.Flush();
    return w.out;
}

inline std::string Base64(const std::string& in, bool url) {
    static const char* std64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static const char* url64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const char* t = url ? url64 : std64;
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    const unsigned char* p = (const unsigned char*)in.data();
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = (p[i] << 16) | (p[i + 1] << 8) | p[i + 2];
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += t[v & 63];
    }
    if (i + 1 == in.size()) {
        uint32_t v = p[i] << 16;
        out += t[v >> 18]; out += t[(v >> 12) & 63];
        if (!url) out += "==";
    } else if (i + 2 == in.size()) {
        uint32_t v = (p[i] << 16) | (p[i + 1] << 8);
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63];
        if (!url) out += "=";
    }
    return out;
}

} // namespace deflate
