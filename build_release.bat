@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

:: 1. プロジェクトのディレクトリに移動
cd /d "C:\Users\kawar\source\repos\Vs2Vm\Vs2Vm"

:: 2. 開発者コマンドプロンプトの環境変数を自動で読み込む
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
) else if exist "C:\Program Files\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
) else (
    echo [Error] Visual Studioのパスが見つかりません。通常の開発者プロンプトから実行してください。
)

:: 3. 古いビルドの残骸をリセット
if exist Release rd /s /q Release
mkdir Release

:: 4. コンパイルを実行 (x64 Release)
echo [Build] x64 Release ビルドを開始します...
cl /std:c++17 /O2 /EHsc /utf-8 /DUNICODE /D_UNICODE /DImGuiCol_TabActive=ImGuiCol_TabSelected /DImGuiCol_TabUnfocused=ImGuiCol_TabDimmedSelected /DImGuiCol_TabUnfocusedActive=ImGuiCol_TabDimmedSelectedOverline /I"C:\Users\kawar\source\repos\Vs2Vm\Vs2Vm\thirdparty\imgui" /I"C:\Users\kawar\source\repos\Vs2Vm\Vs2Vm\thirdparty\imgui\backends" gui_imgui.cpp "C:\Users\kawar\source\repos\Vs2Vm\Vs2Vm\thirdparty\imgui\imgui.cpp" "C:\Users\kawar\source\repos\Vs2Vm\Vs2Vm\thirdparty\imgui\imgui_draw.cpp" "C:\Users\kawar\source\repos\Vs2Vm\Vs2Vm\thirdparty\imgui\imgui_tables.cpp" "C:\Users\kawar\source\repos\Vs2Vm\Vs2Vm\thirdparty\imgui\imgui_widgets.cpp" "C:\Users\kawar\source\repos\Vs2Vm\Vs2Vm\thirdparty\imgui\backends\imgui_impl_win32.cpp" "C:\Users\kawar\source\repos\Vs2Vm\Vs2Vm\thirdparty\imgui\backends\imgui_impl_dx11.cpp" /Fe"Release\gui_imgui.exe" /link d3d11.lib d3dcompiler.lib comdlg32.lib windowscodecs.lib ole32.lib

:: 5. 資産（res フォルダ）を Release フォルダに自動コピー
if exist "res" (
    echo [Copy] res フォルダを Release にコピー中...
    xcopy "res" "Release\res" /E /I /Y /Q >nul
) else (
    echo [Warning] 元の res フォルダが見つかりません。
)

:: 6. 中間ファイル（.obj）を整理して Release に移動
if exist *.obj move *.obj Release\ >nul

echo [Done] 処理が終了しました。Release フォルダ内の gui_imgui.exe を実行してください。
pause
