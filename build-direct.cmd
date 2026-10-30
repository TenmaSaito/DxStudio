@echo off
setlocal
cd /d "%~dp0"
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (echo Visual Studio C++ tools not found.& exit /b 1)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
if not exist build-direct mkdir build-direct
cl /nologo /std:c++20 /EHsc /O2 /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Isrc /Febuild-direct\DxStudio.exe /Fobuild-direct\ src\EditorMain.cpp src\Editor.cpp src\ProjectPanel.cpp project\Scripts\*.cpp src\Engine.cpp src\Renderer.cpp third_party\ufbx\ufbx.c /link /SUBSYSTEM:WINDOWS d3d12.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib comctl32.lib comdlg32.lib ole32.lib shell32.lib uuid.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Isrc /Febuild-direct\Game.exe /Fobuild-direct\ src\GameMain.cpp project\Scripts\*.cpp src\Engine.cpp src\Renderer.cpp third_party\ufbx\ufbx.c /link /SUBSYSTEM:WINDOWS d3d12.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1
copy /y build-direct\DxStudio.exe DxStudio.exe >nul
if errorlevel 1 (echo Could not update DxStudio.exe. Close the editor and retry.& exit /b 1)
copy /y build-direct\Game.exe Game.exe >nul
if errorlevel 1 (echo Could not update Game.exe. Close the running game and retry.& exit /b 1)
echo Build succeeded. Run DxStudio.exe.
