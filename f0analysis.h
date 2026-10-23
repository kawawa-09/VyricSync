// f0analysis.h ------------------------------------------------------------
// ボーカルのみのWAVから、基本周波数(F0)と音量(ダイナミクス)を取り出す。ヘッダオンリー。
//   ・F0: YIN法 (8kHz付近に間引いてから、10msごとに推定)。無声・無音のフレームは 0。
//   ・音量: RMS。有声部分の上位(95%点)を基準に 0〜127 へ正規化。
// ---------------------------------------------------------------------------
#pragma once
#include "wavdata.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace f0 {

    struct Result {
        double hop = 0.01;          // フレーム間隔 (秒)
        std::vector<float> midi;    // 各フレームのF0をMIDIノート番号(小数)で。0 = 無声
        std::vector<float> dyn;     // 各フレームの音量 0〜127
        size_t voicedCount = 0;
        double VoicedRatio() const { return midi.empty() ? 0.0 : (double)voicedCount / (double)midi.size(); }
    };

    inline Result Analyze(const wavdata::Audio& a, double minHz = 70.0, double maxHz = 1000.0) {
        Result r;
        if (a.frames == 0 || a.sampleRate <= 0 || a.channels < 1) return r;

        // 1. モノラル化 + 約8kHzへ間引き (ブロック平均)
        const int decim = std::max(1, (int)std::floor((double)a.sampleRate / 8000.0));
        const double fs = (double)a.sampleRate / (double)decim;
        const size_t n = a.frames / (size_t)decim;
        if (n < 1024) return r;
        const size_t ch = (size_t)a.channels;
        std::vector<float> x(n);
        for (size_t i = 0; i < n; i++) {
            double acc = 0.0;
            for (int k = 0; k < decim; k++) {
                const size_t f = i * (size_t)decim + (size_t)k;
                for (size_t c = 0; c < ch; c++) acc += (double)a.pcm[f * ch + c];
            }
            x[i] = (float)(acc / ((double)decim * (double)ch * 32768.0));
        }

        const int hopS = std::max(1, (int)std::lround(fs * r.hop));
        r.hop = (double)hopS / fs;
        const int W = 256;
        const int tauMin = std::max(2, (int)std::floor(fs / maxHz));
        const int tauMax = std::min(W - 1, (int)std::ceil(fs / minHz));
        const size_t frames = n / (size_t)hopS;
        const int pad = W + tauMax + 8;
        std::vector<float> xp((size_t)pad * 2 + n, 0.0f);
        std::copy(x.begin(), x.end(), xp.begin() + pad);

        r.midi.assign(frames, 0.0f);
        r.dyn.assign(frames, 0.0f);
        std::vector<float> rms(frames, 0.0f);
        std::vector<double> cm((size_t)tauMax + 2, 1.0);

        // 2. フレームごとにYINでF0を推定
        for (size_t f = 0; f < frames; f++) {
            const long long center = (long long)f * hopS;
            const size_t s = (size_t)(center - W / 2 + pad);
            double e = 0.0;
            for (int j = 0; j < W; j++) e += (double)xp[s + (size_t)j] * (double)xp[s + (size_t)j];
            const double rm = std::sqrt(e / (double)W);
            rms[f] = (float)rm;
            if (rm < 0.002) continue;   // ほぼ無音

            double run = 0.0;
            cm[0] = 1.0;
            for (int tau = 1; tau <= tauMax; tau++) {
                double sum = 0.0;
                for (int j = 0; j < W; j++) {
                    const double d = (double)xp[s + (size_t)j] - (double)xp[s + (size_t)j + (size_t)tau];
                    sum += d * d;
                }
                run += sum;
                cm[(size_t)tau] = run > 0.0 ? sum * (double)tau / run : 1.0;
            }
            int best = -1;
            for (int tau = tauMin; tau <= tauMax; tau++) {
                if (cm[(size_t)tau] < 0.15) {
                    while (tau + 1 <= tauMax && cm[(size_t)tau + 1] < cm[(size_t)tau]) tau++;
                    best = tau;
                    break;
                }
            }
            if (best < 0) {
                double mn = 1e30;
                for (int tau = tauMin; tau <= tauMax; tau++) if (cm[(size_t)tau] < mn) { mn = cm[(size_t)tau]; best = tau; }
                if (best < 0 || mn > 0.3) continue;   // 周期性が弱い = 無声
            }
            double t = (double)best;
            if (best > 1 && best < tauMax) {
                const double a0 = cm[(size_t)best - 1], b0 = cm[(size_t)best], c0 = cm[(size_t)best + 1];
                const double den = a0 - 2.0 * b0 + c0;
                if (std::fabs(den) > 1e-12) t = (double)best + 0.5 * (a0 - c0) / den;
            }
            const double hz = fs / t;
            if (hz < minHz || hz > maxHz) continue;
            r.midi[f] = (float)(69.0 + 12.0 * std::log2(hz / 440.0));
        }

        // 3. 後処理: オクターブ飛びの補正 → 短い有声区間の除去 → メディアンで平滑化
        for (size_t f = 1; f < frames; f++) {
            if (r.midi[f] <= 0.0f || r.midi[f - 1] <= 0.0f) continue;
            const float prev = r.midi[f - 1];
            if (std::fabs(r.midi[f] - prev) > 7.0f) {
                float best = r.midi[f];
                for (float sh : { -12.0f, 12.0f }) if (std::fabs(r.midi[f] + sh - prev) < std::fabs(best - prev)) best = r.midi[f] + sh;
                r.midi[f] = (std::fabs(best - prev) <= 3.0f) ? best : 0.0f;
            }
        }
        for (size_t f = 0; f < frames;) {
            if (r.midi[f] <= 0.0f) { f++; continue; }
            size_t e2 = f;
            while (e2 < frames && r.midi[e2] > 0.0f) e2++;
            if (e2 - f < 3) for (size_t k = f; k < e2; k++) r.midi[k] = 0.0f;
            f = e2;
        }
        {
            std::vector<float> src = r.midi;
            for (size_t f = 0; f < frames; f++) {
                if (src[f] <= 0.0f) continue;
                float v[5]; int c = 0;
                for (long long k = (long long)f - 2; k <= (long long)f + 2; k++)
                    if (k >= 0 && k < (long long)frames && src[(size_t)k] > 0.0f) v[c++] = src[(size_t)k];
                std::sort(v, v + c);
                r.midi[f] = v[c / 2];
            }
        }
        for (float m : r.midi) if (m > 0.0f) r.voicedCount++;

        // 4. 音量 (有声部分の上位95%点を基準に 0〜127 へ)
        std::vector<float> vs;
        for (size_t f = 0; f < frames; f++) if (r.midi[f] > 0.0f) vs.push_back(rms[f]);
        if (vs.empty()) vs = rms;
        float ref = 1e-4f;
        if (!vs.empty()) {
            const size_t k = std::min(vs.size() - 1, (size_t)((double)vs.size() * 0.95));
            std::nth_element(vs.begin(), vs.begin() + (long long)k, vs.end());
            ref = std::max(1e-4f, vs[k]);
        }
        std::vector<float> raw(frames);
        for (size_t f = 0; f < frames; f++)
            raw[f] = std::min(127.0f, 100.0f * std::pow(std::min(1.5f, rms[f] / ref), 0.6f));
        for (size_t f = 0; f < frames; f++) {     // 3フレームの平均
            const float p = raw[f > 0 ? f - 1 : f], q = raw[f], nx = raw[f + 1 < frames ? f + 1 : f];
            r.dyn[f] = (p + q + nx) / 3.0f;
        }
        return r;
    }

} // namespace f0
