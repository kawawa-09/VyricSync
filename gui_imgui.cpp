// gui_imgui.cpp --------------------------------------------------------------
// VSQX/VSQ -> VMD 口パク変換 GUI (Dear ImGui版, Win32 + DirectX11)
//
// 必要なもの:
//   Dear ImGui 本体一式 (https://github.com/ocornut/imgui, masterブランチ):
//     imgui.h, imgui.cpp, imgui_draw.cpp, imgui_tables.cpp, imgui_widgets.cpp
//     backends/imgui_impl_win32.h/.cpp
//     backends/imgui_impl_dx11.h/.cpp
//
// ビルド例 (開発者コマンドプロンプト、clを使用):
//   cl /std:c++17 /O2 /EHsc /utf-8 /DUNICODE /D_UNICODE /I imgui /I imgui\backends ^
//      gui_imgui.cpp imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp ^
//      imgui\imgui_widgets.cpp imgui\backends\imgui_impl_win32.cpp imgui\backends\imgui_impl_dx11.cpp ^
//      /link d3d11.lib d3dcompiler.lib comdlg32.lib windowscodecs.lib ole32.lib
//
// 同じフォルダに置くもの: vsqcore.h, mmdmodel.h
// ドッキング(タブ結合・上下左右への分割・境界線での大きさ変更)は独自実装なので、ImGuiのブランチ(master / docking)を問わず動く。
//   ウィンドウを本体の外へ出す機能だけは、docking ブランチの viewport 機能が必要 (imgui_internal.h も使う)。
// テクスチャ: bmp/png/jpg/gif/tiff (WIC) と tga (内蔵デコーダ) に対応。
// toon: モデルが指定する toon画像を読み込む。共有toon(toon01.bmp〜)は、モデルのフォルダ、または
//   exeと同じフォルダの toon フォルダ内に MMD付属の toon01.bmp〜toon10.bmp を置くとそれを使い、
//   無ければ標準toon風のグラデーションで代用する。
// (任意) stb_image.h を同じフォルダに置くと、さらに他の形式も読める
// -----------------------------------------------------------------------------
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <d3d11.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <map>
#include <iomanip>
#include <memory>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <ctime>

#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wincodec.h>
#include <objbase.h>

// stb_image.h があれば利用する (WICで読めない tga などのフォールバック)
#if defined(__has_include)
#if __has_include("stb_image.h")
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define HAVE_STB_IMAGE 1
#endif
#endif

#include "thirdparty/imgui/imgui.h"
#include "thirdparty/imgui/imgui_internal.h"   // ドラッグ中のウィンドウの取得 (ガイド表示・吸着)
#include "thirdparty/imgui/backends/imgui_impl_win32.h"
#include "thirdparty/imgui/backends/imgui_impl_dx11.h"
#include "vsqcore.h"
#include "projectfile.h"
#include "mmdmodel.h"
#include "wavdata.h"
#include "f0analysis.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ---------------------------------------------------------------------------
// D3D11 まわり
// ---------------------------------------------------------------------------
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}
static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}
static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0; sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.SampleDesc.Count = 1; sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.OutputWindow = hWnd;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;
    CreateRenderTarget();
    return true;
}
static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) { g_ResizeWidth = (UINT)LOWORD(lParam); g_ResizeHeight = (UINT)HIWORD(lParam); }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// 文字列変換ヘルパ
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// アプリ状態 & テーマ設定
// ---------------------------------------------------------------------------
static HWND g_hwnd = nullptr;

// ---------------------------------------------------------------------------
// 翻訳: exeと同じフォルダの res/lang/<言語コード>.json (キー = 画面に出す日本語そのもの)
//   日本語が標準で、翻訳ファイルに無い文字はそのまま日本語で表示する。
// ---------------------------------------------------------------------------
static std::map<std::string, std::string> g_tr;
static const char* T(const char* ja) {
    if (g_tr.empty()) return ja;
    auto it = g_tr.find(ja);
    return it == g_tr.end() ? ja : it->second.c_str();
}
struct LangInfo { std::string code, name; };
static std::vector<LangInfo> g_langs;          // res/lang にある翻訳
static std::string g_langSetting = "auto";     // "auto" / "ja" / 言語コード
static bool g_langDirty = true;                // 次のフレームの頭で翻訳を読み直す
static bool g_openSettings = false;            // 設定ダイアログを開く
static bool g_vsync = true;
static float g_customAccent[3] = { 0.850f, 0.430f, 0.200f };

// --- WAV (読み込み・再生・波形表示) ---
static wavdata::Audio g_wav;
static wavdata::Peaks g_wavPeaks;
static bool g_wavLoaded = false;
static std::wstring g_wavPath, g_startupWavPath;
static int g_wavStartFrame = 0;                                   // WAVが始まるフレーム (VMDのフレーム番号)
static float g_wavVolume = 1.0f;
static bool g_wavMute = false;
static bool g_prShowWave = true;                                  // ピアノロールに波形を表示
static float g_wavColor[4] = { 0.35f, 0.65f, 0.95f, 0.50f };      // 波形の色

// --- ボーカルのみのWAV (F0とダイナミクスを取り出して、生成結果に反映する) ---
static f0::Result g_f0;
static bool g_vocalLoaded = false;
static std::wstring g_vocalPath, g_startupVocalPath;
static double g_vocalDuration = 0.0;
static int g_vocalStartFrame = 0;          // ボーカルWAVが始まるフレーム
static float g_wavPitchMix = 100.0f;       // F0をピッチ(口形)へ反映する割合 (%)
static float g_wavDynMix = 100.0f;         // 音量をダイナミクスへ反映する割合 (%)
static bool g_prShowF0 = true;             // 検出したF0をピアノロールに重ねる
static int g_vocalSerial = 0;

// ---------------------------------------------------------------------------
// アイコン: フォントに頼らず、角丸を基調にした図形で自前描画する (位置ずれ・環境差が出ない)
//   座標は アイコン中心を原点、半幅=1 の単位系
// ---------------------------------------------------------------------------
enum { ICN_NONE = 0, ICN_FOLDER, ICN_HOME, ICN_PLAY, ICN_PAUSE, ICN_STOP, ICN_SAVE, ICN_EXPORT, ICN_MUSIC,
       ICN_MODEL, ICN_ADD, ICN_DELETE, ICN_BACK, ICN_FULL, ICN_WINDOW, ICN_DOC, ICN_LOOP,
       ICN_HEART, ICN_TAG, ICN_EDIT, ICN_TRASH, ICN_GEAR, ICN_SEARCH, ICN_CHECK, ICN_WAVE, ICN_COUNT };

// res/images/icon_<名前>.png (白・透明背景。表示時に文字色で着色される)
static const char* kIconNames[ICN_COUNT] = { "", "folder", "home", "play", "pause", "stop", "save", "export", "music",
    "model", "add", "delete", "back", "full", "window", "doc", "loop", "heart", "tag", "edit", "trash", "gear", "search", "check", "wave" };
static ID3D11ShaderResourceView* LoadIconTexture(int icon);   // 後ろで定義 (テクスチャ読み込みの後)

static void DrawIconVec(ImDrawList* dl, int icon, ImVec2 c, float sz, ImU32 col) {   // 画像が無いときの図形描画
    c = ImVec2(std::round(c.x), std::round(c.y));   // ピクセルにそろえて、ぼやけ・ずれを防ぐ
    const float h = sz * 0.5f;
    const float th = std::max(1.6f, sz * 0.12f);
    auto P = [&](float x, float y) { return ImVec2(c.x + x * h, c.y + y * h); };
    auto Fill = [&](float x0, float y0, float x1, float y1, float r) { dl->AddRectFilled(P(x0, y0), P(x1, y1), col, r * h); };
    auto Line = [&](float x0, float y0, float x1, float y1, float w) { dl->AddLine(P(x0, y0), P(x1, y1), col, w); };
    auto Poly = [&](std::initializer_list<ImVec2> pts) {
        for (const ImVec2& q : pts) dl->PathLineTo(q);
        dl->PathStroke(col, 0, th);
    };
    switch (icon) {
    case ICN_FOLDER:
        Fill(-1.0f, -0.75f, -0.05f, -0.2f, 0.25f);          // 見出しのタブ
        Fill(-1.0f, -0.5f, 1.0f, 0.8f, 0.3f);               // 本体
        break;
    case ICN_HOME:
        dl->AddTriangleFilled(P(-1.0f, -0.05f), P(0.0f, -0.95f), P(1.0f, -0.05f), col);
        Fill(-0.7f, -0.1f, 0.7f, 0.9f, 0.2f);
        break;
    case ICN_PLAY:
        dl->AddTriangleFilled(P(-0.55f, -0.85f), P(-0.55f, 0.85f), P(0.9f, 0.0f), col);
        dl->AddTriangle(P(-0.55f, -0.85f), P(-0.55f, 0.85f), P(0.9f, 0.0f), col, th * 0.8f);   // 角を少し丸く見せる
        break;
    case ICN_PAUSE:
        Fill(-0.8f, -0.85f, -0.12f, 0.85f, 0.25f);
        Fill(0.12f, -0.85f, 0.8f, 0.85f, 0.25f);
        break;
    case ICN_STOP:
        Fill(-0.8f, -0.8f, 0.8f, 0.8f, 0.35f);
        break;
    case ICN_SAVE:
        dl->AddRect(P(-0.9f, -0.9f), P(0.9f, 0.9f), col, 0.3f * h, 0, th);   // 本体の枠
        Fill(-0.45f, -0.9f, 0.4f, -0.25f, 0.1f);                              // 上のラベル
        Fill(-0.5f, 0.15f, 0.5f, 0.9f, 0.12f);                                // 下のシャッター
        break;
    case ICN_EXPORT:
        Line(0.0f, -0.95f, 0.0f, 0.15f, th);
        dl->AddTriangleFilled(P(-0.55f, -0.15f), P(0.55f, -0.15f), P(0.0f, 0.55f), col);
        Poly({ P(-0.9f, 0.2f), P(-0.9f, 0.85f), P(0.9f, 0.85f), P(0.9f, 0.2f) });
        break;
    case ICN_MUSIC:   // 八分音符 (大きな音符頭 + 太い棒 + 旗)
        dl->AddCircleFilled(P(-0.38f, 0.5f), 0.5f * h, col, 20);
        Fill(-0.08f, -0.95f, 0.14f, 0.5f, 0.06f);
        dl->AddQuadFilled(P(0.14f, -0.95f), P(0.88f, -0.5f), P(0.78f, 0.0f), P(0.14f, -0.4f), col);
        break;
    case ICN_MODEL:
        dl->AddCircleFilled(P(0.0f, -0.45f), 0.42f * h, col, 20);
        dl->AddRectFilled(P(-0.85f, 0.15f), P(0.85f, 0.95f), col, 0.6f * h, ImDrawFlags_RoundCornersTop);
        break;
    case ICN_ADD:
        Fill(-0.2f, -0.9f, 0.2f, 0.9f, 0.2f);
        Fill(-0.9f, -0.2f, 0.9f, 0.2f, 0.2f);
        break;
    case ICN_DELETE:
        Line(-0.7f, -0.7f, 0.7f, 0.7f, th * 1.3f);
        Line(0.7f, -0.7f, -0.7f, 0.7f, th * 1.3f);
        break;
    case ICN_BACK:
        Line(0.9f, 0.0f, -0.7f, 0.0f, th * 1.2f);
        Poly({ P(-0.05f, -0.7f), P(-0.8f, 0.0f), P(-0.05f, 0.7f) });
        break;
    case ICN_FULL:   // 外向きの4つの角
        Poly({ P(-0.9f, -0.3f), P(-0.9f, -0.9f), P(-0.3f, -0.9f) });
        Poly({ P(0.3f, -0.9f), P(0.9f, -0.9f), P(0.9f, -0.3f) });
        Poly({ P(0.9f, 0.3f), P(0.9f, 0.9f), P(0.3f, 0.9f) });
        Poly({ P(-0.3f, 0.9f), P(-0.9f, 0.9f), P(-0.9f, 0.3f) });
        break;
    case ICN_WINDOW: // 内向きの4つの角
        Poly({ P(-0.9f, -0.3f), P(-0.3f, -0.3f), P(-0.3f, -0.9f) });
        Poly({ P(0.3f, -0.9f), P(0.3f, -0.3f), P(0.9f, -0.3f) });
        Poly({ P(0.9f, 0.3f), P(0.3f, 0.3f), P(0.3f, 0.9f) });
        Poly({ P(-0.3f, 0.9f), P(-0.3f, 0.3f), P(-0.9f, 0.3f) });
        break;
    case ICN_LOOP: {  // 円弧 + 矢じり
        const float a1 = 5.3f;
        dl->PathArcTo(c, 0.68f * h, 0.5f, a1, 20);
        dl->PathStroke(col, 0, th);
        const float ex = cosf(a1) * 0.68f, ey = sinf(a1) * 0.68f, tx = -sinf(a1), ty = cosf(a1), nx = cosf(a1), ny = sinf(a1);
        dl->AddTriangleFilled(P(ex + tx * 0.55f, ey + ty * 0.55f), P(ex + nx * 0.45f, ey + ny * 0.45f), P(ex - nx * 0.45f, ey - ny * 0.45f), col);
        break; }
    case ICN_HEART:
        dl->AddCircleFilled(P(-0.42f, -0.3f), 0.5f * h, col, 16);
        dl->AddCircleFilled(P(0.42f, -0.3f), 0.5f * h, col, 16);
        dl->AddTriangleFilled(P(-0.9f, -0.05f), P(0.9f, -0.05f), P(0.0f, 0.95f), col);
        break;
    case ICN_TAG: {
        const ImVec2 pts[5] = { P(-0.95f, 0.0f), P(-0.35f, -0.75f), P(0.9f, -0.75f), P(0.9f, 0.75f), P(-0.35f, 0.75f) };
        dl->AddPolyline(pts, 5, col, ImDrawFlags_Closed, th);
        dl->AddCircleFilled(P(-0.2f, 0.0f), 0.17f * h, col, 10);
        break; }
    case ICN_EDIT:
        dl->AddQuadFilled(P(0.55f, -0.95f), P(0.95f, -0.55f), P(-0.35f, 0.75f), P(-0.75f, 0.35f), col);
        dl->AddTriangleFilled(P(-0.75f, 0.35f), P(-0.35f, 0.75f), P(-0.95f, 0.95f), col);
        break;
    case ICN_TRASH:
        Line(-0.85f, -0.55f, 0.85f, -0.55f, th);
        Fill(-0.28f, -0.95f, 0.28f, -0.55f, 0.12f);
        Poly({ P(-0.6f, -0.3f), P(-0.5f, 0.92f), P(0.5f, 0.92f), P(0.6f, -0.3f) });
        break;
    case ICN_GEAR:
        dl->AddCircle(c, 0.55f * h, col, 20, th * 1.4f);
        dl->AddCircleFilled(c, 0.2f * h, col, 12);
        for (int k = 0; k < 8; k++) {
            const float a = (float)k * 0.7853982f;
            Line(cosf(a) * 0.62f, sinf(a) * 0.62f, cosf(a) * 0.95f, sinf(a) * 0.95f, th * 1.8f);
        }
        break;
    case ICN_SEARCH:
        dl->AddCircle(P(-0.15f, -0.15f), 0.6f * h, col, 20, th);
        Line(0.3f, 0.3f, 0.95f, 0.95f, th * 1.4f);
        break;
    case ICN_CHECK:
        Poly({ P(-0.8f, 0.05f), P(-0.25f, 0.65f), P(0.85f, -0.65f) });
        break;
    case ICN_WAVE: {
        const float hs[5] = { 0.3f, 0.65f, 0.95f, 0.55f, 0.25f };
        for (int k = 0; k < 5; k++) Fill(-0.8f + k * 0.4f - 0.13f, -hs[k], -0.8f + k * 0.4f + 0.13f, hs[k], 0.13f);
        break; }
    case ICN_DOC:
        dl->AddRect(P(-0.65f, -0.95f), P(0.65f, 0.95f), col, 0.25f * h, 0, th);
        Line(-0.3f, -0.25f, 0.3f, -0.25f, th);
        Line(-0.3f, 0.15f, 0.3f, 0.15f, th);
        Line(-0.3f, 0.55f, 0.1f, 0.55f, th);
        break;
    default: break;
    }
}

static void DrawIcon(ImDrawList* dl, int icon, ImVec2 c, float sz, ImU32 col) {
    if (ID3D11ShaderResourceView* tex = LoadIconTexture(icon)) {
        c = ImVec2(std::round(c.x), std::round(c.y));
        const float h = sz * 0.5f;
        dl->AddImage((ImTextureID)(intptr_t)tex, ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), ImVec2(0, 0), ImVec2(1, 1), col);
        return;
    }
    DrawIconVec(dl, icon, c, sz, col);
}

// アイコン + 文字のボタン。text が空ならアイコンだけ。幅が広いボタンは左寄せ、狭いボタンは中央
static bool IconButton(int icon, const char* text, ImVec2 size = ImVec2(0, 0)) {
    const ImGuiStyle& st = ImGui::GetStyle();
    float isz = std::round(ImGui::GetFontSize() * 0.95f);
    if (((int)isz) % 2) isz += 1.0f;   // 偶数にして中心をピクセルにそろえる
    const bool hasText = text && text[0];
    const ImVec2 ts = hasText ? ImGui::CalcTextSize(text) : ImVec2(0.0f, 0.0f);
    const float gap = hasText ? 8.0f : 0.0f;
    const float total = isz + gap + ts.x;
    if (size.x == 0.0f) size.x = total + st.FramePadding.x * 2.0f + 6.0f;
    else if (size.x < 0.0f) size.x = std::max(1.0f, ImGui::GetContentRegionAvail().x + size.x);
    if (size.y <= 0.0f) size.y = ImGui::GetFrameHeight();
    char id[160];
    snprintf(id, sizeof(id), "###ib_%d_%s", icon, hasText ? text : "");
    const bool pressed = ImGui::Button(id, size);
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    const float midY = (mn.y + mx.y) * 0.5f;
    const float x0 = (size.x > total + 70.0f) ? mn.x + 14.0f : mn.x + (size.x - total) * 0.5f;
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    DrawIcon(dl, icon, ImVec2(x0 + isz * 0.5f, midY), isz, col);
    if (hasText) dl->AddText(ImVec2(x0 + isz + gap, midY - ts.y * 0.5f), col, text);
    return pressed;
}

static bool IconMenuItem(int icon, const char* text, const char* shortcut = nullptr, bool selected = false, bool enabled = true) {
    const float fs = ImGui::GetFontSize();
    const float spW = std::max(1.0f, ImGui::CalcTextSize(" ").x);
    std::string label(std::max(1, (int)std::ceil(fs * 1.5f / spW)), ' ');
    label += text;
    label += "###";
    label += text;
    const bool r = ImGui::MenuItem(label.c_str(), shortcut, selected, enabled);
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    DrawIcon(ImGui::GetWindowDrawList(), icon, ImVec2(mn.x + fs * 0.6f, (mn.y + mx.y) * 0.5f), fs * 0.85f,
        ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled));
    return r;
}

// ---------------------------------------------------------------------------
// スタート画面 / 最近使ったファイル
// ---------------------------------------------------------------------------
struct RecentItem {
    std::wstring path;
    std::string sub;    // 内容の短い説明 (トラック数 / 元のファイル名)
    std::string time;   // 最後に開いた日時
    bool fav = false;   // お気に入り
    std::string cat;    // 分類 (空 = 未分類)
};
static std::vector<RecentItem> g_recent;
static const size_t kRecentMax = 100;
static std::vector<std::string> g_categories;   // ユーザーが作った分類
static int g_startCat = 0;                      // 0:すべて 1:お気に入り 2:プロジェクト 3:曲 4〜:ユーザー分類
static char g_startSearch[128] = "";
static bool g_showStart = true;       // スタート画面を表示中
static bool g_startOnLaunch = true;   // 起動時にスタート画面を表示する
static vsq::SongData g_song;
static bool g_loaded = false;
static int g_selectedTrack = 0;
static std::wstring g_inputPath, g_baseName;
static std::string g_statusText = "vsqx / vsq ファイルを開いてください。";

// パラメータ
static vsq::MorphParams g_p;
static float g_pitchFxPct = 100.0f, g_dynFxPct = 100.0f, g_reducePct = 30.0f;
static float g_smoothMacro = 0.0f;   // 「滑らかさ(まとめて調整)」スライダーの位置 0〜100%
static char g_modelName[256] = "LipSync";
static char g_morphA[64] = "\xE3\x81\x82", g_morphI[64] = "\xE3\x81\x84", g_morphU[64] = "\xE3\x81\x86";
static char g_morphE[64] = "\xE3\x81\x88", g_morphO[64] = "\xE3\x81\x8A", g_morphN[64] = "\xE3\x82\x93";

// テーマスタイル設定
static int g_themeBase = 0; // 0: Dark, 1: White
static float g_accentColor[4] = { 0.850f, 0.430f, 0.200f, 1.00f };
static float g_clearColor[4] = { 0.120f, 0.120f, 0.135f, 1.00f };

// ピアノロール表示パラメータ
static float g_prScrollX = 0.0f;
static float g_prZoomX = 150.0f;
static float g_prRowHeight = 18.0f;
static bool g_prFullScreen = false;
static bool g_prShowPitch = true;    // ピアノロールにピッチ曲線を重ねる
static bool g_prShowDyn = true;      // Dynamicsレーンを出す
static bool g_prShowVel = true;      // Velocityレーンを出す
static bool g_prShowMini = true;     // ノートの手前にミニノートを出す

// ドッキングウィンドウの表示状態
static const int kWinCount = 7;
static bool g_winOpen[kWinCount] = { true, true, true, true, true, true, true };
static bool g_allowDetach = true;     // 本体の外へウィンドウを分離できるようにする (起動時に反映)
static bool g_resetLayout = false;
static bool g_easyDock = true;        // ドラッグ中に水色のドッキングガイドを出して、離した場所にドッキングする

// 独自ドッキングの状態 (設定ファイルに保存して、次回起動時に復元する)
struct FloatRect { float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; bool valid = false; };
static FloatRect g_floatRect[kWinCount];       // 浮かせたウィンドウの位置と大きさ
static bool g_isFloating[kWinCount] = {};      // true: 浮いている / false: ドッキング中
static bool g_floatApply[kWinCount] = {};      // 次のフレームで g_floatRect の位置・大きさを適用する
static std::string g_dockLayoutStr;            // ドッキングの木 (文字列)
static float g_snapPx = 14.0f;        // ウィンドウを動かすとき、辺がこの距離(px)以内なら吸着する (0で無効)
static bool g_layoutLocked = false;   // 配置を固定 (移動・ドッキング解除・閉じる・ドッキング操作を禁止)
static bool g_layoutLockSize = false; // 固定中、サイズ比率(境界線)も動かせなくする
static int g_dockRequest = 0;         // 1: 浮いているウィンドウを本体に戻す  2: すべてを1つのタブグループにまとめる

// 口パク用モーフの割り当てスロット (a,i,u,e,o,N の順)
static const char kSlotKeys[6] = { 'a', 'i', 'u', 'e', 'o', 'N' };
static char* const kMorphBufs[6] = { g_morphA, g_morphI, g_morphU, g_morphE, g_morphO, g_morphN };
static const char* const kSlotLabels[6] = { "\xE3\x81\x82", "\xE3\x81\x84", "\xE3\x81\x86", "\xE3\x81\x88", "\xE3\x81\x8A", "\xE3\x82\x93" };

// MMDモデル / プレビュー / 再生
static mmd::Model g_model;
static bool g_modelLoaded = false;
static std::wstring g_modelPath;
static bool g_camCloseup = true;                 // true: 顔アップ, false: 全身
static float g_camYaw = 0.0f, g_camPitch = 0.0f, g_camDist = 10.0f;
static float g_camTarget[3] = { 0, 10, 0 };
static bool g_playing = false, g_loop = true;
static double g_playTime = 0.0;                  // 再生位置 (秒)
static float g_playSpeed = 1.0f;
// 開いたプロジェクトファイル(.v2v)に入っていたVMD (元のvsqxが見つからないとき、これをプレビューと書き出しに使う)
static std::vector<uint8_t> g_packedVmd;
static std::map<char, vsq::MorphTrack> g_packedTracks;
static bool g_packedActive = false;
static std::map<char, vsq::MorphTrack> g_prevTracks; // プレビュー用に生成したモーフキーフレーム
static double g_prevDuration = 0.0;
static int g_songSerial = 0;                     // 曲を読み込むたびに増える (再生成トリガ用)

// 自動設定 (起動時に復元する内容)
static bool g_autoOpenLast = true;
static std::wstring g_startupInputPath, g_startupModelPath;
static int g_restoreTrack = 0;
static int g_winX = 100, g_winY = 100, g_winW = 1180, g_winH = 820;
static bool g_winMax = false;
static bool g_hasSavedWindow = false;

// ---------------------------------------------------------------------------
// カラー補正・視認性ヘルパー関数
// ---------------------------------------------------------------------------
static ImU32 GetContrastingTextColor(float r, float g, float b) {
    float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    return (lum > 0.52f) ? IM_COL32(20, 20, 20, 255) : IM_COL32(255, 255, 255, 255);
}

static ImVec4 AdjustAccentForTheme(const float rawColor[4], int themeBase) {
    float r = rawColor[0], g = rawColor[1], b = rawColor[2], a = rawColor[3];
    float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;

    if (themeBase == 0) {
        if (lum < 0.25f) {
            float factor = 0.25f / std::max(0.01f, lum);
            r = std::min(1.0f, r * factor + 0.15f);
            g = std::min(1.0f, g * factor + 0.15f);
            b = std::min(1.0f, b * factor + 0.15f);
        }
    }
    else {
        if (lum > 0.70f) {
            float factor = 0.65f / lum;
            r = r * factor;
            g = g * factor;
            b = b * factor;
        }
    }
    return ImVec4(r, g, b, a);
}

// ---------------------------------------------------------------------------
// スタイル設定機能
// ---------------------------------------------------------------------------
static void ApplyCustomStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    if (g_themeBase == 0) {
        ImGui::StyleColorsDark();
    }
    else {
        ImGui::StyleColorsLight();
    }

    s.WindowRounding = 6.0f; s.ChildRounding = 4.0f; s.FrameRounding = 4.0f;
    s.GrabRounding = 4.0f; s.TabRounding = 4.0f; s.PopupRounding = 4.0f;
    s.WindowPadding = ImVec2(10, 10); s.FramePadding = ImVec2(8, 4); s.ItemSpacing = ImVec2(8, 6);
    s.ScrollbarRounding = 6.0f; s.ScrollbarSize = 12.0f; s.GrabMinSize = 10.0f;

    ImVec4 accent = AdjustAccentForTheme(g_accentColor, g_themeBase);
    ImVec4 accentH = ImVec4(std::min(1.0f, accent.x * 1.20f), std::min(1.0f, accent.y * 1.20f), std::min(1.0f, accent.z * 1.20f), 1.00f);
    ImVec4 accentActive = ImVec4(accent.x * 0.85f, accent.y * 0.85f, accent.z * 0.85f, 1.00f);

    ImVec4 bgDark, panelGray, frameGray, frameHovered, tabUnfocused;

    if (g_themeBase == 0) {
        bgDark = ImVec4(0.070f, 0.070f, 0.072f, 1.00f);        // ほぼ無彩色の黒
        panelGray = ImVec4(0.105f, 0.105f, 0.108f, 1.00f);
        frameGray = ImVec4(0.165f, 0.165f, 0.170f, 1.00f);
        frameHovered = ImVec4(0.225f, 0.225f, 0.230f, 1.00f);
        tabUnfocused = ImVec4(0.130f, 0.130f, 0.134f, 1.00f);
    }
    else {
        bgDark = ImVec4(0.880f, 0.885f, 0.900f, 1.00f);
        panelGray = ImVec4(0.960f, 0.965f, 0.970f, 1.00f);
        frameGray = ImVec4(0.850f, 0.855f, 0.870f, 1.00f);
        frameHovered = ImVec4(0.780f, 0.785f, 0.800f, 1.00f);
        tabUnfocused = ImVec4(0.820f, 0.825f, 0.840f, 1.00f);
    }

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = bgDark;
    c[ImGuiCol_ChildBg] = panelGray;
    c[ImGuiCol_PopupBg] = panelGray;
    c[ImGuiCol_FrameBg] = frameGray;
    c[ImGuiCol_FrameBgHovered] = frameHovered;
    c[ImGuiCol_FrameBgActive] = ImVec4(frameGray.x * 0.9f, frameGray.y * 0.9f, frameGray.z * 0.9f, 1.00f);
    c[ImGuiCol_Tab] = tabUnfocused;
    c[ImGuiCol_TabHovered] = accentH;
    c[ImGuiCol_TabActive] = accent;
    c[ImGuiCol_TabUnfocused] = tabUnfocused;
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(tabUnfocused.x * 0.95f, tabUnfocused.y * 0.95f, tabUnfocused.z * 0.95f, 1.00f);
    c[ImGuiCol_ModalWindowDimBg] = (g_themeBase == 0) ? ImVec4(0.0f, 0.0f, 0.0f, 0.75f) : ImVec4(0.0f, 0.0f, 0.0f, 0.55f);   // 設定ダイアログの周りを暗く
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 19090
    c[ImGuiCol_TabSelectedOverline] = accent;           // 選択中タブの上線 (標準は青)
    c[ImGuiCol_TabDimmedSelectedOverline] = accent;
#endif
    // ボタンは普段は控えめな灰色、触れたときにアクセント色 (主要な操作だけ個別にアクセント色で描く)
    c[ImGuiCol_Button] = frameGray;
    c[ImGuiCol_ButtonHovered] = ImVec4(accent.x, accent.y, accent.z, 0.75f);
    c[ImGuiCol_ButtonActive] = accentActive;
    c[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    c[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.75f);
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accentH;
    c[ImGuiCol_CheckMark] = accent;

    g_clearColor[0] = bgDark.x; g_clearColor[1] = bgDark.y; g_clearColor[2] = bgDark.z; g_clearColor[3] = bgDark.w;

#ifdef IMGUI_HAS_VIEWPORT
    // 本体の外に出たウィンドウは角丸・半透明にできないので無効化
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        s.WindowRounding = 0.0f;
        s.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
#endif
}

// ---------------------------------------------------------------------------
// 設定の保存・読み込み (JSON)
//   - プリセット(手動): 生成パラメータ / 出力設定 / テーマ
//   - 自動設定: 上記 + 最後に開いたファイル・ウィンドウ位置・表示設定など。
//     exe と同じフォルダの VyricSync_settings.json に、変更されたとき・終了時に自動保存し、
//     次回起動時に自動で読み込む。
// ---------------------------------------------------------------------------
static std::string JsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
            else o += (char)c;
        }
    }
    return o;
}

// フラットな {"key": 値, ...} だけを扱う簡易パーサ。値はすべて文字列として返す
// (true/false は "1"/"0")。旧形式のプリセットファイルもそのまま読める。
static std::map<std::string, std::string> ParseFlatJson(const std::string& text) {
    std::map<std::string, std::string> kv;
    size_t i = 0, n = text.size();
    if (n >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) i = 3;

    auto ws = [&]() { while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) i++; };
    auto hex4 = [&](uint32_t& v) -> bool {
        if (i + 4 > n) return false;
        v = 0;
        for (int k = 0; k < 4; k++) {
            char h = text[i + k]; v <<= 4;
            if (h >= '0' && h <= '9') v |= (uint32_t)(h - '0');
            else if (h >= 'a' && h <= 'f') v |= (uint32_t)(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v |= (uint32_t)(h - 'A' + 10);
            else return false;
        }
        i += 4;
        return true;
        };
    auto readString = [&](std::string& out) -> bool {
        if (i >= n || text[i] != '"') return false;
        i++;
        while (i < n && text[i] != '"') {
            char c = text[i++];
            if (c != '\\' || i >= n) { out += c; continue; }
            char e = text[i++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                uint32_t cp;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= n && text[i] == '\\' && text[i + 1] == 'u') {
                    size_t save = i; i += 2;
                    uint32_t lo;
                    if (hex4(lo) && lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    else i = save;
                }
                out += vsq::Utf8Encode(cp);
                break;
            }
            default: out += e; break; // \" \\ \/
            }
        }
        if (i >= n) return false;
        i++;
        return true;
        };

    ws();
    if (i >= n || text[i] != '{') return kv;
    i++;
    while (true) {
        ws();
        if (i >= n || text[i] == '}') break;
        if (text[i] == ',') { i++; continue; }
        std::string key;
        if (!readString(key)) break;
        ws();
        if (i >= n || text[i] != ':') break;
        i++; ws();
        std::string val;
        if (i < n && text[i] == '"') {
            if (!readString(val)) break;
        }
        else {
            size_t s = i;
            while (i < n && text[i] != ',' && text[i] != '}' && text[i] != '\n' && text[i] != '\r') i++;
            val = text.substr(s, i - s);
            size_t a = val.find_first_not_of(" \t"), b = val.find_last_not_of(" \t");
            val = (a == std::string::npos) ? "" : val.substr(a, b - a + 1);
            if (val == "true") val = "1"; else if (val == "false") val = "0";
        }
        kv[key] = val;
    }
    return kv;
}

using KvMap = std::map<std::string, std::string>;
static double KvF(const KvMap& kv, const char* k, double def) {
    auto it = kv.find(k);
    if (it == kv.end()) return def;
    try { double v = std::stod(it->second); if (std::isfinite(v)) return v; }
    catch (...) {}
    return def;
}
static int KvI(const KvMap& kv, const char* k, int def) { return (int)KvF(kv, k, def); }
static bool KvB(const KvMap& kv, const char* k, bool def) { return KvF(kv, k, def ? 1.0 : 0.0) != 0.0; }
static bool KvS(const KvMap& kv, const char* k, std::string& out) {
    auto it = kv.find(k);
    if (it == kv.end()) return false;
    out = it->second;
    return true;
}
static void KvCopy(const KvMap& kv, const char* k, char* dst, size_t sz) {
    std::string s;
    if (KvS(kv, k, s)) snprintf(dst, sz, "%s", s.c_str());
}

// session=false: プリセット用 (生成/出力/テーマのみ)  session=true: 自動設定用 (+ 前回の状態)
static std::string BuildSettingsJson(bool session) {
    std::ostringstream os;
    os << std::setprecision(7);
    bool first = true;
    auto sep = [&]() { os << (first ? "\n" : ",\n"); first = false; };
    auto num = [&](const char* k, double v) { sep(); os << "  \"" << k << "\": " << v; };
    auto str = [&](const char* k, const std::string& v) { sep(); os << "  \"" << k << "\": \"" << JsonEscape(v) << "\""; };

    os << "{";
    num("prepMs", g_p.prepMs);
    num("snapMs", g_p.snapMs);
    num("releaseMs", g_p.releaseMs);
    num("smoothCloseMs", g_p.smoothCloseMs);
    num("geminateExtraMs", g_p.geminateExtraMs);
    num("gapMs", g_p.gapMs);
    num("maxWeight", g_p.maxWeight);
    num("consScale", g_p.consScale);
    num("reducePct", g_reducePct);
    num("pitchFxPct", g_pitchFxPct);
    num("dynFxPct", g_dynFxPct);
    num("humanize", g_p.humanize);
    num("legato", g_p.legato);
    num("smoothMs", g_p.smoothMs);
    num("consFloor", g_p.consFloor);
    num("smoothMacro", g_smoothMacro);
    num("fps", g_p.fps);
    str("modelName", g_modelName);
    str("morphA", g_morphA);
    str("morphI", g_morphI);
    str("morphU", g_morphU);
    str("morphE", g_morphE);
    str("morphO", g_morphO);
    str("morphN", g_morphN);
    num("themeBase", g_themeBase);
    num("accentR", g_accentColor[0]);
    num("accentG", g_accentColor[1]);
    num("accentB", g_accentColor[2]);
    num("accentA", g_accentColor[3]);

    if (session) {
        str("inputPath", WideToUtf8(g_inputPath.empty() ? g_startupInputPath : g_inputPath));
        str("vocalPath", WideToUtf8(g_vocalPath.empty() ? g_startupVocalPath : g_vocalPath));
        num("vocalStartFrame", g_vocalStartFrame);
        num("wavPitchMix", g_wavPitchMix);
        num("wavDynMix", g_wavDynMix);
        num("prShowF0", g_prShowF0 ? 1 : 0);
        str("wavPath", WideToUtf8(g_wavPath.empty() ? g_startupWavPath : g_wavPath));
        num("wavStartFrame", g_wavStartFrame);
        num("wavVolume", g_wavVolume);
        num("wavMute", g_wavMute ? 1 : 0);
        num("prShowWave", g_prShowWave ? 1 : 0);
        num("wavColorR", g_wavColor[0]); num("wavColorG", g_wavColor[1]); num("wavColorB", g_wavColor[2]); num("wavColorA", g_wavColor[3]);
        str("mmdModelPath", WideToUtf8(g_modelPath.empty() ? g_startupModelPath : g_modelPath));
        num("autoOpenLast", g_autoOpenLast ? 1 : 0);
        num("selectedTrack", g_loaded ? g_selectedTrack : g_restoreTrack);
        num("prZoomX", g_prZoomX);
        num("prRowHeight", g_prRowHeight);
        num("camCloseup", g_camCloseup ? 1 : 0);
        num("loop", g_loop ? 1 : 0);
        num("playSpeed", g_playSpeed);
        num("winX", g_winX);
        num("winY", g_winY);
        num("winW", g_winW);
        num("winH", g_winH);
        num("winMax", g_winMax ? 1 : 0);
        for (int i = 0; i < kWinCount; i++) { char key[24]; snprintf(key, sizeof(key), "panelOpen%d", i); num(key, g_winOpen[i] ? 1 : 0); }
        num("allowDetach", g_allowDetach ? 1 : 0);
        num("layoutLocked", g_layoutLocked ? 1 : 0);
        num("snapPx", g_snapPx);
        num("easyDock", g_easyDock ? 1 : 0);
        str("dockLayout", g_dockLayoutStr);
        for (int i = 0; i < kWinCount; i++) {
            char key[24], val[120];
            snprintf(key, sizeof(key), "panelFloat%d", i);
            snprintf(val, sizeof(val), "%d,%.1f,%.1f,%.1f,%.1f", g_isFloating[i] ? 1 : 0, g_floatRect[i].x, g_floatRect[i].y, g_floatRect[i].w, g_floatRect[i].h);
            str(key, val);
        }
        num("layoutLockSize", g_layoutLockSize ? 1 : 0);
        num("startOnLaunch", g_startOnLaunch ? 1 : 0);
        str("language", g_langSetting);
        num("vsync", g_vsync ? 1 : 0);
        num("prShowPitch", g_prShowPitch ? 1 : 0);
        num("prShowDyn", g_prShowDyn ? 1 : 0);
        num("prShowVel", g_prShowVel ? 1 : 0);
        num("prShowMini", g_prShowMini ? 1 : 0);
        num("categoryCount", (double)g_categories.size());
        for (size_t i = 0; i < g_categories.size(); i++) { char key[32]; snprintf(key, sizeof(key), "category%zu", i); str(key, g_categories[i]); }
        num("recentCount", (double)g_recent.size());
        for (size_t i = 0; i < g_recent.size(); i++) {
            char key[32];
            snprintf(key, sizeof(key), "recentPath%zu", i); str(key, WideToUtf8(g_recent[i].path));
            snprintf(key, sizeof(key), "recentSub%zu", i);  str(key, g_recent[i].sub);
            snprintf(key, sizeof(key), "recentTime%zu", i); str(key, g_recent[i].time);
            snprintf(key, sizeof(key), "recentFav%zu", i);  num(key, g_recent[i].fav ? 1 : 0);
            snprintf(key, sizeof(key), "recentCat%zu", i);  str(key, g_recent[i].cat);
        }
    }
    os << "\n}\n";
    return os.str();
}

static void ApplySettingsKv(const KvMap& kv, bool session) {
    g_p.prepMs = KvF(kv, "prepMs", g_p.prepMs);
    g_p.snapMs = KvF(kv, "snapMs", g_p.snapMs);
    g_p.releaseMs = KvF(kv, "releaseMs", g_p.releaseMs);
    g_p.smoothCloseMs = KvF(kv, "smoothCloseMs", g_p.smoothCloseMs);
    g_p.geminateExtraMs = KvF(kv, "geminateExtraMs", g_p.geminateExtraMs);
    g_p.gapMs = KvF(kv, "gapMs", g_p.gapMs);
    g_p.maxWeight = KvF(kv, "maxWeight", g_p.maxWeight);
    g_p.consScale = KvF(kv, "consScale", g_p.consScale);
    g_reducePct = (float)KvF(kv, "reducePct", g_reducePct);
    g_pitchFxPct = (float)KvF(kv, "pitchFxPct", g_pitchFxPct);
    g_dynFxPct = (float)KvF(kv, "dynFxPct", g_dynFxPct);
    g_p.humanize = KvF(kv, "humanize", g_p.humanize);
    g_p.legato = std::max(0.0, std::min(1.0, KvF(kv, "legato", g_p.legato)));
    g_p.smoothMs = std::max(0.0, std::min(500.0, KvF(kv, "smoothMs", g_p.smoothMs)));
    g_p.consFloor = std::max(0.0, std::min(1.0, KvF(kv, "consFloor", g_p.consFloor)));
    g_smoothMacro = std::max(0.0f, std::min(100.0f, (float)KvF(kv, "smoothMacro", g_smoothMacro)));
    g_p.fps = KvF(kv, "fps", g_p.fps);
    if (!(g_p.fps > 0.0)) g_p.fps = 30.0;

    KvCopy(kv, "modelName", g_modelName, sizeof(g_modelName));
    KvCopy(kv, "morphA", g_morphA, sizeof(g_morphA));
    KvCopy(kv, "morphI", g_morphI, sizeof(g_morphI));
    KvCopy(kv, "morphU", g_morphU, sizeof(g_morphU));
    KvCopy(kv, "morphE", g_morphE, sizeof(g_morphE));
    KvCopy(kv, "morphO", g_morphO, sizeof(g_morphO));
    KvCopy(kv, "morphN", g_morphN, sizeof(g_morphN));

    g_themeBase = std::max(0, std::min(1, KvI(kv, "themeBase", g_themeBase)));
    g_accentColor[0] = std::max(0.0f, std::min(1.0f, (float)KvF(kv, "accentR", g_accentColor[0])));
    g_accentColor[1] = std::max(0.0f, std::min(1.0f, (float)KvF(kv, "accentG", g_accentColor[1])));
    g_accentColor[2] = std::max(0.0f, std::min(1.0f, (float)KvF(kv, "accentB", g_accentColor[2])));
    g_accentColor[3] = std::max(0.0f, std::min(1.0f, (float)KvF(kv, "accentA", g_accentColor[3])));

    if (!session) return;

    std::string s;
    if (KvS(kv, "inputPath", s)) g_startupInputPath = Utf8ToWide(s);
    if (KvS(kv, "mmdModelPath", s)) g_startupModelPath = Utf8ToWide(s);
    if (KvS(kv, "wavPath", s)) g_startupWavPath = Utf8ToWide(s);
    if (KvS(kv, "vocalPath", s)) g_startupVocalPath = Utf8ToWide(s);
    g_vocalStartFrame = KvI(kv, "vocalStartFrame", g_vocalStartFrame);
    g_wavPitchMix = (float)KvF(kv, "wavPitchMix", g_wavPitchMix);
    g_wavDynMix = (float)KvF(kv, "wavDynMix", g_wavDynMix);
    g_prShowF0 = KvB(kv, "prShowF0", g_prShowF0);
    g_wavStartFrame = KvI(kv, "wavStartFrame", g_wavStartFrame);
    g_wavVolume = (float)KvF(kv, "wavVolume", g_wavVolume);
    g_wavMute = KvB(kv, "wavMute", g_wavMute);
    g_prShowWave = KvB(kv, "prShowWave", g_prShowWave);
    g_wavColor[0] = (float)KvF(kv, "wavColorR", g_wavColor[0]); g_wavColor[1] = (float)KvF(kv, "wavColorG", g_wavColor[1]);
    g_wavColor[2] = (float)KvF(kv, "wavColorB", g_wavColor[2]); g_wavColor[3] = (float)KvF(kv, "wavColorA", g_wavColor[3]);
    g_autoOpenLast = KvB(kv, "autoOpenLast", g_autoOpenLast);
    g_restoreTrack = std::max(0, KvI(kv, "selectedTrack", 0));
    g_prZoomX = std::max(30.0f, std::min(800.0f, (float)KvF(kv, "prZoomX", g_prZoomX)));
    g_prRowHeight = std::max(10.0f, std::min(40.0f, (float)KvF(kv, "prRowHeight", g_prRowHeight)));
    g_camCloseup = KvB(kv, "camCloseup", g_camCloseup);
    g_loop = KvB(kv, "loop", g_loop);
    g_playSpeed = std::max(0.25f, std::min(2.0f, (float)KvF(kv, "playSpeed", g_playSpeed)));
    g_winX = KvI(kv, "winX", g_winX);
    g_winY = KvI(kv, "winY", g_winY);
    g_winW = std::max(640, KvI(kv, "winW", g_winW));
    g_winH = std::max(480, KvI(kv, "winH", g_winH));
    g_winMax = KvB(kv, "winMax", g_winMax);
    g_hasSavedWindow = kv.count("winW") && kv.count("winH");
    for (int i = 0; i < kWinCount; i++) { char key[24]; snprintf(key, sizeof(key), "panelOpen%d", i); g_winOpen[i] = KvB(kv, key, g_winOpen[i]); }
    g_allowDetach = KvB(kv, "allowDetach", g_allowDetach);
    g_layoutLocked = KvB(kv, "layoutLocked", g_layoutLocked);
    g_snapPx = std::max(0.0f, std::min(60.0f, (float)KvF(kv, "snapPx", g_snapPx)));
    g_easyDock = KvB(kv, "easyDock", g_easyDock);
    KvS(kv, "dockLayout", g_dockLayoutStr);
    for (int i = 0; i < kWinCount; i++) {
        char key[24];
        snprintf(key, sizeof(key), "panelFloat%d", i);
        std::string v;
        int fl = 0;
        float fx = 0, fy = 0, fw = 0, fh = 0;
        if (KvS(kv, key, v) && sscanf(v.c_str(), "%d,%f,%f,%f,%f", &fl, &fx, &fy, &fw, &fh) == 5 &&
            std::isfinite(fx) && std::isfinite(fy) && std::isfinite(fw) && std::isfinite(fh) && fw >= 100.0f && fh >= 60.0f && fw <= 8000.0f && fh <= 8000.0f) {
            g_floatRect[i].x = fx; g_floatRect[i].y = fy; g_floatRect[i].w = fw; g_floatRect[i].h = fh; g_floatRect[i].valid = true;
            g_isFloating[i] = (fl != 0);
            g_floatApply[i] = g_isFloating[i];
        }
    }
    g_layoutLockSize = KvB(kv, "layoutLockSize", g_layoutLockSize);
    g_startOnLaunch = KvB(kv, "startOnLaunch", g_startOnLaunch);
    KvS(kv, "language", g_langSetting);
    if (g_langSetting.empty()) g_langSetting = "auto";
    g_langDirty = true;
    g_vsync = KvB(kv, "vsync", g_vsync);
    g_prShowPitch = KvB(kv, "prShowPitch", g_prShowPitch);
    g_prShowDyn = KvB(kv, "prShowDyn", g_prShowDyn);
    g_prShowVel = KvB(kv, "prShowVel", g_prShowVel);
    g_prShowMini = KvB(kv, "prShowMini", g_prShowMini);
    g_categories.clear();
    const int cc = std::max(0, std::min(64, KvI(kv, "categoryCount", 0)));
    for (int i = 0; i < cc; i++) {
        char key[32];
        std::string nm;
        snprintf(key, sizeof(key), "category%d", i);
        if (KvS(kv, key, nm) && !nm.empty()) g_categories.push_back(nm);
    }
    g_recent.clear();
    const int rc = std::max(0, std::min((int)kRecentMax, KvI(kv, "recentCount", 0)));
    for (int i = 0; i < rc; i++) {
        char key[32];
        std::string p, sub, tm, cat;
        snprintf(key, sizeof(key), "recentPath%d", i); if (!KvS(kv, key, p) || p.empty()) continue;
        snprintf(key, sizeof(key), "recentSub%d", i);  KvS(kv, key, sub);
        snprintf(key, sizeof(key), "recentTime%d", i); KvS(kv, key, tm);
        snprintf(key, sizeof(key), "recentCat%d", i);  KvS(kv, key, cat);
        snprintf(key, sizeof(key), "recentFav%d", i);
        RecentItem ri;
        ri.path = Utf8ToWide(p); ri.sub = sub; ri.time = tm; ri.cat = cat; ri.fav = KvB(kv, key, false);
        g_recent.push_back(ri);
    }
}

// 一時ファイルに書いてから置き換える (書き込み途中でのクラッシュで設定が壊れないように)
static bool WriteTextFileAtomic(const std::wstring& path, const std::string& content) {
    std::wstring tmp = path + L".tmp";
    {
        std::ofstream os(tmp, std::ios::binary);
        if (!os) return false;
        os.write(content.data(), (std::streamsize)content.size());
        os.flush();
        if (!os) { os.close(); DeleteFileW(tmp.c_str()); return false; }
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

static void SavePresetJson(const std::wstring& path) {
    if (!WriteTextFileAtomic(path, BuildSettingsJson(false))) throw std::runtime_error("プリセットファイルを保存できません");
}

static void LoadPresetJson(const std::wstring& path) {
    std::string data = ReadFileBytes(path);
    KvMap kv = ParseFlatJson(data);
    if (kv.empty()) throw std::runtime_error("プリセットの形式が正しくありません");
    ApplySettingsKv(kv, false);
    if (ImGui::GetCurrentContext()) ApplyCustomStyle();
}

// --- 自動設定 (exeと同じフォルダ) ---
static std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    std::wstring p(buf, n);
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"" : p.substr(0, s + 1);
}
static std::wstring SettingsPath() { return GetExeDir() + L"VyricSync_settings.json"; }

// --- 翻訳ファイル (res/lang/*.json) ---
static std::wstring LangDir() { return GetExeDir() + L"res\\lang\\"; }

static void ScanLanguages() {
    g_langs.clear();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((LangDir() + L"*.json").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring fn = fd.cFileName;
        const std::wstring stem = fn.substr(0, fn.size() - 5);
        if (stem.empty()) continue;
        LangInfo li;
        li.code = WideToUtf8(stem);
        li.name = li.code;
        try {
            KvMap kv = ParseFlatJson(ReadFileBytes(LangDir() + fn));
            auto it = kv.find("_name");
            if (it != kv.end() && !it->second.empty()) li.name = it->second;
        }
        catch (...) {}
        if (li.code != "ja") g_langs.push_back(li);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(g_langs.begin(), g_langs.end(), [](const LangInfo& a, const LangInfo& b) { return a.code < b.code; });
}

static void LoadLanguage() {
    g_tr.clear();
    std::string code = g_langSetting;
    if (code == "auto") {
        wchar_t loc[LOCALE_NAME_MAX_LENGTH] = L"";
        code = GetUserDefaultLocaleName(loc, LOCALE_NAME_MAX_LENGTH) ? WideToUtf8(loc).substr(0, 2) : std::string("ja");
    }
    if (code == "ja" || code.empty()) return;
    const std::wstring path = LangDir() + Utf8ToWide(code) + L".json";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    try {
        KvMap kv = ParseFlatJson(ReadFileBytes(path));
        for (auto& kvp : kv) if (!kvp.first.empty() && kvp.first[0] != '_' && !kvp.second.empty()) g_tr[kvp.first] = kvp.second;
    }
    catch (...) {}
}

static void LoadAutoSettings() {
    std::wstring path = SettingsPath();
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    try {
        KvMap kv = ParseFlatJson(ReadFileBytes(path));
        ApplySettingsKv(kv, true);
    }
    catch (...) {}
}

static std::string g_settingsSeen, g_settingsSaved;
static double g_settingsChangeTime = 0.0;
// 設定が変わって0.6秒落ち着いたら保存する。force=true なら即保存 (終了時)
static void AutoSaveSettings(double now, bool force) {
    std::string cur = BuildSettingsJson(true);
    if (cur != g_settingsSeen) { g_settingsSeen = cur; g_settingsChangeTime = now; }
    if (cur == g_settingsSaved) return;
    if (force || now - g_settingsChangeTime > 0.6) {
        g_settingsSaved = cur; // 失敗しても毎フレーム再試行しない
        if (!WriteTextFileAtomic(SettingsPath(), cur))
            g_statusText = "設定ファイルを保存できませんでした: " + WideToUtf8(SettingsPath());
    }
}

// ウィンドウ位置・サイズの記録 (終了時にはウィンドウが既に破棄されているので毎フレーム覚えておく)
// 最大化中は「元のサイズ」を保持したまま、最大化フラグだけ記録する。
static void UpdateWindowCache() {
    if (!g_hwnd || IsIconic(g_hwnd)) return;
    if (IsZoomed(g_hwnd)) { g_winMax = true; return; }
    RECT rc;
    if (!GetWindowRect(g_hwnd, &rc)) return;
    g_winX = rc.left;
    g_winY = rc.top;
    g_winW = rc.right - rc.left;
    g_winH = rc.bottom - rc.top;
    g_winMax = false;
}

static void OpenPresetFileDialog() {
    wchar_t fileBuf[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"JSON プリセット (*.json)\0*.json\0すべてのファイル\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"プリセットを読み込む";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        try {
            LoadPresetJson(fileBuf);
            g_statusText = "プリセットを読み込みました: " + WideToUtf8(fileBuf);
        }
        catch (std::exception& e) {
            g_statusText = std::string("読み込み失敗: ") + e.what();
        }
    }
}

static void SavePresetFileDialog() {
    wchar_t fileBuf[MAX_PATH] = L"preset.json";
    OPENFILENAMEW sfn = {};
    sfn.lStructSize = sizeof(sfn);
    sfn.hwndOwner = g_hwnd;
    sfn.lpstrFilter = L"JSON プリセット (*.json)\0*.json\0";
    sfn.lpstrFile = fileBuf;
    sfn.nMaxFile = MAX_PATH;
    sfn.lpstrDefExt = L"json";
    sfn.Flags = OFN_OVERWRITEPROMPT;
    sfn.lpstrTitle = L"プリセットを保存";
    if (GetSaveFileNameW(&sfn)) {
        try {
            SavePresetJson(fileBuf);
            g_statusText = "プリセットを保存しました: " + WideToUtf8(fileBuf);
        }
        catch (std::exception& e) {
            g_statusText = std::string("保存失敗: ") + e.what();
        }
    }
}

// ---------------------------------------------------------------------------
// ファイル操作・生成
// ---------------------------------------------------------------------------
static void LoadSongFromPath(const std::wstring& path) {
    g_packedActive = false;
    g_inputPath = path;
    size_t slash = g_inputPath.find_last_of(L"\\/");
    size_t dot = g_inputPath.find_last_of(L'.');
    g_baseName = g_inputPath.substr(slash == std::wstring::npos ? 0 : slash + 1,
        (dot == std::wstring::npos ? g_inputPath.size() : dot) -
        (slash == std::wstring::npos ? 0 : slash + 1));
    g_songSerial++;
    g_playing = false;
    g_playTime = 0.0;
    try {
        std::string data = ReadFileBytes(g_inputPath);
        std::wstring lower = g_inputPath;
        for (auto& c : lower) c = towlower(c);
        bool isVsqx = lower.size() >= 5 && lower.compare(lower.size() - 5, 5, L".vsqx") == 0;
        g_song = isVsqx ? vsq::ParseVsqx(data) : vsq::ParseVsqSmf(data);
        g_loaded = true;
        g_selectedTrack = 0;
        g_prScrollX = 0.0f;
        g_statusText = "読み込み完了: " + WideToUtf8(g_inputPath);
    }
    catch (std::exception& e) {
        g_loaded = false;
        g_statusText = std::string("読み込みエラー: ") + e.what();
    }
}

// 最近使ったファイルに追加 (同じパスは先頭に移す)
// 生成に使うピッチ/ダイナミクス曲線。ボーカルWAVがあれば、vsqxの曲線とWAV由来の値を設定の割合で混ぜる
static vsq::Curve g_effPitch, g_effDyn;
static std::vector<double> g_effKey;
static void GetEffectiveCurves(int track, const vsq::Curve*& pitch, const vsq::Curve*& dyn) {
    pitch = &g_song.pitchBendCurves[(size_t)track];
    dyn = &g_song.dynamicsCurves[(size_t)track];
    const double mixP = std::max(0.0, std::min(1.0, (double)g_wavPitchMix / 100.0));
    const double mixD = std::max(0.0, std::min(1.0, (double)g_wavDynMix / 100.0));
    if (!g_vocalLoaded || g_f0.midi.empty() || (mixP <= 0.0 && mixD <= 0.0)) return;
    const double fps = g_p.fps > 0.0 ? g_p.fps : 30.0;
    const double bendRange = g_p.bendRangeSemitones > 0.1 ? g_p.bendRangeSemitones : 2.0;
    const std::vector<double> key = { (double)track, (double)g_songSerial, (double)g_vocalSerial, (double)g_vocalStartFrame, fps, bendRange, mixP, mixD };
    if (key != g_effKey) {
        g_effKey = key;
        g_effPitch.pts.clear();
        g_effDyn.pts.clear();
        const vsq::Curve& op = *pitch;
        const vsq::Curve& od = *dyn;
        const auto& notes = g_song.tracks[(size_t)track];
        vsq::TempoMap tm(g_song.resolution, g_song.tempos);
        const double off = (double)g_vocalStartFrame / fps;
        size_t ni = 0;
        long long lastTick = -1;
        for (size_t i = 0; i < g_f0.midi.size(); i++) {
            const double t = (double)i * g_f0.hop + off;
            if (t < 0.0) continue;
            const long long tick = tm.SecToTick(t);
            if (tick <= lastTick) continue;
            lastTick = tick;
            while (ni + 1 < notes.size() && notes[ni + 1].tick <= tick) ni++;
            double pv = op.ValueAt(tick, 0.0);
            if (mixP > 0.0 && !notes.empty() && g_f0.midi[i] > 0.0f) {
                const auto& nt = notes[ni];
                if (tick >= nt.tick && tick < nt.tick + nt.dur) {   // 音符の中だけ、歌声のF0と音符の差をベンド値にする
                    const double raw = std::max(-8191.0, std::min(8191.0, ((double)g_f0.midi[i] - (double)nt.noteNumber) / bendRange * 8192.0));
                    pv = pv * (1.0 - mixP) + raw * mixP;
                }
            }
            g_effPitch.pts.push_back({ tick, pv });
            g_effDyn.pts.push_back({ tick, od.ValueAt(tick, 64.0) * (1.0 - mixD) + (double)g_f0.dyn[i] * mixD });
        }
    }
    if (mixP > 0.0 && !g_effPitch.pts.empty()) pitch = &g_effPitch;
    if (mixD > 0.0 && !g_effDyn.pts.empty()) dyn = &g_effDyn;
}

static void AddRecent(const std::wstring& path, const std::string& sub) {
    auto lower = [](std::wstring x) { for (auto& c : x) c = towlower(c); return x; };
    const std::wstring key = lower(path);
    RecentItem ni;
    ni.path = path;
    ni.sub = sub;
    char tbuf[32] = "";
    time_t t = time(nullptr);
    tm lt = {};
    if (localtime_s(&lt, &t) == 0) strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M", &lt);
    ni.time = tbuf;
    for (const RecentItem& r : g_recent) if (lower(r.path) == key) { ni.fav = r.fav; ni.cat = r.cat; break; }
    g_recent.erase(std::remove_if(g_recent.begin(), g_recent.end(), [&](const RecentItem& r) { return lower(r.path) == key; }), g_recent.end());
    g_recent.insert(g_recent.begin(), ni);
    if (g_recent.size() > kRecentMax) g_recent.resize(kRecentMax);
}

static void OpenFile() {
    wchar_t fileBuf[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"VOCALOID ファイル (*.vsqx;*.vsq)\0*.vsqx;*.vsq\0すべてのファイル\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"vsqx / vsq を開く";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    LoadSongFromPath(fileBuf);
    if (g_loaded) {
        AddRecent(fileBuf, "トラック " + std::to_string(g_song.tracks.size()));
        g_showStart = false;
    }
}

// GUIの「%」系スライダーから、vsq::MorphParams の派生値を計算する (生成・プレビュー共通)
static void PrepareDerivedParams() {
    g_p.pitchScaleMin = 1.0 - 0.2 * (g_pitchFxPct / 100.0);
    g_p.pitchScaleMax = 1.0 + 0.2 * (g_pitchFxPct / 100.0);
    g_p.dynScaleMin = 1.0 - 0.15 * (g_dynFxPct / 100.0);
    g_p.dynScaleMax = 1.0 + 0.15 * (g_dynFxPct / 100.0);
    g_p.bendSensitivity = 0.6 * (g_pitchFxPct / 100.0);
    g_p.reduceEpsilon = (g_reducePct / 100.0) * 0.05;
}

static void GenerateAndSave() {
    if (!g_loaded) { g_statusText = "先にvsqx/vsqファイルを開いてください"; return; }

    PrepareDerivedParams();

    vsq::MorphNames names;
    names.a = g_morphA; names.i = g_morphI; names.u = g_morphU;
    names.e = g_morphE; names.o = g_morphO; names.nasal = g_morphN;
    std::string modelName = g_modelName[0] ? g_modelName : "LipSync";

    try {
        vsq::TempoMap tempoMap(g_song.resolution, g_song.tempos);
        const vsq::Curve* effP = nullptr; const vsq::Curve* effD = nullptr;
        GetEffectiveCurves(g_selectedTrack, effP, effD);
        auto tracks = vsq::BuildMorphTracks(g_song.tracks[g_selectedTrack], tempoMap, g_p, *effP, *effD);

        wchar_t outBuf[MAX_PATH] = L"";
        std::wstring def = g_baseName + L"_lipsync.vmd";
        size_t n = def.size() < (MAX_PATH - 1) ? def.size() : (MAX_PATH - 1);
        wcsncpy(outBuf, def.c_str(), n); outBuf[n] = L'\0';
        OPENFILENAMEW sfn = {};
        sfn.lStructSize = sizeof(sfn);
        sfn.hwndOwner = g_hwnd;
        sfn.lpstrFilter = L"VMDモーションファイル (*.vmd)\0*.vmd\0";
        sfn.lpstrFile = outBuf;
        sfn.nMaxFile = MAX_PATH;
        sfn.lpstrDefExt = L"vmd";
        sfn.Flags = OFN_OVERWRITEPROMPT;
        sfn.lpstrTitle = L"VMDの保存先";
        if (!GetSaveFileNameW(&sfn)) return;

        vsq::WriteVmd(WideToUtf8(outBuf), tracks, names, modelName);

        size_t totalKf = 0; long lastFrame = 0;
        for (auto& kv : tracks) { totalKf += kv.second.size(); for (auto& fr : kv.second) lastFrame = std::max(lastFrame, fr.first); }
        char buf[512];
        snprintf(buf, sizeof(buf), "生成しました。音符数:%zu  キーフレーム:%zu  長さ:%.1f秒",
            g_song.tracks[g_selectedTrack].size(), totalKf, lastFrame / g_p.fps);
        g_statusText = buf;
    }
    catch (std::exception& e) {
        g_statusText = std::string("生成エラー: ") + e.what();
    }
}

// ---------------------------------------------------------------------------
// MMDモデル描画 (DirectX11)
//   モデルをオフスクリーンのレンダーターゲットに描き、ImGui::Image でパネルに表示する。
//   スキニングはせずバインドポーズのまま描画し、口パク用モーフだけを頂点に反映する。
// ---------------------------------------------------------------------------
struct MdlCB {
    DirectX::XMFLOAT4X4 viewProj;
    float diffuse[4];
    float ambient[4];
    float lightDir[4];
    float flags[4];   // x: テクスチャあり
};

static const char* kMdlHlsl = R"HLSL(
cbuffer CB : register(b0) {
    row_major float4x4 viewProj;
    float4 diffuse;
    float4 ambient;
    float4 lightDir;
    float4 flags;
};
Texture2D tex0 : register(t0);
Texture2D toonTex : register(t1);
SamplerState smp : register(s0);
SamplerState toonSmp : register(s1);
struct VSIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };
struct PSIn { float4 pos : SV_POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };
PSIn VSMain(VSIn i) {
    PSIn o;
    o.pos = mul(float4(i.pos, 1.0), viewProj);
    o.nrm = i.nrm;
    o.uv = i.uv;
    return o;
}
float4 PSMain(PSIn i, bool front : SV_IsFrontFace) : SV_Target {
    float3 n = normalize(i.nrm);
    if (!front) n = -n;
    float ndl = dot(n, lightDir.xyz);                       // 光に正対するほど1
    // MMDのtoon: 縦方向が明暗 (上=明るい / 下=影)
    float3 toon = toonTex.Sample(toonSmp, float2(0.5, saturate(0.5 - 0.5 * ndl))).rgb;
    float4 t = (flags.x > 0.5) ? tex0.Sample(smp, i.uv) : float4(1, 1, 1, 1);
    float a = diffuse.a * t.a;
    clip(a - 0.02);
    float3 base = saturate(diffuse.rgb * 0.85 + ambient.rgb * 0.45);
    return float4(saturate(base * toon * t.rgb), a);
}
)HLSL";

static bool g_mdlReady = false;
static ID3D11VertexShader* g_mdlVS = nullptr;
static ID3D11PixelShader* g_mdlPS = nullptr;
static ID3D11InputLayout* g_mdlLayout = nullptr;
static ID3D11Buffer* g_mdlCB = nullptr;
static ID3D11RasterizerState* g_rsCull = nullptr;
static ID3D11RasterizerState* g_rsNoCull = nullptr;
static ID3D11BlendState* g_bsAlpha = nullptr;
static ID3D11DepthStencilState* g_dsState = nullptr;
static ID3D11SamplerState* g_mdlSampler = nullptr;
static ID3D11ShaderResourceView* g_whiteSrv = nullptr;
static ID3D11SamplerState* g_toonSampler = nullptr;
static ID3D11ShaderResourceView* g_toonBuiltin[10] = {};   // 標準toon (プログラムで作った近似)
static ID3D11ShaderResourceView* g_toonModel[10] = {};     // モデルのフォルダ等から読んだ共有toon
static IWICImagingFactory* g_wic = nullptr;

// モデルごとのGPUリソース
static ID3D11Buffer* g_mdlVB = nullptr;
static ID3D11Buffer* g_mdlIB = nullptr;
static std::vector<ID3D11ShaderResourceView*> g_mdlTex;
static float g_lastWeights[6] = { -1, -1, -1, -1, -1, -1 };
static int g_lastMapping[6] = { -2, -2, -2, -2, -2, -2 };
static std::vector<mmd::Vertex> g_morphedTmp;

// プレビュー用レンダーターゲット (MSAA可)
static ID3D11Texture2D* g_pvResolveTex = nullptr;
static ID3D11ShaderResourceView* g_pvSrv = nullptr;
static ID3D11RenderTargetView* g_pvResolveRtv = nullptr;
static ID3D11Texture2D* g_pvMsaaTex = nullptr;
static ID3D11RenderTargetView* g_pvMsaaRtv = nullptr;
static ID3D11Texture2D* g_pvDepthTex = nullptr;
static ID3D11DepthStencilView* g_pvDsv = nullptr;
static int g_pvW = 0, g_pvH = 0;
static UINT g_pvSamples = 1;

template<class T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

static void ReleasePreviewTarget() {
    SafeRelease(g_pvDsv); SafeRelease(g_pvDepthTex);
    SafeRelease(g_pvMsaaRtv); SafeRelease(g_pvMsaaTex);
    SafeRelease(g_pvResolveRtv); SafeRelease(g_pvSrv); SafeRelease(g_pvResolveTex);
    g_pvW = g_pvH = 0;
}

static void ReleaseModelGpu() {
    SafeRelease(g_mdlVB);
    SafeRelease(g_mdlIB);
    for (auto& t : g_mdlTex) SafeRelease(t);
    g_mdlTex.clear();
    for (auto& t : g_toonModel) SafeRelease(t);
    for (int k = 0; k < 6; k++) { g_lastWeights[k] = -1; g_lastMapping[k] = -2; }
}

static void ShutdownModelRenderer() {
    ReleaseModelGpu();
    ReleasePreviewTarget();
    SafeRelease(g_mdlVS); SafeRelease(g_mdlPS); SafeRelease(g_mdlLayout); SafeRelease(g_mdlCB);
    SafeRelease(g_rsCull); SafeRelease(g_rsNoCull); SafeRelease(g_bsAlpha); SafeRelease(g_dsState);
    SafeRelease(g_mdlSampler); SafeRelease(g_whiteSrv); SafeRelease(g_toonSampler);
    for (auto& t : g_toonBuiltin) SafeRelease(t);
    SafeRelease(g_wic);
    g_mdlReady = false;
}

static bool InitModelRenderer() {
    if (!g_pd3dDevice) return false;
    ID3DBlob* vsb = nullptr; ID3DBlob* psb = nullptr; ID3DBlob* err = nullptr;
    auto fail = [&](const char* what) -> bool {
        g_statusText = what;
        if (err) { g_statusText += std::string(": ") + (const char*)err->GetBufferPointer(); err->Release(); err = nullptr; }
        SafeRelease(vsb); SafeRelease(psb);
        ShutdownModelRenderer();
        return false;
        };

    HRESULT hr = D3DCompile(kMdlHlsl, strlen(kMdlHlsl), "mdl.hlsl", nullptr, nullptr, "VSMain", "vs_4_0", 0, 0, &vsb, &err);
    if (FAILED(hr)) return fail("MMD描画: 頂点シェーダーのコンパイルに失敗");
    if (err) { err->Release(); err = nullptr; }
    hr = D3DCompile(kMdlHlsl, strlen(kMdlHlsl), "mdl.hlsl", nullptr, nullptr, "PSMain", "ps_4_0", 0, 0, &psb, &err);
    if (FAILED(hr)) return fail("MMD描画: ピクセルシェーダーのコンパイルに失敗");
    if (err) { err->Release(); err = nullptr; }

    if (FAILED(g_pd3dDevice->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_mdlVS))) return fail("MMD描画: VS作成失敗");
    if (FAILED(g_pd3dDevice->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_mdlPS))) return fail("MMD描画: PS作成失敗");

    D3D11_INPUT_ELEMENT_DESC il[3] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (FAILED(g_pd3dDevice->CreateInputLayout(il, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &g_mdlLayout))) return fail("MMD描画: 入力レイアウト作成失敗");
    SafeRelease(vsb); SafeRelease(psb);

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(MdlCB);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(g_pd3dDevice->CreateBuffer(&bd, nullptr, &g_mdlCB))) return fail("MMD描画: 定数バッファ作成失敗");

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_BACK;
    rd.FrontCounterClockwise = FALSE;   // MMDは時計回りが表
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = TRUE;
    if (FAILED(g_pd3dDevice->CreateRasterizerState(&rd, &g_rsCull))) return fail("MMD描画: ラスタライザ作成失敗");
    rd.CullMode = D3D11_CULL_NONE;
    if (FAILED(g_pd3dDevice->CreateRasterizerState(&rd, &g_rsNoCull))) return fail("MMD描画: ラスタライザ作成失敗");

    D3D11_BLEND_DESC bs = {};
    bs.RenderTarget[0].BlendEnable = TRUE;
    bs.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bs.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bs.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bs.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bs.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bs.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bs.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(g_pd3dDevice->CreateBlendState(&bs, &g_bsAlpha))) return fail("MMD描画: ブレンドステート作成失敗");

    D3D11_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (FAILED(g_pd3dDevice->CreateDepthStencilState(&ds, &g_dsState))) return fail("MMD描画: 深度ステート作成失敗");

    D3D11_SAMPLER_DESC sm = {};
    sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sm.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sm.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(g_pd3dDevice->CreateSamplerState(&sm, &g_mdlSampler))) return fail("MMD描画: サンプラー作成失敗");

    // toon用サンプラー: 縦方向をクランプ、ミップは使わない
    {
        D3D11_SAMPLER_DESC ts = sm;
        ts.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        ts.AddressU = ts.AddressV = ts.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        ts.MinLOD = 0; ts.MaxLOD = 0;
        if (FAILED(g_pd3dDevice->CreateSamplerState(&ts, &g_toonSampler))) return fail("MMD描画: toonサンプラー作成失敗");
    }

    // 標準toon (toon01〜toon10) の近似。MMD付属のtoon画像が見つからないときに使う。
    // {影のR,G,B, 境目の位置(0.5=光と直角), 境目のぼかし幅}
    {
        static const float kToon[10][5] = {
            { 0.82f, 0.82f, 0.82f, 0.55f, 0.35f }, { 0.70f, 0.68f, 0.70f, 0.50f, 0.05f },
            { 0.88f, 0.72f, 0.68f, 0.50f, 0.08f }, { 0.78f, 0.78f, 0.90f, 0.50f, 0.08f },
            { 0.90f, 0.88f, 0.70f, 0.50f, 0.08f }, { 0.70f, 0.85f, 0.72f, 0.50f, 0.08f },
            { 0.60f, 0.58f, 0.62f, 0.50f, 0.05f }, { 0.85f, 0.80f, 0.88f, 0.50f, 0.25f },
            { 0.95f, 0.78f, 0.70f, 0.55f, 0.25f }, { 0.75f, 0.82f, 0.92f, 0.55f, 0.25f },
        };
        const int H = 64;
        for (int i = 0; i < 10; i++) {
            uint8_t px[H * 4];
            for (int y = 0; y < H; y++) {
                float v = (y + 0.5f) / H;
                float e0 = kToon[i][3] - kToon[i][4] * 0.5f - 0.01f, e1 = kToon[i][3] + kToon[i][4] * 0.5f + 0.01f;
                float u = std::max(0.0f, std::min(1.0f, (v - e0) / (e1 - e0)));
                float sm3 = u * u * (3.0f - 2.0f * u);
                for (int c = 0; c < 3; c++) px[y * 4 + c] = (uint8_t)std::lround(255.0f * (1.0f * (1.0f - sm3) + kToon[i][c] * sm3));
                px[y * 4 + 3] = 255;
            }
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = 1; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sd = {};
            sd.pSysMem = px; sd.SysMemPitch = 4;
            ID3D11Texture2D* t = nullptr;
            if (FAILED(g_pd3dDevice->CreateTexture2D(&td, &sd, &t))) return fail("MMD描画: toon作成失敗");
            HRESULT h2 = g_pd3dDevice->CreateShaderResourceView(t, nullptr, &g_toonBuiltin[i]);
            t->Release();
            if (FAILED(h2)) return fail("MMD描画: toon作成失敗");
        }
    }

    // 1x1 白テクスチャ (テクスチャ無し材質用)
    {
        uint8_t white[4] = { 255, 255, 255, 255 };
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = white; sd.SysMemPitch = 4;
        ID3D11Texture2D* t = nullptr;
        if (FAILED(g_pd3dDevice->CreateTexture2D(&td, &sd, &t))) return fail("MMD描画: 白テクスチャ作成失敗");
        HRESULT h2 = g_pd3dDevice->CreateShaderResourceView(t, nullptr, &g_whiteSrv);
        t->Release();
        if (FAILED(h2)) return fail("MMD描画: 白テクスチャ作成失敗");
    }

    g_mdlReady = true;
    return true;
}

// RGBA8ピクセル列からミップマップ付きSRVを作る
static ID3D11ShaderResourceView* CreateSrvFromRGBA(const uint8_t* px, UINT w, UINT h) {
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.MipLevels = 0; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(g_pd3dDevice->CreateTexture2D(&td, nullptr, &tex))) return nullptr;
    g_pd3dDeviceContext->UpdateSubresource(tex, 0, nullptr, px, w * 4, 0);
    ID3D11ShaderResourceView* srv = nullptr;
    if (SUCCEEDED(g_pd3dDevice->CreateShaderResourceView(tex, nullptr, &srv)) && srv)
        g_pd3dDeviceContext->GenerateMips(srv);
    tex->Release();
    return srv;
}

// TGA (無圧縮/RLE, 8bitグレー / 16bit / 24bit / 32bit, カラーマップ付き) を RGBA8 に展開する。
// WICは標準ではTGAを読めず、MMDモデルではTGAテクスチャがよく使われるため自前で実装。
static bool DecodeTga(const std::string& file, std::vector<uint8_t>& out, UINT& W, UINT& H) {
    const uint8_t* d = (const uint8_t*)file.data();
    size_t n = file.size();
    if (n < 18) return false;
    int idLen = d[0], cmType = d[1], type = d[2];
    int cmFirst = d[3] | (d[4] << 8), cmLen = d[5] | (d[6] << 8), cmBits = d[7];
    int w = d[12] | (d[13] << 8), h = d[14] | (d[15] << 8), bpp = d[16], desc = d[17];
    bool rle = (type == 9 || type == 10 || type == 11);
    int base = rle ? type - 8 : type;               // 1: カラーマップ, 2: フルカラー, 3: グレー
    if (base != 1 && base != 2 && base != 3) return false;
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return false;
    if (base == 2 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) return false;
    if (base == 3 && bpp != 8) return false;
    if (base == 1 && (bpp != 8 || cmType != 1 || (cmBits != 24 && cmBits != 32 && cmBits != 15 && cmBits != 16))) return false;
    size_t pos = 18 + (size_t)idLen;
    std::vector<uint8_t> pal;
    if (cmType == 1) {
        size_t eb = (size_t)((cmBits + 7) / 8);
        size_t bytes = (size_t)cmLen * eb;
        if (pos + bytes > n) return false;
        if (base == 1) {
            pal.assign((size_t)(cmFirst + cmLen) * 4, 0);
            for (int i = 0; i < cmLen; i++) {
                const uint8_t* p = d + pos + (size_t)i * eb;
                uint8_t* q = &pal[(size_t)(cmFirst + i) * 4];
                if (eb >= 3) { q[0] = p[2]; q[1] = p[1]; q[2] = p[0]; q[3] = (eb == 4) ? p[3] : 255; }
                else { unsigned v = p[0] | (p[1] << 8); q[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31); q[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31); q[2] = (uint8_t)((v & 31) * 255 / 31); q[3] = 255; }
            }
        }
        pos += bytes;
    }
    size_t bpx = (size_t)((bpp + 7) / 8);
    size_t total = (size_t)w * h;
    std::vector<uint8_t> raw(total * bpx);
    if (!rle) {
        if (pos + raw.size() > n) return false;
        std::memcpy(raw.data(), d + pos, raw.size());
    }
    else {
        size_t o = 0;
        while (o < raw.size()) {
            if (pos >= n) return false;
            uint8_t hd = d[pos++];
            size_t cnt = (size_t)(hd & 0x7F) + 1;
            if (hd & 0x80) {
                if (pos + bpx > n) return false;
                for (size_t k = 0; k < cnt && o < raw.size(); k++) { std::memcpy(&raw[o], d + pos, bpx); o += bpx; }
                pos += bpx;
            }
            else {
                size_t bytes = cnt * bpx;
                if (pos + bytes > n) return false;
                size_t m = std::min(bytes, raw.size() - o);
                std::memcpy(&raw[o], d + pos, m); o += m; pos += bytes;
            }
        }
    }
    out.assign(total * 4, 255);
    bool topDown = (desc & 0x20) != 0, rightToLeft = (desc & 0x10) != 0;
    for (int y = 0; y < h; y++) {
        int sy = topDown ? y : (h - 1 - y);
        for (int x = 0; x < w; x++) {
            int sx = rightToLeft ? (w - 1 - x) : x;
            const uint8_t* p = &raw[((size_t)sy * w + sx) * bpx];
            uint8_t* q = &out[((size_t)y * w + x) * 4];
            if (base == 3) { q[0] = q[1] = q[2] = p[0]; }
            else if (base == 1) {
                size_t idx = (size_t)p[0] * 4;
                if (idx + 3 < pal.size()) { q[0] = pal[idx]; q[1] = pal[idx + 1]; q[2] = pal[idx + 2]; q[3] = pal[idx + 3]; }
            }
            else if (bpp >= 24) { q[0] = p[2]; q[1] = p[1]; q[2] = p[0]; q[3] = (bpp == 32) ? p[3] : 255; }
            else {
                unsigned v = p[0] | (p[1] << 8);
                q[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31); q[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31); q[2] = (uint8_t)((v & 31) * 255 / 31);
                q[3] = (bpp == 16 && !(v & 0x8000)) ? 0 : 255;
            }
        }
    }
    W = (UINT)w; H = (UINT)h;
    return true;
}

// モデルのフォルダ基準でテクスチャの実ファイルを探す。
// 見つからなければファイル名だけでモデルと同じフォルダを探す (パスが書き換わったモデル対策)。
static std::wstring ResolveTexturePath(const std::wstring& dir, const std::wstring& rel) {
    auto exists = [](const std::wstring& p) {
        DWORD a = GetFileAttributesW(p.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
        };
    std::wstring p = dir + rel;
    if (exists(p)) return p;
    size_t sl = rel.find_last_of(L"\\/");
    if (sl != std::wstring::npos) {
        p = dir + rel.substr(sl + 1);
        if (exists(p)) return p;
    }
    return L"";
}

// 画像ファイルを読み込む。WIC (bmp/png/jpg/gif/tiff...) を使い、
// stb_image.h が同じフォルダにあれば tga なども読めるようにフォールバックする。
static ID3D11ShaderResourceView* LoadTextureFile(const std::wstring& path) {
    std::wstring lower = path;
    for (auto& c : lower) c = towlower(c);
    bool isBmp = lower.size() >= 4 && lower.compare(lower.size() - 4, 4, L".bmp") == 0;

    std::vector<uint8_t> px;
    UINT w = 0, h = 0;
    bool ok = false;

    if (!g_wic) CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic));
    if (g_wic) {
        IWICBitmapDecoder* dec = nullptr;
        IWICBitmapFrameDecode* fr = nullptr;
        IWICFormatConverter* cv = nullptr;
        if (SUCCEEDED(g_wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) && dec &&
            SUCCEEDED(dec->GetFrame(0, &fr)) && fr &&
            SUCCEEDED(g_wic->CreateFormatConverter(&cv)) && cv &&
            SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) &&
            SUCCEEDED(cv->GetSize(&w, &h)) && w > 0 && h > 0 && w <= 16384 && h <= 16384) {
            px.resize((size_t)w * h * 4);
            ok = SUCCEEDED(cv->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()));
        }
        SafeRelease(cv); SafeRelease(fr); SafeRelease(dec);
    }
    if (!ok) {   // WICで読めなかった場合はTGAとして試す
        std::string fileData;
        try { fileData = ReadFileBytes(path); }
        catch (...) {}
        if (!fileData.empty()) ok = DecodeTga(fileData, px, w, h);
    }
#ifdef HAVE_STB_IMAGE
    if (!ok) {
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (f) {
            int x = 0, y = 0, n = 0;
            unsigned char* d = stbi_load_from_file(f, &x, &y, &n, 4);
            fclose(f);
            if (d) {
                w = (UINT)x; h = (UINT)y;
                px.assign(d, d + (size_t)w * h * 4);
                stbi_image_free(d);
                ok = (w > 0 && h > 0);
            }
        }
    }
#endif
    if (!ok) return nullptr;
    // MMDではbmpのアルファは無視される (32bit bmpのアルファが0で顔が消えるのを防ぐ)
    if (isBmp) for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    return CreateSrvFromRGBA(px.data(), w, h);
}

static bool CreateModelGpu(const mmd::Model& m, const std::wstring& dir) {
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(m.vertices.size() * sizeof(mmd::Vertex));
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = m.vertices.data();
    if (FAILED(g_pd3dDevice->CreateBuffer(&bd, &sd, &g_mdlVB))) return false;

    D3D11_BUFFER_DESC ib = {};
    ib.ByteWidth = (UINT)(m.indices.size() * sizeof(uint32_t));
    ib.Usage = D3D11_USAGE_IMMUTABLE;
    ib.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA isd = {};
    isd.pSysMem = m.indices.data();
    if (FAILED(g_pd3dDevice->CreateBuffer(&ib, &isd, &g_mdlIB))) return false;

    g_mdlTex.assign(m.texturePaths.size(), nullptr);
    for (size_t i = 0; i < m.texturePaths.size(); i++) {
        if (m.texturePaths[i].empty()) continue;
        std::wstring rel = Utf8ToWide(m.texturePaths[i]);
        for (auto& c : rel) if (c == L'/') c = L'\\';
        std::wstring real = ResolveTexturePath(dir, rel);
        if (!real.empty()) g_mdlTex[i] = LoadTextureFile(real);   // 見つからなければ nullptr (白で描画)
    }

    // 共有toon: 使われている番号だけ、モデルのフォルダ → exe横の toon フォルダの順で画像を探す。
    // どちらにも無ければ、標準toonの近似 (g_toonBuiltin) を使う。
    bool used[10] = {};
    for (auto& mt : m.materials) {
        if (mt.toonTexIndex >= 0 && mt.toonTexIndex < (int)g_mdlTex.size() && g_mdlTex[(size_t)mt.toonTexIndex]) continue;
        int sidx = (mt.toonShared >= 0 && mt.toonShared < 10) ? mt.toonShared : 0;
        used[sidx] = true;
    }
    for (int i = 0; i < 10; i++) {
        if (!used[i] || m.sharedToonNames[i].empty()) continue;
        std::wstring rel = Utf8ToWide(m.sharedToonNames[i]);
        for (auto& c : rel) if (c == L'/') c = L'\\';
        std::wstring real = ResolveTexturePath(dir, rel);
        if (real.empty()) real = ResolveTexturePath(GetExeDir() + L"toon\\", rel);
        if (!real.empty()) g_toonModel[i] = LoadTextureFile(real);
    }
    return true;
}

// カメラを初期位置に戻す (closeup: 顔アップ / それ以外: 全身)
static void ResetCamera(bool closeup) {
    if (!g_modelLoaded) return;
    float H = std::max(1.0f, g_model.bmax[1] - g_model.bmin[1]);
    float cx = (g_model.bmin[0] + g_model.bmax[0]) * 0.5f;
    float cz = (g_model.bmin[2] + g_model.bmax[2]) * 0.5f;
    if (closeup) {
        if (g_model.hasHead) {
            g_camTarget[0] = g_model.headPos[0];
            g_camTarget[1] = g_model.headPos[1] + H * 0.05f;
            g_camTarget[2] = g_model.headPos[2];
        }
        else {
            g_camTarget[0] = cx; g_camTarget[1] = g_model.bmin[1] + H * 0.88f; g_camTarget[2] = cz;
        }
        g_camDist = H * 0.55f;
    }
    else {
        g_camTarget[0] = cx; g_camTarget[1] = g_model.bmin[1] + H * 0.5f; g_camTarget[2] = cz;
        g_camDist = H * 2.0f;
    }
    g_camYaw = 0.0f;
    g_camPitch = 0.0f;
}

static void LoadModelFromPath(const std::wstring& path) {
    if (!g_mdlReady) { g_statusText = "MMDプレビューを初期化できていないため、モデルを読み込めません"; return; }
    try {
        std::string data = ReadFileBytes(path);
        mmd::Model m = mmd::LoadModel(data);
        if (m.vertices.empty() || m.indices.empty()) throw std::runtime_error("表示できる形状がありません");

        ReleaseModelGpu();
        g_modelLoaded = false;
        g_model = std::move(m);

        size_t slash = path.find_last_of(L"\\/");
        std::wstring dir = (slash == std::wstring::npos) ? L"" : path.substr(0, slash + 1);
        if (!CreateModelGpu(g_model, dir)) { ReleaseModelGpu(); throw std::runtime_error("GPUリソースの作成に失敗しました"); }

        g_modelPath = path;
        g_modelLoaded = true;
        ResetCamera(g_camCloseup);

        int texOk = 0;
        for (auto* t : g_mdlTex) if (t) texOk++;
        char buf[512];
        snprintf(buf, sizeof(buf), "モデル読み込み完了: %s (頂点 %zu / 材質 %zu / モーフ %zu / テクスチャ %d/%zu)",
            g_model.name.c_str(), g_model.vertices.size(), g_model.materials.size(), g_model.morphs.size(),
            texOk, g_model.texturePaths.size());
        g_statusText = buf;
        int shown = 0, missing = 0;
        std::string miss;
        for (size_t i = 0; i < g_mdlTex.size(); i++) {
            if (g_mdlTex[i] || g_model.texturePaths[i].empty()) continue;
            missing++;
            if (shown < 3) { miss += (shown ? ", " : "") + g_model.texturePaths[i]; shown++; }
        }
        if (missing) g_statusText += "  ※読めなかったテクスチャ: " + miss + (missing > shown ? " ほか" : "");
    }
    catch (std::exception& e) {
        g_statusText = std::string("モデル読み込みエラー: ") + e.what();
    }
}

static ID3D11ShaderResourceView* g_iconTex[ICN_COUNT] = {};
static bool g_iconTried[ICN_COUNT] = {};
static ID3D11ShaderResourceView* LoadIconTexture(int icon) {
    if (icon <= 0 || icon >= ICN_COUNT) return nullptr;
    if (!g_iconTried[icon]) {
        g_iconTried[icon] = true;
        if (g_pd3dDevice) g_iconTex[icon] = LoadTextureFile(GetExeDir() + L"res\\images\\icon_" + Utf8ToWide(kIconNames[icon]) + L".png");
    }
    return g_iconTex[icon];
}

static void OpenModelDialog() {
    wchar_t fileBuf[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"MMDモデル (*.pmx;*.pmd)\0*.pmx;*.pmd\0すべてのファイル\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"MMDモデルを開く";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    LoadModelFromPath(fileBuf);
}

// 割り当てたモーフ名をモデルから探した結果 (見つからなければ -1)
static int SlotMorphIndex(int slot) {
    if (!g_modelLoaded) return -1;
    return mmd::FindMorph(g_model, kMorphBufs[slot]);
}

// 現在の口パク重みをモデルの頂点に反映してVBを更新する (変化があったときだけ)
static void UpdateModelVertices(const float w[6]) {
    if (!g_mdlVB || !g_modelLoaded) return;
    int mapping[6];
    bool same = true;
    for (int k = 0; k < 6; k++) {
        mapping[k] = SlotMorphIndex(k);
        if (mapping[k] != g_lastMapping[k] || std::fabs(w[k] - g_lastWeights[k]) > 1e-4f) same = false;
    }
    if (same) return;

    std::vector<std::pair<int, float>> active;
    for (int k = 0; k < 6; k++) if (mapping[k] >= 0 && w[k] > 0.0f) active.push_back({ mapping[k], w[k] });
    mmd::ApplyMorphs(g_model, active, g_morphedTmp);

    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(g_pd3dDeviceContext->Map(g_mdlVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        std::memcpy(ms.pData, g_morphedTmp.data(), g_morphedTmp.size() * sizeof(mmd::Vertex));
        g_pd3dDeviceContext->Unmap(g_mdlVB, 0);
        for (int k = 0; k < 6; k++) { g_lastWeights[k] = w[k]; g_lastMapping[k] = mapping[k]; }
    }
}

static bool EnsurePreviewTarget(int w, int h) {
    if (g_pvSrv && w == g_pvW && h == g_pvH) return true;
    ReleasePreviewTarget();

    UINT q1 = 0, q2 = 0;
    g_pd3dDevice->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 4, &q1);
    g_pd3dDevice->CheckMultisampleQualityLevels(DXGI_FORMAT_D32_FLOAT, 4, &q2);
    g_pvSamples = (q1 > 0 && q2 > 0) ? 4 : 1;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    bool ok = SUCCEEDED(g_pd3dDevice->CreateTexture2D(&td, nullptr, &g_pvResolveTex));
    ok = ok && SUCCEEDED(g_pd3dDevice->CreateShaderResourceView(g_pvResolveTex, nullptr, &g_pvSrv));
    ok = ok && SUCCEEDED(g_pd3dDevice->CreateRenderTargetView(g_pvResolveTex, nullptr, &g_pvResolveRtv));

    if (ok && g_pvSamples > 1) {
        D3D11_TEXTURE2D_DESC md = td;
        md.SampleDesc.Count = g_pvSamples;
        md.BindFlags = D3D11_BIND_RENDER_TARGET;
        ok = SUCCEEDED(g_pd3dDevice->CreateTexture2D(&md, nullptr, &g_pvMsaaTex));
        ok = ok && SUCCEEDED(g_pd3dDevice->CreateRenderTargetView(g_pvMsaaTex, nullptr, &g_pvMsaaRtv));
    }
    if (ok) {
        D3D11_TEXTURE2D_DESC dd = td;
        dd.Format = DXGI_FORMAT_D32_FLOAT;
        dd.SampleDesc.Count = g_pvSamples;
        dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ok = SUCCEEDED(g_pd3dDevice->CreateTexture2D(&dd, nullptr, &g_pvDepthTex));
        ok = ok && SUCCEEDED(g_pd3dDevice->CreateDepthStencilView(g_pvDepthTex, nullptr, &g_pvDsv));
    }
    if (!ok) { ReleasePreviewTarget(); return false; }
    g_pvW = w; g_pvH = h;
    return true;
}

// モデルをオフスクリーンRTに描画する
static void RenderModelToTexture(int w, int h) {
    if (!g_mdlReady || !g_modelLoaded || !g_mdlVB || !g_mdlIB) return;
    if (!EnsurePreviewTarget(w, h)) return;
    ID3D11DeviceContext* ctx = g_pd3dDeviceContext;

    ID3D11RenderTargetView* rtv = (g_pvSamples > 1) ? g_pvMsaaRtv : g_pvResolveRtv;
    ctx->OMSetRenderTargets(1, &rtv, g_pvDsv);
    D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    ctx->RSSetViewports(1, &vp);

    float clear[4] = { 0.16f, 0.17f, 0.20f, 1.0f };
    if (g_themeBase == 1) { clear[0] = 0.80f; clear[1] = 0.82f; clear[2] = 0.86f; }
    ctx->ClearRenderTargetView(rtv, clear);
    ctx->ClearDepthStencilView(g_pvDsv, D3D11_CLEAR_DEPTH, 1.0f, 0);

    // カメラ (MMDは左手系・Y上。モデルは-Z向きなので、yaw=0のカメラは-Z側から見る)
    float H = std::max(1.0f, g_model.bmax[1] - g_model.bmin[1]);
    float cp = std::cos(g_camPitch), sp = std::sin(g_camPitch);
    float ex = g_camTarget[0] + g_camDist * std::sin(g_camYaw) * cp;
    float ey = g_camTarget[1] + g_camDist * sp;
    float ez = g_camTarget[2] - g_camDist * std::cos(g_camYaw) * cp;
    DirectX::XMVECTOR eye = DirectX::XMVectorSet(ex, ey, ez, 1.0f);
    DirectX::XMVECTOR tgt = DirectX::XMVectorSet(g_camTarget[0], g_camTarget[1], g_camTarget[2], 1.0f);
    DirectX::XMMATRIX V = DirectX::XMMatrixLookAtLH(eye, tgt, DirectX::XMVectorSet(0, 1, 0, 0));
    float nearZ = std::max(0.05f, g_camDist * 0.05f);
    float farZ = g_camDist + H * 3.0f + 50.0f;
    DirectX::XMMATRIX P = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(30.0f), (float)w / (float)h, nearZ, farZ);

    MdlCB cb = {};
    DirectX::XMStoreFloat4x4(&cb.viewProj, DirectX::XMMatrixMultiply(V, P));
    // ライトはカメラ側やや上から (常に顔が明るく見えるように)
    float lx = ex - g_camTarget[0], ly = ey - g_camTarget[1] + g_camDist * 0.5f, lz = ez - g_camTarget[2];
    float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
    if (ll < 1e-5f) ll = 1.0f;
    cb.lightDir[0] = lx / ll; cb.lightDir[1] = ly / ll; cb.lightDir[2] = lz / ll;

    UINT stride = sizeof(mmd::Vertex), offset = 0;
    ctx->IASetInputLayout(g_mdlLayout);
    ctx->IASetVertexBuffers(0, 1, &g_mdlVB, &stride, &offset);
    ctx->IASetIndexBuffer(g_mdlIB, DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(g_mdlVS, nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &g_mdlCB);
    ctx->PSSetShader(g_mdlPS, nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, &g_mdlCB);
    ID3D11SamplerState* samplers[2] = { g_mdlSampler, g_toonSampler };
    ctx->PSSetSamplers(0, 2, samplers);
    float bf[4] = { 0, 0, 0, 0 };
    ctx->OMSetBlendState(g_bsAlpha, bf, 0xffffffff);
    ctx->OMSetDepthStencilState(g_dsState, 0);

    for (auto& mt : g_model.materials) {
        if (mt.indexCount == 0 || mt.diffuse[3] < 0.001f) continue;
        std::memcpy(cb.diffuse, mt.diffuse, sizeof(float) * 4);
        cb.ambient[0] = mt.ambient[0]; cb.ambient[1] = mt.ambient[1]; cb.ambient[2] = mt.ambient[2]; cb.ambient[3] = 1.0f;
        ID3D11ShaderResourceView* srv = g_whiteSrv;
        cb.flags[0] = 0.0f;
        if (mt.texIndex >= 0 && mt.texIndex < (int)g_mdlTex.size() && g_mdlTex[(size_t)mt.texIndex]) {
            srv = g_mdlTex[(size_t)mt.texIndex];
            cb.flags[0] = 1.0f;
        }
        // toon: 個別toon画像 → 共有toon画像 → 標準toon(近似) の順に選ぶ
        ID3D11ShaderResourceView* toon = nullptr;
        if (mt.toonTexIndex >= 0 && mt.toonTexIndex < (int)g_mdlTex.size()) toon = g_mdlTex[(size_t)mt.toonTexIndex];
        if (!toon) {
            int sidx = (mt.toonShared >= 0 && mt.toonShared < 10) ? mt.toonShared : 0;
            toon = g_toonModel[sidx] ? g_toonModel[sidx] : g_toonBuiltin[sidx];
        }
        ID3D11ShaderResourceView* srvs[2] = { srv, toon };
        ctx->UpdateSubresource(g_mdlCB, 0, nullptr, &cb, 0, 0);
        ctx->PSSetShaderResources(0, 2, srvs);
        ctx->RSSetState(mt.doubleSided ? g_rsNoCull : g_rsCull);
        ctx->DrawIndexed(mt.indexCount, mt.indexStart, 0);
    }

    if (g_pvSamples > 1) ctx->ResolveSubresource(g_pvResolveTex, 0, g_pvMsaaTex, 0, DXGI_FORMAT_R8G8B8A8_UNORM);

    ID3D11ShaderResourceView* nullSrvs[2] = { nullptr, nullptr };
    ctx->PSSetShaderResources(0, 2, nullSrvs);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
// プレビュー用モーフ生成 + 再生制御
// ---------------------------------------------------------------------------
static std::vector<double> GenSignature() {
    return { g_p.prepMs, g_p.snapMs, g_p.releaseMs, g_p.smoothCloseMs, g_p.geminateExtraMs, g_p.gapMs,
             g_p.maxWeight, g_p.consScale, g_p.humanize, g_p.fps, g_p.legato, g_p.smoothMs, g_p.consFloor,
             (double)g_reducePct, (double)g_pitchFxPct, (double)g_dynFxPct,
             (double)g_selectedTrack, (double)g_songSerial,
             (double)(g_vocalLoaded ? g_vocalSerial : 0), (double)g_vocalStartFrame, (double)g_wavPitchMix, (double)g_wavDynMix };
}
static std::vector<double> g_genSig;
static double g_genChangeTime = 0.0;
static bool g_genDirty = true;
static double g_lastRebuildTime = -100.0;

// WAVが始まる時刻 (秒) と終わる時刻
static double WavStartSec() { return (double)g_wavStartFrame / (g_p.fps > 0.0 ? g_p.fps : 30.0); }
static double WavEndSec() { return g_wavLoaded ? WavStartSec() + g_wav.Duration() : 0.0; }

// 現在のパラメータでモーフキーフレームを作り直す (VMD生成と同じ処理なので、プレビュー=出力結果)
static void RebuildPreviewTracksCore() {
    g_prevTracks.clear();
    g_prevDuration = 0.0;
    if (!g_loaded && g_packedActive && !g_packedTracks.empty()) {   // 元のvsqxが無いプロジェクト: 保存してあったVMDを使う
        g_prevTracks = g_packedTracks;
        long lastFrame = 0;
        for (auto& kv : g_prevTracks) for (auto& fr : kv.second) lastFrame = std::max(lastFrame, fr.first);
        g_prevDuration = lastFrame / g_p.fps + 0.5;
        return;
    }
    if (!g_loaded || g_selectedTrack < 0 || g_selectedTrack >= (int)g_song.tracks.size() ||
        g_song.tracks[g_selectedTrack].empty() ||
        g_selectedTrack >= (int)g_song.pitchBendCurves.size() || g_selectedTrack >= (int)g_song.dynamicsCurves.size())
        return;
    try {
        PrepareDerivedParams();
        vsq::TempoMap tm(g_song.resolution, g_song.tempos);
        auto& notes = g_song.tracks[g_selectedTrack];
        const vsq::Curve* effP = nullptr; const vsq::Curve* effD = nullptr;
        GetEffectiveCurves(g_selectedTrack, effP, effD);
        g_prevTracks = vsq::BuildMorphTracks(notes, tm, g_p, *effP, *effD);

        long lastFrame = 0;
        for (auto& kv : g_prevTracks) for (auto& fr : kv.second) lastFrame = std::max(lastFrame, fr.first);
        double endSec = 0.0;
        for (auto& n : notes) endSec = std::max(endSec, tm.TickToSec(n.tick + n.dur));
        g_prevDuration = std::max(lastFrame / g_p.fps, endSec) + 0.5;
    }
    catch (std::exception& e) {
        g_prevTracks.clear();
        g_prevDuration = 0.0;
        g_statusText = std::string("プレビュー生成エラー: ") + e.what();
    }
}

static void RebuildPreviewTracks() {
    RebuildPreviewTracksCore();
    g_prevDuration = std::max(g_prevDuration, WavEndSec());   // WAVが曲より長ければ、その長さまで再生できる
}

static float EvalTrackAt(const vsq::MorphTrack& tr, double frame) {
    if (tr.empty()) return 0.0f;
    if (frame <= (double)tr.front().first) return tr.front().second;
    if (frame >= (double)tr.back().first) return tr.back().second;
    auto it = std::upper_bound(tr.begin(), tr.end(), frame,
        [](double f, const std::pair<long, float>& e) { return f < (double)e.first; });
    const auto& b = *it;
    const auto& a = *(it - 1);   // MMDのモーフ補間は線形
    double t = (frame - (double)a.first) / std::max(1.0, (double)(b.first - a.first));
    return (float)(a.second + (b.second - a.second) * t);
}

static void CurrentMorphWeights(float out[6]) {
    double frame = g_playTime * g_p.fps;
    for (int k = 0; k < 6; k++) {
        auto it = g_prevTracks.find(kSlotKeys[k]);
        out[k] = (it == g_prevTracks.end()) ? 0.0f : std::max(0.0f, std::min(1.0f, EvalTrackAt(it->second, frame)));
    }
}

// 毎フレーム呼ぶ: パラメータ変更を検知してプレビューを再生成 + 再生位置を進める
static void UpdatePreview(double now, float dt) {
    std::vector<double> sig = GenSignature();
    if (sig != g_genSig) { g_genSig = sig; g_genChangeTime = now; g_genDirty = true; }
    // スライダー操作が止まって0.12秒後に再生成。ドラッグし続けている間も0.4秒ごとには更新する
    if (g_genDirty && (now - g_genChangeTime >= 0.12 || now - g_lastRebuildTime >= 0.4)) {
        g_genDirty = false;
        g_lastRebuildTime = now;
        RebuildPreviewTracks();
    }
    if (g_playing) {
        if (g_prevDuration <= 0.0) g_playing = false;
        else {
            g_playTime += (double)dt * (double)g_playSpeed;
            if (g_playTime >= g_prevDuration) {
                if (g_loop) g_playTime = std::fmod(g_playTime, g_prevDuration);
                else { g_playTime = g_prevDuration; g_playing = false; }
            }
        }
    }
    if (g_playTime > g_prevDuration) g_playTime = g_prevDuration;
    if (g_playTime < 0.0) g_playTime = 0.0;
}

// ---------------------------------------------------------------------------
// WAVの再生 (waveOut)。再生位置・シーク・速度・音量が変わったら、その位置から鳴らし直す
// ---------------------------------------------------------------------------
static HWAVEOUT g_hwo = nullptr;
static WAVEHDR g_wavHdr = {};
static std::vector<int16_t> g_audioBuf;
static double g_audSyncWall = 0.0, g_audSyncPos = 0.0, g_audSpeed = 1.0, g_audVol = 1.0;

static double NowSec() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

static void StopAudio() {
    if (!g_hwo) return;
    waveOutReset(g_hwo);
    waveOutUnprepareHeader(g_hwo, &g_wavHdr, sizeof(WAVEHDR));
    waveOutClose(g_hwo);
    g_hwo = nullptr;
    g_wavHdr = WAVEHDR{};
}

static bool StartAudioAt(double tlSec) {
    StopAudio();
    if (!g_wavLoaded || g_wav.frames == 0) return false;
    long long startFrame = (long long)std::floor((tlSec - WavStartSec()) * (double)g_wav.sampleRate);
    long long lead = 0;                                   // WAVの開始より前の位置からなら、先頭に無音を入れる
    if (startFrame < 0) { lead = -startFrame; startFrame = 0; }
    if (startFrame >= (long long)g_wav.frames) return false;
    const size_t ch = (size_t)g_wav.channels;
    const size_t n = g_wav.frames - (size_t)startFrame;
    const float vol = std::max(0.0f, std::min(1.0f, g_wavVolume));
    g_audioBuf.assign((size_t)(lead + (long long)n) * ch, 0);
    for (size_t i = 0; i < n * ch; i++)
        g_audioBuf[(size_t)lead * ch + i] = (int16_t)std::lround((float)g_wav.pcm[(size_t)startFrame * ch + i] * vol);

    WAVEFORMATEX wf = {};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = (WORD)ch;
    wf.nSamplesPerSec = (DWORD)std::max(8000.0, std::min(192000.0, (double)g_wav.sampleRate * (double)g_playSpeed));   // 再生速度に合わせる
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = (WORD)(wf.nChannels * 2);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
    if (waveOutOpen(&g_hwo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { g_hwo = nullptr; return false; }
    g_wavHdr = WAVEHDR{};
    g_wavHdr.lpData = (LPSTR)g_audioBuf.data();
    g_wavHdr.dwBufferLength = (DWORD)(g_audioBuf.size() * sizeof(int16_t));
    waveOutPrepareHeader(g_hwo, &g_wavHdr, sizeof(WAVEHDR));
    waveOutWrite(g_hwo, &g_wavHdr, sizeof(WAVEHDR));
    g_audSyncWall = NowSec();
    g_audSyncPos = tlSec;
    g_audSpeed = (double)g_playSpeed;
    g_audVol = (double)g_wavVolume;
    return true;
}

// 毎フレーム呼ぶ: 再生中なら音を鳴らし、位置がずれた(シーク・ループ)ら鳴らし直す
static void UpdateAudio() {
    static bool wasPlaying = false;
    if (!(g_wavLoaded && !g_wavMute && g_playing)) { StopAudio(); wasPlaying = false; return; }
    const double predicted = g_audSyncPos + (NowSec() - g_audSyncWall) * g_audSpeed;
    if (!wasPlaying || !g_hwo || std::fabs(g_playTime - predicted) > 0.25 ||
        g_audSpeed != (double)g_playSpeed || g_audVol != (double)g_wavVolume) {
        if (!StartAudioAt(g_playTime)) { g_audSyncWall = NowSec(); g_audSyncPos = g_playTime; g_audSpeed = (double)g_playSpeed; g_audVol = (double)g_wavVolume; }
    }
    wasPlaying = true;
}

static void LoadWavFromPath(const std::wstring& path) {
    try {
        wavdata::Audio a = wavdata::Parse(ReadFileBytes(path));
        StopAudio();
        g_wav = std::move(a);
        g_wavPeaks.Build(g_wav);
        g_wavPath = path;
        g_wavLoaded = true;
        g_genDirty = true;
        char msg[160];
        snprintf(msg, sizeof(msg), "WAV読み込み完了 (%.1f秒 / %d Hz / %dch)", g_wav.Duration(), g_wav.sampleRate, g_wav.channels);
        g_statusText = msg;
    }
    catch (std::exception& e) {
        g_statusText = std::string("WAVを開けません: ") + e.what();
    }
}

static void UnloadWav() {
    StopAudio();
    g_wavLoaded = false;
    g_wav = wavdata::Audio();
    g_wavPeaks = wavdata::Peaks();
    g_wavPath.clear();
    g_startupWavPath.clear();
    g_genDirty = true;
}

static void LoadVocalFromPath(const std::wstring& path) {
    try {
        wavdata::Audio a = wavdata::Parse(ReadFileBytes(path));
        f0::Result r = f0::Analyze(a);
        if (r.midi.empty()) throw std::runtime_error("音声が短すぎて解析できません");
        g_vocalDuration = a.Duration();
        g_f0 = std::move(r);
        g_vocalPath = path;
        g_vocalLoaded = true;
        g_vocalSerial++;
        g_genDirty = true;
        char msg[160];
        snprintf(msg, sizeof(msg), "ボーカルWAVを解析しました (%.1f秒 / 有声 %.0f%%)", g_vocalDuration, g_f0.VoicedRatio() * 100.0);
        g_statusText = msg;
    }
    catch (std::exception& e) {
        g_statusText = std::string("ボーカルWAVを開けません: ") + e.what();
    }
}

static void UnloadVocal() {
    g_vocalLoaded = false;
    g_f0 = f0::Result();
    g_vocalPath.clear();
    g_startupVocalPath.clear();
    g_vocalDuration = 0.0;
    g_vocalSerial++;
    g_genDirty = true;
}

static void OpenVocalDialog() {
    wchar_t fileBuf[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"WAVファイル (*.wav)\0*.wav\0すべてのファイル\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"ボーカルのみのWAVを開く";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    LoadVocalFromPath(fileBuf);
}

static void OpenWavDialog() {
    wchar_t fileBuf[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"WAVファイル (*.wav)\0*.wav\0すべてのファイル\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"WAVを開く";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    LoadWavFromPath(fileBuf);
}

static void TogglePlay() {
    if (g_prevDuration <= 0.0) return;
    if (!g_playing && g_playTime >= g_prevDuration - 0.01) g_playTime = 0.0;
    g_playing = !g_playing;
}

static void FormatTime(double s, char* buf, size_t sz) {
    if (s < 0) s = 0;
    int m = (int)(s / 60.0);
    snprintf(buf, sz, "%02d:%04.1f", m, s - m * 60.0);
}

// 再生バーの高さ (シークバー + 操作ボタンの1行 + 余白)。ピアノロールの高さの計算にも使う
static float PlaybackBarHeight() {
    const float seekH = ImGui::GetFontSize() + 4.0f;
    const float btnH = ImGui::GetFrameHeight() + 6.0f;
    return seekH + btnH + ImGui::GetStyle().ItemSpacing.y * 2.0f + 6.0f;
}

// 中央寄せの再生操作 (OpenUtauの再生バー風): 上にシークバー、下に 停止 / 再生 / ループ / 時間 / 速度
static void RenderPlaybackBar() {
    const bool can = g_prevDuration > 0.0;
    const ImGuiStyle& st = ImGui::GetStyle();
    const float btn = ImGui::GetFrameHeight() + 6.0f;
    ImGui::BeginDisabled(!can);

    float t = (float)g_playTime;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 2.0f));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##seek", &t, 0.0f, (float)std::max(0.01, g_prevDuration), "")) g_playTime = (double)t;
    ImGui::PopStyleVar();

    char t0[32], t1[32];
    FormatTime(g_playTime, t0, sizeof(t0));
    FormatTime(g_prevDuration, t1, sizeof(t1));
    char tt[80];
    snprintf(tt, sizeof(tt), "%s / %s", t0, t1);
    const float speedW = 130.0f;
    const float total = btn * 3.0f + ImGui::CalcTextSize(tt).x + speedW + st.ItemSpacing.x * 4.0f;
    const float off = (ImGui::GetContentRegionAvail().x - total) * 0.5f;
    if (off > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);

    if (IconButton(ICN_STOP, "", ImVec2(btn, btn))) { g_playing = false; g_playTime = 0.0; }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_TabActive));   // 再生ボタンは常にアクセント色
    if (IconButton(g_playing ? ICN_PAUSE : ICN_PLAY, "", ImVec2(btn, btn))) TogglePlay();
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (g_loop) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_TabActive));
    if (IconButton(ICN_LOOP, "", ImVec2(btn, btn))) g_loop = !g_loop;
    if (g_loop) ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (btn - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::TextUnformatted(tt);
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (btn - ImGui::GetTextLineHeight()) * 0.5f + (btn - ImGui::GetFrameHeight()) * 0.5f);
    ImGui::SetNextItemWidth(speedW);
    ImGui::SliderFloat("##play_speed", &g_playSpeed, 0.25f, 2.0f, T("速度 %.2fx"));
    ImGui::EndDisabled();
}

static void RenderPreviewPanel(float width, float height) {
    ImGui::BeginChild("preview_container", ImVec2(width, height), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = (int)avail.x, h = (int)avail.y;

    if (!g_modelLoaded || !g_mdlReady || w < 16 || h < 16) {
        const char* msg = g_modelLoaded ? "" : T("MMDモデルなし");
        ImVec2 ts = ImGui::CalcTextSize(msg);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + std::max(0.0f, (avail.x - ts.x) * 0.5f), p.y + std::max(0.0f, (avail.y - ts.y) * 0.5f)),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), msg);
        ImGui::EndChild();
        return;
    }

    float wts[6];
    CurrentMorphWeights(wts);
    UpdateModelVertices(wts);
    RenderModelToTexture(w, h);

    ImVec2 pos = ImGui::GetCursorScreenPos();
    if (g_pvSrv) ImGui::Image((ImTextureID)(intptr_t)g_pvSrv, ImVec2((float)w, (float)h));
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("##preview_input", ImVec2((float)w, (float)h),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();

    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        g_camYaw -= io.MouseDelta.x * 0.008f;
        g_camPitch = std::max(-1.4f, std::min(1.4f, g_camPitch + io.MouseDelta.y * 0.008f));
    }
    if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
        // 画面平面内で平行移動 (右方向 = (cos yaw, 0, sin yaw))
        float k = g_camDist * 0.0016f;
        g_camTarget[0] -= std::cos(g_camYaw) * io.MouseDelta.x * k;
        g_camTarget[2] -= std::sin(g_camYaw) * io.MouseDelta.x * k;
        g_camTarget[1] += io.MouseDelta.y * k;
    }
    if (hovered && io.MouseWheel != 0.0f) {
        float H = std::max(1.0f, g_model.bmax[1] - g_model.bmin[1]);
        g_camDist = std::max(H * 0.05f, std::min(H * 6.0f, g_camDist * std::exp(-io.MouseWheel * 0.12f)));
    }

    // オーバーレイ: モデル名 と 現在のあいうえおん重み
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 ovText = IM_COL32(235, 235, 240, 230);
    dl->AddText(ImVec2(pos.x + 8, pos.y + 6), ovText, g_model.name.c_str());
    float by = pos.y + (float)h - 8.0f - 6.0f * 16.0f;
    for (int k = 0; k < 6; k++) {
        float y = by + k * 16.0f;
        dl->AddText(ImVec2(pos.x + 8, y), ovText, kSlotLabels[k]);
        ImVec2 a(pos.x + 30, y + 3), b(pos.x + 30 + 70, y + 13);
        dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 120), 2.0f);
        dl->AddRectFilled(a, ImVec2(a.x + 70.0f * wts[k], b.y), IM_COL32(230, 120, 60, 230), 2.0f);
        dl->AddRect(a, b, IM_COL32(255, 255, 255, 90), 2.0f);
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// 横方向タイムライン型ピアノロール描画機能 (Piapro Studio NT風ピアノ鍵盤 & Cのみ階名表示)
// ---------------------------------------------------------------------------
static void RenderPianoRollTimeline(float width, float height) {
    ImGui::BeginChild("pianoroll_container", ImVec2(width, height), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();

    if (!g_loaded || g_selectedTrack >= (int)g_song.tracks.size() || g_song.tracks[g_selectedTrack].empty()) {
        ImU32 bgCol = ImGui::GetColorU32(ImGuiCol_FrameBg);
        drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), bgCol);
        const char* msg = T("曲なし");
        ImVec2 txtSz = ImGui::CalcTextSize(msg);
        drawList->AddText(ImVec2(canvasPos.x + (canvasSize.x - txtSz.x) * 0.5f, canvasPos.y + (canvasSize.y - txtSz.y) * 0.5f),
            IM_COL32(180, 180, 180, 255), msg);
        ImGui::EndChild();
        return;
    }

    auto& notes = g_song.tracks[g_selectedTrack];
    vsq::TempoMap tm(g_song.resolution, g_song.tempos);
    const vsq::Curve* effP = nullptr; const vsq::Curve* effD = nullptr;
    GetEffectiveCurves(g_selectedTrack, effP, effD);   // ボーカルWAVの反映後の曲線
    const vsq::Curve& pitchCurve = *effP;
    const vsq::Curve& dynCurve = *effD;

    // トラック内の音高範囲(MIDIノート番号)を算出
    int minNote = 127, maxNote = 0;
    for (auto& n : notes) {
        if (n.noteNumber < minNote) minNote = n.noteNumber;
        if (n.noteNumber > maxNote) maxNote = n.noteNumber;
    }
    if (minNote > maxNote) { minNote = 60; maxNote = 72; }
    minNote = std::max(0, minNote - 2);
    maxNote = std::min(127, maxNote + 2);
    int noteRange = std::max(1, maxNote - minNote + 1);

    const float headerH = 26.0f;    // 時間軸の高さ
    const float keyLabelW = 56.0f;  // 左端の鍵盤幅
    const float laneDynH = g_prShowDyn ? 56.0f : 0.0f;   // Dynamicsレーン
    const float laneVelH = g_prShowVel ? 56.0f : 0.0f;   // Velocityレーン
    const float laneH = laneDynH + laneVelH;
    float rowH = g_prRowHeight;     // 各音高行の高さ
    {   // 表示領域に余りがあるときは、上下の音域を足して鍵盤を領域いっぱいに広げる
        const float availH = std::max(rowH, canvasSize.y - headerH - laneH);
        const int fitRows = (int)std::floor(availH / rowH);
        if (fitRows > noteRange) {
            const int extra = fitRows - noteRange;
            const int lo = extra / 2, hi = extra - lo;
            int nmin = minNote - lo, nmax = maxNote + hi;
            if (nmin < 0) { nmax += -nmin; nmin = 0; }
            if (nmax > 127) { nmin -= nmax - 127; nmax = 127; }
            minNote = std::max(0, nmin);
            maxNote = std::min(127, nmax);
            noteRange = maxNote - minNote + 1;
            if (noteRange * rowH < availH) rowH = availH / (float)noteRange;
        }
    }
    float noteAreaH = noteRange * rowH;
    float totalRollH = headerH + noteAreaH + laneH;

    // --- テーマに応じたカラーパレットの分岐設定 ---
    ImU32 bgCol, headerBg, blackKeyRowBg, whiteKeyRowBg, gridLineCol, textCol;

    if (g_themeBase == 0) { // Dark モード
        bgCol = ImGui::GetColorU32(ImGuiCol_FrameBg);
        headerBg = IM_COL32(24, 24, 26, 255);
        blackKeyRowBg = IM_COL32(0, 0, 0, 45);
        whiteKeyRowBg = IM_COL32(255, 255, 255, 8);
        gridLineCol = IM_COL32(255, 255, 255, 20);
        textCol = IM_COL32(200, 200, 200, 255);
    }
    else { // White モード
        bgCol = IM_COL32(245, 245, 248, 255);
        headerBg = IM_COL32(220, 222, 228, 255);
        blackKeyRowBg = IM_COL32(0, 0, 0, 18);
        whiteKeyRowBg = IM_COL32(255, 255, 255, 255);
        gridLineCol = IM_COL32(0, 0, 0, 25);
        textCol = IM_COL32(50, 50, 55, 255);
    }

    // キャンバス背景
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + std::max(canvasSize.y, totalRollH)), bgCol);

    // キャンバス上の操作（ダブルクリック全画面化、横スクロール＆ズーム）
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("pianoroll_input", ImVec2(canvasSize.x, std::max(canvasSize.y, totalRollH)));
    bool isHovered = ImGui::IsItemHovered();
    bool isActive = ImGui::IsItemActive();

    // 時間軸(上の帯)のクリック/ドラッグで再生位置を移動
    static bool s_seeking = false;
    ImVec2 mp = ImGui::GetIO().MousePos;
    bool inHeader = mp.y < canvasPos.y + headerH && mp.x >= canvasPos.x + keyLabelW;
    if (isHovered && inHeader && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) s_seeking = true;
    if (s_seeking) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            double t = ((double)(mp.x - canvasPos.x - keyLabelW) + (double)g_prScrollX) / (double)g_prZoomX;
            g_playTime = std::max(0.0, std::min(g_prevDuration, t));
        }
        else {
            s_seeking = false;
        }
    }

    // ダブルクリックで全画面切り替え (時間軸の上は除く)
    if (isHovered && !inHeader && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        g_prFullScreen = !g_prFullScreen;
    }

    if (isActive && !s_seeking && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        g_prScrollX -= ImGui::GetIO().MouseDelta.x;
        if (g_prScrollX < 0.0f) g_prScrollX = 0.0f;
    }
    if (isHovered && ImGui::GetIO().MouseWheel != 0.0f) {
        float wheel = ImGui::GetIO().MouseWheel;
        float oldZoom = g_prZoomX;
        g_prZoomX = std::max(30.0f, std::min(800.0f, g_prZoomX * (wheel > 0 ? 1.25f : 0.8f)));
        float mouseX = ImGui::GetIO().MousePos.x - canvasPos.x;
        float timeAtMouse = (g_prScrollX + mouseX) / oldZoom;
        g_prScrollX = std::max(0.0f, timeAtMouse * g_prZoomX - mouseX);
    }

    // 再生中は再生位置が見える範囲に入るようスクロールを追従させる
    if (g_playing && !isActive) {
        float viewW = canvasSize.x - keyLabelW;
        float phx = (float)(g_playTime * g_prZoomX) - g_prScrollX;
        if (phx > viewW * 0.9f || phx < 0.0f)
            g_prScrollX = std::max(0.0f, (float)(g_playTime * g_prZoomX) - viewW * 0.1f);
    }

    // --- 1. 行の背景と鍵盤 ---
    const bool dark = (g_themeBase == 0);
    const float rollL = canvasPos.x + keyLabelW, rollR = canvasPos.x + canvasSize.x;
    const ImVec4 accentV = AdjustAccentForTheme(g_accentColor, g_themeBase);
    const ImU32 accentCol = ImGui::ColorConvertFloat4ToU32(accentV);
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + headerH), headerBg);

    // 再生位置にあるノートの音高 (鍵盤の強調に使う)
    int playingNote = -1;
    for (auto& n : notes) {
        const double a0 = tm.TickToSec(n.tick), a1 = tm.TickToSec(n.tick + n.dur);
        if (g_playTime >= a0 && g_playTime < a1) { playingNote = n.noteNumber; break; }
    }

    const ImU32 octaveLine = dark ? IM_COL32(255, 255, 255, 60) : IM_COL32(0, 0, 0, 70);
    const ImU32 whiteKey = dark ? IM_COL32(206, 209, 216, 255) : IM_COL32(250, 250, 252, 255);
    const ImU32 whiteKeyBack = dark ? IM_COL32(172, 175, 183, 255) : IM_COL32(226, 228, 233, 255);
    const ImU32 keyBorder = dark ? IM_COL32(110, 112, 120, 255) : IM_COL32(190, 195, 205, 255);
    const ImU32 blackKey = dark ? IM_COL32(28, 29, 34, 255) : IM_COL32(62, 65, 74, 255);
    for (int n = minNote; n <= maxNote; n++) {
        const float y = canvasPos.y + headerH + (maxNote - n) * rowH;
        int rem = n % 12;
        if (rem < 0) rem += 12;
        const bool isBlackKey = (rem == 1 || rem == 3 || rem == 6 || rem == 8 || rem == 10);
        const bool playing = (n == playingNote);

        // 行
        drawList->AddRectFilled(ImVec2(rollL, y), ImVec2(rollR, y + rowH), isBlackKey ? blackKeyRowBg : whiteKeyRowBg);
        drawList->AddLine(ImVec2(rollL, y + rowH), ImVec2(rollR, y + rowH), rem == 0 ? octaveLine : gridLineCol);

        // 鍵盤: 白鍵を全幅、黒鍵はその上に短く重ねる
        drawList->AddRectFilled(ImVec2(canvasPos.x, y), ImVec2(rollL, y + rowH), isBlackKey ? whiteKeyBack : (playing ? accentCol : whiteKey));
        drawList->AddLine(ImVec2(canvasPos.x, y + rowH), ImVec2(rollL, y + rowH), keyBorder);
        if (isBlackKey) {
            drawList->AddRectFilled(ImVec2(canvasPos.x, y + 1.0f), ImVec2(canvasPos.x + keyLabelW * 0.62f, y + rowH - 1.0f),
                playing ? accentCol : blackKey, 2.0f);
        }
        if (rem == 0 && rowH >= 12.0f) {   // Cの鍵盤にだけ音名 (C4 など)
            char cLabel[8];
            snprintf(cLabel, sizeof(cLabel), "C%d", n / 12 - 1);
            const ImVec2 ts = ImGui::CalcTextSize(cLabel);
            drawList->AddText(ImVec2(rollL - ts.x - 5.0f, y + (rowH - ts.y) * 0.5f),
                playing ? GetContrastingTextColor(accentV.x, accentV.y, accentV.z) : IM_COL32(40, 40, 48, 255), cLabel);
        }
    }
    drawList->AddLine(ImVec2(rollL, canvasPos.y + headerH), ImVec2(rollL, canvasPos.y + headerH + noteAreaH), keyBorder);

    // --- 2. 時間軸 (目盛りと縦グリッド。主線の間を4分割した補助線つき) ---
    const double maxTimeSec = tm.TickToSec(notes.back().tick + notes.back().dur) + 2.0;
    const float majorStep = (g_prZoomX < 80.0f) ? 2.0f : ((g_prZoomX < 250.0f) ? 1.0f : 0.5f);
    const int kSub = 4;
    const double minorStep = (double)majorStep / kSub;
    const ImU32 majorCol = (gridLineCol & 0x00FFFFFF) | (46u << 24);
    const ImU32 minorCol = (gridLineCol & 0x00FFFFFF) | (14u << 24);
    drawList->PushClipRect(ImVec2(rollL, canvasPos.y), ImVec2(rollR, canvasPos.y + totalRollH), true);
    for (int i = 0; (double)i * minorStep <= maxTimeSec; i++) {
        const double sec = (double)i * minorStep;
        const float x = rollL + (float)(sec * g_prZoomX) - g_prScrollX;
        if (x < rollL - 1.0f || x > rollR) continue;
        const bool major = (i % kSub == 0);
        drawList->AddLine(ImVec2(x, canvasPos.y + headerH), ImVec2(x, canvasPos.y + totalRollH), major ? majorCol : minorCol);
        drawList->AddLine(ImVec2(x, canvasPos.y + headerH - (major ? 8.0f : 4.0f)), ImVec2(x, canvasPos.y + headerH), textCol);
        if (major) {
            char tb[24];
            if (majorStep >= 1.0f) snprintf(tb, sizeof(tb), "%d:%02d", (int)(sec / 60.0), (int)(sec + 0.5) % 60);
            else snprintf(tb, sizeof(tb), "%d:%04.1f", (int)(sec / 60.0), sec - (int)(sec / 60.0) * 60.0);
            drawList->AddText(ImVec2(x + 4.0f, canvasPos.y + 4.0f), textCol, tb);
        }
    }
    drawList->PopClipRect();
    drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + headerH), ImVec2(rollR, canvasPos.y + headerH), keyBorder);

    // --- 2b. WAVの波形 (高さは一定で、表示範囲の真ん中に出す) ---
    if (g_wavLoaded && g_prShowWave) {
        drawList->PushClipRect(ImVec2(rollL, canvasPos.y + headerH), ImVec2(rollR, canvasPos.y + headerH + noteAreaH), true);
        const ImVec2 wp = ImGui::GetWindowPos();
        const float visT = std::max(canvasPos.y + headerH, wp.y);
        const float visB = std::min(canvasPos.y + headerH + noteAreaH, wp.y + ImGui::GetWindowHeight());
        const float midY = (visT + visB) * 0.5f;
        const float amp = 48.0f;
        const double wavOff = WavStartSec();
        const ImU32 wcol = ImGui::ColorConvertFloat4ToU32(ImVec4(g_wavColor[0], g_wavColor[1], g_wavColor[2], g_wavColor[3]));
        for (float x = rollL; x < rollR; x += 1.0f) {
            const double t0 = (double)(x - rollL + g_prScrollX) / (double)g_prZoomX - wavOff;
            const double t1 = t0 + 1.0 / (double)g_prZoomX;
            float lo = 0.0f, hi = 0.0f;
            if (!g_wavPeaks.Query(t0 * (double)g_wav.sampleRate, t1 * (double)g_wav.sampleRate, lo, hi)) continue;
            drawList->AddLine(ImVec2(x + 0.5f, midY - hi * amp), ImVec2(x + 0.5f, midY - lo * amp + 1.0f), wcol, 1.0f);
        }
        drawList->PopClipRect();
    }

    // --- 3. ノート (塗りつぶしの角丸。歌詞はノートの中に表示) ---
    drawList->PushClipRect(ImVec2(rollL, canvasPos.y + headerH), ImVec2(rollR, canvasPos.y + headerH + noteAreaH), true);
    const ImU32 noteFill = ImGui::ColorConvertFloat4ToU32(ImVec4(accentV.x, accentV.y, accentV.z, 0.92f));
    const ImU32 noteFillPlay = ImGui::ColorConvertFloat4ToU32(ImVec4(std::min(1.0f, accentV.x * 1.25f + 0.1f),
        std::min(1.0f, accentV.y * 1.25f + 0.1f), std::min(1.0f, accentV.z * 1.25f + 0.1f), 1.0f));
    const ImU32 noteBorder = ImGui::ColorConvertFloat4ToU32(ImVec4(accentV.x * 0.55f, accentV.y * 0.55f, accentV.z * 0.55f, 1.0f));
    const ImU32 noteText = GetContrastingTextColor(accentV.x, accentV.y, accentV.z);
    const ImU32 notePhoneme = (noteText & 0x00FFFFFF) | (170u << 24);

    for (size_t i = 0; i < notes.size(); i++) {
        auto& n = notes[i];
        const double startSec = tm.TickToSec(n.tick);
        const double endSec = tm.TickToSec(n.tick + n.dur);
        const float x1 = rollL + (float)(startSec * g_prZoomX) - g_prScrollX;
        const float x2 = rollL + (float)(endSec * g_prZoomX) - g_prScrollX;
        if (x2 < rollL - 4.0f || x1 > rollR + 4.0f) continue;
        const float y = canvasPos.y + headerH + (maxNote - n.noteNumber) * rowH;

        // ミニノート (ノートの手前の小さな目印)
        if (g_prShowMini) {
            const float miniX2 = x1 - 3.0f, miniX1 = miniX2 - 11.0f;
            if (miniX2 >= rollL) {
                const float mh = std::max(4.0f, rowH * 0.45f);
                const float my1 = y + (rowH - mh) * 0.5f;
                drawList->AddRectFilled(ImVec2(miniX1, my1), ImVec2(miniX2, my1 + mh), dark ? IM_COL32(120, 126, 138, 190) : IM_COL32(150, 156, 168, 210), 2.5f);
                drawList->AddRect(ImVec2(miniX1, my1), ImVec2(miniX2, my1 + mh), dark ? IM_COL32(170, 176, 188, 255) : IM_COL32(100, 106, 118, 255), 2.5f, 0, 1.0f);
            }
        }
        const bool playing = (g_playTime >= startSec && g_playTime < endSec);
        const ImVec2 nMin(x1, y + 1.5f);
        const ImVec2 nMax(x1 + std::max(3.0f, x2 - x1 - 1.0f), y + rowH - 1.5f);
        const float rounding = std::min(5.0f, (rowH - 3.0f) * 0.45f);
        drawList->AddRectFilled(nMin, nMax, playing ? noteFillPlay : noteFill, rounding);
        drawList->AddRect(nMin, nMax, playing ? IM_COL32(255, 255, 255, 230) : noteBorder, rounding, 0, playing ? 2.0f : 1.2f);

        if (nMax.x - nMin.x > 12.0f && !n.lyric.empty()) {
            drawList->PushClipRect(ImVec2(nMin.x + 2.0f, nMin.y), ImVec2(nMax.x - 2.0f, nMax.y), true);
            const float ty = y + (rowH - ImGui::GetFontSize()) * 0.5f;
            drawList->AddText(ImVec2(x1 + 5.0f, ty), noteText, n.lyric.c_str());
            if (!n.phoneme.empty()) {
                const float lw = ImGui::CalcTextSize(n.lyric.c_str()).x;
                drawList->AddText(ImVec2(x1 + 5.0f + lw + 5.0f, ty), notePhoneme, ("[" + n.phoneme + "]").c_str());
            }
            drawList->PopClipRect();
        }
    }

    // --- 4. ピッチ曲線 (音高にそろえた細い赤線) ---
    if (g_prShowPitch) {
        const double startSecAll = tm.TickToSec(notes.front().tick);
        const double endSecAll = tm.TickToSec(notes.back().tick + notes.back().dur);
        const int numSteps = (int)(canvasSize.x / 3.0f);
        ImVec2 prevPt;
        bool hasPrev = false;
        for (int i = 0; i <= numSteps; i++) {
            const float x = rollL + i * 3.0f;
            const double timeSec = (double)(x + g_prScrollX - rollL) / g_prZoomX;
            if (timeSec < startSecAll || timeSec > endSecAll) { hasPrev = false; continue; }
            const long long tick = tm.SecToTick(timeSec);
            int baseNoteNumber = notes.front().noteNumber;
            for (auto& n : notes) {
                if (tick >= n.tick && tick <= (n.tick + n.dur)) { baseNoteNumber = n.noteNumber; break; }
                else if (n.tick > tick) break;
            }
            const double pb = pitchCurve.ValueAt(tick, 0.0);
            const double actualNote = (double)baseNoteNumber + (pb / 8192.0) * 2.0;
            const ImVec2 pt(x, canvasPos.y + headerH + (maxNote - (float)actualNote + 0.5f) * rowH);
            if (hasPrev) drawList->AddLine(prevPt, pt, IM_COL32(255, 90, 90, 220), 1.6f);
            prevPt = pt;
            hasPrev = true;
        }
    }
    // 検出したF0 (ボーカルWAVから取った歌声の音高。緑の線)
    if (g_vocalLoaded && g_prShowF0 && !g_f0.midi.empty()) {
        const double off = (double)g_vocalStartFrame / (g_p.fps > 0.0 ? g_p.fps : 30.0);
        const double tL = (double)g_prScrollX / (double)g_prZoomX - off;
        const long i0 = std::max(0L, (long)std::floor(tL / g_f0.hop) - 1);
        const ImU32 f0col = IM_COL32(90, 225, 130, 230);
        ImVec2 prevPt(0, 0);
        bool hasPrev = false;
        for (size_t i = (size_t)i0; i < g_f0.midi.size(); i++) {
            const float x = rollL + (float)(((double)i * g_f0.hop + off) * (double)g_prZoomX) - g_prScrollX;
            if (x > rollR + 2.0f) break;
            if (g_f0.midi[i] <= 0.0f) { hasPrev = false; continue; }
            const ImVec2 pt(x, canvasPos.y + headerH + ((float)maxNote - g_f0.midi[i] + 0.5f) * rowH);
            if (hasPrev) drawList->AddLine(prevPt, pt, f0col, 1.6f);
            prevPt = pt;
            hasPrev = true;
        }
    }
    drawList->PopClipRect();

    // --- 5. 強弱(ダイナミクス)レーン: ノートの下に独立して表示 ---
    if (g_prShowDyn) {
        const float ly0 = canvasPos.y + headerH + noteAreaH, ly1 = ly0 + laneDynH;
        drawList->AddRectFilled(ImVec2(canvasPos.x, ly0), ImVec2(rollR, ly1), headerBg);
        drawList->AddText(ImVec2(canvasPos.x + 6.0f, ly0 + 5.0f), textCol, "Dynamics");
        drawList->PushClipRect(ImVec2(rollL, ly0), ImVec2(rollR, ly1), true);
        const double startSecAll = tm.TickToSec(notes.front().tick);
        const double endSecAll = tm.TickToSec(notes.back().tick + notes.back().dur);
        const ImU32 dynFill = IM_COL32(60, 190, 225, 130), dynTop = IM_COL32(90, 215, 245, 230);
        float prevTop = 0.0f;
        bool hasPrev = false;
        for (float x = rollL; x <= rollR; x += 2.0f) {
            const double timeSec = (double)(x + g_prScrollX - rollL) / g_prZoomX;
            if (timeSec < startSecAll || timeSec > endSecAll) { hasPrev = false; continue; }
            const double dyn = dynCurve.ValueAt(tm.SecToTick(timeSec), 64.0);
            const float top = ly1 - 4.0f - (float)(dyn / 127.0) * (laneDynH - 10.0f);
            drawList->AddRectFilled(ImVec2(x, top), ImVec2(x + 2.0f, ly1 - 4.0f), dynFill);
            if (hasPrev) drawList->AddLine(ImVec2(x - 2.0f, prevTop), ImVec2(x, top), dynTop, 1.5f);
            prevTop = top;
            hasPrev = true;
        }
        drawList->PopClipRect();
        drawList->AddLine(ImVec2(canvasPos.x, ly0), ImVec2(rollR, ly0), keyBorder);
    }

    // --- 5b. Velocityレーン: ノートごとの値を、ノートの開始位置に棒で表示 ---
    if (g_prShowVel) {
        const float vy0 = canvasPos.y + headerH + noteAreaH + laneDynH, vy1 = vy0 + laneVelH;
        drawList->AddRectFilled(ImVec2(canvasPos.x, vy0), ImVec2(rollR, vy1), headerBg);
        drawList->AddText(ImVec2(canvasPos.x + 6.0f, vy0 + 5.0f), textCol, "Velocity");
        drawList->PushClipRect(ImVec2(rollL, vy0), ImVec2(rollR, vy1), true);
        for (size_t i = 0; i < notes.size(); i++) {
            const auto& n = notes[i];
            const double startSec = tm.TickToSec(n.tick), endSec = tm.TickToSec(n.tick + n.dur);
            const float x1 = rollL + (float)(startSec * g_prZoomX) - g_prScrollX;
            const float x2 = rollL + (float)(endSec * g_prZoomX) - g_prScrollX;
            if (x2 < rollL - 8.0f || x1 > rollR + 8.0f) continue;
            const int vel = std::max(0, std::min(127, n.velocity));
            const float bx = x1 + 5.0f;
            const float topY = vy1 - 6.0f - (float)vel / 127.0f * (laneVelH - 18.0f);
            const bool playing = (g_playTime >= startSec && g_playTime < endSec);
            const ImU32 c = playing ? noteFillPlay : noteFill;
            drawList->AddLine(ImVec2(bx, vy1 - 4.0f), ImVec2(bx, topY), c, 2.5f);
            drawList->AddCircleFilled(ImVec2(bx, topY), 4.5f, c, 12);
            if (x2 - x1 > 38.0f) {
                char vb[8];
                snprintf(vb, sizeof(vb), "%d", vel);
                drawList->AddText(ImVec2(bx + 8.0f, topY - ImGui::GetFontSize() * 0.5f), textCol, vb);
            }
        }
        drawList->PopClipRect();
        drawList->AddLine(ImVec2(canvasPos.x, vy0), ImVec2(rollR, vy0), keyBorder);
    }

    // --- 6. 再生位置 (全レーンを貫く) ---
    const float phX = rollL + (float)(g_playTime * g_prZoomX) - g_prScrollX;
    const bool phVisible = (phX >= rollL && phX <= rollR);
    const ImU32 phCol = IM_COL32(255, 210, 60, 255);
    if (phVisible) {
        drawList->PushClipRect(ImVec2(rollL, canvasPos.y + headerH), ImVec2(rollR, canvasPos.y + totalRollH), true);
        drawList->AddLine(ImVec2(phX, canvasPos.y + headerH), ImVec2(phX, canvasPos.y + totalRollH), phCol, 2.0f);
        drawList->PopClipRect();
        drawList->AddTriangleFilled(ImVec2(phX - 6.0f, canvasPos.y + 3.0f), ImVec2(phX + 6.0f, canvasPos.y + 3.0f),
            ImVec2(phX, canvasPos.y + headerH - 2.0f), phCol);
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// UIレイアウト描画 (整理されたデザイン)
// ---------------------------------------------------------------------------
// モーフ名の入力欄。モデル読み込み済みなら「...」でモデルのモーフ一覧から選べ、
// 入力した名前がモデルに存在するか (OK / 未検出) も表示する。
static void MorphField(const char* label, char* buf, size_t sz, int slot) {
    ImGui::PushID(slot);
    const float btnW = 36.0f;
    ImGui::SetNextItemWidth(std::max(60.0f, ImGui::CalcItemWidth() - btnW - ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputText("##morph_in", buf, sz);
    ImGui::SameLine();
    ImGui::BeginDisabled(!g_modelLoaded);
    if (ImGui::Button("...", ImVec2(btnW, 0))) ImGui::OpenPopup("morph_pick");
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("morph_pick")) {
        ImGui::BeginChild("morph_pick_list", ImVec2(300, 260), true);
        for (size_t i = 0; i < g_model.morphs.size(); i++) {
            const mmd::Morph& mo = g_model.morphs[i];
            if (mo.type != 0 && mo.type != 1) continue; // 口パクに使えるのは頂点/グループモーフ
            ImGui::PushID((int)i);
            const std::string lbl = mo.name.empty() ? std::string("(名前なし)") : mo.name;
            if (ImGui::Selectable(lbl.c_str())) {
                snprintf(buf, sz, "%s", mo.name.c_str());
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    if (g_modelLoaded) {
        bool ok = mmd::FindMorph(g_model, buf) >= 0;
        ImGui::SameLine();
        if (ok) ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "OK");
        else ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "%s", T("未検出"));
    }
    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// ドッキング対応ウィンドウ
//   各項目は独立したウィンドウ。タイトルバーのドラッグで移動・他のウィンドウとタブ化・
//   画面端へのドッキングができ、境界線のドラッグで大きさ(比率)を変えられる。
//   (docking ブランチの ImGui なら、本体の外へ分離して別ウィンドウにもできる)
//   配置は VyricSync_ui.ini に自動保存される。
// ---------------------------------------------------------------------------
enum { W_TRACKS, W_TIMELINE, W_PREVIEW, W_FILE, W_PARAM, W_OUTPUT, W_PRESET };
static const char* const kWinTitles[kWinCount] = {
    "トラック", "タイムライン", "プレビュー", "ファイル", "パラメータ", "出力", "プリセット"
};
// 表示名を変えても配置が壊れないよう、ImGuiのウィンドウIDは "###id" の部分だけで決める (MikuMikuWorldと同じ方式)
static const char* const kWinIds[kWinCount] = {
    "###w_tracks", "###w_timeline", "###w_preview", "###w_file", "###w_param", "###w_output", "###w_preset"
};
static std::string WinLabel(int i) { return std::string(T(kWinTitles[i])) + kWinIds[i]; }

// メニューバーの下に置く固定ツールバーの高さと、その下のドッキング領域
static float ToolbarHeight() { return ImGui::GetFrameHeight() + 8.0f; }
static void GetDockArea(ImVec2& pos, ImVec2& size) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float tb = ToolbarHeight();
    pos = ImVec2(vp->WorkPos.x, vp->WorkPos.y + tb);
    size = ImVec2(vp->WorkSize.x, std::max(1.0f, vp->WorkSize.y - tb));
}

// ===========================================================================
// 独自ドッキングのモデル (ImGuiに依存しない純粋なロジック)
//   画面は「分割ノード」と「タブグループ(葉)」の二分木で表す。
//   葉は複数のパネルをタブとして持つ。分割ノードは2つの子を左右または上下に並べる。
// ===========================================================================
enum { ED_CENTER = 0, ED_LEFT, ED_RIGHT, ED_UP, ED_DOWN };
struct EdRect { float l, t, r, b; };

static bool EdInside(const EdRect& r, float mx, float my) { return mx >= r.l && mx <= r.r && my >= r.t && my <= r.b; }

// 配置後に占める範囲のプレビュー (zone側に ratio の割合)
static EdRect EdPreview(const EdRect& n, int zone, float ratio) {
    float w = n.r - n.l, h = n.b - n.t;
    switch (zone) {
    case ED_LEFT:  return { n.l, n.t, n.l + w * ratio, n.b };
    case ED_RIGHT: return { n.r - w * ratio, n.t, n.r, n.b };
    case ED_UP:    return { n.l, n.t, n.r, n.t + h * ratio };
    case ED_DOWN:  return { n.l, n.b - h * ratio, n.r, n.b };
    default:       return n;
    }
}

// ---- 水色のドッキングガイド (ImGui標準の見た目: 中央=タブにまとめる / 上下左右=その方向に分割) ----
struct EdGuides { EdRect c, l, r, u, d; };
static EdRect EdBox(float cx, float cy, float w, float h) { return { cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f }; }

// タブグループの中央に十字に並べる。タブ欄(tabH)を除いた領域の中心に置き、小さいときは縮める
static EdGuides EdGuideRects(const EdRect& n, float tabH, float baseS) {
    float w = n.r - n.l, h = n.b - n.t - tabH;
    float s = std::max(14.0f, std::min(baseS, std::min(w, h) / 4.2f));
    float step = s * 1.3f;
    float cx = (n.l + n.r) * 0.5f, cy = n.t + tabH + h * 0.5f;
    return { EdBox(cx, cy, s, s), EdBox(cx - step, cy, s, s), EdBox(cx + step, cy, s, s), EdBox(cx, cy - step, s, s), EdBox(cx, cy + step, s, s) };
}

// ドッキング領域全体の4辺の中央に置く細長いボタン (中央ボタンは使わない)
static EdGuides EdOuterGuideRects(const EdRect& a, float baseS) {
    float w = a.r - a.l, h = a.b - a.t;
    float s = std::max(14.0f, std::min(baseS, std::min(w, h) / 5.0f));
    const float lg = s * 1.5f, sh = s * 0.74f, m = 4.0f;
    float cx = (a.l + a.r) * 0.5f, cy = (a.t + a.b) * 0.5f;
    return { { 0, 0, 0, 0 },
             EdBox(a.l + m + sh * 0.5f, cy, sh, lg), EdBox(a.r - m - sh * 0.5f, cy, sh, lg),
             EdBox(cx, a.t + m + sh * 0.5f, lg, sh), EdBox(cx, a.b - m - sh * 0.5f, lg, sh) };
}

// マウスがどのガイドの上にあるか (ED_xxx)。なければ -1
static int EdHitGuide(const EdGuides& g, float mx, float my, bool withCenter) {
    if (withCenter && EdInside(g.c, mx, my)) return ED_CENTER;
    if (EdInside(g.l, mx, my)) return ED_LEFT;
    if (EdInside(g.r, mx, my)) return ED_RIGHT;
    if (EdInside(g.u, mx, my)) return ED_UP;
    if (EdInside(g.d, mx, my)) return ED_DOWN;
    return -1;
}

// ---- 二分木 ----
struct DockNode {
    int id = 0;
    bool leaf = true;
    std::vector<int> panels;          // 葉: タブとして持つパネル番号
    int active = 0;                   // 葉: 選択中のタブ (panels の添字)
    bool horizontal = true;           // 分割: true=左右に並べる / false=上下に並べる
    float ratio = 0.5f;               // 分割: 最初の子が占める割合
    std::unique_ptr<DockNode> c[2];   // 分割: 子
    DockNode* parent = nullptr;
    EdRect r = { 0, 0, 0, 0 };        // 毎フレーム計算される配置
};
using DockPtr = std::unique_ptr<DockNode>;
static int g_dockNextId = 1;

static DockPtr DockNewLeaf(const std::vector<int>& panels) {
    DockPtr n(new DockNode());
    n->id = g_dockNextId++;
    n->leaf = true;
    n->panels = panels;
    n->active = 0;
    return n;
}
static DockPtr DockNewSplit(bool horizontal, float ratio, DockPtr a, DockPtr b) {
    DockPtr n(new DockNode());
    n->id = g_dockNextId++;
    n->leaf = false;
    n->horizontal = horizontal;
    n->ratio = ratio;
    a->parent = n.get();
    b->parent = n.get();
    n->c[0] = std::move(a);
    n->c[1] = std::move(b);
    return n;
}

static DockPtr* DockSlot(DockPtr& root, DockNode* n) {
    if (!n->parent) return &root;
    return (n->parent->c[0].get() == n) ? &n->parent->c[0] : &n->parent->c[1];
}

static DockNode* DockFindLeaf(DockNode* n, int panel) {
    if (!n) return nullptr;
    if (n->leaf) {
        for (int p : n->panels) if (p == panel) return n;
        return nullptr;
    }
    DockNode* r = DockFindLeaf(n->c[0].get(), panel);
    return r ? r : DockFindLeaf(n->c[1].get(), panel);
}
static DockNode* DockFindById(DockNode* n, int id) {
    if (!n) return nullptr;
    if (n->id == id) return n;
    if (n->leaf) return nullptr;
    DockNode* r = DockFindById(n->c[0].get(), id);
    return r ? r : DockFindById(n->c[1].get(), id);
}
static void DockCollectLeaves(DockNode* n, std::vector<DockNode*>& out) {
    if (!n) return;
    if (n->leaf) { out.push_back(n); return; }
    DockCollectLeaves(n->c[0].get(), out);
    DockCollectLeaves(n->c[1].get(), out);
}
static void DockCollectSplits(DockNode* n, std::vector<DockNode*>& out) {
    if (!n || n->leaf) return;
    out.push_back(n);
    DockCollectSplits(n->c[0].get(), out);
    DockCollectSplits(n->c[1].get(), out);
}
// いちばん広いタブグループ (配置がまだ無ければ最初の葉)
static DockNode* DockLargestLeaf(DockNode* root) {
    std::vector<DockNode*> leaves;
    DockCollectLeaves(root, leaves);
    DockNode* best = nullptr;
    float bestArea = -1.0f;
    for (DockNode* n : leaves) {
        float a = (n->r.r - n->r.l) * (n->r.b - n->r.t);
        if (a > bestArea) { bestArea = a; best = n; }
    }
    return best;
}

// パネルを木から外す。タブグループが空になったら取り除いて、兄弟ノードを親の位置に繰り上げる
static void DockRemovePanel(DockPtr& root, int panel) {
    DockNode* leaf = DockFindLeaf(root.get(), panel);
    if (!leaf) return;
    auto it = std::find(leaf->panels.begin(), leaf->panels.end(), panel);
    int k = (int)(it - leaf->panels.begin());
    leaf->panels.erase(it);
    if (!leaf->panels.empty()) {
        if (k < leaf->active) leaf->active--;
        leaf->active = std::max(0, std::min((int)leaf->panels.size() - 1, leaf->active));
        return;
    }
    DockNode* p = leaf->parent;
    if (!p) { root.reset(); return; }
    int idx = (p->c[0].get() == leaf) ? 0 : 1;
    DockPtr sibling = std::move(p->c[1 - idx]);
    DockNode* gp = p->parent;
    DockPtr* slot = DockSlot(root, p);
    sibling->parent = gp;
    *slot = std::move(sibling);   // p とその子(空の葉)はここで破棄される
}

// 末尾のタブとして追加し、選択状態にする
static void DockAddTab(DockNode* leaf, int panel) {
    leaf->panels.push_back(panel);
    leaf->active = (int)leaf->panels.size() - 1;
}

// target を分割し、zone側に panel だけの新しい葉を置く (share = 新しい側が占める割合)
static DockNode* DockSplit(DockPtr& root, DockNode* target, int zone, int panel, float share) {
    DockPtr fresh = DockNewLeaf({ panel });
    DockNode* freshRaw = fresh.get();
    if (!target) { root = std::move(fresh); return freshRaw; }
    const bool horizontal = (zone == ED_LEFT || zone == ED_RIGHT);
    const bool newFirst = (zone == ED_LEFT || zone == ED_UP);
    DockNode* gp = target->parent;
    DockPtr* slot = DockSlot(root, target);
    DockPtr old = std::move(*slot);
    DockPtr split;
    if (newFirst) split = DockNewSplit(horizontal, share, std::move(fresh), std::move(old));
    else          split = DockNewSplit(horizontal, 1.0f - share, std::move(old), std::move(fresh));
    split->parent = gp;
    *slot = std::move(split);
    return freshRaw;
}

// 配置を計算する。gap=境界線の太さ、minSz=1つの領域の最小の大きさ
static void DockLayout(DockNode* n, const EdRect& r, float gap, float minSz) {
    if (!n) return;
    n->r = r;
    if (n->leaf) return;
    float total = n->horizontal ? (r.r - r.l) : (r.b - r.t);
    float avail = std::max(1.0f, total - gap);
    float mn = std::min(minSz, avail * 0.5f);
    float a = std::max(mn, std::min(avail - mn, avail * n->ratio));
    n->ratio = a / avail;
    EdRect r0 = r, r1 = r;
    if (n->horizontal) { r0.r = r.l + a; r1.l = r0.r + gap; }
    else { r0.b = r.t + a; r1.t = r0.b + gap; }
    DockLayout(n->c[0].get(), r0, gap, minSz);
    DockLayout(n->c[1].get(), r1, gap, minSz);
}
// 2つの子の間の境界線の矩形 (DockLayout の後に有効)
static EdRect DockSplitterRect(const DockNode* n) {
    if (n->horizontal) return { n->c[0]->r.r, n->r.t, n->c[1]->r.l, n->r.b };
    return { n->r.l, n->c[0]->r.b, n->r.r, n->c[1]->r.t };
}
// 境界線をマウス位置 (mx,my) まで動かしたときの割合
static float DockRatioFromMouse(const DockNode* n, float mx, float my, float gap) {
    float total = n->horizontal ? (n->r.r - n->r.l) : (n->r.b - n->r.t);
    float pos = n->horizontal ? (mx - n->r.l) : (my - n->r.t);
    float avail = std::max(1.0f, total - gap);
    return std::max(0.02f, std::min(0.98f, (pos - gap * 0.5f) / avail));
}

// ---- 保存・読み込み用の文字列 ----
//   葉:   L[0,3]@1      (パネル0と3をタブに持ち、選択はタブ1)
//   分割: H0.2500(A,B)  (左右に並べる: 左が25%) / V0.5000(A,B) (上下に並べる)
//   空:   N
static void DockSerializeRec(const DockNode* n, std::string& o) {
    char buf[48];
    if (n->leaf) {
        o += "L[";
        for (size_t i = 0; i < n->panels.size(); i++) { if (i) o += ","; o += std::to_string(n->panels[i]); }
        o += "]@" + std::to_string(n->active);
        return;
    }
    std::snprintf(buf, sizeof(buf), "%c%.4f(", n->horizontal ? 'H' : 'V', (double)n->ratio);
    o += buf;
    DockSerializeRec(n->c[0].get(), o);
    o += ",";
    DockSerializeRec(n->c[1].get(), o);
    o += ")";
}
static std::string DockSerialize(const DockNode* root) {
    if (!root) return "N";
    std::string o;
    DockSerializeRec(root, o);
    return o;
}

struct DockParser {
    const std::string& s;
    size_t i = 0;
    int depth = 0;
    bool ok = true;
    bool used[16] = {};
    explicit DockParser(const std::string& str) : s(str) {}
    bool eat(char c) { if (i < s.size() && s[i] == c) { i++; return true; } return false; }
    bool readInt(int& v) {
        size_t st = i;
        long long x = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') { x = x * 10 + (s[i] - '0'); if (x > 100000) return false; i++; }
        if (i == st) return false;
        v = (int)x;
        return true;
    }
};
static DockPtr DockParseNode(DockParser& p) {
    if (!p.ok || p.i >= p.s.size() || p.depth >= 16) { p.ok = false; return nullptr; }
    p.depth++;
    DockPtr result;
    char c = p.s[p.i];
    if (c == 'L') {
        p.i++;
        std::vector<int> panels;
        if (!p.eat('[')) p.ok = false;
        while (p.ok) {
            int v;
            if (!p.readInt(v) || v < 0 || v >= kWinCount || p.used[v]) { p.ok = false; break; }
            p.used[v] = true;
            panels.push_back(v);
            if (p.eat(',')) continue;
            break;
        }
        if (p.ok && !p.eat(']')) p.ok = false;
        int active = 0;
        if (p.ok && (!p.eat('@') || !p.readInt(active))) p.ok = false;
        if (p.ok) {
            result = DockNewLeaf(panels);
            result->active = std::max(0, std::min((int)panels.size() - 1, active));
        }
    }
    else if (c == 'H' || c == 'V') {
        p.i++;
        const char* b = p.s.c_str() + p.i;
        char* e = nullptr;
        double d = std::strtod(b, &e);
        if (e == b || !std::isfinite(d)) { p.ok = false; }
        else p.i += (size_t)(e - b);
        float ratio = (float)std::max(0.05, std::min(0.95, d));
        DockPtr a, bb;
        if (p.ok && !p.eat('(')) p.ok = false;
        if (p.ok) a = DockParseNode(p);
        if (p.ok && !p.eat(',')) p.ok = false;
        if (p.ok) bb = DockParseNode(p);
        if (p.ok && !p.eat(')')) p.ok = false;
        if (p.ok && a && bb) result = DockNewSplit(c == 'H', ratio, std::move(a), std::move(bb));
        else p.ok = false;
    }
    else p.ok = false;
    p.depth--;
    return result;
}
// 成功したら out に木を入れて true。形式が壊れていたら false (out は変更しない)
static bool DockParse(const std::string& s, DockPtr& out) {
    if (s == "N") { out.reset(); return true; }
    DockParser p(s);
    DockPtr r = DockParseNode(p);
    if (!p.ok || !r || p.i != s.size()) return false;
    out = std::move(r);
    return true;
}

// 標準の配置: 左=トラック/ファイル・出力  中央=タイムライン  右=プレビュー/設定3つ (パネル番号は W_xxx の順)
static const char* DockDefaultText() {
    return "H0.2000(V0.3500(L[0]@0,L[3]@0),H0.6250(L[1]@0,V0.5200(L[2]@0,L[4,5,6]@0)))";
}

// ===========================================================================
// 独自ドッキング: ImGuiとの接続 (ImGuiのブランチ・バージョンに依存しない)
//   ・ドッキング中のパネルは、所属するタブグループの領域に毎フレーム配置する普通のImGuiウィンドウ
//   ・タブ欄と境界線も、専用の小さなウィンドウとして描く
//   ・浮かせたパネルは、普通のウィンドウとして動かせる。ドラッグ中は水色のガイドを出し、離すとドッキングする
// ===========================================================================
static DockPtr g_dockRoot;
static bool g_dockInit = false;
static bool g_dockFirstFrame = true;
static const float kDockGap = 6.0f;          // 境界線の太さ
static const float kDockMinSize = 90.0f;     // 1つの領域の最小の大きさ
static int g_selectPanel = -1;               // 次のフレームでタブを選択状態にするパネル
static bool g_raise[kWinCount] = {};         // 浮かせたパネルを前面に出す
static bool g_dockTargetValid = false;       // ドラッグ中、吸着先が出ているか (ウィンドウ同士の吸着を止めるため)
static DockNode* g_panelLeaf[kWinCount] = {};

struct DockTabDrag { bool active = false; int panel = -1; ImVec2 offset = ImVec2(0.0f, 0.0f); };
static DockTabDrag g_tabDrag;                // タブをつかんで引き出している最中
struct DockTarget {
    bool valid = false;
    bool outer = false;                      // 領域全体の外周か
    int leafId = 0;
    int zone = ED_CENTER;
    EdRect preview = { 0, 0, 0, 0 };
    int panel = -1;
};
static DockTarget g_dtLast;                  // ドラッグ中の直前の吸着先
static DockTarget g_dtPending;               // 離して確定した吸着先 (次のフレームの頭で反映)
static int g_dragPrev = -1;

static float DockTabH() { return ImGui::GetFrameHeight() + 4.0f; }
static float DockGuideS() { return std::max(30.0f, std::min(52.0f, ImGui::GetFontSize() * 2.6f)); }
// ドッキング領域 (ツールバーの下から、状態表示の行の上まで)
static EdRect DockArea() {
    ImVec2 p, s;
    GetDockArea(p, s);
    EdRect a;
    a.l = p.x; a.t = p.y; a.r = p.x + s.x;
    a.b = p.y + std::max(1.0f, s.y - ImGui::GetFrameHeightWithSpacing());
    return a;
}
static FloatRect MakeFloat(float x, float y, float w, float h) {
    FloatRect f;
    f.x = x; f.y = y; f.w = w; f.h = h; f.valid = true;
    return f;
}

// パネルのウィンドウを開始する。ドッキング中は所属する領域にぴったり配置し、浮いているときは普通のウィンドウにする
static bool PanelBegin(int idx, ImGuiWindowFlags flags = 0) {
    if (!g_winOpen[idx]) return false;
    const std::string label = WinLabel(idx);
    DockNode* leaf = g_isFloating[idx] ? nullptr : g_panelLeaf[idx];

    if (leaf) {   // ---- ドッキング中 ----
        if (leaf->active < 0 || leaf->active >= (int)leaf->panels.size() || leaf->panels[(size_t)leaf->active] != idx) return false;   // 選ばれていないタブ
        const float th = DockTabH();
        ImGui::SetNextWindowPos(ImVec2(leaf->r.l, leaf->r.t + th));
        ImGui::SetNextWindowSize(ImVec2(std::max(1.0f, leaf->r.r - leaf->r.l), std::max(1.0f, leaf->r.b - leaf->r.t - th)));
#ifdef IMGUI_HAS_VIEWPORT
        ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
#endif
        flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        const bool vis = ImGui::Begin(label.c_str(), nullptr, flags);
        ImGui::PopStyleVar();
        if (!vis) { ImGui::End(); return false; }
        return true;
    }

    // ---- 浮いている ----
    ImGuiIO& io = ImGui::GetIO();
    FloatRect& fr = g_floatRect[idx];
    const bool following = (g_tabDrag.active && g_tabDrag.panel == idx);
    if (g_floatApply[idx] || !fr.valid) {
        if (!fr.valid) { EdRect a = DockArea(); fr = MakeFloat(a.l + 80.0f, a.t + 60.0f, 420.0f, 360.0f); }
        bool clampIn = true;
#ifdef IMGUI_HAS_VIEWPORT
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) clampIn = false;   // 本体の外(別のOSウィンドウ)に出てよい
#endif
        if (clampIn) {   // 保存された位置が画面の外にならないようにする
            EdRect a = DockArea();
            fr.x = std::max(a.l - fr.w + 80.0f, std::min(a.r - 80.0f, fr.x));
            fr.y = std::max(a.t, std::min(a.b - 40.0f, fr.y));
        }
        ImGui::SetNextWindowSize(ImVec2(fr.w, fr.h), ImGuiCond_Always);
        if (!following) ImGui::SetNextWindowPos(ImVec2(fr.x, fr.y), ImGuiCond_Always);
        g_floatApply[idx] = false;
    }
    if (following) ImGui::SetNextWindowPos(ImVec2(io.MousePos.x - g_tabDrag.offset.x, io.MousePos.y - g_tabDrag.offset.y), ImGuiCond_Always);
    if (g_raise[idx]) { ImGui::SetNextWindowFocus(); g_raise[idx] = false; }

    flags |= ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (g_layoutLocked) flags |= ImGuiWindowFlags_NoMove;
    if (g_layoutLocked && g_layoutLockSize) flags |= ImGuiWindowFlags_NoResize;
    const bool vis = ImGui::Begin(label.c_str(), g_layoutLocked ? nullptr : &g_winOpen[idx], flags);
    {
        ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
        fr = MakeFloat(p.x, p.y, s.x, s.y);   // 保存・ドッキング解除時の大きさのために覚えておく
    }
    if (!vis) { ImGui::End(); return false; }
    return true;
}

// パネルを木から外して、浮かせる
static void DockUndock(int panel, const ImVec2& pos, const ImVec2& size) {
    DockRemovePanel(g_dockRoot, panel);
    g_isFloating[panel] = true;
    g_floatRect[panel] = MakeFloat(pos.x, pos.y, size.x, size.y);
    g_floatApply[panel] = true;
    g_raise[panel] = true;
}

static void DockResetDefault() {
    DockPtr d;
    DockParse(DockDefaultText(), d);
    g_dockRoot = std::move(d);
    for (int i = 0; i < kWinCount; i++) { g_isFloating[i] = false; g_winOpen[i] = true; g_floatApply[i] = false; }
    g_dockFirstFrame = true;
}

// 開いているパネルは、必ずどこかにある (木の中か、浮いているか)。閉じたパネルは木から外す
static void DockEnsurePlacement() {
    for (int i = 0; i < kWinCount; i++) {
        DockNode* leaf = DockFindLeaf(g_dockRoot.get(), i);
        if (!g_winOpen[i] || g_isFloating[i]) { if (leaf) DockRemovePanel(g_dockRoot, i); continue; }
        if (leaf) continue;
        if (!g_dockRoot) g_dockRoot = DockNewLeaf({ i });
        else if (DockNode* big = DockLargestLeaf(g_dockRoot.get())) DockAddTab(big, i);
        g_selectPanel = i;
    }
}

// メニューからの操作と、ドラッグで確定したドッキングを反映する (木の形が変わるので、パネルの描画より前に呼ぶ)
static void DockApplyRequests() {
    if (g_layoutLocked) { g_resetLayout = false; g_dockRequest = 0; g_dtPending = DockTarget(); return; }
    if (g_resetLayout) { DockResetDefault(); g_statusText = "ウィンドウの配置を初期状態に戻しました"; }
    g_resetLayout = false;

    if (g_dockRequest == 1) {          // 浮いているものを本体のタブグループに戻す
        int moved = 0;
        for (int i = 0; i < kWinCount; i++) {
            if (!g_isFloating[i] || !g_winOpen[i]) continue;
            g_isFloating[i] = false;
            g_floatApply[i] = false;
            if (!g_dockRoot) g_dockRoot = DockNewLeaf({ i });
            else if (DockNode* big = DockLargestLeaf(g_dockRoot.get())) DockAddTab(big, i);
            moved++;
        }
        char msg[96];
        std::snprintf(msg, sizeof(msg), moved ? "浮いていたウィンドウ %d 個を本体に戻しました" : "浮いているウィンドウはありません", moved);
        g_statusText = msg;
    }
    else if (g_dockRequest == 2) {     // 開いているものを全部、1つのタブグループにまとめる
        std::vector<int> all;
        for (int i = 0; i < kWinCount; i++) { g_isFloating[i] = false; g_floatApply[i] = false; if (g_winOpen[i]) all.push_back(i); }
        g_dockRoot = all.empty() ? DockPtr() : DockNewLeaf(all);
        g_dockFirstFrame = true;
        char msg[96];
        std::snprintf(msg, sizeof(msg), "ウィンドウ %d 個を1つのタブにまとめました", (int)all.size());
        g_statusText = msg;
    }
    g_dockRequest = 0;

    if (g_dtPending.valid) {           // ドラッグを離して確定したドッキング
        DockTarget d = g_dtPending;
        g_dtPending = DockTarget();
        const int p = d.panel;
        if (p >= 0 && p < kWinCount && g_winOpen[p] && g_isFloating[p]) {
            DockNode* leaf = d.outer ? nullptr : DockFindById(g_dockRoot.get(), d.leafId);
            if (d.outer || leaf) {
                DockRemovePanel(g_dockRoot, p);
                g_isFloating[p] = false;
                g_floatApply[p] = false;
                if (!g_dockRoot) g_dockRoot = DockNewLeaf({ p });
                else if (d.outer) DockSplit(g_dockRoot, g_dockRoot.get(), d.zone == ED_CENTER ? ED_RIGHT : d.zone, p, 0.5f);
                else if (d.zone == ED_CENTER) DockAddTab(leaf, p);
                else DockSplit(g_dockRoot, leaf, d.zone, p, 0.5f);
                g_selectPanel = p;
            }
        }
    }
}

// 各タブグループのタブ欄。タブをつかんで欄の外へ引き出すと、浮いたウィンドウになる
static void DockTabBars() {
    if (!g_dockRoot) return;
    std::vector<DockNode*> leaves;
    DockCollectLeaves(g_dockRoot.get(), leaves);
    ImGuiIO& io = ImGui::GetIO();
    const float th = DockTabH();
    struct UndockReq { int panel; ImVec2 pos; ImVec2 size; ImVec2 grab; };
    std::vector<UndockReq> undocks;
    std::vector<int> closes;

    for (DockNode* n : leaves) {
        char name[32];
        std::snprintf(name, sizeof(name), "##dtab%d", n->id);
        ImGui::SetNextWindowPos(ImVec2(n->r.l, n->r.t));
        ImGui::SetNextWindowSize(ImVec2(std::max(1.0f, n->r.r - n->r.l), th));
#ifdef IMGUI_HAS_VIEWPORT
        ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
#endif
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        const ImGuiWindowFlags f = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
        const bool vis = ImGui::Begin(name, nullptr, f);
        ImGui::PopStyleVar(3);
        if (vis && ImGui::BeginTabBar("##tb", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll)) {
            for (size_t k = 0; k < n->panels.size(); k++) {
                const int p = n->panels[k];
                bool open = true;
                ImGuiTabItemFlags tf = 0;
                if (g_selectPanel == p || (g_dockFirstFrame && (int)k == n->active)) tf |= ImGuiTabItemFlags_SetSelected;
                const bool sel = ImGui::BeginTabItem(WinLabel(p).c_str(), g_layoutLocked ? nullptr : &open, tf);
                if (!g_layoutLocked && ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 6.0f)) {
                    const float mx = io.MousePos.x, my = io.MousePos.y;
                    if (my < n->r.t - 8.0f || my > n->r.t + th + 8.0f || mx < n->r.l - 24.0f || mx > n->r.r + 24.0f) {
                        UndockReq u;
                        u.panel = p;
                        const float w = std::max(360.0f, std::min(760.0f, n->r.r - n->r.l));
                        const float h = std::max(240.0f, std::min(560.0f, n->r.b - n->r.t));
                        u.size = ImVec2(w, h);
                        u.grab = ImVec2(w * 0.25f, th * 0.5f);
                        u.pos = ImVec2(mx - u.grab.x, my - u.grab.y);
                        undocks.push_back(u);
                    }
                }
                if (sel) { n->active = (int)k; ImGui::EndTabItem(); }
                if (!open) closes.push_back(p);
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }
    g_selectPanel = -1;
    g_dockFirstFrame = false;

    for (int p : closes) g_winOpen[p] = false;
    if (!undocks.empty() && !g_tabDrag.active) {
        const UndockReq& u = undocks[0];
        DockUndock(u.panel, u.pos, u.size);
        g_tabDrag.active = true;
        g_tabDrag.panel = u.panel;
        g_tabDrag.offset = u.grab;
    }
}

// 領域どうしの境界線。ドラッグで比率(大きさ)を変えられる
static void DockSplitters() {
    if (!g_dockRoot) return;
    std::vector<DockNode*> sp;
    DockCollectSplits(g_dockRoot.get(), sp);
    const bool locked = g_layoutLocked && g_layoutLockSize;
    ImGuiIO& io = ImGui::GetIO();
    for (DockNode* n : sp) {
        const EdRect s = DockSplitterRect(n);
        const ImVec2 size(std::max(1.0f, s.r - s.l), std::max(1.0f, s.b - s.t));
        char name[32];
        std::snprintf(name, sizeof(name), "##dsplit%d", n->id);
        ImGui::SetNextWindowPos(ImVec2(s.l, s.t));
        ImGui::SetNextWindowSize(size);
#ifdef IMGUI_HAS_VIEWPORT
        ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
#endif
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
        const ImGuiWindowFlags f = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
        const bool vis = ImGui::Begin(name, nullptr, f);
        ImGui::PopStyleVar(4);
        if (vis && !locked) {
            ImGui::InvisibleButton("##sp", size);
            const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
            if (hov || act) ImGui::SetMouseCursor(n->horizontal ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
            if (act) n->ratio = DockRatioFromMouse(n, io.MousePos.x, io.MousePos.y, kDockGap);
            if (act || hov)
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(s.l, s.t), ImVec2(s.r, s.b), act ? IM_COL32(110, 175, 245, 255) : IM_COL32(110, 175, 245, 160));
        }
        ImGui::End();
    }
}

// 水色のガイドボタンを1つ描く (ImGui標準と同じ見た目: 角丸の四角、中央に分割線、分割する側の半分が明るい)
static void EdDrawGuideButton(ImDrawList* dl, const EdRect& r, int zone, bool hovered) {
    const ImU32 fill = hovered ? IM_COL32(66, 135, 222, 235) : IM_COL32(50, 104, 176, 185);
    const ImU32 lite = hovered ? IM_COL32(115, 180, 248, 240) : IM_COL32(86, 150, 226, 200);
    const ImU32 line = IM_COL32(140, 195, 248, 225);
    dl->AddRectFilled(ImVec2(r.l, r.t), ImVec2(r.r, r.b), fill, 4.0f);
    if (zone != ED_CENTER) {
        const EdRect h = EdPreview({ r.l + 1.5f, r.t + 1.5f, r.r - 1.5f, r.b - 1.5f }, zone, 0.5f);
        dl->AddRectFilled(ImVec2(h.l, h.t), ImVec2(h.r, h.b), lite, 3.0f);
        if (zone == ED_LEFT || zone == ED_RIGHT) dl->AddLine(ImVec2((r.l + r.r) * 0.5f, r.t + 2.0f), ImVec2((r.l + r.r) * 0.5f, r.b - 2.0f), line, 1.5f);
        else dl->AddLine(ImVec2(r.l + 2.0f, (r.t + r.b) * 0.5f), ImVec2(r.r - 2.0f, (r.t + r.b) * 0.5f), line, 1.5f);
    }
    dl->AddRect(ImVec2(r.l, r.t), ImVec2(r.r, r.b), line, 4.0f, 0, hovered ? 2.0f : 1.5f);
}
static void EdDrawGuideSet(ImDrawList* dl, const EdGuides& g, bool withCenter, int hovered) {
    if (withCenter) EdDrawGuideButton(dl, g.c, ED_CENTER, hovered == ED_CENTER);
    EdDrawGuideButton(dl, g.l, ED_LEFT, hovered == ED_LEFT);
    EdDrawGuideButton(dl, g.r, ED_RIGHT, hovered == ED_RIGHT);
    EdDrawGuideButton(dl, g.u, ED_UP, hovered == ED_UP);
    EdDrawGuideButton(dl, g.d, ED_DOWN, hovered == ED_DOWN);
}

// ウィンドウ(またはタブ)をドラッグしている間: ガイドとプレビューを出し、離したら吸着先を確定する
static void DockDragUpdate(const EdRect& area) {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiContext& g = *ImGui::GetCurrentContext();

    int dragPanel = -1;
    if (g_tabDrag.active) {
        if (io.MouseDown[0]) dragPanel = g_tabDrag.panel;
        else g_tabDrag.active = false;
    }
    if (dragPanel < 0 && g.MovingWindow) {   // 浮いているウィンドウのタイトルバーをドラッグしている
        ImGuiWindow* rw = g.MovingWindow->RootWindow;
        for (int i = 0; i < kWinCount; i++) {
            if (!g_isFloating[i] || !g_winOpen[i]) continue;
            ImGuiWindow* w = ImGui::FindWindowByName(kWinIds[i]);
            if (w && w == rw) { dragPanel = i; break; }
        }
    }

    g_dockTargetValid = false;
    if (dragPanel >= 0 && g_easyDock && !g_layoutLocked) {
        DockTarget t;
        t.panel = dragPanel;
        const float mx = io.MousePos.x, my = io.MousePos.y;
        const float th = DockTabH(), S = DockGuideS();

        std::vector<DockNode*> leaves;
        if (g_dockRoot) DockCollectLeaves(g_dockRoot.get(), leaves);
        DockNode* leaf = nullptr;
        for (DockNode* n : leaves) if (EdInside(n->r, mx, my)) { leaf = n; break; }

        const EdGuides og = EdOuterGuideRects(area, S);
        const EdGuides ig = leaf ? EdGuideRects(leaf->r, th, S) : EdGuides();
        const int hitO = EdHitGuide(og, mx, my, false);
        const int hitI = leaf ? EdHitGuide(ig, mx, my, true) : -1;

        if (hitO >= 0) {
            t.valid = true; t.outer = true; t.zone = hitO; t.preview = EdPreview(area, hitO, 0.5f);
        }
        else if (hitI >= 0) {
            t.valid = true; t.leafId = leaf->id; t.zone = hitI; t.preview = EdPreview(leaf->r, hitI, 0.5f);
        }
        else if (leaf && my < leaf->r.t + th) {   // 相手のタブ欄の上 → タブにまとめる
            t.valid = true; t.leafId = leaf->id; t.zone = ED_CENTER; t.preview = leaf->r;
        }
        else if (!g_dockRoot && EdInside(area, mx, my)) {   // 何も配置されていないときは、領域のどこでも新しく配置できる
            t.valid = true; t.outer = true; t.zone = ED_CENTER; t.preview = area;
        }

        g_dragPrev = dragPanel;
        g_dtLast = t;
        g_dockTargetValid = t.valid;

#ifdef IMGUI_HAS_VIEWPORT
        ImDrawList* dl = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
#else
        ImDrawList* dl = ImGui::GetForegroundDrawList();
#endif
        if (t.valid) {   // 配置後に占める範囲 (青い半透明)
            dl->AddRectFilled(ImVec2(t.preview.l, t.preview.t), ImVec2(t.preview.r, t.preview.b), IM_COL32(50, 100, 175, 120));
            dl->AddRect(ImVec2(t.preview.l, t.preview.t), ImVec2(t.preview.r, t.preview.b), IM_COL32(110, 170, 240, 215), 0.0f, 0, 2.0f);
        }
        if (g_dockRoot) EdDrawGuideSet(dl, og, false, hitO);
        if (leaf) EdDrawGuideSet(dl, ig, true, hitI);
        return;
    }

    if (g_dragPrev >= 0) {   // ドラッグが終わった: 直前の吸着先があれば確定 (次のフレームの頭で反映)
        if (g_dtLast.valid && !io.MouseDown[0] && !g_layoutLocked) g_dtPending = g_dtLast;
        g_dragPrev = -1;
        g_dtLast = DockTarget();
    }
}

// 毎フレームのドッキング処理。パネルの描画より前に呼ぶ
static void DockFrame() {
    if (!g_dockInit) {
        g_dockInit = true;
        DockPtr loaded;
        if (g_dockLayoutStr.empty() || !DockParse(g_dockLayoutStr, loaded)) DockParse(DockDefaultText(), loaded);   // 保存が無い・壊れている → 標準の配置
        g_dockRoot = std::move(loaded);
        for (int i = 0; i < kWinCount; i++) if (g_isFloating[i]) { DockRemovePanel(g_dockRoot, i); g_raise[i] = true; }
        g_dockFirstFrame = true;
    }
    DockApplyRequests();
    DockEnsurePlacement();

    const EdRect area = DockArea();
    if (g_dockRoot) DockLayout(g_dockRoot.get(), area, kDockGap, kDockMinSize);
    DockTabBars();
    DockEnsurePlacement();
    if (g_dockRoot) DockLayout(g_dockRoot.get(), area, kDockGap, kDockMinSize);
    for (int i = 0; i < kWinCount; i++) g_panelLeaf[i] = DockFindLeaf(g_dockRoot.get(), i);
    DockSplitters();
    DockDragUpdate(area);

    g_dockLayoutStr = DockSerialize(g_dockRoot.get());   // 設定ファイルに保存する内容

    // 状態表示 (ドッキング領域の下の1行)
    ImGui::GetBackgroundDrawList()->AddText(ImVec2(area.l + 10.0f, area.b + 3.0f), ImGui::GetColorU32(ImGuiCol_Text), g_statusText.c_str());
}

// ---------------------------------------------------------------------------
// プロジェクトファイル (.v2v)
//   生成設定などのメタデータ(JSON)と、生成したVMD(zlib圧縮)を1つのファイルにまとめる。
//   形式の詳細は projectfile.h の先頭を参照。
// ---------------------------------------------------------------------------
static v2v::ProjectState CaptureProjectState() {
    v2v::ProjectState s;
    s.sourcePath = WideToUtf8(g_inputPath);
    size_t sl = s.sourcePath.find_last_of("\\/");
    s.sourceName = (sl == std::string::npos) ? s.sourcePath : s.sourcePath.substr(sl + 1);
    s.track = g_selectedTrack;
    s.trackCount = g_loaded ? (int)g_song.tracks.size() : 0;
    s.noteCount = (g_loaded && g_selectedTrack >= 0 && g_selectedTrack < (int)g_song.tracks.size()) ? (int)g_song.tracks[(size_t)g_selectedTrack].size() : 0;
    s.vmdModelName = g_modelName[0] ? g_modelName : "LipSync";
    s.mmdName = g_modelLoaded ? g_model.name : std::string();
    s.mmdPath = g_modelLoaded ? WideToUtf8(g_modelPath) : std::string();
    s.morphA = g_morphA; s.morphI = g_morphI; s.morphU = g_morphU; s.morphE = g_morphE; s.morphO = g_morphO; s.morphN = g_morphN;
    s.p = g_p;
    s.reducePct = g_reducePct; s.pitchFxPct = g_pitchFxPct; s.dynFxPct = g_dynFxPct; s.smoothMacro = g_smoothMacro;
    return s;
}

static void ApplyProjectState(const v2v::ProjectState& s) {
    snprintf(g_modelName, sizeof(g_modelName), "%s", s.vmdModelName.c_str());
    snprintf(g_morphA, sizeof(g_morphA), "%s", s.morphA.c_str());
    snprintf(g_morphI, sizeof(g_morphI), "%s", s.morphI.c_str());
    snprintf(g_morphU, sizeof(g_morphU), "%s", s.morphU.c_str());
    snprintf(g_morphE, sizeof(g_morphE), "%s", s.morphE.c_str());
    snprintf(g_morphO, sizeof(g_morphO), "%s", s.morphO.c_str());
    snprintf(g_morphN, sizeof(g_morphN), "%s", s.morphN.c_str());
    g_p.prepMs = s.p.prepMs; g_p.snapMs = s.p.snapMs; g_p.releaseMs = s.p.releaseMs; g_p.smoothCloseMs = s.p.smoothCloseMs;
    g_p.geminateExtraMs = s.p.geminateExtraMs; g_p.gapMs = s.p.gapMs; g_p.maxWeight = s.p.maxWeight; g_p.consScale = s.p.consScale;
    g_p.humanize = s.p.humanize; g_p.fps = s.p.fps; g_p.legato = s.p.legato; g_p.smoothMs = s.p.smoothMs; g_p.consFloor = s.p.consFloor;
    g_reducePct = (float)s.reducePct; g_pitchFxPct = (float)s.pitchFxPct; g_dynFxPct = (float)s.dynFxPct; g_smoothMacro = (float)s.smoothMacro;
}

static void SaveProject() {
    if (!g_loaded || g_selectedTrack < 0 || g_selectedTrack >= (int)g_song.tracks.size()) { g_statusText = "先にvsqx/vsqファイルを開いてください"; return; }
    PrepareDerivedParams();
    vsq::MorphNames names;
    names.a = g_morphA; names.i = g_morphI; names.u = g_morphU; names.e = g_morphE; names.o = g_morphO; names.nasal = g_morphN;
    const std::string modelName = g_modelName[0] ? g_modelName : "LipSync";
    try {
        vsq::TempoMap tempoMap(g_song.resolution, g_song.tempos);
        const vsq::Curve* effP = nullptr; const vsq::Curve* effD = nullptr;
        GetEffectiveCurves(g_selectedTrack, effP, effD);
        auto tracks = vsq::BuildMorphTracks(g_song.tracks[(size_t)g_selectedTrack], tempoMap, g_p, *effP, *effD);
        const std::vector<uint8_t> vmd = vsq::BuildVmdBytes(tracks, names, modelName);
        const std::vector<uint8_t> file = v2v::PackProject(CaptureProjectState(), vmd, 9);

        wchar_t outBuf[MAX_PATH] = L"";
        std::wstring def = g_baseName + L".v2v";
        size_t n = def.size() < (MAX_PATH - 1) ? def.size() : (MAX_PATH - 1);
        wcsncpy(outBuf, def.c_str(), n); outBuf[n] = L'\0';
        OPENFILENAMEW sfn = {};
        sfn.lStructSize = sizeof(sfn);
        sfn.hwndOwner = g_hwnd;
        sfn.lpstrFilter = L"プロジェクトファイル (*.v2v)\0*.v2v\0";
        sfn.lpstrFile = outBuf;
        sfn.nMaxFile = MAX_PATH;
        sfn.lpstrDefExt = L"v2v";
        sfn.Flags = OFN_OVERWRITEPROMPT;
        sfn.lpstrTitle = L"プロジェクトの保存先";
        if (!GetSaveFileNameW(&sfn)) return;
        if (!WriteTextFileAtomic(outBuf, std::string((const char*)file.data(), file.size()))) throw std::runtime_error("ファイルを書き込めません");

        g_packedVmd = vmd;   // 保存した内容を、そのまま書き出しにも使えるようにしておく
        char buf[256];
        snprintf(buf, sizeof(buf), "プロジェクトを保存しました: %zu バイト (VMD %zu バイトをzlib圧縮)", file.size(), vmd.size());
        g_statusText = buf;
    }
    catch (std::exception& e) {
        g_statusText = std::string("プロジェクトの保存エラー: ") + e.what();
    }
}

static void OpenProjectFromPath(const std::wstring& fileBuf) {
    try {
        const std::string data = ReadFileBytes(fileBuf);
        v2v::Json meta;
        std::vector<uint8_t> vmd;
        std::string err;
        if (!v2v::UnpackProject((const uint8_t*)data.data(), data.size(), meta, vmd, err)) throw std::runtime_error(err);
        const v2v::ProjectState st = v2v::StateFromJson(meta);
        ApplyProjectState(st);

        // 保存してあったVMDを、口パクのキーフレームに戻す (元のvsqxが無くても、プレビューと書き出しができる)
        g_packedTracks.clear();
        vsq::VmdData vd;
        std::string perr;
        if (vsq::ParseVmd(vmd.data(), vmd.size(), vd, perr)) {
            vsq::MorphNames mn;
            mn.a = st.morphA; mn.i = st.morphI; mn.u = st.morphU; mn.e = st.morphE; mn.o = st.morphO; mn.nasal = st.morphN;
            g_packedTracks = vsq::VmdToTracks(vd, mn);
        }
        g_packedVmd = vmd;

        auto exists = [](const std::wstring& p) {
            DWORD a = GetFileAttributesW(p.c_str());
            return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
            };
        // 元のvsqx/vsq (見つかれば読み込み、同じトラックを選ぶ)
        const std::wstring srcW = Utf8ToWide(st.sourcePath);
        if (!st.sourcePath.empty() && exists(srcW)) {
            LoadSongFromPath(srcW);
            if (g_loaded && st.track >= 0 && st.track < (int)g_song.tracks.size()) g_selectedTrack = st.track;
        }
        else {
            g_loaded = false;   // 別の曲が開いていても、このプロジェクトの内容を表示する
            g_playing = false;
            g_playTime = 0.0;
            g_songSerial++;
            std::wstring nm = Utf8ToWide(st.sourceName);
            size_t dot = nm.find_last_of(L'.');
            g_baseName = nm.empty() ? std::wstring(L"project") : nm.substr(0, dot == std::wstring::npos ? nm.size() : dot);
        }
        g_packedActive = !g_loaded;
        // MMDモデル
        const std::wstring mdlW = Utf8ToWide(st.mmdPath);
        bool mdlOk = false;
        if (!st.mmdPath.empty() && exists(mdlW)) { LoadModelFromPath(mdlW); mdlOk = g_modelLoaded; }

        std::string msg = "プロジェクトを開きました: " + st.sourceName;
        msg += g_loaded ? " (元のファイルを読み込みました)" : " (元のファイルが見つからないため、保存してあったVMDを表示します)";
        if (!st.mmdPath.empty() && !mdlOk) msg += " / MMDモデルが見つかりません: " + st.mmdPath;
        g_statusText = msg;
        AddRecent(fileBuf, st.sourceName);
        g_showStart = false;
    }
    catch (std::exception& e) {
        g_statusText = std::string("プロジェクトを開けません: ") + e.what();
    }
}

static void OpenProject() {
    wchar_t fileBuf[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"プロジェクトファイル (*.v2v)\0*.v2v\0すべてのファイル\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"プロジェクトを開く";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    OpenProjectFromPath(fileBuf);
}

static void ExportPackedVmd() {
    if (g_packedVmd.empty()) { g_statusText = "書き出せるVMDがありません (プロジェクトを開くか、保存してください)"; return; }
    wchar_t outBuf[MAX_PATH] = L"";
    std::wstring def = g_baseName + L"_lipsync.vmd";
    size_t n = def.size() < (MAX_PATH - 1) ? def.size() : (MAX_PATH - 1);
    wcsncpy(outBuf, def.c_str(), n); outBuf[n] = L'\0';
    OPENFILENAMEW sfn = {};
    sfn.lStructSize = sizeof(sfn);
    sfn.hwndOwner = g_hwnd;
    sfn.lpstrFilter = L"VMDモーションファイル (*.vmd)\0*.vmd\0";
    sfn.lpstrFile = outBuf;
    sfn.nMaxFile = MAX_PATH;
    sfn.lpstrDefExt = L"vmd";
    sfn.Flags = OFN_OVERWRITEPROMPT;
    sfn.lpstrTitle = L"VMDの保存先";
    if (!GetSaveFileNameW(&sfn)) return;
    if (!WriteTextFileAtomic(outBuf, std::string((const char*)g_packedVmd.data(), g_packedVmd.size()))) { g_statusText = "VMDを書き込めません"; return; }
    char buf[128];
    snprintf(buf, sizeof(buf), "プロジェクト内のVMDを書き出しました (%zu バイト)", g_packedVmd.size());
    g_statusText = buf;
}

static void WindowTracks() {
    if (!PanelBegin(W_TRACKS)) return;
    if (g_loaded) {
        for (size_t i = 0; i < g_song.tracks.size(); i++) {
            std::string preview;
            int shown = 0;
            for (auto& n : g_song.tracks[i]) { if (shown >= 12) break; if (!n.lyric.empty()) { preview += n.lyric; shown++; } }
            char label[256];
            snprintf(label, sizeof(label), " Track #%d (%zu音) : %s", (int)i + 1, g_song.tracks[i].size(), preview.c_str());
            if (ImGui::Selectable(label, (int)i == g_selectedTrack)) g_selectedTrack = (int)i;
        }
    }
    else {
        ImGui::TextDisabled(" -");
    }
    ImGui::End();
}

// ピアノロール + 再生バー (ウィンドウ内・全画面の両方で使う)
static void TimelineContent(bool fullscreen) {
    if (IconButton(fullscreen ? ICN_WINDOW : ICN_FULL, fullscreen ? T("元に戻す") : T("全画面"))) g_prFullScreen = !g_prFullScreen;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::SliderFloat(T("鍵盤の高さ"), &g_prRowHeight, 10.0f, 40.0f, "%.0fpx");

    const float barH = PlaybackBarHeight();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    RenderPianoRollTimeline(avail.x, std::max(80.0f, avail.y - barH - 4.0f));
    ImGui::Dummy(ImVec2(0, 2));
    RenderPlaybackBar();
}

static void WindowTimeline() {
    static bool s_fsPrev = false;
    if (g_prFullScreen) {
        // 全画面: 本体ウィンドウ全体を覆う専用ウィンドウに描く (ドッキング中の配置には触れない)
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImVec2 ap, as;
        GetDockArea(ap, as);
        ImGui::SetNextWindowPos(ap);
        ImGui::SetNextWindowSize(as);
#ifdef IMGUI_HAS_VIEWPORT
        ImGui::SetNextWindowViewport(vp->ID);
#endif
        (void)vp;
        if (!s_fsPrev) ImGui::SetNextWindowFocus();
        ImGuiWindowFlags f = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::Begin("##timeline_fullscreen", nullptr, f);
        TimelineContent(true);
        ImGui::End();
        s_fsPrev = true;
        if (PanelBegin(W_TIMELINE)) {
            ImGui::End();
        }
        return;
    }
    s_fsPrev = false;
    if (!PanelBegin(W_TIMELINE, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) return;
    TimelineContent(false);
    ImGui::End();
}

static void WindowPreview() {
    if (!PanelBegin(W_PREVIEW, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) return;
    if (ImGui::SmallButton(T(" 顔アップ "))) { g_camCloseup = true; ResetCamera(true); }
    ImGui::SameLine();
    if (ImGui::SmallButton(T(" 全身 "))) { g_camCloseup = false; ResetCamera(false); }
    RenderPreviewPanel(0.0f, 0.0f);   // 残り領域いっぱいに表示
    ImGui::End();
}

static void WindowFile() {
    if (!PanelBegin(W_FILE)) return;
    const ImVec4 okCol(0.4f, 0.8f, 0.4f, 1.0f), ngCol(0.9f, 0.4f, 0.3f, 1.0f);
    auto fileName = [](const std::wstring& p) { size_t sl = p.find_last_of(L"\\/"); return WideToUtf8(sl == std::wstring::npos ? p : p.substr(sl + 1)); };

    if (IconButton(ICN_MUSIC, T("VSQX / VSQ を開く"), ImVec2(-1, 34))) OpenFile();
    if (!g_inputPath.empty()) ImGui::TextColored(okCol, "%s", fileName(g_inputPath).c_str());

    ImGui::Dummy(ImVec2(0, 6));
    if (IconButton(ICN_MODEL, T("MMDモデルを開く"), ImVec2(-1, 34))) OpenModelDialog();
    if (g_modelLoaded) {
        ImGui::TextColored(okCol, "%s", g_model.name.c_str());
        for (int k = 0; k < 6; k++) {
            if (k) ImGui::SameLine();
            const bool ok = SlotMorphIndex(k) >= 0;
            ImGui::TextColored(ok ? okCol : ngCol, "%s", kSlotLabels[k]);
        }
    }

    ImGui::Dummy(ImVec2(0, 6));
    if (IconButton(ICN_WAVE, T("WAVを開く"), ImVec2(-1, 34))) OpenWavDialog();
    if (g_wavLoaded) {
        std::wstring wn = g_wavPath;
        const size_t sl = wn.find_last_of(L"\\/");
        if (sl != std::wstring::npos) wn = wn.substr(sl + 1);
        ImGui::TextColored(okCol, "%s", WideToUtf8(wn).c_str());
    }

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::BeginDisabled(!g_loaded);
    if (IconButton(ICN_EXPORT, T("VMDを生成"), ImVec2(-1, 34))) GenerateAndSave();
    ImGui::EndDisabled();

    ImGui::Dummy(ImVec2(0, 6));
    if (IconButton(ICN_FOLDER, T("プロジェクトを開く"), ImVec2(-1, 30))) OpenProject();
    ImGui::BeginDisabled(!g_loaded);
    if (IconButton(ICN_SAVE, T("プロジェクトを保存"), ImVec2(-1, 30))) SaveProject();
    ImGui::EndDisabled();
    ImGui::BeginDisabled(g_packedVmd.empty());
    if (IconButton(ICN_EXPORT, T("保存済みVMDを書き出す"), ImVec2(-1, 30))) ExportPackedVmd();
    ImGui::EndDisabled();
    ImGui::End();
}

// 「滑らかさ(まとめて調整)」: 関連する個別の設定をまとめて書き換える (0%=標準, 100%=最もなめらか)
static void ApplySmoothMacro() {
    const double s = std::min(1.0, std::max(0.0, (double)g_smoothMacro / 100.0));
    vsq::ApplySmoothness(g_p, s);
    g_pitchFxPct = (float)(100.0 - 40.0 * s);   // ピッチ(ビブラート等)による口の揺れも控えめにする
    g_dynFxPct = (float)(100.0 - 30.0 * s);
}

static void WindowParam() {
    if (!PanelBegin(W_PARAM)) return;
    ImGui::Dummy(ImVec2(0, 6));

    auto sliderD = [](const char* label, double* target, float lo, float hi, const char* fmt = "%.0f") {
        float v = (float)*target;
        if (ImGui::SliderFloat(label, &v, lo, hi, fmt)) *target = (double)v;
        };

    if (ImGui::CollapsingHeader(T("口の滑らかさ"), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SliderFloat(T("滑らかさ (まとめて調整)"), &g_smoothMacro, 0.0f, 100.0f, "%.0f%%")) ApplySmoothMacro();
        if (ImGui::Button(T(" 標準 "))) { g_smoothMacro = 0.0f; ApplySmoothMacro(); }
        ImGui::SameLine();
        if (ImGui::Button(T(" なめらか "))) { g_smoothMacro = 50.0f; ApplySmoothMacro(); }
        ImGui::SameLine();
        if (ImGui::Button(T(" とてもなめらか "))) { g_smoothMacro = 100.0f; ApplySmoothMacro(); }
        ImGui::Dummy(ImVec2(0, 4));
        {
            float lg = (float)(g_p.legato * 100.0);
            if (ImGui::SliderFloat(T("音の間で口を閉じ切らない割合"), &lg, 0.0f, 100.0f, "%.0f%%")) g_p.legato = (double)lg / 100.0;
            float cf = (float)(g_p.consFloor * 100.0);
            if (ImGui::SliderFloat(T("子音のときも口を残す割合"), &cf, 0.0f, 100.0f, "%.0f%%")) g_p.consFloor = (double)cf / 100.0;
        }
        sliderD(T("動きを均す幅 (ms)"), &g_p.smoothMs, 0, 200);
    }

    if (ImGui::CollapsingHeader(T("発音タイミング"), ImGuiTreeNodeFlags_DefaultOpen)) {
        sliderD(T("子音準備の長さ (ms)"), &g_p.prepMs, 10, 150);
        sliderD(T("母音が開く速さ (ms)"), &g_p.snapMs, 15, 200);
        sliderD(T("母音から閉じるまで (ms)"), &g_p.releaseMs, 10, 200);
        sliderD(T("滑らかに閉じる時間 (ms)"), &g_p.smoothCloseMs, 60, 600);
        sliderD(T("促音「っ」保持時間 (ms)"), &g_p.geminateExtraMs, 0, 200);
        sliderD(T("無音で口を閉じる目安 (ms)"), &g_p.gapMs, 30, 300);
    }

    if (ImGui::CollapsingHeader(T("口の開き具合"), ImGuiTreeNodeFlags_DefaultOpen)) {
        sliderD(T("母音基準ウェイト"), &g_p.maxWeight, 0.3f, 1.0f, "%.2f");
        sliderD(T("子音の強さ倍率"), &g_p.consScale, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat(T("キーフレーム削減率 (%)"), &g_reducePct, 0, 100, "%.0f%%");
    }

    if (ImGui::CollapsingHeader(T("ピッチ / ダイナミクス連動"))) {
        ImGui::SliderFloat(T("ピッチによる口形変化 (%)"), &g_pitchFxPct, 0, 200, "%.0f%%");
        ImGui::SliderFloat(T("ダイナミクス連動変化 (%)"), &g_dynFxPct, 0, 200, "%.0f%%");
    }

    if (ImGui::CollapsingHeader(T("ボーカルWAV (F0 / ダイナミクス)"), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (IconButton(ICN_MUSIC, T("ボーカルのみのWAVを開く"), ImVec2(-1, 30))) OpenVocalDialog();
        if (g_vocalLoaded) {
            std::wstring vn = g_vocalPath;
            const size_t sl = vn.find_last_of(L"\\/");
            if (sl != std::wstring::npos) vn = vn.substr(sl + 1);
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "%s", WideToUtf8(vn).c_str());
            ImGui::SliderFloat(T("ピッチへの反映 (%)"), &g_wavPitchMix, 0.0f, 100.0f, "%.0f%%");
            ImGui::SliderFloat(T("ダイナミクスへの反映 (%)"), &g_wavDynMix, 0.0f, 100.0f, "%.0f%%");
            ImGui::InputInt(T("ボーカルの開始フレーム"), &g_vocalStartFrame);
            ImGui::Checkbox(T("検出したF0を重ねて表示"), &g_prShowF0);
            if (ImGui::Button(T("ボーカルWAVを外す"), ImVec2(-1, 28))) UnloadVocal();
        }
    }

    if (ImGui::CollapsingHeader(T("ゆらぎ"))) {
        sliderD(T("ゆらぎ付加 (%)"), &g_p.humanize, 0.0f, 1.0f, "%.2f");
    }
    ImGui::End();
}

static void WindowOutput() {
    if (!PanelBegin(W_OUTPUT)) return;
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextUnformatted(T("モデル名"));
    ImGui::InputText("##model", g_modelName, sizeof(g_modelName));
    if (g_modelLoaded && ImGui::Button(T("読み込み中のモデル名を使う"))) snprintf(g_modelName, sizeof(g_modelName), "%s", g_model.name.c_str());

    ImGui::Dummy(ImVec2(0, 6));
    float fpsF = (float)g_p.fps;
    if (ImGui::InputFloat(T("フレームレート (fps)"), &fpsF, 1.0f, 5.0f, "%.0f")) g_p.fps = fpsF > 0 ? fpsF : 30.0;

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    ImGui::TextUnformatted(T("口パク用モーフ"));
    MorphField(T("あ (A)"), g_morphA, sizeof(g_morphA), 0);
    MorphField(T("い (I)"), g_morphI, sizeof(g_morphI), 1);
    MorphField(T("う (U)"), g_morphU, sizeof(g_morphU), 2);
    MorphField(T("え (E)"), g_morphE, sizeof(g_morphE), 3);
    MorphField(T("お (O)"), g_morphO, sizeof(g_morphO), 4);
    MorphField(T("ん (N)"), g_morphN, sizeof(g_morphN), 5);
    ImGui::End();
}

static void WindowPreset() {
    if (!PanelBegin(W_PRESET)) return;
    ImGui::Dummy(ImVec2(0, 4));
    if (IconButton(ICN_SAVE, T("プリセットを保存"), ImVec2(-1, 32))) SavePresetFileDialog();
    if (IconButton(ICN_FOLDER, T("プリセットを読み込む"), ImVec2(-1, 32))) OpenPresetFileDialog();
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 8));
    if (IconButton(ICN_GEAR, T("設定を開く"), ImVec2(-1, 32))) g_openSettings = true;
    ImGui::End();
}

// ---------------------------------------------------------------------------
// ウィンドウの吸着
//   フローティングのウィンドウをドラッグで動かしているとき、本体の端や他のウィンドウの辺の
//   近くに来ると、辺が自動でぴたっとそろう。Altキーを押している間は吸着しない。
// ---------------------------------------------------------------------------
struct SnapRect { float l, t, r, b; };
struct SnapResult {
    bool hasX = false, hasY = false;
    float dx = 0.0f, dy = 0.0f;
    float gx = 0.0f, gxy0 = 0.0f, gxy1 = 0.0f;   // 縦の案内線: x座標と、縦方向の範囲
    float gy = 0.0f, gyx0 = 0.0f, gyx1 = 0.0f;   // 横の案内線: y座標と、横方向の範囲
};

// me: 動かしているウィンドウの矩形  area: 作業領域(外枠)  others: 吸着先になる他のウィンドウ
// th: 吸着する距離(px)。辺と辺の距離が th 以内なら、いちばん近い辺に合わせるための移動量(dx,dy)を返す。
static SnapResult ComputeSnap(const SnapRect& me, const SnapRect& area, const std::vector<SnapRect>& others, float th) {
    SnapResult res;
    float bx = th + 1.0f, by = th + 1.0f;
    auto offerX = [&](float target, float mine, const SnapRect& o) {
        float d = target - mine;
        if (std::fabs(d) <= th && std::fabs(d) < std::fabs(bx)) {
            bx = d; res.hasX = true; res.dx = d; res.gx = target;
            res.gxy0 = std::min(me.t, o.t); res.gxy1 = std::max(me.b, o.b);
        }
        };
    auto offerY = [&](float target, float mine, const SnapRect& o) {
        float d = target - mine;
        if (std::fabs(d) <= th && std::fabs(d) < std::fabs(by)) {
            by = d; res.hasY = true; res.dy = d; res.gy = target;
            res.gyx0 = std::min(me.l, o.l); res.gyx1 = std::max(me.r, o.r);
        }
        };
    // 外枠 (作業領域) の4辺
    offerX(area.l, me.l, area); offerX(area.r, me.r, area);
    offerY(area.t, me.t, area); offerY(area.b, me.b, area);
    // 他のウィンドウ: 隣に並べる / 辺をそろえる。離れた位置の窓には吸着しない
    for (const SnapRect& o : others) {
        if (me.b + th >= o.t && me.t - th <= o.b) {
            offerX(o.r, me.l, o); offerX(o.l, me.r, o);
            offerX(o.l, me.l, o); offerX(o.r, me.r, o);
        }
        if (me.r + th >= o.l && me.l - th <= o.r) {
            offerY(o.b, me.t, o); offerY(o.t, me.b, o);
            offerY(o.t, me.t, o); offerY(o.b, me.b, o);
        }
    }
    return res;
}

static void SnapMovingWindow() {
    if (g_snapPx < 0.5f || ImGui::GetIO().KeyAlt) return;
    if (g_dockTargetValid) return;   // ドッキングの吸着先が出ている間は、ウィンドウ同士の吸着はしない
    ImGuiContext& g = *ImGui::GetCurrentContext();
    if (!g.MovingWindow) return;
    ImGuiWindow* mw = g.MovingWindow->RootWindow;
    if (!mw || (mw->Flags & ImGuiWindowFlags_NoMove) || mw->Size.x < 8.0f || mw->Size.y < 8.0f) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
#ifdef IMGUI_HAS_VIEWPORT
    if ((ImGuiViewport*)mw->Viewport != vp) return;   // 本体の外(別OSウィンドウ)へ出たものは対象外
#endif

    std::vector<SnapRect> others;
    for (ImGuiWindow* w : g.Windows) {
        if (w == mw || !w->WasActive || w->Hidden || w->ParentWindow) continue;           // 子ウィンドウ・ポップアップ等は除く
        if (w->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoInputs)) continue;
        if ((w->Flags & ImGuiWindowFlags_NoMove) && (w->Flags & ImGuiWindowFlags_NoTitleBar)) continue;   // 土台・メニューバー・全画面表示
#ifdef IMGUI_HAS_VIEWPORT
        if ((ImGuiViewport*)w->Viewport != vp) continue;
#endif
        if (w->Size.x < 8.0f || w->Size.y < 8.0f) continue;
        others.push_back({ w->Pos.x, w->Pos.y, w->Pos.x + w->Size.x, w->Pos.y + w->Size.y });
    }
    ImVec2 areaPos, areaSize;
    GetDockArea(areaPos, areaSize);
    SnapRect area = { areaPos.x, areaPos.y, areaPos.x + areaSize.x, areaPos.y + areaSize.y };
    SnapRect me = { mw->Pos.x, mw->Pos.y, mw->Pos.x + mw->Size.x, mw->Pos.y + mw->Size.y };
    SnapResult r = ComputeSnap(me, area, others, g_snapPx);
    if (!r.hasX && !r.hasY) return;

    // 案内線 (吸着した辺に引く)
#ifdef IMGUI_HAS_VIEWPORT
    ImDrawList* dl = ImGui::GetForegroundDrawList(vp);
#else
    ImDrawList* dl = ImGui::GetForegroundDrawList();
#endif
    const ImU32 col = IM_COL32(90, 170, 255, 230);
    if (r.hasX) dl->AddLine(ImVec2(r.gx, r.gxy0 + (r.hasY ? r.dy : 0.0f)), ImVec2(r.gx, r.gxy1 + (r.hasY ? r.dy : 0.0f)), col, 2.0f);
    if (r.hasY) dl->AddLine(ImVec2(r.gyx0 + (r.hasX ? r.dx : 0.0f), r.gy), ImVec2(r.gyx1 + (r.hasX ? r.dx : 0.0f), r.gy), col, 2.0f);

    ImGui::SetWindowPos(mw, ImVec2(mw->Pos.x + (r.hasX ? r.dx : 0.0f), mw->Pos.y + (r.hasY ? r.dy : 0.0f)), ImGuiCond_Always);
}

static void RenderMainMenu() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu(T("ファイル"))) {
        if (IconMenuItem(ICN_HOME, T("スタート画面"))) g_showStart = true;
        ImGui::Separator();
        if (IconMenuItem(ICN_MUSIC, T("VSQX / VSQ を開く..."))) OpenFile();
        if (IconMenuItem(ICN_MODEL, T("MMDモデルを開く..."))) OpenModelDialog();
        if (IconMenuItem(ICN_WAVE, T("WAVを開く..."))) OpenWavDialog();
        if (IconMenuItem(ICN_MUSIC, T("ボーカルのみのWAVを開く..."))) OpenVocalDialog();
        if (IconMenuItem(ICN_EXPORT, T("VMDを生成..."), nullptr, false, g_loaded)) GenerateAndSave();
        ImGui::Separator();
        if (IconMenuItem(ICN_FOLDER, T("プロジェクトを開く..."))) OpenProject();
        if (IconMenuItem(ICN_SAVE, T("プロジェクトを保存..."), "Ctrl+S", false, g_loaded)) SaveProject();
        if (IconMenuItem(ICN_EXPORT, T("保存済みVMDを書き出す..."), nullptr, false, !g_packedVmd.empty())) ExportPackedVmd();
        ImGui::Separator();
        if (IconMenuItem(ICN_GEAR, T("設定..."), "Ctrl+,")) g_openSettings = true;
        if (ImGui::MenuItem(T("終了"))) PostQuitMessage(0);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(T("表示"))) {
        for (int i = 0; i < kWinCount; i++) ImGui::MenuItem(T(kWinTitles[i]), nullptr, &g_winOpen[i]);
        ImGui::Separator();
        if (ImGui::MenuItem(T("配置を初期状態に戻す"), nullptr, false, !g_layoutLocked)) g_resetLayout = true;
        ImGui::MenuItem(T("配置を固定"), nullptr, &g_layoutLocked);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

// メニューバーの下に固定されるツールバー (MikuMikuWorldの ##app_toolbar と同じ: ドッキング・移動・サイズ変更不可)
static void RenderToolbar() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, ToolbarHeight()));
#ifdef IMGUI_HAS_VIEWPORT
    ImGui::SetNextWindowViewport(vp->ID);
#endif
    ImGuiWindowFlags f = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##app_toolbar", nullptr, f);
    ImGui::PopStyleVar(3);

    auto sep = []() { ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine(); };

    const ImVec4 accentC = ImGui::GetStyleColorVec4(ImGuiCol_TabActive);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));    // 平らなボタン (触れたときだけ色が付く)
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accentC.x, accentC.y, accentC.z, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(accentC.x, accentC.y, accentC.z, 0.85f));

    if (IconButton(ICN_HOME, T("ホーム"))) g_showStart = true;
    sep();
    if (IconButton(ICN_MUSIC, T("曲"))) OpenFile();
    ImGui::SameLine();
    if (IconButton(ICN_MODEL, T("モデル"))) OpenModelDialog();
    ImGui::SameLine();
    if (IconButton(ICN_WAVE, "WAV")) OpenWavDialog();
    ImGui::SameLine();
    if (IconButton(ICN_MUSIC, T("ボーカル"))) OpenVocalDialog();
    ImGui::SameLine();
    if (IconButton(ICN_FOLDER, T("プロジェクト"))) OpenProject();
    sep();
    ImGui::BeginDisabled(!g_loaded);
    if (IconButton(ICN_SAVE, T("保存"))) SaveProject();
    ImGui::SameLine();
    if (IconButton(ICN_EXPORT, T("VMD生成"))) GenerateAndSave();
    ImGui::EndDisabled();
    sep();
    if (IconButton(ICN_GEAR, T("設定"))) g_openSettings = true;

    ImGui::PopStyleColor(3);
    ImGui::End();
}

// ---------------------------------------------------------------------------
// スタート画面 (起動時に表示。左: 操作ボタン / 右: 最近使ったファイル)
// ---------------------------------------------------------------------------
static bool FileExistsW(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void OpenRecentItem(const RecentItem& it) {
    const std::wstring path = it.path;   // 読み込み中に g_recent が並び替わっても安全なようにコピー
    if (!FileExistsW(path)) { g_statusText = "ファイルが見つかりません: " + WideToUtf8(path); return; }
    std::wstring lower = path;
    for (auto& c : lower) c = towlower(c);
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, L".v2v") == 0) {
        OpenProjectFromPath(path);
    }
    else {
        LoadSongFromPath(path);
        if (g_loaded) {
            AddRecent(path, "トラック " + std::to_string(g_song.tracks.size()));
            g_showStart = false;
        }
    }
}

static bool IsProjectPath(const std::wstring& path) {
    std::wstring lower = path;
    for (auto& c : lower) c = towlower(c);
    return lower.size() >= 4 && lower.compare(lower.size() - 4, 4, L".v2v") == 0;
}

// スタート画面の分類 (0:すべて 1:お気に入り 2:プロジェクト 3:曲 4〜:ユーザー分類) に入るか
static bool PassCategory(const RecentItem& it, int cat) {
    if (cat == 0) return true;
    if (cat == 1) return it.fav;
    if (cat == 2) return IsProjectPath(it.path);
    if (cat == 3) return !IsProjectPath(it.path);
    const size_t ui = (size_t)(cat - 4);
    return ui < g_categories.size() && it.cat == g_categories[ui];
}

static bool ContainsNoCase(const std::string& hay, const char* needle) {
    if (!needle || !needle[0]) return true;
    std::string h = hay, n = needle;
    for (auto& c : h) c = (char)tolower((unsigned char)c);
    for (auto& c : n) c = (char)tolower((unsigned char)c);
    return h.find(n) != std::string::npos;
}

// 左の分類リストの1行 (アイコンと文字を行の縦中央にそろえる)
static bool CategoryRow(int icon, const char* label, bool selected) {
    const float fs = ImGui::GetFontSize();
    const std::string id = std::string("##cat_") + label;
    const bool r = ImGui::Selectable(id.c_str(), selected, 0, ImVec2(0, 32.0f));
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    const float midY = (mn.y + mx.y) * 0.5f;
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    DrawIcon(dl, icon, ImVec2(mn.x + 26.0f, midY), fs * 0.9f, col);
    dl->AddText(ImVec2(mn.x + 48.0f, midY - fs * 0.5f), col, label);
    return r;
}

static char g_catNameBuf[64] = "";
static int g_catEditIdx = -1;   // -1: 追加 / 0〜: その分類の名前変更

static void RenderStartScreen() {
    if (!g_showStart) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) { g_showStart = false; return; }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
#ifdef IMGUI_HAS_VIEWPORT
    ImGui::SetNextWindowViewport(vp->ID);
#endif
    if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) ImGui::SetNextWindowFocus();
    ImGuiWindowFlags f = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##start_screen", nullptr, f);
    ImGui::PopStyleVar(3);

    // ================= 左: 操作と分類 =================
    const float sideW = 250.0f;
    {
        const ImVec4 wb = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        const float k = (g_themeBase == 0) ? 0.62f : 0.94f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(wb.x * k, wb.y * k, wb.z * k, 1.0f));   // 左の帯は背景より一段暗く
    }
    ImGui::BeginChild("##start_side", ImVec2(sideW, 0), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(ImVec2(12, 14));
    ImGui::BeginGroup();
    const float bw = sideW - 24.0f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_TabActive));
    if (IconButton(ICN_ADD, T("新規 (VSQX / VSQ)"), ImVec2(bw, 50.0f))) OpenFile();
    ImGui::PopStyleColor();
    if (IconButton(ICN_FOLDER, T("プロジェクトを開く"), ImVec2(bw, 36.0f))) OpenProject();
    if (IconButton(ICN_MODEL, T("MMDモデルを開く"), ImVec2(bw, 36.0f))) OpenModelDialog();
    if (g_modelLoaded) {
        ImGui::PushTextWrapPos(sideW - 12.0f);
        ImGui::TextDisabled("%s", g_model.name.c_str());
        ImGui::PopTextWrapPos();
    }
    if (IconButton(ICN_BACK, T("エディタに戻る"), ImVec2(bw, 36.0f))) g_showStart = false;

    // 分類の追加 / 名前変更 / 削除
    ImGui::Dummy(ImVec2(0, 4));
    const ImVec2 sq(34.0f, 30.0f);
    if (IconButton(ICN_ADD, "", sq)) { g_catEditIdx = -1; g_catNameBuf[0] = '\0'; ImGui::OpenPopup("##cat_name"); }
    ImGui::SameLine();
    ImGui::BeginDisabled(g_startCat < 4);
    if (IconButton(ICN_TRASH, "", sq)) {
        const size_t ui = (size_t)(g_startCat - 4);
        if (ui < g_categories.size()) {
            const std::string gone = g_categories[ui];
            for (RecentItem& r : g_recent) if (r.cat == gone) r.cat.clear();
            g_categories.erase(g_categories.begin() + (long)ui);
            g_startCat = 0;
        }
    }
    ImGui::SameLine();
    if (IconButton(ICN_EDIT, "", sq)) {
        const size_t ui = (size_t)(g_startCat - 4);
        if (ui < g_categories.size()) {
            g_catEditIdx = (int)ui;
            snprintf(g_catNameBuf, sizeof(g_catNameBuf), "%s", g_categories[ui].c_str());
            ImGui::OpenPopup("##cat_name");
        }
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("##cat_name")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(200.0f);
        const bool enter = ImGui::InputTextWithHint("##catname", T("分類の名前"), g_catNameBuf, sizeof(g_catNameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (enter || ImGui::Button("OK")) {
            std::string nm = g_catNameBuf;
            while (!nm.empty() && nm.back() == ' ') nm.pop_back();
            const bool dup = std::find(g_categories.begin(), g_categories.end(), nm) != g_categories.end();
            if (!nm.empty() && !dup) {
                if (g_catEditIdx < 0) { g_categories.push_back(nm); g_startCat = 4 + (int)g_categories.size() - 1; }
                else {
                    const std::string old = g_categories[(size_t)g_catEditIdx];
                    for (RecentItem& r : g_recent) if (r.cat == old) r.cat = nm;
                    g_categories[(size_t)g_catEditIdx] = nm;
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::EndGroup();

    // 分類リスト
    ImGui::SetCursorPos(ImVec2(0, ImGui::GetCursorPosY() + 6.0f));
    ImGui::Separator();
    ImGui::BeginChild("##start_cats", ImVec2(0, -(ImGui::GetFrameHeight() + 22.0f)), false);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f);
    if (CategoryRow(ICN_FOLDER, T("すべて"), g_startCat == 0)) g_startCat = 0;
    if (CategoryRow(ICN_HEART, T("お気に入り"), g_startCat == 1)) g_startCat = 1;
    if (CategoryRow(ICN_SAVE, T("プロジェクト"), g_startCat == 2)) g_startCat = 2;
    if (CategoryRow(ICN_MUSIC, T("曲"), g_startCat == 3)) g_startCat = 3;
    for (size_t i = 0; i < g_categories.size(); i++)
        if (CategoryRow(ICN_TAG, g_categories[i].c_str(), g_startCat == 4 + (int)i)) g_startCat = 4 + (int)i;
    ImGui::EndChild();

    // 下: 起動時の表示と設定
    ImGui::SetCursorPos(ImVec2(14, ImGui::GetWindowHeight() - ImGui::GetFrameHeight() - 12.0f));
    ImGui::Checkbox(T("起動時に表示"), &g_startOnLaunch);
    ImGui::SameLine(sideW - 44.0f);
    if (IconButton(ICN_GEAR, "", ImVec2(32.0f, 0.0f))) g_openSettings = true;
    ImGui::EndChild();

    // ================= 右: ファイル一覧 =================
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));   // エディタと同じ背景
    ImGui::BeginChild("##start_main", ImVec2(0, 0), false);
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(ImVec2(20, 14));
    {
        const char* title = g_startCat == 0 ? T("すべて") : g_startCat == 1 ? T("お気に入り") : g_startCat == 2 ? T("プロジェクト") : g_startCat == 3 ? T("曲")
            : ((size_t)(g_startCat - 4) < g_categories.size() ? g_categories[(size_t)(g_startCat - 4)].c_str() : "");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(title);
    }
    {
        const float sw = 240.0f;
        ImGui::SameLine(0.0f, 24.0f);
        ImGui::SetNextItemWidth(sw);
        ImGui::InputTextWithHint("##start_search", T("検索"), g_startSearch, sizeof(g_startSearch));
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", g_statusText.c_str());
    }
    ImGui::SetCursorPosX(20);
    ImGui::Separator();

    const float cardW = 390.0f, cardH = 150.0f, gap = 12.0f, footH = 34.0f;
    const float availW = ImGui::GetContentRegionAvail().x - 40.0f;
    const int cols = std::max(1, (int)((availW + gap) / (cardW + gap)));
    ImGui::Dummy(ImVec2(0, 6));
    const ImVec2 origin = ImVec2(ImGui::GetWindowPos().x + 20.0f - ImGui::GetScrollX(), ImGui::GetCursorScreenPos().y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 colCard = ImGui::GetColorU32(ImGuiCol_FrameBg);
    const ImU32 colHover = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
    const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 colDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 colThumb = ImGui::GetColorU32(ImGuiCol_TabActive);

    // 表示する項目 (分類と検索で絞り込み)
    std::vector<int> shown;
    for (int i = 0; i < (int)g_recent.size(); i++) {
        const RecentItem& it = g_recent[(size_t)i];
        if (!PassCategory(it, g_startCat)) continue;
        std::wstring fn = it.path;
        const size_t sl = fn.find_last_of(L"\\/");
        if (sl != std::wstring::npos) fn = fn.substr(sl + 1);
        if (!ContainsNoCase(WideToUtf8(fn), g_startSearch)) continue;
        shown.push_back(i);
    }

    int removeIdx = -1, openIdx = -1;
    for (int k = 0; k < (int)shown.size(); k++) {
        const int i = shown[(size_t)k];
        RecentItem& it = g_recent[(size_t)i];
        const int col = k % cols, row = k / cols;
        const ImVec2 p0(origin.x + col * (cardW + gap), origin.y + row * (cardH + gap));
        const ImVec2 p1(p0.x + cardW, p0.y + cardH);
        const bool exists = FileExistsW(it.path);

        std::wstring fnW = it.path;
        const size_t sl = fnW.find_last_of(L"\\/");
        if (sl != std::wstring::npos) fnW = fnW.substr(sl + 1);
        const size_t dot = fnW.find_last_of(L'.');
        const std::string stem = WideToUtf8(dot == std::wstring::npos ? fnW : fnW.substr(0, dot));
        std::string ext = dot == std::wstring::npos ? "" : WideToUtf8(fnW.substr(dot));
        for (auto& c : ext) c = (char)tolower((unsigned char)c);

        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton("##card", ImVec2(cardW, cardH - footH));
        const bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) openIdx = i;

        dl->AddRectFilled(p0, p1, hov ? colHover : colCard, 6.0f);
        const ImVec2 t0(p0.x + 12, p0.y + 12), t1(p0.x + 112, p0.y + 112);
        dl->AddRectFilled(t0, t1, colThumb, 6.0f);
        DrawIcon(dl, IsProjectPath(it.path) ? ICN_DOC : ICN_MUSIC, ImVec2((t0.x + t1.x) * 0.5f, (t0.y + t1.y) * 0.5f), 56.0f, colText);

        dl->PushClipRect(ImVec2(p0.x + 124, p0.y), ImVec2(p1.x - 8, p1.y - footH), true);
        const char* labels[4] = { T("ファイル名"), T("形式"), T("内容"), T("最終使用") };
        float labW = 0.0f;
        for (int r = 0; r < 4; r++) labW = std::max(labW, ImGui::CalcTextSize(labels[r]).x);
        const float valX = p0.x + 124.0f + labW + 14.0f;
        const std::string values[4] = { stem, ext, !exists ? std::string(T("ファイルが見つかりません")) : (it.sub.empty() ? std::string("-") : it.sub), it.time.empty() ? std::string("-") : it.time };
        const float lh = ImGui::GetTextLineHeightWithSpacing() + 2.0f;
        for (int r = 0; r < 4; r++) {
            const float y = p0.y + 12 + lh * r;
            dl->AddText(ImVec2(p0.x + 124, y), colDim, labels[r]);
            dl->AddText(ImVec2(valX, y), (!exists && (r == 0 || r == 2)) ? IM_COL32(230, 110, 80, 255) : colText, values[r].c_str());
        }
        dl->PopClipRect();
        if (hov) ImGui::SetTooltip("%s", WideToUtf8(it.path).c_str());

        // 下の行: お気に入り / 分類 / 一覧から外す
        const float fy = p1.y - footH + 3.0f, fh = footH - 6.0f;
        ImGui::SetCursorScreenPos(ImVec2(p0.x + 8.0f, fy));
        if (ImGui::InvisibleButton("##fav", ImVec2(28.0f, fh))) it.fav = !it.fav;
        DrawIcon(dl, ICN_HEART, ImVec2(p0.x + 22.0f, fy + fh * 0.5f), 16.0f, it.fav ? IM_COL32(236, 86, 122, 255) : colDim);

        ImGui::SetCursorScreenPos(ImVec2(p0.x + 42.0f, fy));
        const std::string catName = it.cat.empty() ? std::string(T("未分類")) : it.cat;
        if (IconButton(ICN_TAG, catName.c_str(), ImVec2(170.0f, fh))) ImGui::OpenPopup("##catpick");
        if (ImGui::BeginPopup("##catpick")) {
            if (ImGui::Selectable(T("未分類"), it.cat.empty())) it.cat.clear();
            for (const std::string& c : g_categories) if (ImGui::Selectable(c.c_str(), it.cat == c)) it.cat = c;
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos(ImVec2(p0.x + 42.0f + 170.0f + 6.0f, fy));
        if (IconButton(ICN_DELETE, "", ImVec2(28.0f, fh))) removeIdx = i;
        ImGui::PopID();
    }
    if (openIdx >= 0 && openIdx != removeIdx) { const RecentItem copy = g_recent[(size_t)openIdx]; OpenRecentItem(copy); }
    if (removeIdx >= 0) g_recent.erase(g_recent.begin() + removeIdx);

    if (shown.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + 4.0f));
        ImGui::TextDisabled("%s", T("該当するファイルがありません"));
    }
    else {
        const int rows = ((int)shown.size() + cols - 1) / cols;
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rows * (cardH + gap)));
        ImGui::Dummy(ImVec2(1, 1));
    }

    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// 設定ダイアログ (一般 / タイムライン / プレビュー / レイアウト)
// ---------------------------------------------------------------------------
static void SettingsRow(const char* label) {   // 左にラベル、右に操作部
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetWindowWidth() * 0.42f);
    ImGui::SetNextItemWidth(-1);
}

static void RenderSettingsDialog() {
    if (g_openSettings) { g_langDirty = true; ImGui::OpenPopup("###settings"); g_openSettings = false; }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(780.0f, vp->WorkSize.x - 40.0f), std::min(640.0f, vp->WorkSize.y - 40.0f)), ImGuiCond_Appearing);
    const std::string title = std::string(T("設定")) + "###settings";
    bool open = true;
    if (!ImGui::BeginPopupModal(title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) return;

    const ImGuiTreeNodeFlags kOpen = ImGuiTreeNodeFlags_DefaultOpen;
    const float bodyH = -(ImGui::GetFrameHeight() + 14.0f);
    if (ImGui::BeginTabBar("##settings_tabs")) {
        // ---------------- 一般 ----------------
        if (ImGui::BeginTabItem(T("一般"))) {
            ImGui::BeginChild("##sb_general", ImVec2(0, bodyH), false);
            if (ImGui::CollapsingHeader(T("言語"), kOpen)) {
                SettingsRow(T("言語"));
                std::string cur = g_langSetting == "auto" ? std::string(T("自動")) : g_langSetting == "ja" ? std::string("日本語") : g_langSetting;
                for (const LangInfo& l : g_langs) if (l.code == g_langSetting) cur = l.name;
                if (ImGui::BeginCombo("##lang", cur.c_str())) {
                    auto item = [&](const char* label, const std::string& code) {
                        if (ImGui::Selectable(label, g_langSetting == code)) { g_langSetting = code; g_langDirty = true; }
                    };
                    item(T("自動"), "auto");
                    item("日本語", "ja");
                    for (const LangInfo& l : g_langs) item(l.name.c_str(), l.code);
                    ImGui::EndCombo();
                }
            }
            if (ImGui::CollapsingHeader(T("起動"), kOpen)) {
                SettingsRow(T("前回のファイルを開く"));
                ImGui::Checkbox("##autoopen", &g_autoOpenLast);
                SettingsRow(T("スタート画面を表示"));
                ImGui::Checkbox("##startlaunch", &g_startOnLaunch);
            }
            if (ImGui::CollapsingHeader(T("プリセット"), kOpen)) {
                if (IconButton(ICN_SAVE, T("保存 (.json)"), ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 4.0f, 0))) SavePresetFileDialog();
                ImGui::SameLine();
                if (IconButton(ICN_FOLDER, T("読み込み (.json)"), ImVec2(-1, 0))) OpenPresetFileDialog();
            }
            if (ImGui::CollapsingHeader(T("テーマ"), kOpen)) {
                SettingsRow(T("基本テーマ"));
                const char* bases[] = { T("ダーク"), T("ホワイト") };
                if (ImGui::Combo("##base_theme", &g_themeBase, bases, 2)) ApplyCustomStyle();

                // アクセント色: 一番左がカスタム色、続いて標準の色
                static const float kSwatch[9][3] = {
                    { 0.20f, 0.55f, 0.85f }, { 0.40f, 0.80f, 0.85f }, { 0.55f, 0.30f, 0.80f }, { 0.90f, 0.70f, 0.60f }, { 0.30f, 0.30f, 0.90f },
                    { 0.45f, 0.75f, 0.15f }, { 0.85f, 0.10f, 0.35f }, { 0.85f, 0.50f, 0.10f }, { 0.50f, 0.25f, 0.55f } };
                auto same = [](const float* a, const float* b) { return std::fabs(a[0] - b[0]) < 0.01f && std::fabs(a[1] - b[1]) < 0.01f && std::fabs(a[2] - b[2]) < 0.01f; };
                bool isPreset = false;
                for (int k = 0; k < 9; k++) if (same(g_accentColor, kSwatch[k])) isPreset = true;
                if (!isPreset) { g_customAccent[0] = g_accentColor[0]; g_customAccent[1] = g_accentColor[1]; g_customAccent[2] = g_accentColor[2]; }

                ImGui::Dummy(ImVec2(0, 4));
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(T("アクセント色"));
                for (int k = 0; k < 10; k++) {
                    const float* c = (k == 0) ? g_customAccent : kSwatch[k - 1];
                    const bool sel = same(g_accentColor, c);
                    ImGui::PushID(k);
                    if (k > 0) ImGui::SameLine();
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    if (ImGui::InvisibleButton("##sw", ImVec2(30.0f, 30.0f))) {
                        g_accentColor[0] = c[0]; g_accentColor[1] = c[1]; g_accentColor[2] = c[2]; g_accentColor[3] = 1.0f;
                        ApplyCustomStyle();
                    }
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(p, ImVec2(p.x + 30.0f, p.y + 30.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], 1.0f)), 5.0f);
                    if (ImGui::IsItemHovered()) dl->AddRect(p, ImVec2(p.x + 30.0f, p.y + 30.0f), IM_COL32(255, 255, 255, 200), 5.0f, 0, 1.5f);
                    if (sel) DrawIcon(dl, ICN_CHECK, ImVec2(p.x + 15.0f, p.y + 15.0f), 16.0f, GetContrastingTextColor(c[0], c[1], c[2]));
                    ImGui::PopID();
                }

                ImGui::Dummy(ImVec2(0, 6));
                SettingsRow(T("カスタム色 (Hex)"));
                static char hex[16] = "";
                static bool hexActive = false;
                if (!hexActive) snprintf(hex, sizeof(hex), "#%02X%02X%02X", (int)(g_customAccent[0] * 255.0f + 0.5f), (int)(g_customAccent[1] * 255.0f + 0.5f), (int)(g_customAccent[2] * 255.0f + 0.5f));
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 40.0f);
                if (ImGui::InputText("##hex", hex, sizeof(hex), ImGuiInputTextFlags_CharsUppercase)) {
                    unsigned r = 0, g = 0, b = 0;
                    if (strlen(hex) == 7 && hex[0] == '#' && sscanf(hex + 1, "%2x%2x%2x", &r, &g, &b) == 3) {
                        g_customAccent[0] = r / 255.0f; g_customAccent[1] = g / 255.0f; g_customAccent[2] = b / 255.0f;
                        g_accentColor[0] = g_customAccent[0]; g_accentColor[1] = g_customAccent[1]; g_accentColor[2] = g_customAccent[2]; g_accentColor[3] = 1.0f;
                        ApplyCustomStyle();
                    }
                }
                hexActive = ImGui::IsItemActive();
                ImGui::SameLine();
                if (ImGui::ColorEdit3("##customcol", g_customAccent, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
                    g_accentColor[0] = g_customAccent[0]; g_accentColor[1] = g_customAccent[1]; g_accentColor[2] = g_customAccent[2]; g_accentColor[3] = 1.0f;
                    ApplyCustomStyle();
                }
            }
            if (ImGui::CollapsingHeader(T("画面"), kOpen)) {
                SettingsRow(T("VSync (垂直同期)"));
                ImGui::Checkbox("##vsync", &g_vsync);
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        // ---------------- タイムライン ----------------
        if (ImGui::BeginTabItem(T("タイムライン"))) {
            ImGui::BeginChild("##sb_timeline", ImVec2(0, bodyH), false);
            if (ImGui::CollapsingHeader(T("ピアノロール"), kOpen)) {
                SettingsRow(T("鍵盤の高さ"));
                ImGui::SliderFloat("##rowh", &g_prRowHeight, 10.0f, 40.0f, "%.0f px");
                SettingsRow(T("横ズーム"));
                ImGui::SliderFloat("##zoomx", &g_prZoomX, 30.0f, 800.0f, "%.0f px/s");
            }
            if (ImGui::CollapsingHeader(T("表示"), kOpen)) {
                SettingsRow(T("ミニノート"));
                ImGui::Checkbox("##showmini", &g_prShowMini);
                SettingsRow(T("ピッチ曲線"));
                ImGui::Checkbox("##showpitch", &g_prShowPitch);
                SettingsRow("Velocity");
                ImGui::Checkbox("##showvel", &g_prShowVel);
                SettingsRow("Dynamics");
                ImGui::Checkbox("##showdyn", &g_prShowDyn);
            }
            if (ImGui::CollapsingHeader("WAV", kOpen)) {
                if (IconButton(ICN_WAVE, T("WAVを開く"), ImVec2(-1, 0))) OpenWavDialog();
                ImGui::BeginDisabled(!g_wavLoaded);
                SettingsRow(T("波形を表示"));
                ImGui::Checkbox("##showwave", &g_prShowWave);
                SettingsRow(T("開始フレーム"));
                if (ImGui::InputInt("##wavstart", &g_wavStartFrame)) g_genDirty = true;
                SettingsRow(T("音量"));
                ImGui::SliderFloat("##wavvol", &g_wavVolume, 0.0f, 1.0f, "%.2f");
                SettingsRow(T("ミュート"));
                ImGui::Checkbox("##wavmute", &g_wavMute);
                SettingsRow(T("波形の色"));
                ImGui::ColorEdit4("##wavcolor", g_wavColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
                if (ImGui::Button(T("WAVを外す"), ImVec2(-1, 30))) UnloadWav();
                ImGui::EndDisabled();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        // ---------------- プレビュー ----------------
        if (ImGui::BeginTabItem(T("プレビュー"))) {
            ImGui::BeginChild("##sb_preview", ImVec2(0, bodyH), false);
            if (ImGui::CollapsingHeader(T("カメラ"), kOpen)) {
                SettingsRow(T("初期位置"));
                if (ImGui::RadioButton(T("顔アップ"), g_camCloseup)) { g_camCloseup = true; ResetCamera(true); }
                ImGui::SameLine();
                if (ImGui::RadioButton(T("全身"), !g_camCloseup)) { g_camCloseup = false; ResetCamera(false); }
            }
            if (ImGui::CollapsingHeader(T("再生"), kOpen)) {
                SettingsRow(T("再生速度"));
                ImGui::SliderFloat("##speed", &g_playSpeed, 0.25f, 2.0f, "%.2fx");
                SettingsRow(T("ループ"));
                ImGui::Checkbox("##loop", &g_loop);
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        // ---------------- レイアウト ----------------
        if (ImGui::BeginTabItem(T("レイアウト"))) {
            ImGui::BeginChild("##sb_layout", ImVec2(0, bodyH), false);
            if (ImGui::CollapsingHeader(T("ウィンドウ"), kOpen)) {
                SettingsRow(T("ドッキングガイド"));
                ImGui::Checkbox("##easydock", &g_easyDock);
                SettingsRow(T("吸着する距離"));
                ImGui::SliderFloat("##snap", &g_snapPx, 0.0f, 40.0f, g_snapPx < 0.5f ? T("なし") : "%.0f px");
                SettingsRow(T("配置を固定"));
                ImGui::Checkbox("##lock", &g_layoutLocked);
                ImGui::BeginDisabled(!g_layoutLocked);
                SettingsRow(T("サイズ比率も固定"));
                ImGui::Checkbox("##locksize", &g_layoutLockSize);
                ImGui::EndDisabled();
#ifdef IMGUI_HAS_VIEWPORT
                SettingsRow(T("本体の外へ出せる (再起動後)"));
                ImGui::Checkbox("##detach", &g_allowDetach);
#endif
            }
            if (ImGui::CollapsingHeader(T("配置の操作"), kOpen)) {
                ImGui::BeginDisabled(g_layoutLocked);
                if (ImGui::Button(T("初期状態に戻す"), ImVec2(-1, 30))) g_resetLayout = true;
                if (ImGui::Button(T("浮いているウィンドウを戻す"), ImVec2(-1, 30))) g_dockRequest = 1;
                if (ImGui::Button(T("すべて1つのタブにまとめる"), ImVec2(-1, 30))) g_dockRequest = 2;
                ImGui::EndDisabled();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 130.0f);
    if (ImGui::Button("OK", ImVec2(116.0f, 0.0f)) || !open) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

static void RenderUI() {
    // キーボードショートカット (Escで全画面表示解除 / Spaceで再生・一時停止。文字入力中は除く)
    if (g_prFullScreen && ImGui::IsKeyPressed(ImGuiKey_Escape)) g_prFullScreen = false;
    if (!g_showStart && ImGui::IsKeyPressed(ImGuiKey_Space, false) && !ImGui::GetIO().WantTextInput) TogglePlay();
    if (!g_showStart && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && !ImGui::GetIO().WantTextInput && g_loaded) SaveProject();

    UpdateAudio();
    if (g_langDirty) { ScanLanguages(); LoadLanguage(); g_langDirty = false; }   // 翻訳の読み直しはフレームの頭で行う
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Comma, false)) g_openSettings = true;

    {   // 表示領域がごく小さいとき (最小化の途中など) はレイアウトを計算しない。計算するとドックの比率が壊れる
        const ImGuiIO& io0 = ImGui::GetIO();
        if (io0.DisplaySize.x < 200.0f || io0.DisplaySize.y < 150.0f) return;
    }

    RenderMainMenu();
    RenderToolbar();

    DockFrame();          // 配置の計算 / タブ欄 / 境界線 / ドラッグ中の水色のガイド / 状態表示
    SnapMovingWindow();   // 浮いているウィンドウを動かすとき、端や他のウィンドウの辺に吸着させる

    WindowTracks();
    WindowTimeline();
    WindowPreview();
    WindowFile();
    WindowParam();
    WindowOutput();
    WindowPreset();

    RenderStartScreen();  // 最前面に重ねて表示
    RenderSettingsDialog();
}

// ---------------------------------------------------------------------------
// エントリポイント
// ---------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // WIC (テクスチャ読み込み) 用
    LoadAutoSettings();                                  // 前回の設定を復元 (exeと同じフォルダのjson)
    g_showStart = g_startOnLaunch;                       // 起動時はスタート画面

    if (g_hasSavedWindow) {
        // モニター構成が変わって画面外になっていたら、既定位置に戻す
        RECT rc = { g_winX, g_winY, g_winX + g_winW, g_winY + g_winH };
        if (!MonitorFromRect(&rc, MONITOR_DEFAULTTONULL)) { g_winX = 100; g_winY = 100; }
    }

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hInst, nullptr, nullptr, nullptr, nullptr,
                       L"VyricSyncImGuiWindow", nullptr };
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowW(wc.lpszClassName, L"VSQX/VSQ -> VMD 口パク変換 (ImGui)",
        WS_OVERLAPPEDWINDOW, g_winX, g_winY, g_winW, g_winH,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(g_hwnd)) { CleanupDeviceD3D(); UnregisterClassW(wc.lpszClassName, wc.hInstance); return 1; }

    ShowWindow(g_hwnd, g_winMax ? SW_SHOWMAXIMIZED : nCmdShow);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;   // タイトルバーだけでウィンドウを動かす (ピアノロール・プレビューの操作と干渉させない)
#ifdef IMGUI_HAS_VIEWPORT
    if (g_allowDetach) {
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // 浮かせたウィンドウを本体の外へ出せる
        io.ConfigViewportsNoDefaultParent = false;
        io.ConfigViewportsNoAutoMerge = false;     // 本体の中で浮かせたウィンドウは、本体の中にとどまる (外へ出したときだけ独立する)
        io.ConfigViewportsNoDecoration = true;     // 外へ出たウィンドウもImGuiのタイトルバーで動かす
        io.ConfigViewportsNoTaskBarIcon = true;    // 外へ出たウィンドウをタスクバーに並べない
    }
#endif
    io.IniFilename = nullptr;                      // ウィンドウの配置は独自に保存するので、ImGuiのiniは使わない

    // 日本語フォント読み込み
    {
        ImFontConfig fontCfg;
        const ImWchar* jpRanges = io.Fonts->GetGlyphRangesJapanese();
        const char* candidates[] = {
            "C:\\Windows\\Fonts\\meiryo.ttc",
            "C:\\Windows\\Fonts\\YuGothM.ttc",
            "C:\\Windows\\Fonts\\msgothic.ttc",
        };
        ImFont* loaded = nullptr;
        for (const char* path : candidates) {
            loaded = io.Fonts->AddFontFromFileTTF(path, 18.0f, &fontCfg, jpRanges);
            if (loaded) break;
        }
    }

    ApplyCustomStyle();

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    InitModelRenderer();

    // 前回開いていたファイルを自動で開く
    if (g_autoOpenLast) {
        auto exists = [](const std::wstring& p) { return !p.empty() && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; };
        if (exists(g_startupInputPath)) {
            LoadSongFromPath(g_startupInputPath);
            if (g_loaded && g_restoreTrack < (int)g_song.tracks.size()) g_selectedTrack = g_restoreTrack;
        }
        if (exists(g_startupModelPath)) LoadModelFromPath(g_startupModelPath);
        if (exists(g_startupWavPath)) LoadWavFromPath(g_startupWavPath);
        if (exists(g_startupVocalPath)) LoadVocalFromPath(g_startupVocalPath);
    }

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (IsIconic(g_hwnd)) { Sleep(30); continue; }   // 最小化中は描画もレイアウト計算もしない (表示領域0でレイアウトが崩れるのを防ぐ)

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10); continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        double nowT = ImGui::GetTime();
        UpdateWindowCache();
        UpdatePreview(nowT, std::min(io.DeltaTime, 0.1f));

        RenderUI();

        AutoSaveSettings(nowT, false);

        ImGui::Render();
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, g_clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

#ifdef IMGUI_HAS_VIEWPORT
        // 本体の外へ分離したウィンドウの更新と描画
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
#endif

        HRESULT hr = g_pSwapChain->Present(g_vsync ? 1 : 0, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    StopAudio();
    AutoSaveSettings(ImGui::GetTime(), true);   // 終了時に最終状態を保存
    ShutdownModelRenderer();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(g_hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CoUninitialize();
    return 0;
}