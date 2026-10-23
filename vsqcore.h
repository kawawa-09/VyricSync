// vsqcore.h ---------------------------------------------------------------
// VSQX/VSQ -> VMD (口パクモーション) 変換コアエンジン
// 外部ライブラリ非依存。Win32 GUI / コンソール / テストのどこからでも
// #include "vsqcore.h" するだけで使える(ヘッダオンリー、単一TU想定)。
// ---------------------------------------------------------------------------
#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

// Windows.h が NOMINMAX なしでインクルードされた場合に備えた保険
// (max/minマクロがあると std::max/std::min が構文的に壊れるため)
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace vsq {

    // ===========================================================================
    // UTF-8 ヘルパ (かな比較・正規化に必要な最小限)
    // ===========================================================================
    inline std::vector<uint32_t> Utf8Decode(const std::string& s) {
        std::vector<uint32_t> out;
        size_t i = 0, n = s.size();
        while (i < n) {
            unsigned char c = s[i];
            uint32_t cp; int len;
            if (c < 0x80) { cp = c; len = 1; }
            else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
            else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
            else { i++; continue; } // 不正バイトはスキップ
            if (i + len > n) break;
            for (int k = 1; k < len; k++) cp = (cp << 6) | (s[i + k] & 0x3F);
            out.push_back(cp);
            i += len;
        }
        return out;
    }
    inline std::string Utf8Encode(uint32_t cp) {
        std::string out;
        if (cp < 0x80) { out += char(cp); }
        else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
        else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
        return out;
    }
    // 全角カタカナ(30A1-30F6) -> ひらがな(-0x60) に正規化した文字列を返す
    inline std::string KataToHira(const std::string& s) {
        auto cps = Utf8Decode(s);
        std::string out;
        for (auto cp : cps) {
            if (cp >= 0x30A1 && cp <= 0x30F6) cp -= 0x60;
            out += Utf8Encode(cp);
        }
        return out;
    }

    // ===========================================================================
    // Shift_JIS (CP932) エンコード: ASCII + ひらがな/カタカナのみサポート
    //   (VMDのモーフ名・モデル名は通常これで十分。未対応文字は '?' に置換)
    // ===========================================================================
    inline const std::unordered_map<uint32_t, uint16_t>& SjisKanaTable() {
        static const std::unordered_map<uint32_t, uint16_t> table = {
            {0x3041,0x829f},{0x3042,0x82a0},{0x3043,0x82a1},{0x3044,0x82a2},{0x3045,0x82a3},
            {0x3046,0x82a4},{0x3047,0x82a5},{0x3048,0x82a6},{0x3049,0x82a7},{0x304A,0x82a8},
            {0x304B,0x82a9},{0x304C,0x82aa},{0x304D,0x82ab},{0x304E,0x82ac},{0x304F,0x82ad},
            {0x3050,0x82ae},{0x3051,0x82af},{0x3052,0x82b0},{0x3053,0x82b1},{0x3054,0x82b2},
            {0x3055,0x82b3},{0x3056,0x82b4},{0x3057,0x82b5},{0x3058,0x82b6},{0x3059,0x82b7},
            {0x305A,0x82b8},{0x305B,0x82b9},{0x305C,0x82ba},{0x305D,0x82bb},{0x305E,0x82bc},
            {0x305F,0x82bd},{0x3060,0x82be},{0x3061,0x82bf},{0x3062,0x82c0},{0x3063,0x82c1},
            {0x3064,0x82c2},{0x3065,0x82c3},{0x3066,0x82c4},{0x3067,0x82c5},{0x3068,0x82c6},
            {0x3069,0x82c7},{0x306A,0x82c8},{0x306B,0x82c9},{0x306C,0x82ca},{0x306D,0x82cb},
            {0x306E,0x82cc},{0x306F,0x82cd},{0x3070,0x82ce},{0x3071,0x82cf},{0x3072,0x82d0},
            {0x3073,0x82d1},{0x3074,0x82d2},{0x3075,0x82d3},{0x3076,0x82d4},{0x3077,0x82d5},
            {0x3078,0x82d6},{0x3079,0x82d7},{0x307A,0x82d8},{0x307B,0x82d9},{0x307C,0x82da},
            {0x307D,0x82db},{0x307E,0x82dc},{0x307F,0x82dd},{0x3080,0x82de},{0x3081,0x82df},
            {0x3082,0x82e0},{0x3083,0x82e1},{0x3084,0x82e2},{0x3085,0x82e3},{0x3086,0x82e4},
            {0x3087,0x82e5},{0x3088,0x82e6},{0x3089,0x82e7},{0x308A,0x82e8},{0x308B,0x82e9},
            {0x308C,0x82ea},{0x308D,0x82eb},{0x308E,0x82ec},{0x308F,0x82ed},{0x3090,0x82ee},
            {0x3091,0x82ef},{0x3092,0x82f0},{0x3093,0x82f1},{0x30FC,0x815b},{0x30FB,0x8145},
        };
        return table;
    }
    // 1文字(コードポイント)を Shift_JIS バイト列へ。戻り値は1 or 2バイト。
    inline std::string SjisEncodeChar(uint32_t cp) {
        if (cp < 0x80) return std::string(1, char(cp));
        const auto& t = SjisKanaTable();
        auto it = t.find(cp);
        if (it != t.end()) {
            uint16_t v = it->second;
            std::string out;
            out += char((v >> 8) & 0xFF);
            out += char(v & 0xFF);
            return out;
        }
        // カタカナも(ひらがな+0x60で)簡易対応
        if (cp >= 0x30A1 && cp <= 0x30F6) return SjisEncodeChar(cp - 0x60);
        return std::string(1, '?');
    }
    // Shift_JIS バイト列 -> UTF-8 (ひらがな/カタカナ/ASCIIのみ対応。未対応バイトは'?')
    //   旧形式 .vsq(Standard MIDI)の歌詞メタイベントは Shift_JIS で格納されているため必要。
    inline std::string SjisDecode(const std::string& in) {
        static std::unordered_map<uint16_t, uint32_t> rev;
        if (rev.empty()) for (auto& kv : SjisKanaTable()) rev[kv.second] = kv.first;
        std::string out;
        size_t i = 0, n = in.size();
        while (i < n) {
            uint8_t b = (uint8_t)in[i];
            if (b < 0x80) { out += char(b); i++; continue; }
            if (i + 1 < n) {
                uint16_t v = (uint16_t(b) << 8) | (uint8_t)in[i + 1];
                auto it = rev.find(v);
                if (it != rev.end()) { out += Utf8Encode(it->second); i += 2; continue; }
            }
            out += '?'; i++; // 漢字など未対応バイトの簡易フォールバック
        }
        return out;
    }
    // UTF-8文字列を Shift_JIS の固定長バイト列(length バイト、0パディング)に変換
    inline std::vector<uint8_t> SjisEncodeFixed(const std::string& utf8, size_t length) {
        std::vector<uint8_t> out(length, 0);
        auto cps = Utf8Decode(utf8);
        size_t pos = 0;
        for (auto cp : cps) {
            std::string b = SjisEncodeChar(cp);
            if (pos + b.size() > length) break;
            for (char c : b) out[pos++] = uint8_t(c);
        }
        return out;
    }

    // ===========================================================================
    // 汎用データ構造
    // ===========================================================================
    struct Note {
        long long tick = 0;
        long long dur = 0;
        std::string lyric;    // UTF-8 かな
        std::string phoneme;  // VOCALOID風 音素記号 (空なら未使用)
        int noteNumber = 60;  // MIDIノート番号 (<n>)。音高スケーリングに使用
        int velocity = 64;    // <v> (0-127)。子音の長さ(速さ)に使用
    };
    struct TempoPoint { long long tick; double bpm; };

    // ピッチベンド(P) / ダイナミクス(D) 等、tick付きの連続値カーブ共通クラス
    struct Curve {
        std::vector<std::pair<long long, double>> pts; // tick昇順
        double ValueAt(long long tick, double defaultVal) const {
            if (pts.empty()) return defaultVal;
            auto it = std::upper_bound(pts.begin(), pts.end(), tick,
                [](long long t, const std::pair<long long, double>& p) { return t < p.first; });
            if (it == pts.begin()) return pts.front().second;
            if (it == pts.end()) return pts.back().second;
            auto p2 = *it; auto p1 = *(it - 1);
            if (p2.first == p1.first) return p2.second;
            double frac = double(tick - p1.first) / double(p2.first - p1.first);
            return p1.second + frac * (p2.second - p1.second);
        }
    };

    struct SongData {
        int resolution = 480;
        std::vector<TempoPoint> tempos;
        std::vector<std::vector<Note>> tracks;
        std::vector<Curve> pitchBendCurves; // tracksと同じインデックス。cc id="P"
        std::vector<Curve> dynamicsCurves;  // tracksと同じインデックス。cc id="D"
    };

    // ===========================================================================
    // 最小限の XML パーサ (VSQX用: 属性は読み飛ばし、要素名とテキスト/CDATAのみ扱う)
    // ===========================================================================
    struct XmlNode {
        std::string tag;
        std::string text;
        std::vector<XmlNode> children;
        std::map<std::string, std::string> attrs;

        const XmlNode* Child(const std::string& t) const {
            for (auto& c : children) if (c.tag == t) return &c;
            return nullptr;
        }
        std::vector<const XmlNode*> Children(const std::string& t) const {
            std::vector<const XmlNode*> out;
            for (auto& c : children) if (c.tag == t) out.push_back(&c);
            return out;
        }
        std::string ChildText(const std::string& t) const {
            auto* c = Child(t);
            return c ? c->text : std::string();
        }
        std::string Attr(const std::string& name) const {
            auto it = attrs.find(name);
            return it != attrs.end() ? it->second : std::string();
        }
    };

    class XmlParser {
    public:
        explicit XmlParser(const std::string& src) : s(src), n(src.size()) {}

        XmlNode ParseDocument() {
            SkipMisc();
            return ParseElement();
        }

    private:
        const std::string& s;
        size_t n;
        size_t i = 0;

        void SkipWs() { while (i < n && isspace((unsigned char)s[i])) i++; }

        void SkipMisc() {
            for (;;) {
                SkipWs();
                if (Starts("<?")) { i = s.find("?>", i); i = (i == std::string::npos) ? n : i + 2; continue; }
                if (Starts("<!--")) { i = s.find("-->", i); i = (i == std::string::npos) ? n : i + 3; continue; }
                break;
            }
        }
        bool Starts(const char* lit) const {
            size_t len = strlen(lit);
            return i + len <= n && s.compare(i, len, lit) == 0;
        }
        static std::string DecodeEntities(const std::string& in) {
            std::string out; out.reserve(in.size());
            for (size_t k = 0; k < in.size();) {
                if (in[k] == '&') {
                    if (in.compare(k, 5, "&amp;") == 0) { out += '&'; k += 5; continue; }
                    if (in.compare(k, 4, "&lt;") == 0) { out += '<'; k += 4; continue; }
                    if (in.compare(k, 4, "&gt;") == 0) { out += '>'; k += 4; continue; }
                    if (in.compare(k, 6, "&apos;") == 0) { out += '\''; k += 6; continue; }
                    if (in.compare(k, 6, "&quot;") == 0) { out += '"'; k += 6; continue; }
                }
                out += in[k]; k++;
            }
            return out;
        }

        XmlNode ParseElement() {
            XmlNode node;
            // '<' の直後
            i++; // consume '<'
            size_t tagStart = i;
            while (i < n && s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r' &&
                s[i] != '/' && s[i] != '>') i++;
            node.tag = s.substr(tagStart, i - tagStart);
            // 属性をパースする ( id="D" のような attr を <cc><v id="D">64</v></cc> から拾うため)
            bool selfClose = false;
            while (i < n) {
                while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
                if (i < n && s[i] == '/' && i + 1 < n && s[i + 1] == '>') { selfClose = true; i += 2; break; }
                if (i < n && s[i] == '>') { i++; break; }
                if (i >= n) break;
                size_t nameStart = i;
                while (i < n && s[i] != '=' && s[i] != ' ' && s[i] != '\t' && s[i] != '>' && s[i] != '/') i++;
                std::string attrName = s.substr(nameStart, i - nameStart);
                while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
                std::string attrVal;
                if (i < n && s[i] == '=') {
                    i++;
                    while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
                    if (i < n && (s[i] == '"' || s[i] == '\'')) {
                        char q = s[i]; i++;
                        size_t vs = i;
                        while (i < n && s[i] != q) i++;
                        attrVal = DecodeEntities(s.substr(vs, i - vs));
                        if (i < n) i++;
                    }
                }
                if (!attrName.empty()) node.attrs[attrName] = attrVal;
            }
            if (selfClose) return node;

            std::string textBuf;
            for (;;) {
                if (i >= n) break;
                if (Starts("<![CDATA[")) {
                    size_t start = i + 9;
                    size_t end = s.find("]]>", start);
                    if (end == std::string::npos) end = n;
                    textBuf += s.substr(start, end - start);
                    i = (end == n) ? n : end + 3;
                    continue;
                }
                if (Starts("<!--")) { i = s.find("-->", i); i = (i == std::string::npos) ? n : i + 3; continue; }
                if (Starts("</")) {
                    i += 2;
                    size_t start = i;
                    while (i < n && s[i] != '>') i++;
                    if (i < n) i++;
                    node.text = DecodeEntities(textBuf);
                    return node;
                }
                if (s[i] == '<') {
                    node.children.push_back(ParseElement());
                    continue;
                }
                size_t start = i;
                while (i < n && s[i] != '<') i++;
                textBuf += s.substr(start, i - start);
            }
            node.text = DecodeEntities(textBuf);
            return node;
        }
    };

    inline void Trim(std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
    }

    // ===========================================================================
    // VSQX パース
    // ===========================================================================
    inline SongData ParseVsqx(const std::string& xmlText) {
        XmlParser parser(xmlText);
        XmlNode root = parser.ParseDocument();
        SongData song;

        const XmlNode* master = root.Child("masterTrack");
        if (!master) throw std::runtime_error("masterTrack が見つかりません(vsqxとして不正です)");
        song.resolution = std::stoi(master->ChildText("resolution"));
        for (auto* t : master->Children("tempo")) {
            long long tick = std::stoll(t->ChildText("t"));
            double bpm = std::stod(t->ChildText("v")) / 100.0;
            song.tempos.push_back({ tick, bpm });
        }
        for (auto* vt : root.Children("vsTrack")) {
            std::vector<Note> notes;
            Curve pitchCurve, dynCurve;
            for (auto* part : vt->Children("vsPart")) {
                for (auto* noteEl : part->Children("note")) {
                    Note note;
                    note.tick = std::stoll(noteEl->ChildText("t"));
                    note.dur = std::stoll(noteEl->ChildText("dur"));
                    note.lyric = noteEl->ChildText("y");
                    note.phoneme = noteEl->ChildText("p");
                    std::string nStr = noteEl->ChildText("n");
                    if (!nStr.empty()) note.noteNumber = std::stoi(nStr);
                    std::string vStr = noteEl->ChildText("v");
                    if (!vStr.empty()) note.velocity = std::stoi(vStr);
                    Trim(note.lyric);
                    Trim(note.phoneme);
                    notes.push_back(note);
                }
                for (auto* ccEl : part->Children("cc")) {
                    auto* vEl = ccEl->Child("v");
                    if (!vEl) continue;
                    std::string id = vEl->Attr("id");
                    long long tick = 0;
                    try { tick = std::stoll(ccEl->ChildText("t")); }
                    catch (...) { continue; }
                    double val = 0;
                    try { val = std::stod(vEl->text); }
                    catch (...) { continue; }
                    if (id == "P") pitchCurve.pts.push_back({ tick, val });
                    else if (id == "D") dynCurve.pts.push_back({ tick, val });
                }
            }
            std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.tick < b.tick; });
            std::sort(pitchCurve.pts.begin(), pitchCurve.pts.end());
            std::sort(dynCurve.pts.begin(), dynCurve.pts.end());
            song.tracks.push_back(std::move(notes));
            song.pitchBendCurves.push_back(std::move(pitchCurve));
            song.dynamicsCurves.push_back(std::move(dynCurve));
        }
        return song;
    }

    // ===========================================================================
    // VSQ (Standard MIDI) パース -- かな歌詞のみのフォールバック経路
    // ===========================================================================
    inline uint32_t ReadVarLen(const std::string& d, size_t& i) {
        uint32_t value = 0;
        while (i < d.size()) {
            uint8_t b = (uint8_t)d[i++];
            value = (value << 7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
        return value;
    }
    inline SongData ParseVsqSmf(const std::string& data) {
        SongData song;
        if (data.size() < 14 || data.compare(0, 4, "MThd") != 0)
            throw std::runtime_error("不正な.vsqファイルです(MThdが見つかりません)");
        auto be32 = [&](size_t p) {
            return (uint32_t(uint8_t(data[p])) << 24) | (uint32_t(uint8_t(data[p + 1])) << 16) |
                (uint32_t(uint8_t(data[p + 2])) << 8) | uint32_t(uint8_t(data[p + 3]));
            };
        auto be16 = [&](size_t p) { return (uint16_t(uint8_t(data[p])) << 8) | uint16_t(uint8_t(data[p + 1])); };

        uint32_t headerLen = be32(4);
        uint16_t numTracks = be16(8 + 2);
        uint16_t division = be16(8 + headerLen - 2);
        if (division & 0x8000) throw std::runtime_error("SMPTEタイム形式には対応していません");
        song.resolution = division;
        song.tempos.push_back({ 0, 120.0 });

        size_t pos = 8 + headerLen;
        for (int tr = 0; tr < numTracks; tr++) {
            if (data.compare(pos, 4, "MTrk") != 0) throw std::runtime_error("壊れた.vsqファイルです");
            uint32_t trackLen = be32(pos + 4);
            size_t trackEnd = pos + 8 + trackLen;
            size_t i = pos + 8;
            long long tick = 0;
            int runningStatus = -1;
            std::string pendingLyric;
            bool hasPending = false;
            bool hasActive = false;
            long long activeTick = 0;
            int activeNoteNum = 60;
            int activeVelocity = 64;
            std::string activeLyric;
            std::vector<Note> notes;

            while (i < trackEnd) {
                tick += ReadVarLen(data, i);
                int status = (uint8_t)data[i];
                int event;
                if (status < 0x80) { event = runningStatus; }
                else { event = status; i++; if (event < 0xF0) runningStatus = event; }

                if (event == 0xFF) {
                    int metaType = (uint8_t)data[i++];
                    uint32_t len = ReadVarLen(data, i);
                    std::string payload = data.substr(i, len);
                    i += len;
                    if (metaType == 0x05) { pendingLyric = SjisDecode(payload); hasPending = true; Trim(pendingLyric); }
                    else if (metaType == 0x51 && len >= 3) {
                        uint32_t micros = (uint8_t(payload[0]) << 16) | (uint8_t(payload[1]) << 8) | uint8_t(payload[2]);
                        double bpm = micros ? 60000000.0 / micros : 120.0;
                        song.tempos.push_back({ tick, bpm });
                    }
                }
                else if (event == 0xF0 || event == 0xF7) {
                    uint32_t len = ReadVarLen(data, i);
                    i += len;
                }
                else {
                    int hi = event & 0xF0;
                    int nData = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
                    uint8_t p0 = data[i], p1 = (nData == 2) ? (uint8_t)data[i + 1] : 0;
                    i += nData;
                    if (hi == 0x90 && p1 != 0) {
                        if (hasActive) notes.push_back({ activeTick, tick - activeTick, activeLyric, "", activeNoteNum, activeVelocity });
                        hasActive = true; activeTick = tick; activeNoteNum = p0; activeVelocity = p1;
                        activeLyric = hasPending ? pendingLyric : std::string();
                        hasPending = false;
                    }
                    else if (hi == 0x80 || (hi == 0x90 && p1 == 0)) {
                        if (hasActive) {
                            notes.push_back({ activeTick, tick - activeTick, activeLyric, "", activeNoteNum, activeVelocity });
                            hasActive = false;
                        }
                    }
                }
            }
            if (hasActive) notes.push_back({ activeTick, std::max<long long>(1, tick - activeTick), activeLyric, "", activeNoteNum, activeVelocity });
            std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.tick < b.tick; });
            if (!notes.empty()) {
                song.tracks.push_back(std::move(notes));
                song.pitchBendCurves.push_back(Curve{});
                song.dynamicsCurves.push_back(Curve{});
            }
            pos = trackEnd;
        }
        std::sort(song.tempos.begin(), song.tempos.end(), [](const TempoPoint& a, const TempoPoint& b) { return a.tick < b.tick; });
        return song;
    }

    inline SongData ParseSongFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("ファイルを開けません: " + path);
        std::ostringstream ss; ss << f.rdbuf();
        std::string data = ss.str();
        std::string lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        bool isVsqx = lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".vsqx") == 0;
        bool isVsq = lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".vsq") == 0 && !isVsqx;
        if (isVsqx) return ParseVsqx(data);
        if (isVsq) return ParseVsqSmf(data);
        // 拡張子不明: まずXMLとして試す
        try { return ParseVsqx(data); }
        catch (...) { return ParseVsqSmf(data); }
    }

    // ===========================================================================
    // テンポマップ: tick -> 秒
    // ===========================================================================
    class TempoMap {
    public:
        TempoMap(int resolution, std::vector<TempoPoint> tempos) : resolution_(resolution) {
            if (tempos.empty()) tempos.push_back({ 0, 120.0 });
            std::sort(tempos.begin(), tempos.end(), [](auto& a, auto& b) { return a.tick < b.tick; });
            if (tempos.front().tick != 0) tempos.insert(tempos.begin(), { 0, tempos.front().bpm });
            ticks_.push_back(tempos[0].tick); bpms_.push_back(tempos[0].bpm); secAt_.push_back(0.0);
            for (size_t k = 1; k < tempos.size(); k++) {
                long long dtTicks = tempos[k].tick - ticks_.back();
                double secPerTick = 60.0 / (bpms_.back() * resolution_);
                secAt_.push_back(secAt_.back() + dtTicks * secPerTick);
                ticks_.push_back(tempos[k].tick);
                bpms_.push_back(tempos[k].bpm);
            }
        }
        double TickToSec(long long tick) const {
            size_t idx = std::upper_bound(ticks_.begin(), ticks_.end(), tick) - ticks_.begin();
            idx = (idx == 0) ? 0 : idx - 1;
            if (idx >= ticks_.size()) idx = ticks_.size() - 1;
            double secPerTick = 60.0 / (bpms_[idx] * resolution_);
            return secAt_[idx] + (tick - ticks_[idx]) * secPerTick;
        }
        // TickToSec の逆変換(ピッチ/ダイナミクスのカーブをフレーム単位でサンプリングする際に使用)
        long long SecToTick(double sec) const {
            size_t idx = std::upper_bound(secAt_.begin(), secAt_.end(), sec) - secAt_.begin();
            idx = (idx == 0) ? 0 : idx - 1;
            if (idx >= secAt_.size()) idx = secAt_.size() - 1;
            double secPerTick = 60.0 / (bpms_[idx] * resolution_);
            if (secPerTick <= 0) return ticks_[idx];
            return ticks_[idx] + (long long)std::lround((sec - secAt_[idx]) / secPerTick);
        }
    private:
        int resolution_;
        std::vector<long long> ticks_;
        std::vector<double> bpms_;
        std::vector<double> secAt_;
    };

    // ===========================================================================
    // 音素/かな -> (子音クラス, 母音) 解析
    // ===========================================================================
    enum class ConsClass { None, Closed, Alveolar, Velar, GlideW, GlideJ };

    struct PoseWeight { std::map<char, double> w; }; // 'a','i','u','e','o' -> weight

    inline const std::map<ConsClass, PoseWeight>& PoseTable() {
        static const std::map<ConsClass, PoseWeight> t = {
            {ConsClass::Closed,   {{}}},
            {ConsClass::Alveolar, {{{'i', 0.25}}}},
            {ConsClass::Velar,    {{{'a', 0.15}}}},
            {ConsClass::GlideW,   {{{'u', 0.35}}}},
            {ConsClass::GlideJ,   {{{'i', 0.30}}}},
        };
        return t;
    }
    inline const std::unordered_map<std::string, ConsClass>& PhonemeConsClass() {
        static const std::unordered_map<std::string, ConsClass> t = {
            {"p",ConsClass::Closed},{"b",ConsClass::Closed},{"m",ConsClass::Closed},
            {"m'",ConsClass::Closed},{"p\\",ConsClass::Closed},
            {"t",ConsClass::Alveolar},{"d",ConsClass::Alveolar},{"ts",ConsClass::Alveolar},
            {"dz",ConsClass::Alveolar},{"s",ConsClass::Alveolar},{"z",ConsClass::Alveolar},
            {"n",ConsClass::Alveolar},{"4",ConsClass::Alveolar},{"4'",ConsClass::Alveolar},
            {"tS",ConsClass::Alveolar},{"dZ",ConsClass::Alveolar},{"S",ConsClass::Alveolar},
            {"J",ConsClass::Alveolar},{"C",ConsClass::Alveolar},
            {"k",ConsClass::Velar},{"g",ConsClass::Velar},{"k'",ConsClass::Velar},
            {"g'",ConsClass::Velar},{"h",ConsClass::Velar},
            {"w",ConsClass::GlideW},{"j",ConsClass::GlideJ},
            {"N",ConsClass::Closed},{"N\\",ConsClass::Closed},
        };
        return t;
    }
    inline const std::set<std::string>& PhonemeNasalAlone() {
        static const std::set<std::string> s = { "m", "n", "N", "N\\" };
        return s;
    }
    inline const std::unordered_map<std::string, char>& PhonemeVowelMap() {
        static const std::unordered_map<std::string, char> t = { {"a",'a'},{"i",'i'},{"M",'u'},{"e",'e'},{"o",'o'} };
        return t;
    }

    inline std::vector<std::string> SplitWs(const std::string& s) {
        std::vector<std::string> out; std::istringstream iss(s); std::string tok;
        while (iss >> tok) out.push_back(tok);
        return out;
    }
    inline std::string BeforeHash(const std::string& s) {
        auto p = s.find('#');
        return p == std::string::npos ? s : s.substr(0, p);
    }

    struct KanaEntry { ConsClass cls; char vowel; std::string cons; };

    inline std::map<std::string, KanaEntry>& KanaTable() {
        static std::map<std::string, KanaEntry> t;
        static bool init = false;
        if (!init) {
            init = true;
            auto add = [&](const std::string& k, ConsClass c, char v, const std::string& cons) { t[k] = { c, v, cons }; };
            add("あ", ConsClass::None, 'a', ""); add("い", ConsClass::None, 'i', ""); add("う", ConsClass::None, 'u', "");
            add("え", ConsClass::None, 'e', ""); add("お", ConsClass::None, 'o', "");
            struct Row { const char* kana5; ConsClass cls; const char* cons; };
            std::vector<Row> rows = {
                {"かきくけこ", ConsClass::Velar, "k"}, {"がぎぐげご", ConsClass::Velar, "g"},
                {"さしすせそ", ConsClass::Alveolar, "s"}, {"ざじずぜぞ", ConsClass::Alveolar, "z"},
                {"たちつてと", ConsClass::Alveolar, "t"}, {"だぢづでど", ConsClass::Alveolar, "d"},
                {"なにぬねの", ConsClass::Alveolar, "n"}, {"はひふへほ", ConsClass::Closed, "h"},
                {"ばびぶべぼ", ConsClass::Closed, "b"}, {"ぱぴぷぺぽ", ConsClass::Closed, "p"},
                {"まみむめも", ConsClass::Closed, "m"}, {"らりるれろ", ConsClass::Alveolar, "4"},
            };
            const char vs[5] = { 'a','i','u','e','o' };
            for (auto& row : rows) {
                auto cps = Utf8Decode(row.kana5);
                for (int k = 0; k < 5; k++) add(Utf8Encode(cps[k]), row.cls, vs[k], row.cons);
            }
            add("や", ConsClass::GlideJ, 'a', "j"); add("ゆ", ConsClass::GlideJ, 'u', "j"); add("よ", ConsClass::GlideJ, 'o', "j");
            add("わ", ConsClass::GlideW, 'a', "w"); add("を", ConsClass::GlideW, 'o', "w");
            add("ん", ConsClass::Closed, 'N', "");  // 撥音「ん」は専用モーフ('N')の対象にする
            add("っ", ConsClass::Closed, 0, "");    add("ー", ConsClass::None, 0, "");

            std::vector<std::tuple<std::string, ConsClass, std::string>> yoonBase = {
                {"き",ConsClass::Velar,"k"}, {"ぎ",ConsClass::Velar,"g"}, {"し",ConsClass::Alveolar,"s"}, {"じ",ConsClass::Alveolar,"z"},
                {"ち",ConsClass::Alveolar,"t"}, {"ぢ",ConsClass::Alveolar,"d"}, {"に",ConsClass::Alveolar,"n"},
                {"ひ",ConsClass::Closed,"h"}, {"び",ConsClass::Closed,"b"}, {"ぴ",ConsClass::Closed,"p"},
                {"み",ConsClass::Closed,"m"}, {"り",ConsClass::Alveolar,"4"},
            };
            std::vector<std::pair<std::string, char>> small3 = { {"ゃ",'a'}, {"ゅ",'u'}, {"ょ",'o'} };
            for (auto& b : yoonBase) for (auto& sm : small3) add(std::get<0>(b) + sm.first, std::get<1>(b), sm.second, std::get<2>(b));

            std::vector<std::tuple<std::string, ConsClass, std::string>> extBase = {
                {"ふ",ConsClass::Closed,"h"}, {"う",ConsClass::GlideW,"w"}, {"ゔ",ConsClass::Closed,"b"},
                {"て",ConsClass::Alveolar,"t"}, {"で",ConsClass::Alveolar,"d"}, {"と",ConsClass::Alveolar,"t"}, {"ど",ConsClass::Alveolar,"d"},
                {"ち",ConsClass::Alveolar,"t"}, {"し",ConsClass::Alveolar,"s"}, {"じ",ConsClass::Alveolar,"z"},
            };
            std::vector<std::pair<std::string, char>> small5 = { {"ぁ",'a'},{"ぃ",'i'},{"ぅ",'u'},{"ぇ",'e'},{"ぉ",'o'} };
            for (auto& b : extBase) for (auto& sm : small5) add(std::get<0>(b) + sm.first, std::get<1>(b), sm.second, std::get<2>(b));
        }
        return t;
    }

    struct PhonemeResult {
        ConsClass cls = ConsClass::None;
        char vowel = 0;           // 'a','i','u','e','o' の他、撥音「ん」は 'N' を使う
        bool geminate = false;
        std::string consSymbol;   // 子音の生シンボル(k, s, t, n, h, m, j, 4, w など)。個別テーブル用
        double weightMul = 1.0;   // ブレス(br1-5)など、開き具合を一律で弱めたい場合の倍率
    };

    // EVEC(...) のような、歌詞ではない技術的なプレースホルダを検出する。
    // 通常のかな歌詞は '(' を含まないため、これを含む場合は歌唱データではないとみなす。
    inline bool IsTechnicalMarkerLyric(const std::string& lyric) {
        return lyric.find('(') != std::string::npos;
    }

    // br1〜br5 のようなブレス(息継ぎ)サンプルの歌詞を検出する
    inline bool IsBreathLyric(const std::string& lyric) {
        if (lyric.size() != 3) return false;
        char c0 = (char)tolower((unsigned char)lyric[0]);
        char c1 = (char)tolower((unsigned char)lyric[1]);
        return c0 == 'b' && c1 == 'r' && lyric[2] >= '1' && lyric[2] <= '5';
    }

    inline PhonemeResult AnalyzePhoneme(const Note& note) {
        PhonemeResult r;
        if (IsTechnicalMarkerLyric(note.lyric)) return r; // EVEC等は完全に無音(閉じ)として扱う
        if (IsBreathLyric(note.lyric)) {
            // ブレス: 子音の勢いは無く、「あ」の口をうっすら開けたまま息を吸うイメージ
            r.vowel = 'a';
            r.weightMul = 0.3;
            return r;
        }
        if (!note.phoneme.empty()) {
            auto tokens = SplitWs(note.phoneme);
            if (tokens.empty()) return r;
            // 最初に現れる母音トークンを探す(例: "a h" や "n o m" のように、母音の後に
            // 何かが続く記法があるため、"最後のトークン"では無く先頭から探す)。
            // 母音以降のトークンは仕様により無視する(最初の子音+母音だけを使う)。
            size_t vowelIdx = tokens.size();
            char vowel = 0;
            for (size_t k = 0; k < tokens.size(); k++) {
                auto vmIt = PhonemeVowelMap().find(tokens[k]);
                if (vmIt != PhonemeVowelMap().end()) { vowel = vmIt->second; vowelIdx = k; break; }
            }
            std::vector<std::string> consTokens;
            for (size_t k = 0; k < vowelIdx; k++) consTokens.push_back(BeforeHash(tokens[k]));

            bool geminate = consTokens.size() >= 2 && consTokens[0] == consTokens[1];
            if (consTokens.empty()) { r.vowel = vowel; return r; }
            const std::string& first = consTokens[0];
            if (vowel == 0 && PhonemeNasalAlone().count(first)) { r.cls = ConsClass::None; r.vowel = 'N'; return r; }
            auto clsIt = PhonemeConsClass().find(first);
            r.cls = (clsIt != PhonemeConsClass().end()) ? clsIt->second : ConsClass::Alveolar;
            r.vowel = vowel; r.geminate = geminate; r.consSymbol = first;
            return r;
        }
        if (note.lyric.empty()) return r;
        std::string norm = KataToHira(note.lyric);
        auto& table = KanaTable();
        auto it = table.find(norm);
        if (it != table.end()) {
            r.cls = it->second.cls; r.vowel = it->second.vowel; r.consSymbol = it->second.cons;
            r.geminate = (norm == "\xE3\x81\xA3");
            return r;
        }
        // 2文字プレフィックスで再試行
        auto cps = Utf8Decode(norm);
        if (cps.size() >= 2) {
            std::string two = Utf8Encode(cps[0]) + Utf8Encode(cps[1]);
            it = table.find(two);
            if (it != table.end()) { r.cls = it->second.cls; r.vowel = it->second.vowel; r.consSymbol = it->second.cons; return r; }
        }
        if (!cps.empty()) {
            std::string one = Utf8Encode(cps[0]);
            it = table.find(one);
            if (it != table.end()) { r.cls = it->second.cls; r.vowel = it->second.vowel; r.consSymbol = it->second.cons; return r; }
        }
        return r;
    }

    // ===========================================================================
    // モーフキーフレーム生成
    // ===========================================================================
    struct MorphParams {
        // タイミング
        double prepMs = 70.0;    // 子音の「準備」を見せておく時間(この間は姿勢を保持)
        double snapMs = 60.0;    // 準備の姿勢から母音の開きへ一気に切り替わる時間(短いほど瞬間的)
        double releaseMs = 60.0; // 発音が終わった後、口が閉じ切るまでの時間
        double smoothCloseMs = 250.0; // 次のノートまでgapMs以上空く(または最後のノート)場合の、滑らかに閉じるまでの時間
        double geminateExtraMs = 70.0; // 促音「っ」などの追加の保持時間
        double gapMs = 90.0;     // これより間が空いたら一旦完全に閉じる
        double consScale = 1.0;  // 子音の予備動作の強さ

        // 母音の開き具合
        double maxWeight = 0.75;    // 基準となる開きの最大値(ピッチ/ダイナミクスで変動する前の値。変動の余地を残すため控えめ)
        double hardMaxWeight = 1.0; // 最終的なクランプ上限

        // ピッチ(音高+ピッチベンド)による開き具合の変化
        double pitchLow = 55.0, pitchHigh = 76.0;       // この範囲のMIDIノート番号を下限〜上限とみなす(音符固定の音高位置による分)
        double pitchScaleMin = 0.8, pitchScaleMax = 1.2; // 低いほど小さく、高いほど大きく開く
        double bendRangeSemitones = 2.0;                 // ピッチベンドの±8192が何半音に相当するか
        // ピッチベンドによる「揺れ」は、上のpitchLow〜pitchHighという広いレンジに希釈されると
        // 些細な変化に見えてしまうため、別枠で強めに反映する(±bendRangeSemitonesの振れ幅で
        // 最大±bendSensitivityだけ開き具合を直接動かす)
        double bendSensitivity = 0.6;
        // ピッチベンドがこの半音数ぶれたら、bendSensitivityの約76%(tanh(1))が効くとみなす。
        // 小さいほど、些細なベンド/ビブラートでも大きく口が動く。
        double bendFullSemitones = 0.6;

        // 人間らしさ(ゆらぎ)の強さ 0〜1 (0で完全に機械的)。音符ごとの開きのばらつき、
        // ゆるやかな顎のゆらぎ、微細な震え、開き始めのオーバーシュート、短い音の開き不足、
        // 開始タイミングの微小なずれ、次の母音を先取りする口の動き(調音結合)を加える。
        double humanize = 0.5;
        unsigned humanSeed = 1;

        // ダイナミクス(cc id="D", 0-127)による開き具合の変化。ピッチの上に重ねて乗算される
        double dynScaleMin = 0.85, dynScaleMax = 1.15;

        // velocity(<v>, 0-127)による子音の長さの変化: 速い(高velocity)ほど短く鋭く、
        // 遅い(低velocity)ほど長くゆったり発音する
        double consDurationScaleAtMinVel = 1.6; // velocity=0相当のときの子音準備時間の倍率
        double consDurationScaleAtMaxVel = 0.6; // velocity=127相当のときの子音準備時間の倍率

        double sampleMs = 10.0; // 音符が伸びている間、ピッチ/ダイナミクスをこの間隔で再サンプリングする
        // (下のsampleFはフレーム単位で最低1フレームに丸められるため、
        //  実質「毎フレーム」再サンプリングされる=最速)

// キーフレーム削減: 生成後、直線補間でほぼ再現できる(見た目にほとんど影響しない)
// 中間キーフレームを間引く。0なら削減なし。値が大きいほど強く間引く(荒くなる)。
        double reduceEpsilon = 0.015;

        // ---- 口の滑らかさ (バタバタして見えるのを抑える) ----
        // 音と音の間があまり空いていない(gapMs未満)とき、前の母音の開きをこの割合(0〜1)だけ残したまま
        // 次の音へ移る。0なら従来どおり、いったん完全に閉じてから次の音を開く(間隔が短いほど
        // 口が開閉を繰り返してバタバタして見えやすい)。
        double legato = 0.0;
        // 仕上げに、全モーフへ時間方向のガウシアン平滑化(幅=この値のms)をかける。0なら無効。
        // 細かい開閉の往復や震えが均され、動きがなめらかになる(代わりに、ごく短い音の開きは少し浅くなる)。
        double smoothMs = 0.0;
        // 子音の準備姿勢(「さ」「ま」「な」など口をほとんど閉じる子音)でも、母音の開き(maxWeight)に対して
        // この割合(0〜1)以上は口を開けたままにする。音節ごとに深く閉じて開く往復を浅くする。0で従来どおり。
        double consFloor = 0.0;

        double fps = 30.0;
    };
    // 「滑らかさ」のまとめ設定: s=0 で標準値、s=1 で最もなめらかな値になるよう、関連する各パラメータをまとめて設定する。
    // (ピッチ/ダイナミクス連動の強さは、この構造体の外にあるGUI側の値なので、呼び出し側で別に調整する)
    inline void ApplySmoothness(MorphParams& p, double s) {
        s = std::min(1.0, std::max(0.0, s));
        const double e = 1.0 - (1.0 - s) * (1.0 - s); // 立ち上がりを早める(途中の値でも、はっきり効果が出るように)
        const MorphParams d;                          // 標準値
        auto lerp = [](double a, double b, double t) { return a + (b - a) * t; };
        p.prepMs = lerp(d.prepMs, 45.0, e);
        p.snapMs = lerp(d.snapMs, 130.0, e);
        p.releaseMs = lerp(d.releaseMs, 150.0, e);
        p.smoothCloseMs = lerp(d.smoothCloseMs, 400.0, e);
        p.gapMs = lerp(d.gapMs, 300.0, e);
        p.consScale = lerp(d.consScale, 0.7, s);
        p.humanize = lerp(d.humanize, 0.2, s);
        p.legato = lerp(0.0, 0.6, e);
        p.smoothMs = lerp(0.0, 100.0, e);
        p.consFloor = lerp(0.0, 0.55, e);
    }
    using MorphTrack = std::vector<std::pair<long, float>>; // (frame, weight) 昇順

    // Ramer-Douglas-Peucker法によるキーフレーム簡略化。
    // 直線補間で誤差epsilon以内に収まる中間点を取り除く(始点・終点は必ず残す)。
    inline MorphTrack SimplifyTrack(const MorphTrack& pts, double epsilon) {
        if (epsilon <= 0.0 || pts.size() <= 2) return pts;
        std::vector<char> keep(pts.size(), 0);
        keep.front() = 1; keep.back() = 1;

        std::vector<std::pair<size_t, size_t>> stack;
        stack.push_back({ 0, pts.size() - 1 });
        while (!stack.empty()) {
            auto [lo, hi] = stack.back(); stack.pop_back();
            if (hi <= lo + 1) continue;
            long f0 = pts[lo].first; double w0 = pts[lo].second;
            long f1 = pts[hi].first; double w1 = pts[hi].second;
            double maxDev = -1.0; size_t maxIdx = lo;
            for (size_t k = lo + 1; k < hi; k++) {
                double t = (f1 == f0) ? 0.0 : double(pts[k].first - f0) / double(f1 - f0);
                double interp = w0 + t * (w1 - w0);
                double dev = std::fabs(double(pts[k].second) - interp);
                if (dev > maxDev) { maxDev = dev; maxIdx = k; }
            }
            if (maxDev > epsilon) {
                keep[maxIdx] = 1;
                stack.push_back({ lo, maxIdx });
                stack.push_back({ maxIdx, hi });
            }
        }
        MorphTrack out;
        for (size_t i = 0; i < pts.size(); i++) if (keep[i]) out.push_back(pts[i]);
        return out;
    }

    // 時間方向のガウシアン平滑化。キーフレームを毎フレームの値に展開(MMDと同じ線形補間)してから
    // 畳み込み、全フレームのキーフレームとして返す(あとでSimplifyTrackで間引く)。
    // 前後対称なので遅れは出ず、発音の少し前から口が動き出す(自然な準備動作)になる。
    inline MorphTrack SmoothTrack(const MorphTrack& pts, double sigmaFrames, double hardMax) {
        if (pts.empty() || sigmaFrames < 0.35) return pts;
        const int R = (int)std::ceil(3.0 * sigmaFrames);
        const long first = pts.front().first, last = pts.back().first;
        const long lo = std::max<long>(0, first - R), hi = last + R;
        const size_t n = (size_t)(hi - lo + 1);
        std::vector<double> src(n), dst(n);
        size_t k = 0;
        for (size_t i = 0; i < n; i++) {
            const long f = lo + (long)i;
            if (f <= first) { src[i] = pts.front().second; continue; }
            if (f >= last) { src[i] = pts.back().second; continue; }
            while (k + 1 < pts.size() && pts[k + 1].first <= f) k++;
            const long f0 = pts[k].first, f1 = pts[k + 1].first;
            const double t = (f1 == f0) ? 0.0 : double(f - f0) / double(f1 - f0);
            src[i] = pts[k].second + t * (double(pts[k + 1].second) - pts[k].second);
        }
        std::vector<double> ker((size_t)(2 * R + 1));
        double sum = 0.0;
        for (int j = -R; j <= R; j++) { double v = std::exp(-0.5 * (j / sigmaFrames) * (j / sigmaFrames)); ker[(size_t)(j + R)] = v; sum += v; }
        for (auto& v : ker) v /= sum;
        for (size_t i = 0; i < n; i++) {
            double acc = 0.0;
            for (int j = -R; j <= R; j++) {
                long idx = (long)i + j;
                idx = std::min<long>((long)n - 1, std::max<long>(0, idx)); // 端は端の値で延長
                acc += ker[(size_t)(j + R)] * src[(size_t)idx];
            }
            dst[i] = std::min(hardMax, std::max(0.0, acc));
        }
        MorphTrack out;
        out.reserve(n);
        for (size_t i = 0; i < n; i++) out.push_back({ lo + (long)i, (float)dst[i] });
        return out;
    }

    inline double PitchDynScale(const MorphParams& p, const Curve& pitchCurve, const Curve& dynCurve,
        int noteNumber, long long tick) {
        double bendRaw = pitchCurve.ValueAt(tick, 0.0); // 生値。範囲はおおむね -8192..8191
        double bendSemi = (bendRaw / 8192.0) * p.bendRangeSemitones;

        // 音符固定の音高位置による、広いレンジでのゆるやかなスケール
        double staticNorm = (noteNumber - p.pitchLow) / std::max(1e-6, (p.pitchHigh - p.pitchLow));
        staticNorm = std::min(1.0, std::max(0.0, staticNorm));
        double staticScale = p.pitchScaleMin + staticNorm * (p.pitchScaleMax - p.pitchScaleMin);

        // ピッチベンドによる揺れは、希釈されないよう単独で強めに反映する
        // tanhで滑らかに飽和させつつ、小さな揺れも大きく増幅する
        double bendContrib = std::tanh(bendSemi / std::max(1e-6, p.bendFullSemitones)) * p.bendSensitivity;

        double pitchScale = staticScale + bendContrib;

        double dynRaw = dynCurve.ValueAt(tick, 64.0); // カーブが無い場合は中庸の64を既定値とする
        double dynNorm = std::min(1.0, std::max(0.0, dynRaw / 127.0));
        double dynScale = p.dynScaleMin + dynNorm * (p.dynScaleMax - p.dynScaleMin);

        return pitchScale * dynScale;
    }


    // ---- 人間らしいゆらぎ用の決定的ノイズ(同じ入力なら常に同じ結果) ----
    inline double HashNoise(uint32_t n) {
        n = (n << 13) ^ n;
        return 1.0 - double((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu) / 1073741824.0; // -1..1
    }
    inline double SmoothNoise(double x, uint32_t seed) {
        double fl = std::floor(x);
        uint32_t i = (uint32_t)(int64_t)fl;
        double f = x - fl;
        double a = HashNoise(i * 2654435761u + seed), b = HashNoise((i + 1) * 2654435761u + seed);
        double t = (1.0 - std::cos(f * 3.14159265358979)) * 0.5;
        return a * (1.0 - t) + b * t;
    }

    inline std::map<char, MorphTrack> BuildMorphTracks(const std::vector<Note>& notes, const TempoMap& tempoMap,
        const MorphParams& p,
        const Curve& pitchCurve = Curve{}, const Curve& dynCurve = Curve{}) {
        const char keys[6] = { 'a','i','u','e','o','N' };
        std::map<char, std::map<long, float>> maps;
        for (char k : keys) maps[k] = {};

        auto setKf = [&](double frameD, const std::map<char, double>& weights) {
            long frame = (long)std::lround(std::max(0.0, frameD));
            for (char k : keys) {
                auto it = weights.find(k);
                maps[k][frame] = (float)(it != weights.end() ? it->second : 0.0);
            }
            };
        auto clampW = [&](double w) { return std::min(p.hardMaxWeight, std::max(0.0, w)); };

        // 子音記号(の代表的なもの)ごとに、「どのモーフを起点にするか」「強さ」を個別指定する。
        // ここに無い記号は、従来通りの調音点ベースのPoseTable()にフォールバックする。
        struct ConsPose { char sourceMorph; double intensity; };
        static const std::map<std::string, ConsPose> kConsPoseTable = {
            {"k",  {0,   1.4}},  // 母音自身の形、インパクトをかなり強く('\0'=母音自身を表すセンチネル)
            {"k'", {0,   1.4}},
            {"s",  {0,   0.30}}, // 母音自身の形、薄い形から
            {"t",  {0,   1.1 }}, // 母音自身の形、強いインパクトだが少し薄い形
            {"n",  {'N', 0.30}}, // 「ん」から、薄い形
            {"h",  {0,   0.45}}, // 母音自身の形、少し薄い形から
            {"m",  {'u', 0.30}}, // 「う」から、薄い形
            {"m'", {'u', 0.30}},
            {"j",  {'i', 0.30}}, // 「い」から、薄い形 (y)
            {"4",  {'u', 0.60}}, // 「う」から、中くらい (r)
            {"4'", {'u', 0.60}},
            {"w",  {'u', 0.50}}, // 「う」から、少しインパクトが弱く中くらい
        };

        struct Ev {
            double sf, ef; long long tick, endTick; ConsClass cls; char vowel; bool geminate;
            int noteNumber; int velocity; std::string consSymbol; double weightMul;
        };
        std::vector<Ev> events;
        char carryVowel = 0; // 直前の母音(「-」歌詞での継続に使う)
        for (auto& n : notes) {
            auto res = AnalyzePhoneme(n);
            // 歌詞が「-」(半角ハイフン)のノートは、前の音符の母音をそのまま継続する
            // (子音なしで、直前の母音へ引き継ぐ。VSQXでタイ/メリスマの延長を表す一般的な記法)
            if (n.lyric == "-" && carryVowel != 0) {
                res.cls = ConsClass::None;
                res.vowel = carryVowel;
                res.consSymbol.clear();
                res.geminate = false;
            }
            if (res.vowel != 0) carryVowel = res.vowel;
            long long endTick = n.tick + std::max<long long>(1, n.dur);
            double sf = tempoMap.TickToSec(n.tick) * p.fps;
            double ef = tempoMap.TickToSec(endTick) * p.fps;
            events.push_back({ sf, ef, n.tick, endTick, res.cls, res.vowel, res.geminate, n.noteNumber, n.velocity,
                               res.consSymbol, res.weightMul });
        }

        // ---- 人間らしさ: 音符ごとのばらつきを先に決めておく ----
        const double H = std::min(1.0, std::max(0.0, p.humanize));
        std::vector<double> noteRnd(events.size()), noteRnd2(events.size()), durFactor(events.size(), 1.0);
        for (size_t i = 0; i < events.size(); i++) {
            noteRnd[i] = HashNoise((uint32_t)(i * 7919u + p.humanSeed * 104729u + 13u));
            noteRnd2[i] = HashNoise((uint32_t)(i * 6151u + p.humanSeed * 1299709u + 29u));
            // 短い音は口が十分開ききらない(調音の不足): 約200ms未満で最大30%弱くなる
            double durMs = (events[i].ef - events[i].sf) / p.fps * 1000.0;
            double shortness = std::max(0.0, 1.0 - durMs / 200.0);
            durFactor[i] = 1.0 - H * 0.30 * shortness;
            // 開始タイミングの微小なずれ(±0.8フレーム程度)。直前ノートの開始より前/自身の終了より後にはならない
            if (H > 0.0 && i > 0) {
                double jit = noteRnd2[i] * 0.8 * H;
                double lo = events[i - 1].sf + 2.0, hi = events[i].ef - 1.0;
                if (hi > lo) events[i].sf = std::min(hi, std::max(lo, events[i].sf + jit));
            }
        }
        const double tauF = std::max(1.0, 0.10 * p.fps); // オーバーシュートが落ち着くまで約100ms
        auto humanMul = [&](size_t idx, double frame) {
            if (H <= 0.0) return 1.0;
            double t = frame / p.fps;
            double m = durFactor[idx];
            m *= 1.0 + 0.10 * H * noteRnd[idx];                                    // 音符ごとの開きのばらつき
            m *= 1.0 + 0.07 * H * SmoothNoise(t * 1.2, p.humanSeed * 31u + 5u);     // ゆるやかな顎のゆらぎ
            m *= 1.0 + 0.035 * H * SmoothNoise(t * 6.0, p.humanSeed * 57u + 11u);   // 微細な震え
            double dt = frame - events[idx].sf;
            if (dt >= 0.0) m *= 1.0 + 0.14 * H * std::exp(-dt / tauF);              // 開き始めのオーバーシュート
            return m;
            };
        auto vowelW = [&](size_t idx, double frame, long long tick) {
            const Ev& e = events[idx];
            double s = PitchDynScale(p, pitchCurve, dynCurve, e.noteNumber, tick);
            return clampW(p.maxWeight * s * e.weightMul * humanMul(idx, frame));
            };
        double prepF = std::max(1.0, p.prepMs / 1000.0 * p.fps);
        double snapF = std::max(1.0, p.snapMs / 1000.0 * p.fps);
        double releaseF = std::max(1.0, p.releaseMs / 1000.0 * p.fps);
        double smoothCloseF = std::max(releaseF, p.smoothCloseMs / 1000.0 * p.fps);
        double gemF = p.geminateExtraMs / 1000.0 * p.fps;
        double gapF = std::max(1.0, p.gapMs / 1000.0 * p.fps);
        double sampleF = std::max(1.0, p.sampleMs / 1000.0 * p.fps);

        // 第1パス: 他ノートとの兼ね合いを考慮する前の、各ノート本来の「準備開始位置」を
        // 先に計算しておく(velocityで子音の長さも加味)。これにより、後続ノートの子音準備が
        // 前ノートの持続時間へ自然に食い込めるようにする(隙間なく詰まっている高速なパッセージでも
        // 発音し直しの動きが潰れないようにするため)。
        std::vector<double> rawPrepStart(events.size()), rawSnapStart(events.size());
        for (size_t i = 0; i < events.size(); i++) {
            auto& e = events[i];
            double velNorm = std::min(1.0, std::max(0.0, e.velocity / 127.0));
            double velDurScale = p.consDurationScaleAtMinVel +
                velNorm * (p.consDurationScaleAtMaxVel - p.consDurationScaleAtMinVel);
            double thisPrep = prepF * velDurScale + (e.geminate ? gemF : 0.0);
            rawSnapStart[i] = e.sf - snapF;
            rawPrepStart[i] = e.sf - thisPrep - snapF;
        }

        setKf(0, {});
        double prevEnd = 0.0;
        bool closedAfterPrev = true; // 直前のキーフレーム時点で口が閉じているか
        for (size_t idx = 0; idx < events.size(); idx++) {
            auto& ev = events[idx];
            // 前ノート自身の開始位置より前には絶対に行かない、という最低限の安全装置のみ設ける
            // (prevEndでは無く、前ノートのsfを下限にすることで、前ノートの持続時間への食い込みを許可する)
            double floorForPrep = idx > 0 ? events[idx - 1].sf + 1.0 : 0.0;
            double prepStart = std::max(rawPrepStart[idx], floorForPrep);
            double snapStart = std::max(rawSnapStart[idx], prepStart);
            snapStart = std::min(snapStart, ev.sf);

            // 直前が「閉じた状態」で、このノートの最初のキーフレームまで間が空く場合は、
            // 開き始める直前まで閉じたまま保持するキーフレームを置く。
            // (これが無いと、閉じた点から次の最初のキーフレームまで直線補間され、
            //  長い休符の間にじわじわ口が開いていってしまう)
            if (closedAfterPrev) {
                double firstKf = (ev.cls != ConsClass::None) ? prepStart : ev.sf;
                double entry = (ev.cls != ConsClass::None) ? 1.0 : snapF;
                double hold = firstKf - entry;
                if (hold > prevEnd + 0.5) setKf(hold, {});
            }

            // 子音の「準備」の姿勢を、snapの直前まで保持する(準備している感じを出す)。
            // 母音が同じでも子音が違えば(あるいはNoneから何かに変われば)必ず表示され、
            // 発音し直しているように見える。前ノートの持続時間に食い込んでもよい。
            if (ev.cls != ConsClass::None) {
                std::map<char, double> pose;
                auto cpIt = kConsPoseTable.find(ev.consSymbol);
                if (cpIt != kConsPoseTable.end() && (cpIt->second.sourceMorph != 0 || ev.vowel != 0)) {
                    // 個別指定のある子音: 起点のモーフ(指定なしなら母音自身)を、指定の強さで見せる。
                    // 強さは基準の開き(maxWeight)に対する倍率。1.0を超えるものは、母音に入る前に
                    // 一度強く「インパクト」を出してから落ち着く動きになる。
                    char src = cpIt->second.sourceMorph ? cpIt->second.sourceMorph : ev.vowel;
                    double w = p.maxWeight * cpIt->second.intensity * p.consScale;
                    pose[src] = std::min(p.hardMaxWeight, std::max(0.0, w));
                }
                else {
                    // 個別指定のない子音は、従来どおり調音点ベースの汎用姿勢
                    auto poseIt = PoseTable().find(ev.cls);
                    if (poseIt != PoseTable().end())
                        for (auto& kv : poseIt->second.w) pose[kv.first] = kv.second * p.consScale;
                }
                if (p.consFloor > 0.0 && ev.vowel != 0) {
                    // 準備姿勢の開きの合計が足りなければ、これから発音する母音の形で不足分を補う
                    double have = 0.0;
                    for (auto& kv : pose) have += kv.second;
                    const double need = std::min(1.0, p.consFloor) * p.maxWeight;
                    if (have < need) pose[ev.vowel] = std::min(p.hardMaxWeight, pose[ev.vowel] + (need - have));
                }
                if (H > 0.0) for (auto& kv : pose) kv.second = std::min(p.hardMaxWeight, std::max(0.0, kv.second * (1.0 + 0.08 * H * noteRnd2[idx])));
                if (prepStart < snapStart) setKf(prepStart, pose);
                setKf(snapStart, pose);
            }

            if (ev.vowel != 0) {
                // snapStart -> sf の短い時間で一気に開く(瞬間的な発声)
                double onsetW = vowelW(idx, ev.sf, ev.tick);
                std::map<char, double> vpose = { {ev.vowel, onsetW} };
                setKf(ev.sf, vpose);

                const bool hasNext = idx + 1 < events.size();
                const double gapToNext = hasNext ? events[idx + 1].sf - ev.ef : 1e9;
                auto isPlainVowel = [](char v) { return v == 'a' || v == 'i' || v == 'u' || v == 'e' || v == 'o'; };

                // 次のノートが「同じ母音・子音なし(=単なる継続)」であれば、閉じずにそのまま継続させる
                bool continuesSameVowel = hasNext &&
                    events[idx + 1].vowel == ev.vowel &&
                    events[idx + 1].cls == ConsClass::None &&
                    gapToNext <= gapF;

                // 次の音が(子音なしの)母音で、さらに次の音の長さがこの音のほぼ半分のときは、
                // 閉じずに、この音の終わり際から次の母音へ滑らかにクロスフェードして繋ぐ
                bool smoothConnect = false;
                if (hasNext && idx + 2 < events.size()) {
                    auto& nx = events[idx + 1];
                    auto& nn = events[idx + 2];
                    double durA = ev.ef - ev.sf, durC = nn.ef - nn.sf;
                    smoothConnect = nx.cls == ConsClass::None && isPlainVowel(nx.vowel) && gapToNext <= gapF &&
                        durA > 0.0 && std::fabs(durC - durA * 0.5) <= durA * 0.15;
                }
                double crossStart = ev.ef;
                if (smoothConnect && !continuesSameVowel) {
                    double durA = ev.ef - ev.sf;
                    double crossF = std::min(durA * 0.5, std::max(snapF, durA * 0.25));
                    crossStart = std::max(ev.sf + 1.0, ev.ef - crossF);
                }

                // 次ノートが子音の準備を必要とする場合、そこに食い込まれる前までしか
                // この母音を保持・サンプリングしない(前ノートの持続時間を後続の
                // 準備動作に譲る=結果的に発音し直しに見える)
                double sustainLimit = ev.ef;
                bool nextNeedsPrep = false;
                if (idx + 1 < events.size()) {
                    nextNeedsPrep = events[idx + 1].cls != ConsClass::None;
                    if (nextNeedsPrep) {
                        double nextPrepStart = std::max(rawPrepStart[idx + 1], ev.sf + 1.0);
                        sustainLimit = std::min(sustainLimit, nextPrepStart);
                    }
                    else {
                        sustainLimit = std::min(sustainLimit, std::max(ev.ef, events[idx + 1].sf));
                    }
                }
                if (smoothConnect && !continuesSameVowel) sustainLimit = std::min(sustainLimit, crossStart);
                sustainLimit = std::max(sustainLimit, ev.sf);

                // 次の母音を先取りして、終わり際に少しだけ次の母音の形へ口が動く(調音結合)
                auto addAnticipation = [&](std::map<char, double>& wm, double f, double limit) {
                    if (H <= 0.0 || !hasNext || smoothConnect || nextNeedsPrep || gapToNext > gapF) return;
                    const Ev& nx = events[idx + 1];
                    if (!isPlainVowel(nx.vowel) || nx.vowel == ev.vowel) return;
                    double win = std::min(4.0, 0.3 * (ev.ef - ev.sf));
                    if (win < 1.0) return;
                    double frac = std::min(1.0, std::max(0.0, (f - (limit - win)) / win));
                    double cw = p.maxWeight * 0.18 * H * frac * nx.weightMul;
                    if (cw > 0.001) wm[nx.vowel] = std::min(p.hardMaxWeight, cw);
                    };

                // 発音が続く間、ピッチ/ダイナミクスに合わせて開き具合を随時サンプリングする
                double lastSampled = ev.sf;
                for (double f = ev.sf + sampleF; f < sustainLimit; f += sampleF) {
                    long long tick = tempoMap.SecToTick(f / p.fps);
                    std::map<char, double> wm = { {ev.vowel, vowelW(idx, f, tick)} };
                    addAnticipation(wm, f, sustainLimit);
                    setKf(f, wm);
                    lastSampled = f;
                }
                // データ上のdurが終わる時点までは(食い込まれない限り)しっかり開きを保つ
                double heldEnd = std::min(ev.ef, sustainLimit);
                if (heldEnd > lastSampled) {
                    std::map<char, double> wm = { {ev.vowel, vowelW(idx, heldEnd, tempoMap.SecToTick(heldEnd / p.fps))} };
                    addAnticipation(wm, heldEnd, sustainLimit);
                    setKf(heldEnd, wm);
                }

                // 次ノートが子音の準備を必要とし、その準備が(ほぼ)隙間なく続く場合は、
                // その準備動作が実質的な「閉じ」を兼ねるので追加の解放は不要。
                // 準備が始まるまでかなり間が空く場合は、通常どおり閉じる。
                double nextPrepStartClamped = hasNext ? std::max(rawPrepStart[idx + 1], ev.sf + 1.0) : 1e9;
                bool bridgePrep = nextNeedsPrep && (nextPrepStartClamped - ev.ef) < gapF;

                if (continuesSameVowel || smoothConnect) {
                    prevEnd = ev.ef;
                    closedAfterPrev = false;
                }
                else if (bridgePrep) {
                    prevEnd = sustainLimit;
                    closedAfterPrev = false;
                }
                else {
                    const bool bigGap = !hasNext || gapToNext >= gapF;
                    bool partialClose = false; // true: 口を閉じ切らず、母音の開きを一部残したまま次へ繋ぐ(legato)
                    double closeEnd;
                    if (bigGap) {
                        // 一定以上次のノートが無い(または最後の)場合は、時間をかけて滑らかに閉じる。
                        // 次の開き始め(の直前)までには閉じ切るようにする。
                        closeEnd = ev.ef + smoothCloseF;
                        if (hasNext) {
                            double nextFirst = (events[idx + 1].cls != ConsClass::None)
                                ? nextPrepStartClamped : events[idx + 1].sf - snapF;
                            closeEnd = std::min(closeEnd, std::max(ev.ef + releaseF, nextFirst - 1.0));
                        }
                        double w0 = vowelW(idx, heldEnd, tempoMap.SecToTick(heldEnd / p.fps));
                        double span = closeEnd - heldEnd;
                        if (span >= 2.0) {
                            for (double fr : {0.25, 0.5, 0.75}) {
                                double s = fr * fr * (3.0 - 2.0 * fr); // smoothstep
                                setKf(heldEnd + span * fr, { {ev.vowel, w0 * (1.0 - s)} });
                            }
                        }
                    }
                    else {
                        closeEnd = ev.ef + releaseF;
                        if (hasNext) closeEnd = std::min(closeEnd, events[idx + 1].sf);
                        partialClose = p.legato > 0.0 && hasNext;
                    }
                    if (closeEnd > ev.ef) {
                        if (partialClose) {
                            const double keep = std::min(1.0, std::max(0.0, p.legato));
                            const double w0 = vowelW(idx, heldEnd, tempoMap.SecToTick(heldEnd / p.fps));
                            setKf(closeEnd, { {ev.vowel, w0 * keep} });
                        }
                        else setKf(closeEnd, {});
                    }
                    prevEnd = std::max(ev.ef, closeEnd);
                    closedAfterPrev = !partialClose;
                }
            }
            else {
                setKf(ev.sf, {});
                prevEnd = std::max(ev.ef, ev.sf);
                closedAfterPrev = true;
            }
        }
        setKf(prevEnd, {});

        std::map<char, MorphTrack> tracks;
        // smoothMs: ガウシアンの標準偏差 = 幅の半分(ms)。平滑化したぶん、間引きは最低限かける
        const double sigmaF = std::max(0.0, p.smoothMs) * 0.5 / 1000.0 * p.fps;
        const double eps = sigmaF >= 0.35 ? std::max(p.reduceEpsilon, 0.003) : p.reduceEpsilon;
        for (char k : keys) {
            MorphTrack v(maps[k].begin(), maps[k].end());
            if (sigmaF >= 0.35) v = SmoothTrack(v, sigmaF, p.hardMaxWeight);
            tracks[k] = SimplifyTrack(v, eps);
        }
        return tracks;
    }

    // ===========================================================================
    // VMD 書き出し (表情/モーフのみ、ボーン0件)
    // ===========================================================================
    struct MorphNames {
        std::string a = "\xE3\x81\x82", i = "\xE3\x81\x84", u = "\xE3\x81\x86", e = "\xE3\x81\x88", o = "\xE3\x81\x8A";
        std::string nasal = "\xE3\x82\x93"; // 「ん」。撥音を専用モーフで表現する
    };
    // (UTF-8での「あ」「い」「う」「え」「お」「ん」のバイト列をエスケープで明示)

    // VMDをメモリ上のバイト列として組み立てる (表情/モーフのみ、ボーン0件)。WriteVmd はこれをファイルへ書くだけ。
    inline std::vector<uint8_t> BuildVmdBytes(const std::map<char, MorphTrack>& tracks,
        const MorphNames& names, const std::string& modelName) {
        std::vector<uint8_t> out;
        auto writeFixed = [&](const std::string& text, size_t len) {
            auto b = SjisEncodeFixed(text, len);
            out.insert(out.end(), b.begin(), b.end());
            };
        auto writeU32 = [&](uint32_t v) { for (int k = 0; k < 4; k++) out.push_back((uint8_t)((v >> (8 * k)) & 0xFF)); };
        auto writeF32 = [&](float v) { uint32_t u; std::memcpy(&u, &v, 4); writeU32(u); };

        writeFixed("Vocaloid Motion Data 0002", 30);
        writeFixed(modelName, 20);
        writeU32(0); // bone frame count

        struct Frame { std::string name; long frame; float weight; };
        std::vector<Frame> frames;
        std::map<char, std::string> nameOf = { {'a', names.a}, {'i', names.i}, {'u', names.u}, {'e', names.e}, {'o', names.o}, {'N', names.nasal} };
        // 挿入順を a,i,u,e,o に固定してから frame 昇順で安定ソート(同一フレーム内の並びを
        // リファレンス実装(Python/JS版)と一致させるため)
        const char order[6] = { 'a', 'i', 'u', 'e', 'o', 'N' };
        for (char k : order) {
            auto it = tracks.find(k);
            if (it == tracks.end()) continue;
            for (auto& fr : it->second) frames.push_back({ nameOf[k], fr.first, fr.second });
        }
        std::stable_sort(frames.begin(), frames.end(), [](const Frame& a, const Frame& b) { return a.frame < b.frame; });

        writeU32((uint32_t)frames.size());
        for (auto& fr : frames) {
            writeFixed(fr.name, 15);
            writeU32((uint32_t)fr.frame);
            writeF32(fr.weight);
        }
        writeU32(0); writeU32(0); writeU32(0); writeU32(0); // camera/light/shadow/ik
        return out;
    }

    inline void WriteVmd(const std::string& path, const std::map<char, MorphTrack>& tracks,
        const MorphNames& names, const std::string& modelName) {
        std::ofstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("出力ファイルを開けません: " + path);
        std::vector<uint8_t> bytes = BuildVmdBytes(tracks, names, modelName);
        f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
    }

    // ---- VMD の読み込み (モーフのキーフレームだけ。プロジェクトファイルに保存したVMDの再生・確認用) ----
    struct VmdMorphFrame { uint8_t name[15]; long frame; float weight; };
    struct VmdData {
        std::string modelNameRaw;                // 先頭のモデル名(20バイトの生のバイト列、0以降は切り捨て)
        uint32_t boneFrameCount = 0;
        std::vector<VmdMorphFrame> morphs;
    };
    // 成功なら true。壊れている・途中で切れている場合は false と理由(err)を返す
    inline bool ParseVmd(const uint8_t* d, size_t n, VmdData& out, std::string& err) {
        out = VmdData();
        static const char kHeader[] = "Vocaloid Motion Data 0002";
        if (n < 30 + 20 + 4 || std::memcmp(d, kHeader, sizeof(kHeader) - 1) != 0) { err = "VMD形式ではありません"; return false; }
        size_t pos = 30;
        for (size_t k = 0; k < 20 && d[pos + k] != 0; k++) out.modelNameRaw.push_back((char)d[pos + k]);
        pos += 20;
        auto readU32 = [&](uint32_t& v) -> bool {
            if (n - pos < 4) return false;
            v = (uint32_t)d[pos] | ((uint32_t)d[pos + 1] << 8) | ((uint32_t)d[pos + 2] << 16) | ((uint32_t)d[pos + 3] << 24);
            pos += 4;
            return true;
            };
        uint32_t boneCount = 0;
        if (!readU32(boneCount)) { err = "VMDが途中で切れています"; return false; }
        if ((uint64_t)boneCount * 111u > (uint64_t)(n - pos)) { err = "VMDのボーンフレーム数が不正です"; return false; }
        pos += (size_t)boneCount * 111u;
        out.boneFrameCount = boneCount;
        uint32_t morphCount = 0;
        if (!readU32(morphCount)) { err = "VMDが途中で切れています"; return false; }
        if ((uint64_t)morphCount * 23u > (uint64_t)(n - pos)) { err = "VMDのモーフフレーム数が不正です"; return false; }
        out.morphs.reserve(morphCount);
        for (uint32_t k = 0; k < morphCount; k++) {
            VmdMorphFrame f;
            std::memcpy(f.name, d + pos, 15);
            pos += 15;
            uint32_t fr = 0, wbits = 0;
            readU32(fr); readU32(wbits);   // 長さは上で確認済み
            f.frame = (long)fr;
            std::memcpy(&f.weight, &wbits, 4);
            out.morphs.push_back(f);
        }
        return true;
    }

    // モーフ名(あ/い/う/え/お/ん)で振り分けて、a,i,u,e,o,N ごとのキーフレーム列に戻す
    inline std::map<char, MorphTrack> VmdToTracks(const VmdData& v, const MorphNames& names) {
        const char order[6] = { 'a', 'i', 'u', 'e', 'o', 'N' };
        const std::string nm[6] = { names.a, names.i, names.u, names.e, names.o, names.nasal };
        std::vector<uint8_t> raw[6];
        for (int k = 0; k < 6; k++) raw[k] = SjisEncodeFixed(nm[k], 15);
        std::map<char, MorphTrack> tracks;
        for (char c : order) tracks[c] = {};
        for (auto& f : v.morphs) {
            for (int k = 0; k < 6; k++) {
                if (std::memcmp(f.name, raw[k].data(), 15) == 0) { tracks[order[k]].push_back({ f.frame, f.weight }); break; }
            }
        }
        for (auto& kv : tracks) std::stable_sort(kv.second.begin(), kv.second.end(), [](const std::pair<long, float>& a, const std::pair<long, float>& b) { return a.first < b.first; });
        return tracks;
    }

} // namespace vsq