@echo off
setlocal
cd /d "%~dp0"
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (echo Visual Studio C++ tools not found.& exit /b 1)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
if not exist build-direct mkdir build-direct
cl /nologo /std:c++20 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Isrc /Febuild-direct\EditorInteractionTests.exe /Fobuild-direct\ tests\EditorInteractionTests.cpp
if errorlevel 1 exit /b 1
build-direct\EditorInteractionTests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Isrc /Febuild-direct\SceneTests.exe /Fobuild-direct\ tests\SceneTests.cpp src\Engine.cpp third_party\ufbx\ufbx.c project\Scripts\MyComponent.cpp /link user32.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
build-direct\SceneTests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Isrc /Febuild-direct\RenderSmoke.exe /Fobuild-direct\ tests\RenderSmoke.cpp src\Engine.cpp src\Renderer.cpp third_party\ufbx\ufbx.c /link d3d12.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1
build-direct\RenderSmoke.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Isrc /Febuild-direct\ExportLaunch.exe /Fobuild-direct\ tests\ExportLaunch.cpp src\Engine.cpp third_party\ufbx\ufbx.c /link user32.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
build-direct\ExportLaunch.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Isrc /Febuild-direct\ExportSafety.exe /Fobuild-direct\ tests\ExportSafety.cpp src\Engine.cpp third_party\ufbx\ufbx.c /link user32.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
build-direct\ExportSafety.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Isrc /Febuild-direct\ProjectPanelTests.exe /Fobuild-direct\ tests\ProjectPanelTests.cpp src\ProjectPanel.cpp /link user32.lib gdi32.lib comctl32.lib shell32.lib ole32.lib uuid.lib
if errorlevel 1 exit /b 1
build-direct\ProjectPanelTests.exe
if errorlevel 1 exit /b 1
