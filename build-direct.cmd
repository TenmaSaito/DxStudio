@echo off
setlocal
cd /d "%~dp0"
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (echo Visual Studio C++ tools not found.& exit /b 1)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
call "%~dp0build-sources.cmd"
if not exist build-direct mkdir build-direct
cl %DXS_CL_FLAGS% /O2 /Febuild-direct\DxStudio.exe /Fobuild-direct\ %DXS_EDITOR_SOURCES% project\Scripts\*.cpp %DXS_ENGINE_SOURCES% %DXS_RENDER_SOURCES% /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED %DXS_RENDER_LIBS% %DXS_WINDOW_LIBS% %DXS_EDITOR_LIBS%
if errorlevel 1 exit /b 1
cl %DXS_CL_FLAGS% /O2 /Febuild-direct\Game.exe /Fobuild-direct\ src\GameMain.cpp project\Scripts\*.cpp %DXS_ENGINE_SOURCES% %DXS_RENDER_SOURCES% /link /SUBSYSTEM:WINDOWS %DXS_RENDER_LIBS% %DXS_WINDOW_LIBS%
if errorlevel 1 exit /b 1
copy /y build-direct\DxStudio.exe DxStudio.exe >nul
if errorlevel 1 (echo Could not update DxStudio.exe. Close the editor and retry.& exit /b 1)
copy /y build-direct\Game.exe Game.exe >nul
if errorlevel 1 (echo Could not update Game.exe. Close the running game and retry.& exit /b 1)
echo Build succeeded. Run DxStudio.exe.
