// wavdata.h ---------------------------------------------------------------
// WAV (RIFF/WAVE) 読み込み。再生用に 16bit PCM (モノラル or ステレオ) へ変換する。
// ---------------------------------------------------------------------------
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace wavdata {

    struct Audio {
        std::vector<int16_t> pcm;  // インターリーブ済みの16bit PCM
        int channels = 0;          // 1 または 2
        int sampleRate = 0;        // Hz
        size_t frames = 0;         // サンプルフレーム数
        double Duration() const { return sampleRate > 0 ? double(frames) / double(sampleRate) : 0.0; }
    };

    inline uint16_t RdU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
    inline uint32_t RdU32(const uint8_t* p) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    inline int16_t FloatToS16(double x) {
        if (!(x == x)) return 0;                 // NaN
        if (x > 1.0) x = 1.0;
        if (x < -1.0) x = -1.0;
        return (int16_t)std::lround(x * 32767.0);
    }

    inline Audio Parse(const std::string& file) {
        const uint8_t* d = reinterpret_cast<const uint8_t*>(file.data());
        const size_t n = file.size();
        if (n < 12 || std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0)
            throw std::runtime_error("WAVファイルではありません (RIFF/WAVE ヘッダがありません)");

        uint16_t tag = 0, ch = 0, bits = 0;
        uint32_t rate = 0;
        bool haveFmt = false;
        const uint8_t* data = nullptr;
        size_t dataLen = 0;

        size_t pos = 12;
        while (pos + 8 <= n) {
            const uint8_t* c = d + pos;
            const size_t body = pos + 8;
            size_t len = RdU32(c + 4);
            const size_t avail = n - body;
            if (len > avail) len = avail;
            if (std::memcmp(c, "fmt ", 4) == 0) {
                if (len < 16) throw std::runtime_error("fmt チャンクが短すぎます");
                tag = RdU16(d + body);
                ch = RdU16(d + body + 2);
                rate = RdU32(d + body + 4);
                bits = RdU16(d + body + 14);
                if (tag == 0xFFFE && len >= 26) tag = RdU16(d + body + 24);
                haveFmt = true;
            }
            else if (std::memcmp(c, "data", 4) == 0 && !data) {
                data = d + body;
                dataLen = len;
            }
            pos = body + len + (len & 1);
        }
        if (!haveFmt) throw std::runtime_error("fmt チャンクが見つかりません");
        if (!data) throw std::runtime_error("data チャンクが見つかりません");
        if (ch < 1 || rate == 0) throw std::runtime_error("チャンネル数またはサンプリングレートが不正です");

        const bool isPcm = (tag == 1);
        const bool isFloat = (tag == 3);
        if (!(isPcm && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) &&
            !(isFloat && (bits == 32 || bits == 64))) {
            char msg[128];
            snprintf(msg, sizeof(msg), "未対応のWAV形式です (形式コード %u, %ubit)", (unsigned)tag, (unsigned)bits);
            throw std::runtime_error(msg);
        }

        const size_t bps = bits / 8;
        const size_t frameBytes = bps * ch;
        const size_t frames = dataLen / frameBytes;
        if (frames == 0) throw std::runtime_error("音声データが空です");

        Audio a;
        a.channels = (ch == 1) ? 1 : 2;
        a.sampleRate = (int)rate;
        a.frames = frames;
        a.pcm.resize(frames * a.channels);

        auto rd = [&](const uint8_t* p) -> double {
            if (isFloat) {
                if (bits == 32) { float f; std::memcpy(&f, p, 4); return f; }
                double f; std::memcpy(&f, p, 8); return f;
            }
            switch (bits) {
            case 8:  return ((int)p[0] - 128) / 128.0;
            case 16: return (int16_t)RdU16(p) / 32768.0;
            case 24: { int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8; return v / 8388608.0; }
            default: return (int32_t)RdU32(p) / 2147483648.0;
            }
            };

        if (isPcm && bits == 16 && ch <= 2) {
            for (size_t i = 0; i < frames * ch; i++) a.pcm[i] = (int16_t)RdU16(data + i * 2);
            return a;
        }
        for (size_t f = 0; f < frames; f++) {
            const uint8_t* fp = data + f * frameBytes;
            if (ch == 1) {
                a.pcm[f] = FloatToS16(rd(fp));
            }
            else if (ch == 2) {
                a.pcm[f * 2] = FloatToS16(rd(fp));
                a.pcm[f * 2 + 1] = FloatToS16(rd(fp + bps));
            }
            else {
                double l = 0, r = 0; int nl = 0, nr = 0;
                for (int k = 0; k < ch; k++) {
                    double v = rd(fp + (size_t)k * bps);
                    if (k % 2 == 0) { l += v; nl++; }
                    else { r += v; nr++; }
                }
                a.pcm[f * 2] = FloatToS16(nl ? l / nl : 0.0);
                a.pcm[f * 2 + 1] = FloatToS16(nr ? r / nr : 0.0);
            }
        }
        return a;
    }

    struct Peaks {
        struct MinMax { int16_t lo, hi; };
        static constexpr size_t kBaseBlock = 64;
        std::vector<std::vector<MinMax>> level;
        const Audio* src = nullptr;
        float scale = 1.0f;

        static size_t BlockSize(size_t k) { return kBaseBlock << (2 * k); }

        void Build(const Audio& a) {
            level.clear();
            src = &a;
            scale = 1.0f;
            if (a.frames == 0) return;
            const size_t ch = (size_t)a.channels;
            std::vector<MinMax> l0((a.frames + kBaseBlock - 1) / kBaseBlock);
            int peak = 0;
            for (size_t b = 0; b < l0.size(); b++) {
                int lo = 32767, hi = -32768;
                const size_t f1 = (std::min)(a.frames, (b + 1) * kBaseBlock);
                for (size_t f = b * kBaseBlock; f < f1; f++)
                    for (size_t c = 0; c < ch; c++) {
                        int v = a.pcm[f * ch + c];
                        lo = (std::min)(lo, v); hi = (std::max)(hi, v);
                    }
                l0[b] = { (int16_t)lo, (int16_t)hi };
                peak = (std::max)(peak, (std::max)(std::abs(lo), std::abs(hi)));
            }
            level.push_back(std::move(l0));
            while (level.back().size() > 1) {
                const auto& prev = level.back();
                std::vector<MinMax> next((prev.size() + 3) / 4);
                for (size_t i = 0; i < next.size(); i++) {
                    int lo = 32767, hi = -32768;
                    for (size_t j = i * 4; j < (std::min)(prev.size(), i * 4 + 4); j++) {
                        lo = (std::min)(lo, (int)prev[j].lo); hi = (std::max)(hi, (int)prev[j].hi);
                    }
                    next[i] = { (int16_t)lo, (int16_t)hi };
                }
                level.push_back(std::move(next));
            }
            if (peak > 0) scale = 32767.0f / (float)peak;
        }

        bool Query(double f0d, double f1d, float& lo, float& hi) const {
            if (!src || src->frames == 0 || level.empty()) return false;
            if (f1d <= 0.0 || f0d >= (double)src->frames) return false;
            size_t f0 = (size_t)(std::max)(0.0, std::floor(f0d));
            size_t f1 = (size_t)(std::min)((double)src->frames, std::ceil(f1d));
            if (f1 <= f0) f1 = (std::min)(src->frames, f0 + 1);
            int mn = 32767, mx = -32768;
            const size_t range = f1 - f0;
            if (range < kBaseBlock * 2) {
                const size_t ch = (size_t)src->channels;
                for (size_t f = f0; f < f1; f++)
                    for (size_t c = 0; c < ch; c++) { int v = src->pcm[f * ch + c]; mn = (std::min)(mn, v); mx = (std::max)(mx, v); }
            }
            else {
                size_t k = 0;
                while (k + 1 < level.size() && BlockSize(k + 1) <= range) k++;
                const size_t bs = BlockSize(k);
                const size_t b0 = f0 / bs, b1 = (std::min)(level[k].size(), (f1 + bs - 1) / bs);
                for (size_t b = b0; b < b1; b++) { mn = (std::min)(mn, (int)level[k][b].lo); mx = (std::max)(mx, (int)level[k][b].hi); }
            }
            if (mn > mx) return false;
            lo = (std::max)(-1.0f, mn / 32768.0f * scale);
            hi = (std::min)(1.0f, mx / 32768.0f * scale);
            return true;
        }
    };

} // namespace wavdata