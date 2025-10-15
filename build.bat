@echo off
setlocal enabledelayedexpansion

rem -----------------------------------------------------------------------------
rem  Windows helper mirroring build.sh:
rem    * Configures the preset-driven build if needed
rem    * Reuses the cached preset marker to avoid redundant reconfigure cycles
rem    * Builds the envy target via the same CMake preset
rem  MSVC is required—bootstrap the Visual Studio environment before invoking
rem  CMake so Ninja binds to cl.exe instead of the GNU toolchain that ships
rem  with GitHub-hosted runners.
rem -----------------------------------------------------------------------------

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%.") do set "ROOT_DIR=%%~fI"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"

set "CMAKE_BIN=%CMAKE%"
if not defined CMAKE_BIN set "CMAKE_BIN=cmake"

set "PRESET=release-lto-on"
set "CACHE_DIR=%ROOT_DIR%\out\cache\third_party"
set "BUILD_DIR=%ROOT_DIR%\out\build"
set "CACHE_FILE=%BUILD_DIR%\CMakeCache.txt"
set "PRESET_FILE=%BUILD_DIR%\.envy-preset"

if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VSINSTALLPATH=%%i"
    )
)

if defined VSINSTALLPATH (
    call "%VSINSTALLPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
    if errorlevel 1 goto :fail
) else (
    set "VCVARSALL=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    if exist "%VCVARSALL%" (
        call "%VCVARSALL%" >nul
        if errorlevel 1 goto :fail
    ) else (
        echo Failed to locate Visual Studio toolchain. MSVC is required to build Envy.&echo Install Visual Studio Build Tools with the "Desktop development with C++" workload.
        goto :fail
    )
)

if not exist "%CACHE_DIR%" (
    mkdir "%CACHE_DIR%" || goto :fail
)

set "need_configure=1"
if exist "%CACHE_FILE%" (
    if exist "%PRESET_FILE%" (
        set /p cached_preset=<"%PRESET_FILE%"
        if /i "%cached_preset%"=="%PRESET%" (
            set "need_configure=0"
        )
    )
)

if "%need_configure%"=="1" (
    "%CMAKE_BIN%" --preset "%PRESET%" --log-level=STATUS
    if errorlevel 1 goto :fail
    if not exist "%BUILD_DIR%" (
        mkdir "%BUILD_DIR%" || goto :fail
    )
    >"%PRESET_FILE%" echo %PRESET%
)

"%CMAKE_BIN%" --build --preset "%PRESET%"
if errorlevel 1 goto :fail

echo Build completed successfully.
exit /b 0

:fail
echo.
echo Build failed. See messages above for details.
exit /b 1
