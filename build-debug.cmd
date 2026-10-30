@echo off
setlocal
cd /d "%~dp0"
set "DXS_PROJECT=%~1"
if not defined DXS_PROJECT set "DXS_PROJECT=%~dp0project"
if not exist "%DXS_PROJECT%\Scripts" (echo Project Scripts folder not found: %DXS_PROJECT%& exit /b 1)
if not exist build-debug mkdir build-debug
call :build >build-debug\build.log 2>&1
set "DXS_RESULT=%ERRORLEVEL%"
type build-debug\build.log
if not "%DXS_RESULT%"=="0" exit /b %DXS_RESULT%
echo Debug build succeeded. Output: build-debug
exit /b 0

:build
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "DXS_VSROOT=%%i"
if not defined DXS_VSROOT (echo Visual Studio C++ tools not found.& exit /b 1)
call "%DXS_VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /Od /Zi /MTd /utf-8 /D_DEBUG /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Isrc /Febuild-debug\DxStudio-Debug.exe /Fdbuild-debug\DxStudio-Debug.pdb /Fobuild-debug\ src\EditorMain.cpp src\Editor.cpp src\ProjectPanel.cpp "%DXS_PROJECT%\Scripts\*.cpp" src\Engine.cpp src\Renderer.cpp third_party\ufbx\ufbx.c /link /DEBUG /SUBSYSTEM:WINDOWS d3d12.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib comctl32.lib comdlg32.lib ole32.lib shell32.lib uuid.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /Od /Zi /MTd /utf-8 /D_DEBUG /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Isrc /Febuild-debug\Game-Debug.exe /Fdbuild-debug\Game-Debug.pdb /Fobuild-debug\ src\GameMain.cpp "%DXS_PROJECT%\Scripts\*.cpp" src\Engine.cpp src\Renderer.cpp third_party\ufbx\ufbx.c /link /DEBUG /SUBSYSTEM:WINDOWS d3d12.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib
exit /b %ERRORLEVEL%
