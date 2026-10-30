@echo off
setlocal
cd /d "%~dp0"
set "DXS_PROJECT=%~1"
if not defined DXS_PROJECT set "DXS_PROJECT=%~dp0project"
set "ANDROID_SDK_ROOT=%USERPROFILE%\AppData\Local\Android\Sdk"
set "ANDROID_HOME=%ANDROID_SDK_ROOT%"
set "JAVA_HOME=C:\Program Files\Adobe\Adobe Animate 2024\jre"
if not exist "%JAVA_HOME%\bin\java.exe" set "JAVA_HOME=C:\Program Files\Android\Android Studio\jbr"
set "DXS_GRADLE=%USERPROFILE%\.gradle\wrapper\dists\gradle-8.12-bin\cetblhg4pflnnks72fxwobvgv\gradle-8.12\bin\gradle.bat"
if not exist "%DXS_PROJECT%\scene.dxs" (echo Project scene not found: %DXS_PROJECT%\scene.dxs& exit /b 1)
if not exist "%ANDROID_SDK_ROOT%\ndk\28.2.13676358" (echo Android NDK 28.2.13676358 not found.& exit /b 1)
if not exist "%DXS_GRADLE%" (echo Gradle 8.12 not found.& exit /b 1)
if not exist build-android mkdir build-android
copy /y "%DXS_PROJECT%\scene.dxs" android\app\src\main\assets\scene.dxs >nul
pushd android
call "%DXS_GRADLE%" --offline --no-daemon :app:assembleDebug >..\build-android\build.log 2>&1
set "DXS_RESULT=%ERRORLEVEL%"
popd
type build-android\build.log
if not "%DXS_RESULT%"=="0" exit /b %DXS_RESULT%
copy /y android\app\build\outputs\apk\debug\app-debug.apk build-android\DxStudio-Android-debug.apk >nul
if errorlevel 1 exit /b 1
echo Android APK build succeeded: build-android\DxStudio-Android-debug.apk
