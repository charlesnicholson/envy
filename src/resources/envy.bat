@echo off
setlocal EnableDelayedExpansion

set "ENVY_MIRROR=@@DOWNLOAD_URL@@"
set "FALLBACK_VERSION=@@ENVY_VERSION@@"

set "MANIFEST="
set "CANDIDATE="
set "DIR=%CD%"
:findloop
if exist "%DIR%\envy.lua" (
    set "IS_ROOT=true"
    for /f "usebackq tokens=1,2,3,4 delims= " %%a in ("%DIR%\envy.lua") do (
        if "%%a"=="--" if "%%b"=="@envy" if "%%c"=="root" (
            set "VAL=%%d"
            if "!VAL!"=="""false""" set "IS_ROOT=false"
        )
    )
    if "!IS_ROOT!"=="true" (
        set "MANIFEST=%DIR%\envy.lua"
        goto :found
    ) else (
        set "CANDIDATE=%DIR%\envy.lua"
    )
)
for %%I in ("%DIR%\..") do set "PARENT=%%~fI"
if "%PARENT%"=="%DIR%" (
    if defined CANDIDATE (
        set "MANIFEST=!CANDIDATE!"
        goto :found
    )
    echo ERROR: envy.lua not found >&2 & exit /b 1
)
set "DIR=%PARENT%"
goto :findloop
:found

set "VERSION="
set "MANIFEST_CACHE="
set "MANIFEST_MIRROR="
set /a LINE_COUNT=0

for /f "usebackq tokens=1,2,3,* delims= " %%a in ("%MANIFEST%") do (
    set /a LINE_COUNT+=1
    if !LINE_COUNT! GTR 20 goto :done_parse
    if "%%a"=="--" if "%%b"=="@envy" (
        set "KEY=%%c"
        set "VAL=%%d"
        if defined VAL (
            set "VAL=!VAL:~1,-1!"
            if "!KEY!"=="version" set "VERSION=!VAL!"
            if "!KEY!"=="cache" set "MANIFEST_CACHE=!VAL!"
            if "!KEY!"=="mirror" set "MANIFEST_MIRROR=!VAL!"
        )
    )
)
:done_parse

if "%VERSION%"=="" (
    echo WARNING: @envy version not found in %MANIFEST%, using fallback %FALLBACK_VERSION% >&2
    set "VERSION=%FALLBACK_VERSION%"
)

if defined MANIFEST_MIRROR set "ENVY_MIRROR=%MANIFEST_MIRROR%"

if defined ENVY_CACHE_ROOT (
    set "CACHE=%ENVY_CACHE_ROOT%"
) else if defined MANIFEST_CACHE (
    set "CACHE=%MANIFEST_CACHE%"
    if "!CACHE:~0,1!"=="~" set "CACHE=%USERPROFILE%!CACHE:~1!"
) else (
    set "CACHE=%LOCALAPPDATA%\envy"
)

set "ENVY_BIN=%CACHE%\envy\%VERSION%\envy.exe"
if exist "%ENVY_BIN%" goto :run

echo Downloading envy %VERSION%... >&2
set "URL=%ENVY_MIRROR%/v%VERSION%/envy-windows-x86_64.zip"
set "TEMP_DIR=%TEMP%\envy-%VERSION%-%RANDOM%"
set "TEMP_ZIP=%TEMP_DIR%.zip"
powershell -NoProfile -Command "Invoke-WebRequest -Uri '%URL%' -OutFile '%TEMP_ZIP%' -UseBasicParsing"
if errorlevel 1 (echo ERROR: Failed to download envy from %URL% >&2 & del "%TEMP_ZIP%" 2>nul & exit /b 1)
powershell -NoProfile -Command "Expand-Archive -Path '%TEMP_ZIP%' -DestinationPath '%TEMP_DIR%' -Force"
if errorlevel 1 (echo ERROR: Failed to extract envy >&2 & del "%TEMP_ZIP%" 2>nul & exit /b 1)
"%TEMP_DIR%\envy.exe" %*
exit /b %ERRORLEVEL%

:run
"%ENVY_BIN%" %*
