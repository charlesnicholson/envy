# envy shell hook v1 — managed by envy; do not edit
$global:_ENVY_HOOK_VERSION = 1

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
                    if ($global:_ENVY_BIN_DIR) {
                        $parts = $env:PATH -split [regex]::Escape($sep)
                        $parts = $parts | Where-Object { $_ -ne $global:_ENVY_BIN_DIR }
                        $env:PATH = $parts -join $sep
                    }
                    $env:PATH = "$binDir$sep$env:PATH"
                    $global:_ENVY_BIN_DIR = $binDir
                }
                $env:ENVY_PROJECT_ROOT = $manifestDir
                return
            }
        }
    }

    # Left all projects or no bin — clean up
    if ($global:_ENVY_BIN_DIR) {
        $parts = $env:PATH -split [regex]::Escape($sep)
        $parts = $parts | Where-Object { $_ -ne $global:_ENVY_BIN_DIR }
        $env:PATH = $parts -join $sep
        $global:_ENVY_BIN_DIR = $null
    }
    Remove-Item Env:\ENVY_PROJECT_ROOT -ErrorAction SilentlyContinue
}

# Wrap prompt to call hook on every prompt render
$global:_ENVY_LAST_PWD = $null
$global:_ENVY_BIN_DIR = $null

if (-not (Test-Path Function:\global:_envy_original_prompt)) {
    Copy-Item Function:\prompt Function:\global:_envy_original_prompt
    function global:prompt {
        _envy_hook
        _envy_original_prompt
    }
}

# Activate for current directory
_envy_hook
