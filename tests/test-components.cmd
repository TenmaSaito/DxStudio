@echo off
setlocal
cd /d "%~dp0.."
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT exit /b 1
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
if not exist build-component-test mkdir build-component-test
cl /nologo /std:c++20 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Isrc /Febuild-component-test\SceneTests.exe /Fobuild-component-test\ tests\SceneTests.cpp src\Engine.cpp third_party\ufbx\ufbx.c project\Scripts\MyComponent.cpp /link user32.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
build-component-test\SceneTests.exe
exit /b %errorlevel%
