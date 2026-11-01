@echo off
setlocal
cd /d "%~dp0"
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (echo Visual Studio C++ tools not found.& exit /b 1)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
call "%~dp0build-sources.cmd"
if not exist build-direct mkdir build-direct
call :run EditorInteractionTests tests\EditorInteractionTests.cpp
if errorlevel 1 exit /b 1
call :run CoreTests "tests\CoreTests.cpp project\Scripts\CoroutineTemplate.cpp %DXS_ENGINE_SOURCES%" "%DXS_WINDOW_LIBS% d3dcompiler.lib"
if errorlevel 1 exit /b 1
call :run SceneTests "tests\SceneTests.cpp %DXS_ENGINE_SOURCES% project\Scripts\MyComponent.cpp" "%DXS_WINDOW_LIBS% d3dcompiler.lib"
if errorlevel 1 exit /b 1
call :run PhysicsTests "tests\PhysicsTests.cpp %DXS_ENGINE_SOURCES%" "%DXS_WINDOW_LIBS% d3dcompiler.lib"
if errorlevel 1 exit /b 1
call :run InputTests "tests\InputTests.cpp %DXS_ENGINE_SOURCES%" "%DXS_WINDOW_LIBS% d3dcompiler.lib"
if errorlevel 1 exit /b 1
call :run RenderSmoke "tests\RenderSmoke.cpp %DXS_ENGINE_SOURCES% %DXS_RENDER_SOURCES%" "%DXS_RENDER_LIBS% %DXS_WINDOW_LIBS%"
if errorlevel 1 exit /b 1
call :run GraphicsApiTests "tests\GraphicsApiTests.cpp %DXS_ENGINE_SOURCES% %DXS_RENDER_SOURCES% project\Scripts\MyComponent.cpp" "%DXS_RENDER_LIBS% %DXS_WINDOW_LIBS%"
if errorlevel 1 exit /b 1
call :run ExportLaunch "tests\ExportLaunch.cpp %DXS_ENGINE_SOURCES%" "%DXS_WINDOW_LIBS% d3dcompiler.lib"
if errorlevel 1 exit /b 1
call :run ExportSafety "tests\ExportSafety.cpp %DXS_ENGINE_SOURCES%" "%DXS_WINDOW_LIBS% d3dcompiler.lib"
if errorlevel 1 exit /b 1
call :run ModelConvertTests "tests\ModelConvertTests.cpp %DXS_ENGINE_SOURCES%" "%DXS_WINDOW_LIBS% d3dcompiler.lib"
if errorlevel 1 exit /b 1
call :run ThumbnailTests "tests\ThumbnailTests.cpp %DXS_ENGINE_SOURCES%" "%DXS_WINDOW_LIBS% d3dcompiler.lib"
if errorlevel 1 exit /b 1
call :run ProjectPanelTests "tests\ProjectPanelTests.cpp src\ProjectPanel.cpp src\EditorTheme.cpp src\EditorLocale.cpp" "%DXS_WINDOW_LIBS% %DXS_EDITOR_LIBS%"
if errorlevel 1 exit /b 1
echo All tests passed.
exit /b 0

rem :run <name> "<sources>" "<libs>"  -- compiles build-direct\<name>.exe and runs it.
:run
if not exist build-direct\%~1 mkdir build-direct\%~1
cl %DXS_CL_FLAGS% /Febuild-direct\%~1.exe /Fobuild-direct\%~1\ %~2 /link %~3
if errorlevel 1 exit /b 1
build-direct\%~1.exe
rem A failed assert aborts with a negative code, which 'if errorlevel 1' would miss.
if not "%errorlevel%"=="0" (echo %~1 FAILED with exit code %errorlevel%& exit /b 1)
exit /b 0
