// gui_win32.cpp -------------------------------------------------------------
// VSQX/VSQ -> VMD 口パク変換 GUI (Windows / Win32 API, 外部ライブラリ不要)
//
// ビルド方法:
//   MinGW-w64:  g++ -O2 -municode -o vsq2vmd_gui.exe gui_win32.cpp -lcomdlg32 -lcomctl32
//   MSVC     :  cl /O2 /EHsc gui_win32.cpp /link comdlg32.lib comctl32.lib
//
// vsqcore.h と同じフォルダに置いてビルドしてください。
// -----------------------------------------------------------------------------
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX   // windows.h が max/min をマクロ定義するのを防ぐ(std::max/std::minが壊れるため必須)
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <sstream>
#include <cwctype>   // towlower
#include <cstdlib>   // wcstod
#include "vsqcore.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// ---- ID ---------------------------------------------------------------
enum {
    ID_OPEN = 1001, ID_TRACKLIST, ID_GENERATE,
    ID_ATTACK, ID_RELEASE, ID_GEM, ID_GAP, ID_MAXW, ID_CONS, ID_PITCHFX, ID_DYNFX, ID_SNAP, ID_REDUCE, ID_HUMAN,
    ID_MODELNAME, ID_FPS, ID_MORPH_A, ID_MORPH_I, ID_MORPH_U, ID_MORPH_E, ID_MORPH_O, ID_MORPH_N,
    ID_STATUS, ID_PREVIEW,
    ID_LBL_ATTACK, ID_LBL_RELEASE, ID_LBL_GEM, ID_LBL_GAP, ID_LBL_MAXW, ID_LBL_CONS,
};

static HWND g_hTrackList, g_hStatus, g_hPreview;
static HWND g_hSlider[11];
static HWND g_hSliderLabel[11];
static HWND g_hModelName, g_hFps, g_hMorph[6];
static vsq::SongData g_song;
static bool g_loaded = false;
static int g_selectedTrack = 0;
static std::wstring g_inputPath, g_baseName;

// UTF-8 <-> UTF-16 変換 (Windows既定APIのみ使用)
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
    return out;
}
static std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len, nullptr, nullptr);
    return out;
}
static std::string ReadFileBytes(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("ファイルを開けません");
    DWORD size = GetFileSize(h, nullptr);
    std::string buf(size, 0);
    DWORD read = 0;
    ReadFile(h, &buf[0], size, &read, nullptr);
    CloseHandle(h);
    return buf;
}

static const wchar_t* SLIDER_LABEL[11] = {
    L"子音の準備の長さ", L"母音から閉じるまで", L"促音「っ」の追加保持",
    L"間が空いたら閉じる目安", L"母音の最大ウェイト", L"子音の口形の強さ",
    L"ピッチによる開きの変化", L"ダイナミクスによる変化",
    L"母音が開く速さ(小さいほど瞬間的)", L"キーフレーム削減の強さ",
    L"人間らしさ(ゆらぎ)"
};
static const int SLIDER_MIN[11] = { 10, 10, 0, 30, 30, 0, 0, 0, 15, 0, 0 };
static const int SLIDER_MAX[11] = { 150, 200, 200, 300, 100, 200, 200, 200, 200, 100, 100 };
static const int SLIDER_DEFAULT[11] = { 70, 60, 70, 90, 75, 100, 100, 100, 60, 30, 50 };
static const wchar_t* SLIDER_UNIT[11] = { L"ms", L"ms", L"ms", L"ms", L"%", L"%", L"%", L"%", L"ms", L"%", L"%" };

static void UpdateSliderLabel(int idx) {
    int val = (int)SendMessage(g_hSlider[idx], TBM_GETPOS, 0, 0);
    wchar_t buf[128];
    wsprintf(buf, L"%s: %d%s", SLIDER_LABEL[idx], val, SLIDER_UNIT[idx]);
    SetWindowText(g_hSliderLabel[idx], buf);
}

static void PopulateTrackList() {
    SendMessage(g_hTrackList, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_song.tracks.size(); i++) {
        std::string preview;
        int shown = 0;
        for (auto& n : g_song.tracks[i]) {
            if (shown >= 20) break;
            if (!n.lyric.empty()) { preview += n.lyric; shown++; }
        }
        std::wostringstream ws;
        ws << L"#" << i << L"  (" << g_song.tracks[i].size() << L"音)  " << Utf8ToWide(preview);
        SendMessage(g_hTrackList, LB_ADDSTRING, 0, (LPARAM)ws.str().c_str());
    }
    if (!g_song.tracks.empty()) SendMessage(g_hTrackList, LB_SETCURSEL, 0, 0);
}

static void UpdatePreview() {
    if (!g_loaded || g_selectedTrack >= (int)g_song.tracks.size()) { SetWindowText(g_hPreview, L""); return; }
    std::wostringstream ws;
    auto& notes = g_song.tracks[g_selectedTrack];
    size_t limit = notes.size() < 300 ? notes.size() : 300;
    for (size_t i = 0; i < limit; i++) {
        auto res = vsq::AnalyzePhoneme(notes[i]);
        const wchar_t* clsName = L"-";
        switch (res.cls) {
        case vsq::ConsClass::Closed: clsName = L"閉"; break;
        case vsq::ConsClass::Alveolar: clsName = L"歯茎"; break;
        case vsq::ConsClass::Velar: clsName = L"軟口蓋"; break;
        case vsq::ConsClass::GlideW: clsName = L"わ"; break;
        case vsq::ConsClass::GlideJ: clsName = L"や"; break;
        default: break;
        }
        ws << Utf8ToWide(notes[i].lyric) << L"\t" << Utf8ToWide(notes[i].phoneme)
            << L"\t[" << clsName << L"]\t" << (res.vowel ? std::wstring(1, res.vowel) : L"-") << L"\r\n";
    }
    if (notes.size() > 300) ws << L"...(先頭300音のみ表示。生成には全" << notes.size() << L"音を使用)\r\n";
    SetWindowText(g_hPreview, ws.str().c_str());
}

static void OnOpenFile(HWND hwnd) {
    wchar_t fileBuf[MAX_PATH] = L"";
    OPENFILENAME ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"VOCALOID ファイル (*.vsqx;*.vsq)\0*.vsqx;*.vsq\0すべてのファイル\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"vsqx / vsq を開く";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileName(&ofn)) return;

    g_inputPath = fileBuf;
    size_t slash = g_inputPath.find_last_of(L"\\/");
    size_t dot = g_inputPath.find_last_of(L'.');
    g_baseName = g_inputPath.substr(slash == std::wstring::npos ? 0 : slash + 1,
        (dot == std::wstring::npos ? g_inputPath.size() : dot) -
        (slash == std::wstring::npos ? 0 : slash + 1));
    try {
        std::string data = ReadFileBytes(g_inputPath);
        std::wstring lower = g_inputPath;
        for (auto& c : lower) c = towlower(c);
        bool isVsqx = lower.size() >= 5 && lower.compare(lower.size() - 5, 5, L".vsqx") == 0;
        g_song = isVsqx ? vsq::ParseVsqx(data) : vsq::ParseVsqSmf(data);
        g_loaded = true;
        g_selectedTrack = 0;
        PopulateTrackList();
        UpdatePreview();
        SetWindowText(g_hStatus, L"読み込み完了。トラックを選択して「VMDを生成」を押してください。");
    }
    catch (std::exception& e) {
        g_loaded = false;
        MessageBoxW(hwnd, Utf8ToWide(e.what()).c_str(), L"読み込みエラー", MB_OK | MB_ICONERROR);
    }
}

static void OnGenerate(HWND hwnd) {
    if (!g_loaded) { MessageBoxW(hwnd, L"先にvsqx/vsqファイルを開いてください", L"確認", MB_OK); return; }

    vsq::MorphParams p;
    p.prepMs = (double)SendMessage(g_hSlider[0], TBM_GETPOS, 0, 0);
    p.releaseMs = (double)SendMessage(g_hSlider[1], TBM_GETPOS, 0, 0);
    p.geminateExtraMs = (double)SendMessage(g_hSlider[2], TBM_GETPOS, 0, 0);
    p.gapMs = (double)SendMessage(g_hSlider[3], TBM_GETPOS, 0, 0);
    p.maxWeight = (double)SendMessage(g_hSlider[4], TBM_GETPOS, 0, 0) / 100.0;
    p.consScale = (double)SendMessage(g_hSlider[5], TBM_GETPOS, 0, 0) / 100.0;

    // ピッチ/ダイナミクスの「変化の強さ」スライダー(0-200%)を、
    // 既定の非対称レンジ(0.8-1.2 / 0.85-1.15)からの偏差にスケールする
    double pitchFx = (double)SendMessage(g_hSlider[6], TBM_GETPOS, 0, 0) / 100.0;
    double dynFx = (double)SendMessage(g_hSlider[7], TBM_GETPOS, 0, 0) / 100.0;
    p.pitchScaleMin = 1.0 - 0.2 * pitchFx;
    p.pitchScaleMax = 1.0 + 0.2 * pitchFx;
    p.dynScaleMin = 1.0 - 0.15 * dynFx;
    p.dynScaleMax = 1.0 + 0.15 * dynFx;
    p.bendSensitivity = 0.6 * pitchFx; // ピッチベンドの揺れの強さも同じスライダーで調整
    p.humanize = (double)SendMessage(g_hSlider[10], TBM_GETPOS, 0, 0) / 100.0;

    p.snapMs = (double)SendMessage(g_hSlider[8], TBM_GETPOS, 0, 0);
    double reducePct = (double)SendMessage(g_hSlider[9], TBM_GETPOS, 0, 0) / 100.0;
    p.reduceEpsilon = reducePct * 0.05; // 0%→0(削減なし) 100%→0.05(強めに間引く)

    wchar_t fpsBuf[32]; GetWindowText(g_hFps, fpsBuf, 32);
    p.fps = wcstod(fpsBuf, nullptr); if (p.fps <= 0) p.fps = 30.0;

    wchar_t nameBuf[256];
    vsq::MorphNames names;
    GetWindowText(g_hMorph[0], nameBuf, 256); names.a = WideToUtf8(nameBuf);
    GetWindowText(g_hMorph[1], nameBuf, 256); names.i = WideToUtf8(nameBuf);
    GetWindowText(g_hMorph[2], nameBuf, 256); names.u = WideToUtf8(nameBuf);
    GetWindowText(g_hMorph[3], nameBuf, 256); names.e = WideToUtf8(nameBuf);
    GetWindowText(g_hMorph[4], nameBuf, 256); names.o = WideToUtf8(nameBuf);
    GetWindowText(g_hMorph[5], nameBuf, 256); names.nasal = WideToUtf8(nameBuf);

    wchar_t modelBuf[256]; GetWindowText(g_hModelName, modelBuf, 256);
    std::string modelName = WideToUtf8(modelBuf);
    if (modelName.empty()) modelName = "LipSync";

    try {
        vsq::TempoMap tempoMap(g_song.resolution, g_song.tempos);
        auto tracks = vsq::BuildMorphTracks(g_song.tracks[g_selectedTrack], tempoMap, p,
            g_song.pitchBendCurves[g_selectedTrack],
            g_song.dynamicsCurves[g_selectedTrack]);

        wchar_t outBuf[MAX_PATH] = L"";
        std::wstring def = g_baseName + L"_lipsync.vmd";
        size_t copyLen = def.size() < (MAX_PATH - 1) ? def.size() : (MAX_PATH - 1);
        wcsncpy(outBuf, def.c_str(), copyLen);
        outBuf[copyLen] = L'\0';
        OPENFILENAME sfn = {};
        sfn.lStructSize = sizeof(sfn);
        sfn.hwndOwner = hwnd;
        sfn.lpstrFilter = L"VMDモーションファイル (*.vmd)\0*.vmd\0";
        sfn.lpstrFile = outBuf;
        sfn.nMaxFile = MAX_PATH;
        sfn.lpstrDefExt = L"vmd";
        sfn.Flags = OFN_OVERWRITEPROMPT;
        sfn.lpstrTitle = L"VMDの保存先";
        if (!GetSaveFileName(&sfn)) return;

        vsq::WriteVmd(WideToUtf8(outBuf), tracks, names, modelName);

        size_t totalKf = 0; long lastFrame = 0;
        for (auto& kv : tracks) { totalKf += kv.second.size(); for (auto& fr : kv.second) lastFrame = std::max(lastFrame, fr.first); }
        // wsprintf は浮動小数点(%f)を扱えないため wostringstream で組み立てる
        std::wostringstream msg;
        msg.setf(std::ios::fixed); msg.precision(1);
        msg << L"生成しました: " << outBuf << L"\n音符数: " << g_song.tracks[g_selectedTrack].size()
            << L" / キーフレーム: " << totalKf << L" / 長さ: " << (lastFrame / p.fps) << L"秒";
        SetWindowText(g_hStatus, msg.str().c_str());
    }
    catch (std::exception& e) {
        MessageBoxW(hwnd, Utf8ToWide(e.what()).c_str(), L"生成エラー", MB_OK | MB_ICONERROR);
    }
}

static HWND CreateLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent, nullptr, nullptr, nullptr);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
        auto setFont = [&](HWND h) { SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE); };

        HWND btnOpen = CreateWindowW(L"BUTTON", L"vsqx / vsq を開く...", WS_CHILD | WS_VISIBLE,
            12, 10, 200, 28, hwnd, (HMENU)(INT_PTR)ID_OPEN, nullptr, nullptr);
        setFont(btnOpen);

        CreateLabel(hwnd, L"トラック:", 12, 48, 100, 18);
        g_hTrackList = CreateWindowW(L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
            12, 68, 560, 70, hwnd, (HMENU)(INT_PTR)ID_TRACKLIST, nullptr, nullptr);
        setFont(g_hTrackList);

        // スライダー6個を2列×3段で配置(左列 i=0,2,4 / 右列 i=1,3,5)
        {
            int leftX = 12, rightX = 310, y0 = 148, rowH = 52;
            for (int i = 0; i < 11; i++) {
                int row = i / 2;
                int x = (i % 2 == 0) ? leftX : rightX;
                int y = y0 + row * rowH;
                g_hSliderLabel[i] = CreateLabel(hwnd, L"", x, y, 280, 18);
                setFont(g_hSliderLabel[i]);
                g_hSlider[i] = CreateWindowW(TRACKBAR_CLASS, nullptr, WS_CHILD | WS_VISIBLE | TBS_HORZ,
                    x, y + 18, 280, 26, hwnd, (HMENU)(INT_PTR)(ID_ATTACK + i), nullptr, nullptr);
                SendMessage(g_hSlider[i], TBM_SETRANGE, TRUE, MAKELONG(SLIDER_MIN[i], SLIDER_MAX[i]));
                SendMessage(g_hSlider[i], TBM_SETPOS, TRUE, SLIDER_DEFAULT[i]);
            }
        }
        for (int i = 0; i < 11; i++) UpdateSliderLabel(i);

        int detY = 148 + 6 * 52 + 10;
        CreateLabel(hwnd, L"モデル名", 12, detY, 90, 18);
        g_hModelName = CreateWindowW(L"EDIT", L"LipSync", WS_CHILD | WS_VISIBLE | WS_BORDER, 12, detY + 18, 130, 22, hwnd, (HMENU)(INT_PTR)ID_MODELNAME, nullptr, nullptr);
        CreateLabel(hwnd, L"fps", 152, detY, 40, 18);
        g_hFps = CreateWindowW(L"EDIT", L"30", WS_CHILD | WS_VISIBLE | WS_BORDER, 152, detY + 18, 50, 22, hwnd, (HMENU)(INT_PTR)ID_FPS, nullptr, nullptr);
        const wchar_t* morphLbl[6] = { L"あ", L"い", L"う", L"え", L"お", L"ん" };
        int mx = 220;
        for (int i = 0; i < 6; i++) {
            CreateLabel(hwnd, morphLbl[i], mx, detY, 20, 18);
            g_hMorph[i] = CreateWindowW(L"EDIT", morphLbl[i], WS_CHILD | WS_VISIBLE | WS_BORDER, mx, detY + 18, 50, 22, hwnd, (HMENU)(INT_PTR)(ID_MORPH_A + i), nullptr, nullptr);
            setFont(g_hMorph[i]);
            mx += 58;
        }
        setFont(g_hModelName); setFont(g_hFps);

        HWND btnGen = CreateWindowW(L"BUTTON", L"VMDを生成...", WS_CHILD | WS_VISIBLE,
            12, detY + 50, 578, 32, hwnd, (HMENU)(INT_PTR)ID_GENERATE, nullptr, nullptr);
        setFont(btnGen);

        g_hStatus = CreateLabel(hwnd, L"vsqx / vsq ファイルを開いてください。", 12, detY + 92, 578, 20);
        setFont(g_hStatus);

        g_hPreview = CreateWindowW(L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            12, detY + 118, 578, 160, hwnd, (HMENU)(INT_PTR)ID_PREVIEW, nullptr, nullptr);
        setFont(g_hPreview);
        return 0;
    }
    case WM_HSCROLL: {
        for (int i = 0; i < 11; i++) if ((HWND)lParam == g_hSlider[i]) UpdateSliderLabel(i);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_OPEN) OnOpenFile(hwnd);
        else if (LOWORD(wParam) == ID_GENERATE) OnGenerate(hwnd);
        else if (LOWORD(wParam) == ID_TRACKLIST && HIWORD(wParam) == LBN_SELCHANGE) {
            g_selectedTrack = (int)SendMessage(g_hTrackList, LB_GETCURSEL, 0, 0);
            UpdatePreview();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ANSI版 WinMain を使用(引数(コマンドライン文字列)は未使用のため、
// wWinMain特有のリンカ/エントリポイント設定が不要になり、Visual Studio・
// MinGWのどちらでも追加設定なしでビルドできる)
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"Vsq2VmdGuiWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"VSQX/VSQ → VMD 口パク変換",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 630, 776,
        nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}