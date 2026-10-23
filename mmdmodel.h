// mmdmodel.h --------------------------------------------------------------
// MMDモデル (PMX 2.0/2.1, PMD) 読み込み + 頂点モーフ評価。ヘッダオンリー。
// 描画APIには依存しない (Windows以外でもコンパイル可)。
//
// 対応内容:
//   - 頂点 / 面 / 材質 / テクスチャ名 / ボーン名・位置 / モーフ
//   - モーフは「頂点モーフ」と「グループモーフ」を評価 (ボーン/UV/材質モーフは読み飛ばし)
//   - スキニングはしない (バインドポーズのまま表示。口パクのプレビュー用途)
// -------------------------------------------------------------------------
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace mmd {

    struct Vertex {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
    };

    struct Material {
        std::string name;
        float diffuse[4] = { 1, 1, 1, 1 };
        float specular[3] = { 0, 0, 0 };
        float specPower = 0;
        float ambient[3] = { 0.5f, 0.5f, 0.5f };
        bool doubleSided = false;
        int texIndex = -1;          // Model::texturePaths の添字 (-1: なし)
        int toonTexIndex = -1;      // 個別toon: Model::texturePaths の添字 (-1: 共有toonを使う)
        int toonShared = -1;        // 共有toon番号 0-9 (toon01〜toon10)。-1 は標準(0番)扱い
        uint32_t indexStart = 0;    // Model::indices 内の開始位置
        uint32_t indexCount = 0;
    };

    struct VertexOffset { uint32_t index; float x, y, z; };
    struct GroupOffset { int morph; float rate; };

    struct Morph {
        std::string name;           // UTF-8
        int type = -1;              // 0:グループ 1:頂点 それ以外は無視
        std::vector<VertexOffset> vtx;
        std::vector<GroupOffset> grp;
    };

    struct Model {
        std::string name;                       // UTF-8
        bool isPmx = true;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<std::string> texturePaths;  // UTF-8, 区切りは'/'に正規化
        std::vector<Material> materials;
        std::vector<Morph> morphs;
        std::string sharedToonNames[10];        // 共有toonのファイル名 (PMXは toon01.bmp〜、PMDはファイル内の指定)
        bool hasHead = false;                   // 「頭」ボーンがあるか
        float headPos[3] = { 0, 0, 0 };
        float bmin[3] = { 0, 0, 0 }, bmax[3] = { 0, 0, 0 };
        Model() {
            for (int i = 0; i < 10; i++) { char b[16]; std::snprintf(b, sizeof(b), "toon%02d.bmp", i + 1); sharedToonNames[i] = b; }
        }
    };

    // -------------------------------------------------------------------------
    // 文字コード変換
    // -------------------------------------------------------------------------
    inline std::string Utf16LEToUtf8(const uint8_t* p, size_t bytes) {
        std::string out;
        size_t n = bytes / 2;
        for (size_t i = 0; i < n; i++) {
            uint32_t c = p[i * 2] | (p[i * 2 + 1] << 8);
            if (c >= 0xD800 && c < 0xDC00 && i + 1 < n) {
                uint32_t c2 = p[(i + 1) * 2] | (p[(i + 1) * 2 + 1] << 8);
                if (c2 >= 0xDC00 && c2 < 0xE000) {
                    c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
                    i++;
                }
            }
            if (c < 0x80) out += (char)c;
            else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
            else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
            else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
        }
        return out;
    }

    // Shift_JIS(CP932) -> UTF-8。Windows以外ではASCII部分のみ保持する簡易版。
    inline std::string SjisToUtf8(const std::string& s) {
#ifdef _WIN32
        if (s.empty()) return "";
        int wl = MultiByteToWideChar(932, 0, s.c_str(), (int)s.size(), nullptr, 0);
        if (wl <= 0) return s;
        std::wstring w(wl, 0);
        MultiByteToWideChar(932, 0, s.c_str(), (int)s.size(), &w[0], wl);
        int ul = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wl, nullptr, 0, nullptr, nullptr);
        if (ul <= 0) return s;
        std::string out(ul, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wl, &out[0], ul, nullptr, nullptr);
        return out;
#else
        std::string out;
        for (unsigned char c : s) if (c < 0x80) out += (char)c;
        return out;
#endif
    }

    inline std::string NormalizePath(std::string s) {
        for (auto& c : s) if (c == '\\') c = '/';
        return s;
    }

    // -------------------------------------------------------------------------
    // 境界チェック付きバイナリリーダ (リトルエンディアン前提)
    // -------------------------------------------------------------------------
    struct Reader {
        const uint8_t* p; size_t n; size_t pos = 0;
        Reader(const std::string& d) : p((const uint8_t*)d.data()), n(d.size()) {}
        void need(size_t k) const {
            if (k > n || pos > n - k) throw std::runtime_error("モデルファイルが壊れています (途中でデータが終了)");
        }
        template<class T> T get() { need(sizeof(T)); T v; std::memcpy(&v, p + pos, sizeof(T)); pos += sizeof(T); return v; }
        void skip(size_t k) { need(k); pos += k; }
        std::string bytes(size_t k) { need(k); std::string s((const char*)p + pos, k); pos += k; return s; }
        // PMXのインデックス値。vertexIdx=true のときは符号なし (1/2バイト)、それ以外は符号付き(-1=なし)
        int32_t idx(int size, bool isVertex) {
            switch (size) {
            case 1: { uint8_t v = get<uint8_t>(); return isVertex ? (int32_t)v : (int32_t)(int8_t)v; }
            case 2: { uint16_t v = get<uint16_t>(); return isVertex ? (int32_t)v : (int32_t)(int16_t)v; }
            case 4: return get<int32_t>();
            }
            throw std::runtime_error("PMXのインデックスサイズが不正です");
        }
    };

    inline void UpdateBounds(Model& m) {
        if (m.vertices.empty()) return;
        m.bmin[0] = m.bmax[0] = m.vertices[0].px;
        m.bmin[1] = m.bmax[1] = m.vertices[0].py;
        m.bmin[2] = m.bmax[2] = m.vertices[0].pz;
        for (auto& v : m.vertices) {
            m.bmin[0] = std::min(m.bmin[0], v.px); m.bmax[0] = std::max(m.bmax[0], v.px);
            m.bmin[1] = std::min(m.bmin[1], v.py); m.bmax[1] = std::max(m.bmax[1], v.py);
            m.bmin[2] = std::min(m.bmin[2], v.pz); m.bmax[2] = std::max(m.bmax[2], v.pz);
        }
    }

    // 材質のindexStart/Countが実際の面数の範囲に収まるように調整
    inline void ClampMaterials(Model& m) {
        uint32_t total = (uint32_t)m.indices.size();
        for (auto& mt : m.materials) {
            if (mt.indexStart > total) { mt.indexStart = total; mt.indexCount = 0; }
            if (mt.indexStart + mt.indexCount > total) mt.indexCount = total - mt.indexStart;
            mt.indexCount -= mt.indexCount % 3;
        }
    }

    // -------------------------------------------------------------------------
    // PMX
    // -------------------------------------------------------------------------
    inline Model ParsePmx(const std::string& data) {
        Reader r(data);
        Model m; m.isPmx = true;
        if (data.size() < 9 || std::memcmp(data.data(), "PMX ", 4) != 0) throw std::runtime_error("PMXファイルではありません");
        r.skip(4);
        float version = r.get<float>();
        (void)version;
        uint8_t gcount = r.get<uint8_t>();
        if (gcount < 8) throw std::runtime_error("PMXのヘッダが不正です");
        uint8_t g[16] = {};
        for (int i = 0; i < gcount; i++) { uint8_t b = r.get<uint8_t>(); if (i < 16) g[i] = b; }
        const bool utf8 = (g[0] == 1);
        const int addUv = g[1];
        const int vSz = g[2], tSz = g[3], mSz = g[4], bSz = g[5], mrSz = g[6], rbSz = g[7];
        if (addUv > 4) throw std::runtime_error("PMXのヘッダが不正です");

        auto text = [&]() -> std::string {
            int32_t len = r.get<int32_t>();
            if (len < 0) throw std::runtime_error("PMXの文字列長が不正です");
            std::string raw = r.bytes((size_t)len);
            return utf8 ? raw : Utf16LEToUtf8((const uint8_t*)raw.data(), raw.size());
            };

        m.name = text(); text(); text(); text();

        // 頂点
        int32_t vc = r.get<int32_t>();
        if (vc < 0) throw std::runtime_error("頂点数が不正です");
        m.vertices.reserve((size_t)vc);
        for (int32_t i = 0; i < vc; i++) {
            Vertex v;
            v.px = r.get<float>(); v.py = r.get<float>(); v.pz = r.get<float>();
            v.nx = r.get<float>(); v.ny = r.get<float>(); v.nz = r.get<float>();
            v.u = r.get<float>(); v.v = r.get<float>();
            r.skip((size_t)addUv * 16);
            uint8_t wt = r.get<uint8_t>();
            switch (wt) {
            case 0: r.skip((size_t)bSz); break;
            case 1: r.skip((size_t)bSz * 2 + 4); break;
            case 2: r.skip((size_t)bSz * 4 + 16); break;
            case 3: r.skip((size_t)bSz * 2 + 4 + 36); break; // SDEF
            case 4: r.skip((size_t)bSz * 4 + 16); break;     // QDEF
            default: throw std::runtime_error("PMXのウェイト形式が不正です");
            }
            r.skip(4); // edge scale
            m.vertices.push_back(v);
        }

        // 面
        int32_t ic = r.get<int32_t>();
        if (ic < 0) throw std::runtime_error("面数が不正です");
        m.indices.reserve((size_t)ic);
        for (int32_t i = 0; i < ic; i++) {
            int32_t v = r.idx(vSz, true);
            if (v < 0 || v >= vc) v = 0;
            m.indices.push_back((uint32_t)v);
        }

        // テクスチャ
        int32_t tc = r.get<int32_t>();
        if (tc < 0) throw std::runtime_error("テクスチャ数が不正です");
        for (int32_t i = 0; i < tc; i++) m.texturePaths.push_back(NormalizePath(text()));

        // 材質
        int32_t mc = r.get<int32_t>();
        if (mc < 0) throw std::runtime_error("材質数が不正です");
        uint32_t offset = 0;
        for (int32_t i = 0; i < mc; i++) {
            Material mt;
            mt.name = text(); text();
            for (int k = 0; k < 4; k++) mt.diffuse[k] = r.get<float>();
            for (int k = 0; k < 3; k++) mt.specular[k] = r.get<float>();
            mt.specPower = r.get<float>();
            for (int k = 0; k < 3; k++) mt.ambient[k] = r.get<float>();
            uint8_t flag = r.get<uint8_t>();
            mt.doubleSided = (flag & 0x01) != 0;
            r.skip(16 + 4); // edge color + edge size
            int32_t tex = r.idx(tSz, false);
            r.idx(tSz, false); // sphere
            r.skip(1);         // sphere mode
            uint8_t toonFlag = r.get<uint8_t>();
            if (toonFlag == 0) {
                int32_t ti = r.idx(tSz, false);
                mt.toonTexIndex = (ti >= 0 && ti < tc) ? ti : -1;
            }
            else {
                uint8_t sh = r.get<uint8_t>();
                mt.toonShared = (sh < 10) ? (int)sh : -1;
            }
            text(); // memo
            int32_t fc = r.get<int32_t>();
            if (fc < 0) throw std::runtime_error("材質の面数が不正です");
            mt.texIndex = (tex >= 0 && tex < tc) ? tex : -1;
            mt.indexStart = offset;
            mt.indexCount = (uint32_t)fc;
            offset += (uint32_t)fc;
            m.materials.push_back(mt);
        }

        // ボーン (「頭」の位置だけ使う。それ以外は読み飛ばす)
        int32_t bc = r.get<int32_t>();
        if (bc < 0) throw std::runtime_error("ボーン数が不正です");
        for (int32_t i = 0; i < bc; i++) {
            std::string bname = text(); text();
            float pos[3] = { r.get<float>(), r.get<float>(), r.get<float>() };
            r.idx(bSz, false); // parent
            r.skip(4);         // layer
            uint16_t flags = r.get<uint16_t>();
            if (flags & 0x0001) r.idx(bSz, false); else r.skip(12);
            if (flags & (0x0100 | 0x0200)) { r.idx(bSz, false); r.skip(4); }
            if (flags & 0x0400) r.skip(12);
            if (flags & 0x0800) r.skip(24);
            if (flags & 0x2000) r.skip(4);
            if (flags & 0x0020) {
                r.idx(bSz, false); r.skip(4); r.skip(4);
                int32_t links = r.get<int32_t>();
                if (links < 0) throw std::runtime_error("IKリンク数が不正です");
                for (int32_t k = 0; k < links; k++) {
                    r.idx(bSz, false);
                    uint8_t lim = r.get<uint8_t>();
                    if (lim) r.skip(24);
                }
            }
            if (!m.hasHead && bname == "\xE9\xA0\xAD") { // 「頭」
                m.hasHead = true;
                m.headPos[0] = pos[0]; m.headPos[1] = pos[1]; m.headPos[2] = pos[2];
            }
        }

        // モーフ
        int32_t mrc = r.get<int32_t>();
        if (mrc < 0) throw std::runtime_error("モーフ数が不正です");
        m.morphs.resize((size_t)mrc);
        for (int32_t i = 0; i < mrc; i++) {
            Morph& mo = m.morphs[(size_t)i];
            mo.name = text(); text();
            r.skip(1); // panel
            uint8_t type = r.get<uint8_t>();
            int32_t oc = r.get<int32_t>();
            if (oc < 0) throw std::runtime_error("モーフのオフセット数が不正です");
            mo.type = -1;
            switch (type) {
            case 0: // group
                mo.type = 0;
                for (int32_t k = 0; k < oc; k++) {
                    GroupOffset go; go.morph = r.idx(mrSz, false); go.rate = r.get<float>();
                    mo.grp.push_back(go);
                }
                break;
            case 1: // vertex
                mo.type = 1;
                mo.vtx.reserve((size_t)oc);
                for (int32_t k = 0; k < oc; k++) {
                    VertexOffset vo;
                    vo.index = (uint32_t)r.idx(vSz, true);
                    vo.x = r.get<float>(); vo.y = r.get<float>(); vo.z = r.get<float>();
                    mo.vtx.push_back(vo);
                }
                break;
            case 2: for (int32_t k = 0; k < oc; k++) { r.idx(bSz, false); r.skip(28); } break;                 // bone
            case 3: case 4: case 5: case 6: case 7:
                for (int32_t k = 0; k < oc; k++) { r.idx(vSz, true); r.skip(16); } break;                      // uv
            case 8: for (int32_t k = 0; k < oc; k++) { r.idx(mSz, false); r.skip(1 + 28 * 4); } break;         // material
            case 9: for (int32_t k = 0; k < oc; k++) { r.idx(mrSz, false); r.skip(4); } break;                 // flip
            case 10: for (int32_t k = 0; k < oc; k++) { r.idx(rbSz, false); r.skip(1 + 24); } break;  // impulse
            default: throw std::runtime_error("未対応のモーフ種別です");
            }
        }
        ClampMaterials(m);
        UpdateBounds(m);
        return m;
    }

    // -------------------------------------------------------------------------
    // PMD
    // -------------------------------------------------------------------------
    inline std::string CutAtNul(const std::string& s) {
        size_t z = s.find('\0');
        return z == std::string::npos ? s : s.substr(0, z);
    }

    inline Model ParsePmd(const std::string& data) {
        Reader r(data);
        Model m; m.isPmx = false;
        if (data.size() < 7 || std::memcmp(data.data(), "Pmd", 3) != 0) throw std::runtime_error("PMDファイルではありません");
        r.skip(3);
        r.skip(4); // version
        m.name = SjisToUtf8(CutAtNul(r.bytes(20)));
        r.skip(256);

        uint32_t vc = r.get<uint32_t>();
        if ((uint64_t)vc * 38 > data.size()) throw std::runtime_error("頂点数が不正です");
        m.vertices.reserve(vc);
        for (uint32_t i = 0; i < vc; i++) {
            Vertex v;
            v.px = r.get<float>(); v.py = r.get<float>(); v.pz = r.get<float>();
            v.nx = r.get<float>(); v.ny = r.get<float>(); v.nz = r.get<float>();
            v.u = r.get<float>(); v.v = r.get<float>();
            r.skip(2 + 2 + 1 + 1);
            m.vertices.push_back(v);
        }

        uint32_t ic = r.get<uint32_t>();
        if ((uint64_t)ic * 2 > data.size()) throw std::runtime_error("面数が不正です");
        m.indices.reserve(ic);
        for (uint32_t i = 0; i < ic; i++) {
            uint32_t v = r.get<uint16_t>();
            m.indices.push_back(v < vc ? v : 0);
        }

        uint32_t mc = r.get<uint32_t>();
        if ((uint64_t)mc * 70 > data.size()) throw std::runtime_error("材質数が不正です");
        uint32_t offset = 0;
        for (uint32_t i = 0; i < mc; i++) {
            Material mt;
            for (int k = 0; k < 4; k++) mt.diffuse[k] = r.get<float>();
            mt.specPower = r.get<float>();
            for (int k = 0; k < 3; k++) mt.specular[k] = r.get<float>();
            for (int k = 0; k < 3; k++) mt.ambient[k] = r.get<float>();
            uint8_t toonIdx = r.get<uint8_t>();
            r.skip(1); // edge flag
            mt.toonShared = (toonIdx < 10) ? (int)toonIdx : -1;
            uint32_t fc = r.get<uint32_t>();
            std::string tex = CutAtNul(r.bytes(20));
            size_t star = tex.find('*');
            if (star != std::string::npos) tex = tex.substr(0, star);
            tex = NormalizePath(SjisToUtf8(tex));
            if (!tex.empty()) {
                auto it = std::find(m.texturePaths.begin(), m.texturePaths.end(), tex);
                if (it == m.texturePaths.end()) { m.texturePaths.push_back(tex); mt.texIndex = (int)m.texturePaths.size() - 1; }
                else mt.texIndex = (int)(it - m.texturePaths.begin());
            }
            mt.doubleSided = mt.diffuse[3] < 0.99f; // PMDは両面フラグが無いので、半透明のみ両面扱い
            mt.indexStart = offset;
            mt.indexCount = fc;
            offset += fc;
            m.materials.push_back(mt);
        }

        uint16_t bc = r.get<uint16_t>();
        for (uint16_t i = 0; i < bc; i++) {
            std::string bname = SjisToUtf8(CutAtNul(r.bytes(20)));
            r.skip(2 + 2 + 1 + 2);
            float pos[3] = { r.get<float>(), r.get<float>(), r.get<float>() };
            if (!m.hasHead && bname == "\xE9\xA0\xAD") {
                m.hasHead = true;
                m.headPos[0] = pos[0]; m.headPos[1] = pos[1]; m.headPos[2] = pos[2];
            }
        }

        uint16_t ikc = r.get<uint16_t>();
        for (uint16_t i = 0; i < ikc; i++) {
            r.skip(2 + 2);
            uint8_t chain = r.get<uint8_t>();
            r.skip(2 + 4);
            r.skip((size_t)chain * 2);
        }

        uint16_t mrc = r.get<uint16_t>();
        m.morphs.resize(mrc);
        std::vector<uint32_t> baseIdx;
        for (uint16_t i = 0; i < mrc; i++) {
            Morph& mo = m.morphs[i];
            mo.name = SjisToUtf8(CutAtNul(r.bytes(20)));
            uint32_t n = r.get<uint32_t>();
            uint8_t type = r.get<uint8_t>();
            if ((uint64_t)n * 16 > data.size()) throw std::runtime_error("モーフのデータ数が不正です");
            mo.type = 1;
            mo.vtx.reserve(n);
            for (uint32_t k = 0; k < n; k++) {
                VertexOffset vo;
                vo.index = r.get<uint32_t>();
                vo.x = r.get<float>(); vo.y = r.get<float>(); vo.z = r.get<float>();
                mo.vtx.push_back(vo);
            }
            if (type == 0) {
                // base: 以降のモーフは base の頂点リストへの添字で参照する
                baseIdx.clear();
                for (auto& vo : mo.vtx) baseIdx.push_back(vo.index);
                mo.vtx.clear(); // base自体は変形を持たない
            }
            else {
                for (auto& vo : mo.vtx) vo.index = (vo.index < baseIdx.size()) ? baseIdx[vo.index] : 0xFFFFFFFFu;
            }
        }

        // 表示枠などを読み飛ばして、末尾の共有toonファイル名一覧(10個×100バイト)を読む。
        // 古いモデルや途中で切れたファイルでは失敗しても構わない (既定名 toon01.bmp〜 のまま)
        try {
            uint8_t mdc = r.get<uint8_t>(); r.skip((size_t)mdc * 2);
            uint8_t fnc = r.get<uint8_t>(); r.skip((size_t)fnc * 50);
            uint32_t bdc = r.get<uint32_t>();
            if ((uint64_t)bdc * 3 > data.size()) throw std::runtime_error("x");
            r.skip((size_t)bdc * 3);
            uint8_t eng = r.get<uint8_t>();
            if (eng == 1) {
                r.skip(20 + 256);
                r.skip((size_t)bc * 20);
                if (mrc > 0) r.skip((size_t)(mrc - 1) * 20);
                r.skip((size_t)fnc * 50);
            }
            std::string names[10];
            for (int i = 0; i < 10; i++) names[i] = NormalizePath(SjisToUtf8(CutAtNul(r.bytes(100))));
            for (int i = 0; i < 10; i++) if (!names[i].empty()) m.sharedToonNames[i] = names[i];
        }
        catch (const std::exception&) {}

        ClampMaterials(m);
        UpdateBounds(m);
        return m;
    }

    inline Model LoadModel(const std::string& data) {
        if (data.size() >= 4 && std::memcmp(data.data(), "PMX ", 4) == 0) return ParsePmx(data);
        if (data.size() >= 3 && std::memcmp(data.data(), "Pmd", 3) == 0) return ParsePmd(data);
        throw std::runtime_error("PMX / PMD 形式ではありません");
    }

    // -------------------------------------------------------------------------
    // モーフ評価
    // -------------------------------------------------------------------------
    inline int FindMorph(const Model& m, const std::string& name) {
        if (name.empty()) return -1;
        for (size_t i = 0; i < m.morphs.size(); i++) if (m.morphs[i].name == name) return (int)i;
        return -1;
    }

    // active: (モーフ番号, 重み)。out にモーフ適用後の頂点列を書き出す。
    // 頂点モーフは加算、グループモーフは再帰的に (重み×rate) で展開する。
    inline void ApplyMorphs(const Model& m, const std::vector<std::pair<int, float>>& active, std::vector<Vertex>& out) {
        out = m.vertices;
        struct Rec {
            const Model& m; std::vector<Vertex>& out;
            void run(int idx, float w, int depth) {
                if (idx < 0 || idx >= (int)m.morphs.size() || w == 0.0f || depth > 4) return;
                const Morph& mo = m.morphs[(size_t)idx];
                if (mo.type == 1) {
                    for (auto& vo : mo.vtx) {
                        if (vo.index >= out.size()) continue;
                        Vertex& v = out[vo.index];
                        v.px += vo.x * w; v.py += vo.y * w; v.pz += vo.z * w;
                    }
                }
                else if (mo.type == 0) {
                    for (auto& g : mo.grp) run(g.morph, w * g.rate, depth + 1);
                }
            }
        } rec{ m, out };
        for (auto& a : active) rec.run(a.first, a.second, 0);
    }

} // namespace mmd