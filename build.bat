@echo off
setlocal enabledelayedexpansion

where cl >nul 2>&1
if errorlevel 1 (
    echo cl.exe not found on PATH. Please launch from a VS Developer Command Prompt or run vcvars64.bat first.
    exit /b 1
)

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%.") do set "ROOT_DIR=%%~fI"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"

set "CMAKE_BIN=%CMAKE%"
if not defined CMAKE_BIN set "CMAKE_BIN=cmake"

REM Parse arguments: "asan" enables AddressSanitizer
set "PRESET=release-lto-on"
set "ASAN_STATE=OFF"
set "ASAN_FLAG=-DENVY_ENABLE_ASAN=OFF"
if /i "%~1"=="asan" (
    set "PRESET=release-asan-lto-on"
    set "ASAN_STATE=ON"
    set "ASAN_FLAG=-DENVY_ENABLE_ASAN=ON"
)

set "CACHE_DIR=%ROOT_DIR%\out\cache"
set "BUILD_DIR=%ROOT_DIR%\out\build"
set "CACHE_FILE=%BUILD_DIR%\CMakeCache.txt"
set "PRESET_FILE=%BUILD_DIR%\.envy-preset"
set "ASAN_STATE_FILE=%BUILD_DIR%\.envy-asan-state"


if not exist "%CACHE_DIR%" (
    mkdir "%CACHE_DIR%" || goto :fail
)

set "need_configure=1"
set "cached_preset="
set "cached_asan_state="
if exist "%CACHE_FILE%" (
    if exist "%PRESET_FILE%" (
        for /f "usebackq delims=" %%P in ("%PRESET_FILE%") do set "cached_preset=%%P"
    )
    if exist "%ASAN_STATE_FILE%" (
        for /f "usebackq delims=" %%A in ("%ASAN_STATE_FILE%") do set "cached_asan_state=%%A"
    )
    if /i "!cached_preset!"=="%PRESET%" (
        if /i "!cached_asan_state!"=="%ASAN_STATE%" (
            set "need_configure=0"
        )
    )
)

if "%need_configure%"=="1" (
    "%CMAKE_BIN%" --preset "%PRESET%" %ASAN_FLAG% --log-level=STATUS
    if errorlevel 1 goto :fail
    if not exist "%BUILD_DIR%" (
        mkdir "%BUILD_DIR%" || goto :fail
    )
    >"%PRESET_FILE%" echo %PRESET%
    >"%ASAN_STATE_FILE%" echo %ASAN_STATE%
)

"%CMAKE_BIN%" --build --preset "%PRESET%"
if errorlevel 1 goto :fail

echo Build completed successfully.
exit /b 0

:fail
echo.
echo Build failed. See messages above for details.
exit /b 1
