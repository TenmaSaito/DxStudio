@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"
rem Builds the Android Debug APK. Every step, including a missing toolchain,
rem is written to build-android\build.log (the editor shows its last lines).
if not exist build-android mkdir build-android
set "DXS_LOG=%~dp0build-android\build.log"
call :build > "%DXS_LOG%" 2>&1
set "DXS_RESULT=%ERRORLEVEL%"
type "%DXS_LOG%"
exit /b %DXS_RESULT%

:build
set "DXS_PROJECT=%~1"
if not defined DXS_PROJECT set "DXS_PROJECT=%~dp0project"
if not exist "%DXS_PROJECT%\scene.dxs" (echo ERROR: Project scene not found: %DXS_PROJECT%\scene.dxs& exit /b 1)

rem ---- Android SDK: ANDROID_SDK_ROOT / ANDROID_HOME, else the Android Studio default.
if not defined ANDROID_SDK_ROOT if defined ANDROID_HOME set "ANDROID_SDK_ROOT=%ANDROID_HOME%"
if not defined ANDROID_SDK_ROOT set "ANDROID_SDK_ROOT=%LOCALAPPDATA%\Android\Sdk"
if not exist "%ANDROID_SDK_ROOT%\platforms" (
    echo ERROR: Android SDK not found at "%ANDROID_SDK_ROOT%".
    echo Install Android Studio ^(SDK Platform 35, NDK, CMake 3.22.1^) or set ANDROID_SDK_ROOT.
    exit /b 1
)
set "ANDROID_HOME=%ANDROID_SDK_ROOT%"
echo Android SDK: %ANDROID_SDK_ROOT%

rem ---- NDK: the pinned version if installed, else the newest installed one.
set "DXS_NDK=28.2.13676358"
if not exist "%ANDROID_SDK_ROOT%\ndk\%DXS_NDK%" (
    set "DXS_NDK="
    for /f "delims=" %%v in ('dir /b /ad /on "%ANDROID_SDK_ROOT%\ndk" 2^>nul') do set "DXS_NDK=%%v"
)
if not defined DXS_NDK (
    echo ERROR: No Android NDK in "%ANDROID_SDK_ROOT%\ndk".
    echo Install it from Android Studio: SDK Manager ^> SDK Tools ^> NDK ^(Side by side^).
    exit /b 1
)
echo Android NDK: %DXS_NDK%

rem ---- Java 17-23: Gradle 8.12 cannot run on newer Java (recent Android Studio
rem bundles Java 25 -> "Unsupported class file major version 69").
set "DXS_JAVA="
call :checkjava "%JAVA_HOME%"
for /d %%j in ("%USERPROFILE%\.jdks\*" "%ProgramFiles%\Java\*" "%ProgramFiles%\Eclipse Adoptium\*" "%ProgramFiles%\Microsoft\jdk*" "%ProgramFiles%\Zulu\*") do call :checkjava "%%~j"
call :checkjava "%ProgramFiles%\Android\Android Studio\jbr"
call :checkjava "%LOCALAPPDATA%\Programs\Android Studio\jbr"
call :checkjava "%ProgramFiles%\Adobe\Adobe Animate 2024\jre"
if not defined DXS_JAVA (
    echo ERROR: Java 17-23 not found ^(Gradle 8.12 does not run on Java 24+^).
    echo Install a JDK 17 or 21 ^(e.g. Android Studio: Settings ^> Build Tools ^> Gradle ^> Gradle JDK ^> Download JDK^)
    echo or set JAVA_HOME to one.
    exit /b 1
)
set "JAVA_HOME=%DXS_JAVA%"
echo Java: %JAVA_HOME% ^(version %DXS_JAVA_MAJOR%^)

rem ---- Gradle 8.9+ (Android Gradle Plugin 8.7): newest one Android Studio downloaded, else PATH.
set "DXS_GRADLE="
for /d %%d in ("%USERPROFILE%\.gradle\wrapper\dists\gradle-8.*-bin") do (
    for /d %%h in ("%%d\*") do (
        for /d %%g in ("%%h\gradle-8.*") do if exist "%%g\bin\gradle.bat" (
            set "DXS_CANDIDATE=%%~nxg"
            set "DXS_MINOR=!DXS_CANDIDATE:gradle-8.=!"
            for /f "delims=." %%m in ("!DXS_MINOR!") do if %%m GEQ 9 set "DXS_GRADLE=%%g\bin\gradle.bat"
        )
    )
)
if not defined DXS_GRADLE for /f "delims=" %%g in ('where gradle.bat 2^>nul') do if not defined DXS_GRADLE set "DXS_GRADLE=%%g"
if not defined DXS_GRADLE (
    echo ERROR: Gradle 8.9 or newer not found.
    echo Open the android folder once in Android Studio ^(it downloads Gradle^), or put gradle on PATH.
    exit /b 1
)
echo Gradle: %DXS_GRADLE%

copy /y "%DXS_PROJECT%\scene.dxs" android\app\src\main\assets\scene.dxs >nul
pushd android
rem Offline first (fast, no network); online retry downloads missing plugins/packages.
call "%DXS_GRADLE%" --offline --no-daemon -PdxsNdkVersion=%DXS_NDK% :app:assembleDebug
if errorlevel 1 (
    echo.
    echo Offline build failed; retrying online to download missing dependencies...
    call "%DXS_GRADLE%" --no-daemon -PdxsNdkVersion=%DXS_NDK% :app:assembleDebug
)
set "DXS_GRADLE_RESULT=!ERRORLEVEL!"
popd
if not "%DXS_GRADLE_RESULT%"=="0" (echo ERROR: Gradle build failed ^(see the output above^).& exit /b %DXS_GRADLE_RESULT%)
copy /y android\app\build\outputs\apk\debug\app-debug.apk build-android\DxStudio-Android-debug.apk >nul
if errorlevel 1 (echo ERROR: Could not copy the APK.& exit /b 1)
echo Android APK build succeeded: build-android\DxStudio-Android-debug.apk
exit /b 0

rem :checkjava <dir>  -- keeps the first JDK whose major version is 17-23.
:checkjava
if defined DXS_JAVA exit /b 0
if "%~1"=="" exit /b 0
if not exist "%~1\bin\java.exe" exit /b 0
if not exist "%~1\release" exit /b 0
set "DXS_VERSION_LINE="
for /f "usebackq tokens=2 delims==" %%v in (`findstr /b "JAVA_VERSION=" "%~1\release"`) do set "DXS_VERSION_LINE=%%~v"
if not defined DXS_VERSION_LINE exit /b 0
for /f "tokens=1 delims=." %%m in ("%DXS_VERSION_LINE%") do set "DXS_MAJOR=%%m"
if %DXS_MAJOR% GEQ 17 if %DXS_MAJOR% LEQ 23 (
    set "DXS_JAVA=%~1"
    set "DXS_JAVA_MAJOR=%DXS_MAJOR%"
)
exit /b 0
