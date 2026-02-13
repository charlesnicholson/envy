# envy shell hook — managed by envy; do not edit
$global:_ENVY_HOOK_VERSION = 2

# Detect UTF-8 locale for emoji/unicode output
$global:_ENVY_UTF8 = (($env:LC_ALL + $env:LC_CTYPE + $env:LANG) -match '[Uu][Tt][Ff]-?8') -or
    (try { [Console]::OutputEncoding.WebName -eq 'utf-8' } catch { $false })
$global:_ENVY_DASH = if ($global:_ENVY_UTF8) { "`u{2014}" } else { "--" }

function _envy_find_manifest {
    $d = (Get-Location).Path
    while ($d -ne [System.IO.Path]::GetPathRoot($d)) {
        $manifest = Join-Path $d "envy.lua"
        if (Test-Path $manifest -PathType Leaf) {
            $isRoot = $true
            $lines = Get-Content $manifest -TotalCount 20
            foreach ($line in $lines) {
                if ($line -match '^\s*--\s*@envy\s+root\s+"false"') {
                    $isRoot = $false
                    break
                }
            }
            if ($isRoot) { return $d }
        }
        $d = Split-Path $d -Parent
        if (-not $d) { break }
    }
    return $null
}

function _envy_parse_bin($manifestDir) {
    $manifest = Join-Path $manifestDir "envy.lua"
    $lines = Get-Content $manifest -TotalCount 20
    foreach ($line in $lines) {
        if ($line -match '^\s*--\s*@envy\s+bin\s+"([^"\\]*(?:\\.[^"\\]*)*)"') {
            return $Matches[1]
        }
    }
    return $null
}

function _envy_hook {
    if ($env:ENVY_SHELL_HOOK_DISABLE -eq "1") { return }

    $currentDir = (Get-Location).Path
    if ($currentDir -eq $global:_ENVY_LAST_PWD) { return }
    $global:_ENVY_LAST_PWD = $currentDir

    $sep = [System.IO.Path]::PathSeparator

    $manifestDir = _envy_find_manifest
    if ($manifestDir) {
        $binVal = _envy_parse_bin $manifestDir
        if ($binVal) {
            $binDir = Join-Path $manifestDir $binVal
            if (Test-Path $binDir -PathType Container) {
                $binDir = (Resolve-Path $binDir).Path
                if ($binDir -ne $global:_ENVY_BIN_DIR) {
                    # Leaving old project (switching)?
                    if ($global:_ENVY_BIN_DIR) {
                        $oldName = Split-Path $env:ENVY_PROJECT_ROOT -Leaf
                        Write-Host "envy: leaving $oldName $($global:_ENVY_DASH) PATH restored" -ForegroundColor DarkGray
                        $parts = $env:PATH -split [regex]::Escape($sep)
                        $parts = $parts | Where-Object { $_ -ne $global:_ENVY_BIN_DIR }
                        $env:PATH = $parts -join $sep
                    }
                    $env:PATH = "$binDir$sep$env:PATH"
                    $global:_ENVY_BIN_DIR = $binDir
                    $newName = Split-Path $manifestDir -Leaf
                    Write-Host "envy: entering $newName $($global:_ENVY_DASH) tools added to PATH" -ForegroundColor DarkGray
                    $global:_ENVY_PROMPT_ACTIVE = $true
                }
                $env:ENVY_PROJECT_ROOT = $manifestDir
                return
            }
        }
    }

    # Left all projects or no bin — clean up
    if ($global:_ENVY_BIN_DIR) {
        $oldName = Split-Path $env:ENVY_PROJECT_ROOT -Leaf
        Write-Host "envy: leaving $oldName $($global:_ENVY_DASH) PATH restored" -ForegroundColor DarkGray
        $parts = $env:PATH -split [regex]::Escape($sep)
        $parts = $parts | Where-Object { $_ -ne $global:_ENVY_BIN_DIR }
        $env:PATH = $parts -join $sep
        $global:_ENVY_BIN_DIR = $null
        $global:_ENVY_PROMPT_ACTIVE = $false
    }
    Remove-Item Env:\ENVY_PROJECT_ROOT -ErrorAction SilentlyContinue
}

# Wrap prompt to call hook on every prompt render
$global:_ENVY_LAST_PWD = $null
$global:_ENVY_BIN_DIR = $null
$global:_ENVY_PROMPT_ACTIVE = $false

if (-not (Test-Path Function:\global:_envy_original_prompt)) {
    Copy-Item Function:\prompt Function:\global:_envy_original_prompt
    function global:prompt {
        _envy_hook
        if ($global:_ENVY_PROMPT_ACTIVE -and $global:_ENVY_UTF8 -and $env:ENVY_NO_PROMPT -ne "1") {
            Write-Host "`u{1F99D} " -NoNewline
        }
        _envy_original_prompt
    }
}

# Activate for current directory
_envy_hook
