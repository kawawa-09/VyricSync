// projectfile.h -----------------------------------------------------------
// 独自のプロジェクトファイル (.v2v): 生成設定などのメタデータ(JSON)と、生成したVMD(zlib圧縮)を1つにまとめる。
// ヘッダオンリー・外部ライブラリ不要 (zlib互換の圧縮/展開を内蔵)。描画APIにも依存しない。
//
// ファイル構造 (数値はすべてリトルエンディアン)
//   offset  size  内容
//   0       8     マジック  "V2VPRJ" 0x1A 0x00
//   8       2     フォーマットバージョン (現在 1)
//   10      2     フラグ (予約。現在 0)
//   12      4     メタデータ(JSON)のバイト数 M
//   16      M     メタデータ: UTF-8のJSON (人が読める形。下の「メタデータ」を参照)
//   16+M    4     VMDデータ(zlib)のバイト数 Z
//   20+M    Z     VMDデータ: zlib形式(RFC 1950)で圧縮した .vmd の中身
//   20+M+Z  4     ここまでの全バイトのCRC32 (ファイルの破損・途中切れの検出用)
//
// メタデータ(JSON)
//   format="vsq2vmd-project", formatVersion=1, app, createdUtc,
//   source{name,path,track,trackCount,noteCount}      元のvsqx/vsq と使ったトラック
//   model{vmdModelName,mmdName,mmdPath}               VMDの対象モデル名 / 読み込んだMMDモデル
//   morphs{a,i,u,e,o,n}                               口パクに使ったモーフ名
//   params{...}                                       生成設定 (これを使えば同じ設定で作り直せる)
//   vmd{compression="zlib",size,compressedSize,crc32,frames,morphKeyframes,durationSec}
//                                                     VMDの大きさ・CRC32(展開後)・キーフレーム数など
//
// 他のツールから読む例 (Python):
//   import struct, zlib, json
//   b = open("song.v2v","rb").read(); assert b[:6] == b"V2VPRJ"
//   m = struct.unpack_from("<I", b, 12)[0]; meta = json.loads(b[16:16+m])
//   z = struct.unpack_from("<I", b, 16+m)[0]; vmd = zlib.decompress(b[20+m:20+m+z])
// -------------------------------------------------------------------------
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <algorithm>
#include <stdexcept>
#include <utility>
#include "vsqcore.h"

namespace v2v {

    // =======================================================================
    // CRC32 / Adler32
    // =======================================================================
    inline uint32_t Crc32(const uint8_t* d, size_t n, uint32_t crc = 0) {
        static uint32_t table[256];
        static bool init = false;
        if (!init) {
            for (uint32_t i = 0; i < 256; i++) {
                uint32_t c = i;
                for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                table[i] = c;
            }
            init = true;
        }
        crc = ~crc;
        for (size_t i = 0; i < n; i++) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
        return ~crc;
    }
    inline uint32_t Adler32(const uint8_t* d, size_t n) {
        uint32_t a = 1, b = 0;
        while (n > 0) {
            size_t chunk = std::min<size_t>(n, 5552);   // オーバーフローしない最大の塊
            for (size_t i = 0; i < chunk; i++) { a += d[i]; b += a; }
            a %= 65521u; b %= 65521u;
            d += chunk; n -= chunk;
        }
        return (b << 16) | a;
    }

    // =======================================================================
    // zlib 圧縮 (deflate: LZ77 + 動的/固定ハフマン/無圧縮の中から小さいものを選ぶ)
    // =======================================================================
    namespace zdetail {
        static const uint16_t kLenBase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
        static const uint8_t kLenExtra[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
        static const uint16_t kDistBase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
        static const uint8_t kDistExtra[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
        static const uint8_t kClOrder[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };

        struct Tok { uint16_t v; uint16_t dist; };   // dist==0: リテラル(v=バイト値) / dist>0: 一致(v=長さ3..258)

        inline int LenSymbol(int len) {   // 3..258 -> 0..28
            int s = 28;
            while (s > 0 && len < kLenBase[s]) s--;
            if (len == 258) return 28;
            if (s == 28) s = 27;
            return s;
        }
        inline int DistSymbol(int dist) {  // 1..32768 -> 0..29
            int s = 29;
            while (s > 0 && dist < kDistBase[s]) s--;
            return s;
        }

        class BitWriter {
        public:
            explicit BitWriter(std::vector<uint8_t>& o) : out(o) {}
            void Put(uint32_t value, int bits) {   // 下位ビットから詰める
                buf |= (uint64_t)value << cnt;
                cnt += bits;
                while (cnt >= 8) { out.push_back((uint8_t)(buf & 0xFF)); buf >>= 8; cnt -= 8; }
            }
            void Align() { if (cnt > 0) { out.push_back((uint8_t)(buf & 0xFF)); buf = 0; cnt = 0; } }
            void Bytes(const uint8_t* d, size_t n) { out.insert(out.end(), d, d + n); }
        private:
            std::vector<uint8_t>& out;
            uint64_t buf = 0;
            int cnt = 0;
        };

        inline uint32_t Reverse(uint32_t code, int len) {
            uint32_t r = 0;
            for (int i = 0; i < len; i++) { r = (r << 1) | (code & 1); code >>= 1; }
            return r;
        }

        // 出現頻度から、長さの上限(maxBits)つきのハフマン符号長を作る。使われる記号が2個未満なら、完全な符号になるよう補う
        inline void BuildLengths(const std::vector<uint32_t>& freq, int maxBits, std::vector<uint8_t>& lens) {
            const size_t n = freq.size();
            lens.assign(n, 0);
            std::vector<uint32_t> f(freq);
            size_t used = 0;
            for (size_t i = 0; i < n; i++) if (f[i] > 0) used++;
            for (size_t i = 0; used < 2 && i < n; i++) if (f[i] == 0) { f[i] = 1; used++; }   // 完全な符号にするためのダミー
            for (;;) {
                struct Node { uint64_t w; int left, right; };
                std::vector<Node> nodes;
                std::vector<int> heap;   // 重みの小さい順に取り出す簡易ヒープ
                for (size_t i = 0; i < n; i++) if (f[i] > 0) { nodes.push_back({ f[i], -1, (int)i }); }
                const size_t leaves = nodes.size();
                auto cmp = [&](int a, int b) { return nodes[(size_t)a].w > nodes[(size_t)b].w || (nodes[(size_t)a].w == nodes[(size_t)b].w && a > b); };
                for (size_t i = 0; i < leaves; i++) heap.push_back((int)i);
                std::make_heap(heap.begin(), heap.end(), cmp);
                while (heap.size() > 1) {
                    std::pop_heap(heap.begin(), heap.end(), cmp); int a = heap.back(); heap.pop_back();
                    std::pop_heap(heap.begin(), heap.end(), cmp); int b = heap.back(); heap.pop_back();
                    nodes.push_back({ nodes[(size_t)a].w + nodes[(size_t)b].w, a, b });
                    heap.push_back((int)nodes.size() - 1);
                    std::push_heap(heap.begin(), heap.end(), cmp);
                }
                // 根から深さを数える
                std::vector<int> depth(nodes.size(), 0);
                int maxDepth = 0;
                for (int i = (int)nodes.size() - 1; i >= 0; i--) {
                    if (i >= (int)leaves) {
                        depth[(size_t)nodes[(size_t)i].left] = depth[(size_t)i] + 1;
                        depth[(size_t)nodes[(size_t)i].right] = depth[(size_t)i] + 1;
                    }
                    else maxDepth = std::max(maxDepth, depth[(size_t)i]);
                }
                if (maxDepth <= maxBits) {
                    for (size_t i = 0; i < leaves; i++) lens[(size_t)nodes[i].right] = (uint8_t)std::max(1, depth[i]);
                    return;
                }
                for (size_t i = 0; i < n; i++) if (f[i] > 0) f[i] = (f[i] + 1) / 2;   // 偏りをならして、深さを浅くしてやり直す
            }
        }

        // 符号長から正準ハフマン符号(送出用にビット反転済み)を作る
        inline void BuildCodes(const std::vector<uint8_t>& lens, std::vector<uint32_t>& codes) {
            uint32_t count[16] = {}, next[16] = {};
            for (uint8_t l : lens) count[l]++;
            count[0] = 0;
            uint32_t code = 0;
            for (int b = 1; b <= 15; b++) { code = (code + count[b - 1]) << 1; next[b] = code; }
            codes.assign(lens.size(), 0);
            for (size_t i = 0; i < lens.size(); i++) if (lens[i]) codes[i] = Reverse(next[lens[i]]++, lens[i]);
        }

        inline void FixedLengths(std::vector<uint8_t>& lit, std::vector<uint8_t>& dist) {
            lit.assign(288, 8);
            for (int i = 144; i < 256; i++) lit[(size_t)i] = 9;
            for (int i = 256; i < 280; i++) lit[(size_t)i] = 7;
            dist.assign(30, 5);
        }

        inline void WriteStored(BitWriter& bw, const uint8_t* d, size_t n, bool final) {
            size_t pos = 0;
            do {
                size_t chunk = std::min<size_t>(n - pos, 65535);
                const bool last = (pos + chunk >= n);
                bw.Put((final && last) ? 1u : 0u, 1);
                bw.Put(0, 2);
                bw.Align();
                uint8_t h[4] = { (uint8_t)(chunk & 0xFF), (uint8_t)(chunk >> 8), (uint8_t)(~chunk & 0xFF), (uint8_t)((~chunk >> 8) & 0xFF) };
                bw.Bytes(h, 4);
                bw.Bytes(d + pos, chunk);
                pos += chunk;
            } while (pos < n);
        }

        // 1つのブロックを、動的ハフマン・固定ハフマン・無圧縮のうち最も小さい形式で書く
        inline void WriteBlock(BitWriter& bw, const std::vector<Tok>& toks, size_t tb, size_t te, const uint8_t* raw, size_t rawLen, bool final) {
            std::vector<uint32_t> lf(286, 0), df(30, 0);
            uint64_t extra = 0;
            for (size_t i = tb; i < te; i++) {
                const Tok& t = toks[i];
                if (t.dist == 0) lf[t.v]++;
                else {
                    int ls = LenSymbol(t.v), ds = DistSymbol(t.dist);
                    lf[(size_t)(257 + ls)]++; df[(size_t)ds]++;
                    extra += kLenExtra[ls] + kDistExtra[ds];
                }
            }
            lf[256] = 1;   // ブロック終端

            std::vector<uint8_t> ll, dl;
            BuildLengths(lf, 15, ll);
            BuildLengths(df, 15, dl);
            size_t hlit = 286; while (hlit > 257 && ll[hlit - 1] == 0) hlit--;
            size_t hdist = 30; while (hdist > 1 && dl[hdist - 1] == 0) hdist--;

            // 符号長の並びを、16/17/18(繰り返し)を使って短く符号化する
            std::vector<uint8_t> seq(ll.begin(), ll.begin() + (long)hlit);
            seq.insert(seq.end(), dl.begin(), dl.begin() + (long)hdist);
            struct Cl { uint8_t sym, extraBits, extraVal; };
            std::vector<Cl> cls;
            for (size_t i = 0; i < seq.size();) {
                size_t run = 1;
                while (i + run < seq.size() && seq[i + run] == seq[i]) run++;
                if (seq[i] == 0 && run >= 3) {
                    size_t r = std::min<size_t>(run, 138);
                    if (r >= 11) cls.push_back({ 18, 7, (uint8_t)(r - 11) }); else cls.push_back({ 17, 3, (uint8_t)(r - 3) });
                    i += r;
                }
                else if (seq[i] != 0 && run >= 4) {
                    cls.push_back({ seq[i], 0, 0 });
                    size_t r = std::min<size_t>(run - 1, 6);
                    cls.push_back({ 16, 2, (uint8_t)(r - 3) });
                    i += 1 + r;
                }
                else { cls.push_back({ seq[i], 0, 0 }); i++; }
            }
            std::vector<uint32_t> cf(19, 0);
            for (auto& c : cls) cf[c.sym]++;
            std::vector<uint8_t> cl;
            BuildLengths(cf, 7, cl);
            size_t hclen = 19; while (hclen > 4 && cl[kClOrder[hclen - 1]] == 0) hclen--;

            // 大きさの見積もり(ビット数)
            uint64_t dynBits = 3 + 5 + 5 + 4 + 3 * hclen;
            for (auto& c : cls) dynBits += cl[c.sym] + c.extraBits;
            std::vector<uint8_t> fl, fd;
            FixedLengths(fl, fd);
            uint64_t fixBits = 3 + extra;
            for (size_t s = 0; s < 286; s++) { dynBits += (uint64_t)lf[s] * ll[s]; fixBits += (uint64_t)lf[s] * fl[s]; }
            for (size_t s = 0; s < 30; s++) { dynBits += (uint64_t)df[s] * dl[s]; fixBits += (uint64_t)df[s] * 5; }
            dynBits += extra;
            const uint64_t storedBits = (uint64_t)(rawLen / 65535 + 1) * (3 + 7 + 32) + (uint64_t)rawLen * 8;

            if (storedBits <= dynBits && storedBits <= fixBits) { WriteStored(bw, raw, rawLen, final); return; }

            const bool useFixed = fixBits <= dynBits;
            std::vector<uint32_t> lc, dc;
            if (useFixed) {
                bw.Put(final ? 1u : 0u, 1); bw.Put(1, 2);
                BuildCodes(fl, lc); BuildCodes(fd, dc);
                ll = fl; dl = fd;
            }
            else {
                bw.Put(final ? 1u : 0u, 1); bw.Put(2, 2);
                bw.Put((uint32_t)(hlit - 257), 5); bw.Put((uint32_t)(hdist - 1), 5); bw.Put((uint32_t)(hclen - 4), 4);
                for (size_t i = 0; i < hclen; i++) bw.Put(cl[kClOrder[i]], 3);
                std::vector<uint32_t> cc;
                BuildCodes(cl, cc);
                for (auto& c : cls) {
                    bw.Put(cc[c.sym], cl[c.sym]);
                    if (c.extraBits) bw.Put(c.extraVal, c.extraBits);
                }
                BuildCodes(ll, lc); BuildCodes(dl, dc);
            }
            for (size_t i = tb; i < te; i++) {
                const Tok& t = toks[i];
                if (t.dist == 0) bw.Put(lc[t.v], ll[t.v]);
                else {
                    int ls = LenSymbol(t.v), ds = DistSymbol(t.dist);
                    bw.Put(lc[(size_t)(257 + ls)], ll[(size_t)(257 + ls)]);
                    if (kLenExtra[ls]) bw.Put((uint32_t)(t.v - kLenBase[ls]), kLenExtra[ls]);
                    bw.Put(dc[(size_t)ds], dl[(size_t)ds]);
                    if (kDistExtra[ds]) bw.Put((uint32_t)(t.dist - kDistBase[ds]), kDistExtra[ds]);
                }
            }
            bw.Put(lc[256], ll[256]);
        }
    } // namespace zdetail

    // level: 0=無圧縮 1..9 (大きいほど念入りに探す)。戻り値は zlib 形式 (RFC 1950)
    inline std::vector<uint8_t> ZlibCompress(const uint8_t* data, size_t n, int level = 6) {
        using namespace zdetail;
        std::vector<uint8_t> out;
        out.push_back(0x78); out.push_back(0x9C);
        BitWriter bw(out);

        if (level <= 0) {
            WriteStored(bw, data, n, true);
        }
        else {
            level = std::min(level, 9);
            const int maxChain = level <= 1 ? 4 : level <= 3 ? 16 : level <= 5 ? 48 : level <= 7 ? 128 : level == 8 ? 512 : 1024;
            const bool lazy = level >= 4;
            const int niceLen = level >= 8 ? 258 : 128;
            const size_t kWindow = 32768, kHashSize = 1u << 15;
            std::vector<int32_t> head(kHashSize, -1), prev(kWindow, -1);
            auto hashAt = [&](size_t i) -> size_t { return (((size_t)data[i] << 10) ^ ((size_t)data[i + 1] << 5) ^ (size_t)data[i + 2]) & (kHashSize - 1); };
            size_t inserted = 0;   // [0, inserted) の位置はハッシュに登録済み
            auto insertUpTo = [&](size_t pos) {
                for (; inserted < pos; inserted++) {
                    if (inserted + 3 > n) continue;
                    size_t h = hashAt(inserted);
                    prev[inserted & (kWindow - 1)] = head[h];
                    head[h] = (int32_t)inserted;
                }
            };
            auto findMatch = [&](size_t p, int& bestLen, int& bestDist) {
                bestLen = 0; bestDist = 0;
                if (p + 3 > n) return;
                insertUpTo(p);
                const size_t maxLen = std::min<size_t>(258, n - p);
                int32_t cand = head[hashAt(p)];
                int chain = maxChain;
                int32_t lastCand = (int32_t)p;
                while (cand >= 0 && chain-- > 0) {
                    if (cand >= lastCand) break;   // 古い位置が上書きされて循環したときの安全装置
                    size_t dist = p - (size_t)cand;
                    if (dist > kWindow) break;
                    if (data[(size_t)cand + (size_t)bestLen] == data[p + (size_t)bestLen] || bestLen == 0) {
                        size_t l = 0;
                        while (l < maxLen && data[(size_t)cand + l] == data[p + l]) l++;
                        if (l >= 3 && (int)l > bestLen) {
                            bestLen = (int)l; bestDist = (int)dist;
                            if ((int)l >= niceLen || l == maxLen) break;
                        }
                    }
                    lastCand = cand;
                    cand = prev[(size_t)cand & (kWindow - 1)];
                }
            };

            std::vector<Tok> toks;
            size_t blockStart = 0, i = 0;
            const size_t kMaxToks = 32768;
            auto flush = [&](bool final) {
                WriteBlock(bw, toks, 0, toks.size(), data + blockStart, i - blockStart, final);
                toks.clear();
                blockStart = i;
            };
            while (i < n) {
                int len, dist;
                findMatch(i, len, dist);
                if (len >= 3 && lazy && len < niceLen && i + 1 < n) {
                    int len2, dist2;
                    findMatch(i + 1, len2, dist2);
                    if (len2 > len) { len = 0; }   // 1つ先のほうが長く一致する: ここはリテラルにして先へ進む
                }
                if (len >= 3) { toks.push_back({ (uint16_t)len, (uint16_t)dist }); i += (size_t)len; }
                else { toks.push_back({ (uint16_t)data[i], 0 }); i++; }
                if (toks.size() >= kMaxToks && i < n) flush(false);
            }
            flush(true);
        }
        bw.Align();   // 最後の端数ビットをバイトに詰めて吐き出す (これを忘れるとデータが途中で切れて見える)
        uint32_t ad = Adler32(data, n);
        out.push_back((uint8_t)(ad >> 24)); out.push_back((uint8_t)(ad >> 16)); out.push_back((uint8_t)(ad >> 8)); out.push_back((uint8_t)ad);
        return out;
    }

    // =======================================================================
    // zlib 展開 (inflate)。壊れたデータ・大きすぎる出力は false を返す (クラッシュしない)
    // =======================================================================
    namespace zdetail {
        struct Huff { uint16_t count[16]; uint16_t symbol[288]; };
        struct Inflater {
            const uint8_t* in; size_t inLen, inPos = 0;
            uint32_t bitbuf = 0; int bitcnt = 0;
            std::vector<uint8_t>* out; size_t maxOut;

            uint32_t Bits(int need) {
                uint32_t val = bitbuf;
                while (bitcnt < need) {
                    if (inPos >= inLen) throw std::runtime_error("圧縮データが途中で終わっています");
                    val |= (uint32_t)in[inPos++] << bitcnt;
                    bitcnt += 8;
                }
                bitbuf = val >> need;
                bitcnt -= need;
                return val & ((1u << need) - 1);
            }
            static int Build(Huff& h, const uint8_t* lengths, int n) {
                for (int len = 0; len <= 15; len++) h.count[len] = 0;
                for (int s = 0; s < n; s++) h.count[lengths[s]]++;
                if (h.count[0] == n) return 0;
                int left = 1;
                for (int len = 1; len <= 15; len++) { left <<= 1; left -= h.count[len]; if (left < 0) return left; }
                uint16_t offs[16];
                offs[1] = 0;
                for (int len = 1; len < 15; len++) offs[len + 1] = (uint16_t)(offs[len] + h.count[len]);
                for (int s = 0; s < n; s++) if (lengths[s] != 0) h.symbol[offs[lengths[s]]++] = (uint16_t)s;
                return left;
            }
            int Decode(const Huff& h) {
                int code = 0, first = 0, index = 0;
                for (int len = 1; len <= 15; len++) {
                    code |= (int)Bits(1);
                    int count = h.count[len];
                    if (code - count < first) return h.symbol[index + (code - first)];
                    index += count; first += count; first <<= 1; code <<= 1;
                }
                throw std::runtime_error("圧縮データが壊れています (符号)");
            }
            void Codes(const Huff& lc, const Huff& dc) {
                for (;;) {
                    int sym = Decode(lc);
                    if (sym < 256) {
                        if (out->size() >= maxOut) throw std::runtime_error("展開後のサイズが想定を超えています");
                        out->push_back((uint8_t)sym);
                    }
                    else if (sym == 256) return;
                    else {
                        sym -= 257;
                        if (sym >= 29) throw std::runtime_error("圧縮データが壊れています (長さ)");
                        size_t len = kLenBase[sym] + Bits(kLenExtra[sym]);
                        int ds = Decode(dc);
                        if (ds >= 30) throw std::runtime_error("圧縮データが壊れています (距離)");
                        size_t dist = kDistBase[ds] + Bits(kDistExtra[ds]);
                        if (dist > out->size()) throw std::runtime_error("圧縮データが壊れています (距離が範囲外)");
                        if (out->size() + len > maxOut) throw std::runtime_error("展開後のサイズが想定を超えています");
                        size_t from = out->size() - dist;
                        for (size_t k = 0; k < len; k++) out->push_back((*out)[from + k]);
                    }
                }
            }
            void Run() {
                int last;
                do {
                    last = (int)Bits(1);
                    int type = (int)Bits(2);
                    if (type == 0) {
                        bitbuf = 0; bitcnt = 0;
                        if (inLen - inPos < 4) throw std::runtime_error("圧縮データが途中で終わっています");
                        uint32_t len = in[inPos] | ((uint32_t)in[inPos + 1] << 8);
                        uint32_t nlen = in[inPos + 2] | ((uint32_t)in[inPos + 3] << 8);
                        inPos += 4;
                        if (len != (~nlen & 0xFFFF)) throw std::runtime_error("圧縮データが壊れています (無圧縮ブロック)");
                        if (inLen - inPos < len) throw std::runtime_error("圧縮データが途中で終わっています");
                        if (out->size() + len > maxOut) throw std::runtime_error("展開後のサイズが想定を超えています");
                        out->insert(out->end(), in + inPos, in + inPos + len);
                        inPos += len;
                    }
                    else if (type == 1) {
                        static Huff lc, dc;
                        static bool init = false;
                        if (!init) {
                            uint8_t lengths[288];
                            int s = 0;
                            for (; s < 144; s++) lengths[s] = 8;
                            for (; s < 256; s++) lengths[s] = 9;
                            for (; s < 280; s++) lengths[s] = 7;
                            for (; s < 288; s++) lengths[s] = 8;
                            Build(lc, lengths, 288);
                            for (s = 0; s < 30; s++) lengths[s] = 5;
                            Build(dc, lengths, 30);
                            init = true;
                        }
                        Codes(lc, dc);
                    }
                    else if (type == 2) {
                        int nlen = (int)Bits(5) + 257, ndist = (int)Bits(5) + 1, ncode = (int)Bits(4) + 4;
                        if (nlen > 286 || ndist > 30) throw std::runtime_error("圧縮データが壊れています (表の大きさ)");
                        uint8_t lengths[320] = {};
                        for (int i = 0; i < ncode; i++) lengths[kClOrder[i]] = (uint8_t)Bits(3);
                        Huff lc, dc;
                        if (Build(lc, lengths, 19) != 0) throw std::runtime_error("圧縮データが壊れています (符号長の表)");
                        int idx = 0;
                        while (idx < nlen + ndist) {
                            int sym = Decode(lc);
                            if (sym < 16) lengths[idx++] = (uint8_t)sym;
                            else {
                                int len = 0, rep;
                                if (sym == 16) { if (idx == 0) throw std::runtime_error("圧縮データが壊れています (繰り返し)"); len = lengths[idx - 1]; rep = 3 + (int)Bits(2); }
                                else if (sym == 17) rep = 3 + (int)Bits(3);
                                else rep = 11 + (int)Bits(7);
                                if (idx + rep > nlen + ndist) throw std::runtime_error("圧縮データが壊れています (繰り返しの長さ)");
                                while (rep--) lengths[idx++] = (uint8_t)len;
                            }
                        }
                        if (lengths[256] == 0) throw std::runtime_error("圧縮データが壊れています (終端がない)");
                        int err = Build(lc, lengths, nlen);
                        if (err != 0 && (err < 0 || nlen != lc.count[0] + lc.count[1])) throw std::runtime_error("圧縮データが壊れています (リテラル表)");
                        err = Build(dc, lengths + nlen, ndist);
                        if (err != 0 && (err < 0 || ndist != dc.count[0] + dc.count[1])) throw std::runtime_error("圧縮データが壊れています (距離表)");
                        Codes(lc, dc);
                    }
                    else throw std::runtime_error("圧縮データが壊れています (ブロック種別)");
                } while (!last);
            }
        };
    } // namespace zdetail

    // maxOut: 展開後の最大バイト数 (これを超えたら失敗。展開爆弾の対策)
    inline bool ZlibDecompress(const uint8_t* in, size_t inLen, size_t maxOut, std::vector<uint8_t>& out, std::string& err) {
        out.clear();
        if (inLen < 6) { err = "圧縮データが短すぎます"; return false; }
        const unsigned cmf = in[0], flg = in[1];
        if ((cmf & 0x0F) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0) { err = "zlibのヘッダが不正です"; return false; }
        if (flg & 0x20) { err = "辞書つきのzlibには対応していません"; return false; }
        try {
            zdetail::Inflater inf;
            inf.in = in; inf.inLen = inLen; inf.inPos = 2; inf.out = &out; inf.maxOut = maxOut;
            inf.Run();
            if (inLen - inf.inPos < 4) throw std::runtime_error("圧縮データが途中で終わっています (チェックサムがない)");
            const uint8_t* a = in + inf.inPos;
            uint32_t want = ((uint32_t)a[0] << 24) | ((uint32_t)a[1] << 16) | ((uint32_t)a[2] << 8) | a[3];
            if (Adler32(out.data(), out.size()) != want) throw std::runtime_error("圧縮データが壊れています (チェックサム不一致)");
        }
        catch (const std::exception& e) {
            err = e.what();
            out.clear();
            return false;
        }
        return true;
    }

    // =======================================================================
    // 小さなJSON (メタデータ用)。オブジェクトのキーは書いた順に保つ
    // =======================================================================
    struct Json {
        enum Type { Null, Bool, Number, String, Array, Object };
        Type type = Null;
        bool b = false;
        double num = 0.0;
        std::string str;
        std::vector<Json> arr;
        std::vector<std::pair<std::string, Json>> obj;

        static Json MakeBool(bool v) { Json j; j.type = Bool; j.b = v; return j; }
        static Json MakeNum(double v) { Json j; j.type = Number; j.num = v; return j; }
        static Json MakeStr(const std::string& v) { Json j; j.type = String; j.str = v; return j; }
        static Json MakeObject() { Json j; j.type = Object; return j; }
        static Json MakeArray() { Json j; j.type = Array; return j; }

        Json& Set(const std::string& key, Json v) {
            if (type != Object) { type = Object; obj.clear(); }
            for (auto& kv : obj) if (kv.first == key) { kv.second = std::move(v); return kv.second; }
            obj.push_back({ key, std::move(v) });
            return obj.back().second;
        }
        const Json* Find(const std::string& key) const {
            if (type != Object) return nullptr;
            for (auto& kv : obj) if (kv.first == key) return &kv.second;
            return nullptr;
        }
        std::string GetString(const std::string& key, const std::string& def = "") const { const Json* j = Find(key); return (j && j->type == String) ? j->str : def; }
        double GetNumber(const std::string& key, double def = 0.0) const { const Json* j = Find(key); return (j && j->type == Number) ? j->num : def; }
        bool GetBool(const std::string& key, bool def = false) const { const Json* j = Find(key); return (j && j->type == Bool) ? j->b : def; }

        // ---- 書き出し (indent>0 で整形) ----
        static std::string FormatNumber(double v) {
            if (!std::isfinite(v)) return "0";
            if (v == std::floor(v) && std::fabs(v) < 1e15) { char b[32]; std::snprintf(b, sizeof(b), "%lld", (long long)v); return b; }
            char b[40];
            for (int prec = 15; prec <= 17; prec++) {   // 元に戻せる最短の桁数
                std::snprintf(b, sizeof(b), "%.*g", prec, v);
                if (std::strtod(b, nullptr) == v) break;
            }
            return b;
        }
        static void EscapeTo(const std::string& s, std::string& o) {
            o += '"';
            for (unsigned char c : s) {
                switch (c) {
                case '"': o += "\\\""; break;
                case '\\': o += "\\\\"; break;
                case '\n': o += "\\n"; break;
                case '\r': o += "\\r"; break;
                case '\t': o += "\\t"; break;
                case '\b': o += "\\b"; break;
                case '\f': o += "\\f"; break;
                default:
                    if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
                    else o += (char)c;
                }
            }
            o += '"';
        }
        void DumpTo(std::string& o, int indent, int level) const {
            auto nl = [&](int lv) { if (indent > 0) { o += '\n'; o.append((size_t)(indent * lv), ' '); } };
            switch (type) {
            case Null: o += "null"; break;
            case Bool: o += b ? "true" : "false"; break;
            case Number: o += FormatNumber(num); break;
            case String: EscapeTo(str, o); break;
            case Array:
                if (arr.empty()) { o += "[]"; break; }
                o += '[';
                for (size_t i = 0; i < arr.size(); i++) { if (i) o += ','; nl(level + 1); arr[i].DumpTo(o, indent, level + 1); }
                nl(level); o += ']';
                break;
            case Object:
                if (obj.empty()) { o += "{}"; break; }
                o += '{';
                for (size_t i = 0; i < obj.size(); i++) {
                    if (i) o += ',';
                    nl(level + 1);
                    EscapeTo(obj[i].first, o);
                    o += indent > 0 ? ": " : ":";
                    obj[i].second.DumpTo(o, indent, level + 1);
                }
                nl(level); o += '}';
                break;
            }
        }
        std::string Dump(int indent = 2) const { std::string o; DumpTo(o, indent, 0); return o; }

        // ---- 読み込み (厳密なJSON。深さ64まで) ----
        static bool Parse(const std::string& text, Json& out, std::string& err) {
            Parser p(text);
            try {
                p.SkipWs();
                // UTF-8のBOMは許容する
                out = p.Value(0);
                p.SkipWs();
                if (p.i != text.size()) throw std::runtime_error("JSONの末尾に余計な文字があります");
            }
            catch (const std::exception& e) { err = e.what(); out = Json(); return false; }
            return true;
        }
    private:
        struct Parser {
            const std::string& s;
            size_t i = 0;
            explicit Parser(const std::string& t) : s(t) { if (s.size() >= 3 && (uint8_t)s[0] == 0xEF && (uint8_t)s[1] == 0xBB && (uint8_t)s[2] == 0xBF) i = 3; }
            void SkipWs() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++; }
            [[noreturn]] void Fail(const char* m) { throw std::runtime_error(std::string("JSONの形式が正しくありません: ") + m); }
            bool Lit(const char* w) { size_t n = std::strlen(w); if (s.compare(i, n, w) == 0) { i += n; return true; } return false; }
            static void AppendUtf8(std::string& o, uint32_t cp) {
                if (cp < 0x80) o += (char)cp;
                else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
                else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
            }
            uint32_t Hex4() {
                if (i + 4 > s.size()) Fail("\\u の桁が足りません");
                uint32_t v = 0;
                for (int k = 0; k < 4; k++) {
                    char c = s[i++]; v <<= 4;
                    if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
                    else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
                    else Fail("\\u に16進数以外があります");
                }
                return v;
            }
            std::string Str() {
                if (i >= s.size() || s[i] != '"') Fail("文字列が必要です");
                i++;
                std::string o;
                for (;;) {
                    if (i >= s.size()) Fail("文字列が閉じられていません");
                    unsigned char c = (unsigned char)s[i++];
                    if (c == '"') return o;
                    if (c < 0x20) Fail("文字列に制御文字があります");
                    if (c != '\\') { o += (char)c; continue; }
                    if (i >= s.size()) Fail("文字列が閉じられていません");
                    char e = s[i++];
                    switch (e) {
                    case '"': o += '"'; break;
                    case '\\': o += '\\'; break;
                    case '/': o += '/'; break;
                    case 'b': o += '\b'; break;
                    case 'f': o += '\f'; break;
                    case 'n': o += '\n'; break;
                    case 'r': o += '\r'; break;
                    case 't': o += '\t'; break;
                    case 'u': {
                        uint32_t cp = Hex4();
                        if (cp >= 0xD800 && cp < 0xDC00) {
                            if (i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                                size_t save = i; i += 2;
                                uint32_t lo = Hex4();
                                if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                else { i = save; cp = 0xFFFD; }
                            }
                            else cp = 0xFFFD;
                        }
                        else if (cp >= 0xDC00 && cp < 0xE000) cp = 0xFFFD;
                        AppendUtf8(o, cp);
                        break;
                    }
                    default: Fail("不正なエスケープです");
                    }
                }
            }
            Json Value(int depth) {
                if (depth > 64) Fail("入れ子が深すぎます");
                SkipWs();
                if (i >= s.size()) Fail("値がありません");
                char c = s[i];
                if (c == '{') {
                    i++;
                    Json j = MakeObject();
                    SkipWs();
                    if (i < s.size() && s[i] == '}') { i++; return j; }
                    for (;;) {
                        SkipWs();
                        std::string k = Str();
                        SkipWs();
                        if (i >= s.size() || s[i] != ':') Fail("':' が必要です");
                        i++;
                        j.Set(k, Value(depth + 1));
                        SkipWs();
                        if (i < s.size() && s[i] == ',') { i++; continue; }
                        if (i < s.size() && s[i] == '}') { i++; return j; }
                        Fail("',' か '}' が必要です");
                    }
                }
                if (c == '[') {
                    i++;
                    Json j = MakeArray();
                    SkipWs();
                    if (i < s.size() && s[i] == ']') { i++; return j; }
                    for (;;) {
                        j.arr.push_back(Value(depth + 1));
                        SkipWs();
                        if (i < s.size() && s[i] == ',') { i++; continue; }
                        if (i < s.size() && s[i] == ']') { i++; return j; }
                        Fail("',' か ']' が必要です");
                    }
                }
                if (c == '"') return MakeStr(Str());
                if (Lit("true")) return MakeBool(true);
                if (Lit("false")) return MakeBool(false);
                if (Lit("null")) return Json();
                // 数値: -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
                size_t st = i;
                if (i < s.size() && s[i] == '-') i++;
                if (i >= s.size() || !(s[i] >= '0' && s[i] <= '9')) Fail("値が不正です");
                if (s[i] == '0') i++; else while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
                if (i < s.size() && s[i] == '.') { i++; size_t d = i; while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++; if (i == d) Fail("小数点の後に数字が必要です"); }
                if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                    i++;
                    if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
                    size_t d = i; while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
                    if (i == d) Fail("指数部に数字が必要です");
                }
                double v = std::strtod(s.substr(st, i - st).c_str(), nullptr);
                if (!std::isfinite(v)) Fail("数値が大きすぎます");
                return MakeNum(v);
            }
        };
    };

    // =======================================================================
    // プロジェクトの内容 (GUIの設定 <-> JSON)
    // =======================================================================
    struct ProjectState {
        // 元のvsqx/vsq
        std::string sourceName, sourcePath;
        int track = 0, trackCount = 0, noteCount = 0;
        // モデル
        std::string vmdModelName = "LipSync", mmdName, mmdPath;
        // 口パク用モーフ名 (UTF-8。「あ」「い」「う」「え」「お」「ん」)
        std::string morphA = "\xE3\x81\x82", morphI = "\xE3\x81\x84", morphU = "\xE3\x81\x86", morphE = "\xE3\x81\x88", morphO = "\xE3\x81\x8A", morphN = "\xE3\x82\x93";
        // 生成設定
        vsq::MorphParams p;
        double reducePct = 30.0, pitchFxPct = 100.0, dynFxPct = 100.0, smoothMacro = 0.0;
    };

    inline double ClampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

    inline std::string NowUtcIso() {
        std::time_t t = std::time(nullptr);
        std::tm tmv;
#if defined(_WIN32)
        gmtime_s(&tmv, &t);
#else
        gmtime_r(&t, &tmv);
#endif
        char b[32];
        std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", &tmv);
        return b;
    }

    inline Json StateToJson(const ProjectState& s) {
        Json root = Json::MakeObject();
        root.Set("format", Json::MakeStr("vsq2vmd-project"));
        root.Set("formatVersion", Json::MakeNum(1));
        root.Set("app", Json::MakeStr("vsq2vmd-gui"));
        root.Set("createdUtc", Json::MakeStr(NowUtcIso()));
        Json src = Json::MakeObject();
        src.Set("name", Json::MakeStr(s.sourceName));
        src.Set("path", Json::MakeStr(s.sourcePath));
        src.Set("track", Json::MakeNum(s.track));
        src.Set("trackCount", Json::MakeNum(s.trackCount));
        src.Set("noteCount", Json::MakeNum(s.noteCount));
        root.Set("source", std::move(src));
        Json mdl = Json::MakeObject();
        mdl.Set("vmdModelName", Json::MakeStr(s.vmdModelName));
        mdl.Set("mmdName", Json::MakeStr(s.mmdName));
        mdl.Set("mmdPath", Json::MakeStr(s.mmdPath));
        root.Set("model", std::move(mdl));
        Json mo = Json::MakeObject();
        mo.Set("a", Json::MakeStr(s.morphA)); mo.Set("i", Json::MakeStr(s.morphI)); mo.Set("u", Json::MakeStr(s.morphU));
        mo.Set("e", Json::MakeStr(s.morphE)); mo.Set("o", Json::MakeStr(s.morphO)); mo.Set("n", Json::MakeStr(s.morphN));
        root.Set("morphs", std::move(mo));
        Json pr = Json::MakeObject();
        pr.Set("prepMs", Json::MakeNum(s.p.prepMs)); pr.Set("snapMs", Json::MakeNum(s.p.snapMs));
        pr.Set("releaseMs", Json::MakeNum(s.p.releaseMs)); pr.Set("smoothCloseMs", Json::MakeNum(s.p.smoothCloseMs));
        pr.Set("geminateExtraMs", Json::MakeNum(s.p.geminateExtraMs)); pr.Set("gapMs", Json::MakeNum(s.p.gapMs));
        pr.Set("maxWeight", Json::MakeNum(s.p.maxWeight)); pr.Set("consScale", Json::MakeNum(s.p.consScale));
        pr.Set("humanize", Json::MakeNum(s.p.humanize)); pr.Set("fps", Json::MakeNum(s.p.fps));
        pr.Set("legato", Json::MakeNum(s.p.legato)); pr.Set("smoothMs", Json::MakeNum(s.p.smoothMs)); pr.Set("consFloor", Json::MakeNum(s.p.consFloor));
        pr.Set("reducePct", Json::MakeNum(s.reducePct)); pr.Set("pitchFxPct", Json::MakeNum(s.pitchFxPct));
        pr.Set("dynFxPct", Json::MakeNum(s.dynFxPct)); pr.Set("smoothMacro", Json::MakeNum(s.smoothMacro));
        root.Set("params", std::move(pr));
        return root;
    }

    // JSONから設定を読み戻す。無いキーは標準値、範囲外の値は丸める (将来のバージョンで項目が増えても読める)
    inline ProjectState StateFromJson(const Json& root) {
        ProjectState s;
        if (const Json* src = root.Find("source")) {
            s.sourceName = src->GetString("name"); s.sourcePath = src->GetString("path");
            s.track = (int)ClampD(src->GetNumber("track", 0), 0, 100000);
            s.trackCount = (int)ClampD(src->GetNumber("trackCount", 0), 0, 100000);
            s.noteCount = (int)ClampD(src->GetNumber("noteCount", 0), 0, 100000000);
        }
        if (const Json* m = root.Find("model")) {
            s.vmdModelName = m->GetString("vmdModelName", s.vmdModelName);
            s.mmdName = m->GetString("mmdName"); s.mmdPath = m->GetString("mmdPath");
        }
        if (const Json* mo = root.Find("morphs")) {
            s.morphA = mo->GetString("a", s.morphA); s.morphI = mo->GetString("i", s.morphI); s.morphU = mo->GetString("u", s.morphU);
            s.morphE = mo->GetString("e", s.morphE); s.morphO = mo->GetString("o", s.morphO); s.morphN = mo->GetString("n", s.morphN);
        }
        if (const Json* pr = root.Find("params")) {
            vsq::MorphParams d;
            s.p.prepMs = ClampD(pr->GetNumber("prepMs", d.prepMs), 0, 1000);
            s.p.snapMs = ClampD(pr->GetNumber("snapMs", d.snapMs), 1, 1000);
            s.p.releaseMs = ClampD(pr->GetNumber("releaseMs", d.releaseMs), 1, 2000);
            s.p.smoothCloseMs = ClampD(pr->GetNumber("smoothCloseMs", d.smoothCloseMs), 0, 5000);
            s.p.geminateExtraMs = ClampD(pr->GetNumber("geminateExtraMs", d.geminateExtraMs), 0, 1000);
            s.p.gapMs = ClampD(pr->GetNumber("gapMs", d.gapMs), 0, 5000);
            s.p.maxWeight = ClampD(pr->GetNumber("maxWeight", d.maxWeight), 0, 1);
            s.p.consScale = ClampD(pr->GetNumber("consScale", d.consScale), 0, 5);
            s.p.humanize = ClampD(pr->GetNumber("humanize", d.humanize), 0, 1);
            s.p.fps = ClampD(pr->GetNumber("fps", d.fps), 1, 240);
            s.p.legato = ClampD(pr->GetNumber("legato", d.legato), 0, 1);
            s.p.smoothMs = ClampD(pr->GetNumber("smoothMs", d.smoothMs), 0, 500);
            s.p.consFloor = ClampD(pr->GetNumber("consFloor", d.consFloor), 0, 1);
            s.reducePct = ClampD(pr->GetNumber("reducePct", s.reducePct), 0, 100);
            s.pitchFxPct = ClampD(pr->GetNumber("pitchFxPct", s.pitchFxPct), 0, 300);
            s.dynFxPct = ClampD(pr->GetNumber("dynFxPct", s.dynFxPct), 0, 300);
            s.smoothMacro = ClampD(pr->GetNumber("smoothMacro", s.smoothMacro), 0, 100);
        }
        return s;
    }

    // =======================================================================
    // コンテナ (ファイル全体)
    // =======================================================================
    static const uint8_t kMagic[8] = { 'V', '2', 'V', 'P', 'R', 'J', 0x1A, 0x00 };
    static const uint16_t kFormatVersion = 1;
    static const size_t kMaxVmdBytes = 256u * 1024u * 1024u;   // 展開後のVMDの上限

    inline std::string Hex32(uint32_t v) { char b[12]; std::snprintf(b, sizeof(b), "%08x", v); return b; }

    // メタデータ(state + VMDの要約)を作ってから、ファイルのバイト列にする
    inline std::vector<uint8_t> PackProject(const ProjectState& state, const std::vector<uint8_t>& vmd, int level = 6) {
        Json meta = StateToJson(state);
        std::vector<uint8_t> z = ZlibCompress(vmd.data(), vmd.size(), level);

        long lastFrame = 0; size_t keyframes = 0;
        vsq::VmdData parsed; std::string perr;
        if (vsq::ParseVmd(vmd.data(), vmd.size(), parsed, perr)) {
            keyframes = parsed.morphs.size();
            for (auto& f : parsed.morphs) lastFrame = std::max(lastFrame, f.frame);
        }
        Json v = Json::MakeObject();
        v.Set("compression", Json::MakeStr("zlib"));
        v.Set("size", Json::MakeNum((double)vmd.size()));
        v.Set("compressedSize", Json::MakeNum((double)z.size()));
        v.Set("crc32", Json::MakeStr(Hex32(Crc32(vmd.data(), vmd.size()))));
        v.Set("frames", Json::MakeNum((double)lastFrame));
        v.Set("morphKeyframes", Json::MakeNum((double)keyframes));
        v.Set("durationSec", Json::MakeNum(std::floor(lastFrame / std::max(1.0, state.p.fps) * 100.0) / 100.0));
        meta.Set("vmd", std::move(v));

        const std::string metaText = meta.Dump(2);
        std::vector<uint8_t> out;
        out.reserve(24 + metaText.size() + z.size());
        auto u16 = [&](uint32_t x) { out.push_back((uint8_t)(x & 0xFF)); out.push_back((uint8_t)((x >> 8) & 0xFF)); };
        auto u32 = [&](uint32_t x) { for (int k = 0; k < 4; k++) out.push_back((uint8_t)((x >> (8 * k)) & 0xFF)); };
        out.insert(out.end(), kMagic, kMagic + 8);
        u16(kFormatVersion); u16(0);
        u32((uint32_t)metaText.size());
        out.insert(out.end(), metaText.begin(), metaText.end());
        u32((uint32_t)z.size());
        out.insert(out.end(), z.begin(), z.end());
        u32(Crc32(out.data(), out.size()));
        return out;
    }

    // ファイルのバイト列を読む。metaOnly=true ならVMDは展開しない (一覧表示など、メタデータだけ欲しいとき)。
    // 失敗したら false と理由 (err)。どんなに壊れたデータでもクラッシュしない。
    inline bool UnpackProject(const uint8_t* d, size_t n, Json& meta, std::vector<uint8_t>& vmd, std::string& err, bool metaOnly = false) {
        vmd.clear();
        meta = Json();
        if (n < 8 + 2 + 2 + 4 + 4 + 4) { err = "ファイルが小さすぎます (プロジェクトファイルではありません)"; return false; }
        if (std::memcmp(d, kMagic, 8) != 0) { err = "プロジェクトファイル(.v2v)ではありません"; return false; }
        auto rd32 = [&](size_t pos) -> uint32_t { return (uint32_t)d[pos] | ((uint32_t)d[pos + 1] << 8) | ((uint32_t)d[pos + 2] << 16) | ((uint32_t)d[pos + 3] << 24); };
        const uint32_t version = (uint32_t)d[8] | ((uint32_t)d[9] << 8);
        if (version == 0 || version > kFormatVersion) { err = "新しい形式のプロジェクトファイルです。アプリを更新してください"; return false; }
        if (Crc32(d, n - 4) != rd32(n - 4)) { err = "ファイルが壊れているか、途中で切れています (CRC32が一致しません)"; return false; }
        const uint64_t metaLen = rd32(12);
        if (16 + metaLen + 4 + 4 > n) { err = "ファイルの構造が不正です (メタデータの長さ)"; return false; }
        const size_t metaEnd = 16 + (size_t)metaLen;
        std::string perr;
        if (!Json::Parse(std::string((const char*)d + 16, (size_t)metaLen), meta, perr)) { err = "メタデータを読めません: " + perr; return false; }
        if (meta.type != Json::Object || meta.GetString("format") != "vsq2vmd-project") { err = "メタデータの形式が違います"; meta = Json(); return false; }
        const uint64_t zLen = rd32(metaEnd);
        if (metaEnd + 4 + zLen + 4 != n) { err = "ファイルの構造が不正です (データの長さ)"; meta = Json(); return false; }
        if (metaOnly) return true;

        const Json* v = meta.Find("vmd");
        if (!v || v->GetString("compression") != "zlib") { err = "メタデータに、対応しているVMDの情報がありません"; meta = Json(); return false; }
        const double sz = v->GetNumber("size", -1);
        if (!(sz >= 0 && sz <= (double)kMaxVmdBytes)) { err = "メタデータのVMDサイズが不正です"; meta = Json(); return false; }
        std::string zerr;
        if (!ZlibDecompress(d + metaEnd + 4, (size_t)zLen, (size_t)sz, vmd, zerr)) { err = "VMDデータを展開できません: " + zerr; meta = Json(); return false; }
        if ((double)vmd.size() != sz) { err = "VMDのサイズがメタデータと一致しません"; vmd.clear(); meta = Json(); return false; }
        if (Hex32(Crc32(vmd.data(), vmd.size())) != v->GetString("crc32")) { err = "VMDのCRC32がメタデータと一致しません"; vmd.clear(); meta = Json(); return false; }
        return true;
    }
    inline bool UnpackProject(const std::vector<uint8_t>& file, Json& meta, std::vector<uint8_t>& vmd, std::string& err, bool metaOnly = false) {
        return UnpackProject(file.data(), file.size(), meta, vmd, err, metaOnly);
    }

} // namespace v2v
