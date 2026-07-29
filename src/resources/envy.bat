@echo off
REM envy-managed bootstrap script - do not edit
setlocal EnableDelayedExpansion

set "DEFAULT_MIRROR=@@DOWNLOAD_URL@@"
set "LATEST_URL=@@LATEST_URL@@"
set "ENV_MIRROR="
if defined ENVY_MIRROR set "ENV_MIRROR=%ENVY_MIRROR%"
set "FALLBACK_VERSION=@@ENVY_VERSION@@"

set "MANIFEST="
set "CANDIDATE="
set "DIR=%~dp0"
if "!DIR:~-1!"=="\" set "DIR=!DIR:~0,-1!"
:findloop
if exist "!DIR!\envy.lua" (
    set "IS_ROOT=true"
    for /f "usebackq tokens=1,2,3,4 delims= " %%a in ("!DIR!\envy.lua") do (
        if "%%a"=="--" if "%%b"=="@envy" if "%%c"=="root" (
            set "VAL=%%d"
            set "VAL=!VAL:"=!"
            if "!VAL!"=="false" set "IS_ROOT=false"
        )
    )
    if "!IS_ROOT!"=="true" (
        set "MANIFEST=!DIR!\envy.lua"
        goto :found
    ) else (
        set "CANDIDATE=!DIR!\envy.lua"
    )
)
for %%I in ("!DIR!\..") do set "PARENT=%%~fI"
if "!PARENT!"=="!DIR!" (
    if defined CANDIDATE (
        set "MANIFEST=!CANDIDATE!"
        goto :found
    )
    echo ERROR: envy.lua not found >&2 & exit /b 1
)
set "DIR=!PARENT!"
goto :findloop
:found

set "VERSION="
set "MANIFEST_CACHE="
set "MANIFEST_MIRROR="
set /a LINE_COUNT=0

for /f "usebackq tokens=1,2,3,* delims= " %%a in ("!MANIFEST!") do (
    set /a LINE_COUNT+=1
    if !LINE_COUNT! GTR 20 goto :done_parse
    if "%%a"=="--" if "%%b"=="@envy" (
        set "KEY=%%c"
        set "VAL=%%d"
        if defined VAL (
            set "VAL=!VAL:~1,-1!"
            set "VAL=!VAL:\"="!"
            set "VAL=!VAL:\\=\!"
            if "!KEY!"=="version" set "VERSION=!VAL!"
            if "!KEY!"=="cache-win" set "MANIFEST_CACHE=!VAL!"
            if "!KEY!"=="mirror" set "MANIFEST_MIRROR=!VAL!"
        )
    )
)
:done_parse

REM Precedence: ENVY_MIRROR env > @envy mirror directive > envy upstream. Byte-identical to
REM the runtime resolver (src/reexec.cpp), including the last tier: DEFAULT_MIRROR is always
REM envy's own release URL, never a copy of this project's mirror. Stamping the project's
REM mirror here used to make deleting the directive resolve the script to the stale custom
REM mirror while the re-exec'd binary went to upstream -- two binaries, one project.
if defined ENV_MIRROR (
    set "ENVY_MIRROR=!ENV_MIRROR!"
) else if defined MANIFEST_MIRROR (
    set "ENVY_MIRROR=!MANIFEST_MIRROR!"
) else (
    set "ENVY_MIRROR=!DEFAULT_MIRROR!"
)

REM A trailing slash would produce ".../releases//v1.2.3/...". For s3:// that is a distinct
REM key that does not exist, since S3 keys are opaque byte strings.
:striptrail
if "!ENVY_MIRROR:~-1!"=="/" (
    set "ENVY_MIRROR=!ENVY_MIRROR:~0,-1!"
    goto :striptrail
)

set "MIRROR_IS_S3="
if /i "!ENVY_MIRROR:~0,5!"=="s3://" set "MIRROR_IS_S3=1"

REM Probe bare `aws`, not `aws.exe`: AWS CLI v2 installs aws.exe but PATHEXT also resolves
REM aws.cmd/aws.bat shims, and the functional test's mock is a .bat. This deliberately
REM diverges from the curl.exe/tar.exe probes above, which name the exe to be policy-proof.
if not defined MIRROR_IS_S3 goto :mirror_ok
where /q aws && goto :mirror_ok
echo ERROR: mirror "!ENVY_MIRROR!" is an s3:// URI but the aws CLI was not found on PATH. >&2
echo        Install AWS CLI v2, or use an https:// mirror. >&2
exit /b 1
:mirror_ok

if defined ENVY_CACHE_ROOT (
    set "CACHE=!ENVY_CACHE_ROOT!"
) else if defined MANIFEST_CACHE (
    set "CACHE=!MANIFEST_CACHE!"
    if "!CACHE:~0,1!"=="~" set "CACHE=!USERPROFILE!!CACHE:~1!"
) else (
    set "CACHE=!LOCALAPPDATA!\envy"
)

if "!VERSION!"=="" (
    set "LATEST_FILE=!CACHE!\envy\latest"
    if exist "!LATEST_FILE!" (
        set /p LATEST_VER=<"!LATEST_FILE!"
        if defined LATEST_VER (
            if exist "!CACHE!\envy\!LATEST_VER!\envy.exe" set "VERSION=!LATEST_VER!"
        )
    )
)
if not "!VERSION!"=="" goto :version_resolved

REM Ask the mirror first: 'envy mirror-envy' writes a `latest` file at the mirror root, so a
REM private or air-gapped mirror answers for itself -- ENVY_MIRROR or `@envy mirror` may
REM point anywhere, so this is not necessarily github.
set "LATEST_TMP=!TEMP!\envy-latest-%RANDOM%%RANDOM%.txt"
set "GOT="
if defined MIRROR_IS_S3 (
    call aws s3 cp --only-show-errors "!ENVY_MIRROR!/latest" "!LATEST_TMP!" >nul 2>&1 && set "GOT=1"
) else (
    where /q curl.exe && (curl.exe -fsSL --connect-timeout 10 --max-time 300 "!ENVY_MIRROR!/latest" -o "!LATEST_TMP!" >nul 2>&1 && set "GOT=1")
)
if not defined GOT goto :latest_cleanup
REM Trim via an unquoted-set for /f (a literal string here, not a filename -- no usebackq),
REM staging through RAW so a whitespace-only file leaves VERSION empty rather than blank.
set "RAW_VERSION="
set /p RAW_VERSION=<"!LATEST_TMP!"
for /f "tokens=1" %%v in ("!RAW_VERSION!") do set "VERSION=%%v"
:latest_cleanup
del "!LATEST_TMP!" 2>nul
if not "!VERSION!"=="" goto :version_resolved

REM GitHub releases serves no `latest` object, so fall back to its redirect. Skipped for
REM s3:// mirrors, which are never github and must not reach out to it.
if defined MIRROR_IS_S3 goto :version_fallback

REM Prefer native curl.exe (policy-resistant); parse the redirect's trailing tag. Timeouts
REM matter: an unbounded connect stalls for the OS TCP timeout on a blackholed network.
set "REDIR="
set "TAG="
where /q curl.exe && for /f "usebackq tokens=*" %%u in (`curl.exe -fsS -o nul -w "%%{redirect_url}" --connect-timeout 5 --max-time 15 "!LATEST_URL!" 2^>nul`) do set "REDIR=%%u"
if defined REDIR set "REDIR=!REDIR:/=\!"
if defined REDIR for %%a in ("!REDIR!") do set "TAG=%%~nxa"
if defined TAG set "VERSION=!TAG!"
if defined TAG if "!TAG:~0,1!"=="v" set "VERSION=!TAG:~1!"
if "!VERSION!"=="" (
    for /f "tokens=*" %%u in ('powershell -NoProfile -Command "$ProgressPreference='SilentlyContinue'; try { $r=[System.Net.WebRequest]::Create('!LATEST_URL!'); $r.AllowAutoRedirect=$false; $h=$r.GetResponse().Headers['Location']; if($h){($h -split '/')[-1] -replace '^v',''} } catch {}" 2^>nul') do set "VERSION=%%u"
)

:version_fallback
if "!VERSION!"=="" set "VERSION=!FALLBACK_VERSION!"
:version_resolved

set "ENVY_BIN=!CACHE!\envy\!VERSION!\envy.exe"
if exist "!ENVY_BIN!" goto :run

set "ARCH=x86_64"
reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v PROCESSOR_ARCHITECTURE 2>nul | findstr /i "ARM64" >nul 2>&1 && set "ARCH=arm64"

echo Downloading envy !VERSION!... >&2
set "URL=!ENVY_MIRROR!/v!VERSION!/envy-windows-!ARCH!.zip"
REM Escape single quotes for PowerShell (replace ' with '')
set "SAFE_URL=!URL:'=''!"
REM Claim a unique temp dir via atomic mkdir (cmd's %RANDOM% can collide across
REM concurrent bootstraps; mkdir succeeds for exactly one owner of a given name).
set /a TEMP_TRIES=0
:mktemp
set "TEMP_DIR=!TEMP!\envy-%RANDOM%%RANDOM%"
mkdir "!TEMP_DIR!" 2>nul && goto :gottemp
set /a TEMP_TRIES+=1
if !TEMP_TRIES! LSS 10 goto :mktemp
echo ERROR: Could not create a temp directory under !TEMP! >&2 & exit /b 1
:gottemp
set "TEMP_ZIP=!TEMP_DIR!.zip"

REM Download: prefer native curl.exe (policy-resistant), fall back to PowerShell.
set "OK="
if defined MIRROR_IS_S3 goto :dl_s3
goto :dl_http

:dl_s3
REM `call` so an aws resolved to a .bat/.cmd shim returns control here instead of
REM transferring it, and so ERRORLEVEL survives. To a file, never piped into tar: cmd takes
REM ERRORLEVEL from the right side of a pipe only, and tar exits 0 on empty input, so a
REM failed download piped into tar would look like success.
call aws s3 cp --only-show-errors "!URL!" "!TEMP_ZIP!" && set "OK=1"
goto :dl_done

:dl_http
where /q curl.exe && (curl.exe -fsSL "!URL!" -o "!TEMP_ZIP!" && set "OK=1")
if not defined OK (
    powershell -NoProfile -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri '!SAFE_URL!' -OutFile '!TEMP_ZIP!' -UseBasicParsing" && set "OK=1"
)

:dl_done
if not defined OK (echo ERROR: Failed to download envy from !URL! >&2 & rmdir /s /q "!TEMP_DIR!" 2>nul & del "!TEMP_ZIP!" 2>nul & exit /b 1)

REM Extract: prefer native tar.exe (bsdtar reads zip), fall back to PowerShell Expand-Archive.
set "OK="
where /q tar.exe && (tar.exe -xf "!TEMP_ZIP!" -C "!TEMP_DIR!" && set "OK=1")
if not defined OK (
    powershell -NoProfile -Command "$ProgressPreference='SilentlyContinue'; Expand-Archive -Path '!TEMP_ZIP!' -DestinationPath '!TEMP_DIR!' -Force" && set "OK=1"
)
if not defined OK (echo ERROR: Failed to extract envy >&2 & rmdir /s /q "!TEMP_DIR!" 2>nul & del "!TEMP_ZIP!" 2>nul & exit /b 1)
del "!TEMP_ZIP!" 2>nul
REM tar succeeds on an empty archive, so a zero-length object would otherwise fall through
REM to :run and report a missing path instead of a failed download.
if not exist "!TEMP_DIR!\envy.exe" (echo ERROR: archive from !URL! contained no envy binary >&2 & rmdir /s /q "!TEMP_DIR!" 2>nul & exit /b 1)
set "ENVY_BIN=!TEMP_DIR!\envy.exe"

REM envy sync may rewrite this script; single line ensures cmd.exe never reads past here.
:run
"!ENVY_BIN!" %* & exit /b !ERRORLEVEL!
