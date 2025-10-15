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

set "PRESET=release-lto-on"
set "CACHE_DIR=%ROOT_DIR%\out\cache\third_party"
set "BUILD_DIR=%ROOT_DIR%\out\build"
set "CACHE_FILE=%BUILD_DIR%\CMakeCache.txt"
set "PRESET_FILE=%BUILD_DIR%\.envy-preset"


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
