# Build script: configure and compile the Windows project skeleton.
# Requires CMake, Ninja, MinGW-w64 (UCRT) and Qt6 mingw build -- see docs/assessment-plan.md.
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

# ---- locate toolchain (all under D:\deps and user dirs, no admin) ----
$mingwRoot = "D:\deps\mingw64"
$gxx = Get-ChildItem $mingwRoot -Recurse -Filter g++.exe -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $gxx) { throw "MinGW not found: $mingwRoot (run scripts/bootstrap_toolchain.ps1 first)" }
$mingwBin = Split-Path $gxx

$qt = Get-ChildItem D:\deps\qt -Recurse -Filter qmake.exe -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match 'mingw_64' } | Select-Object -First 1 -ExpandProperty FullName
if (-not $qt) { throw "Qt mingw build not found under D:\deps\qt (run scripts/bootstrap_toolchain.ps1 first)" }
$qtRoot = Split-Path (Split-Path $qt)

$cmake = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter cmake.exe -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $cmake) {
    # user Python env (pip-installed) or PATH fallback
    $cmake = Get-ChildItem "$env:LOCALAPPDATA" -Recurse -Filter cmake.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match 'Scripts|CMake' } | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $cmake) { $cmake = (Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source) }
if (-not $cmake) { throw "cmake not found (run scripts/bootstrap_toolchain.ps1 first)" }

$ninja = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter ninja.exe -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $ninja) { $ninja = (Get-Command ninja -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source) }
if (-not $ninja) { throw "ninja not found (run scripts/bootstrap_toolchain.ps1 first)" }
$ninjaDir = Split-Path $ninja

# ---- build ----
$env:PATH = "$mingwBin;$ninjaDir;$env:PATH"

if (-not (Test-Path "$root\build\CMakeCache.txt")) {
    & $cmake -S $root -B "$root\build" -G Ninja `
        -DCMAKE_BUILD_TYPE=Debug `
        -DCMAKE_CXX_COMPILER="$gxx" `
        -DCMAKE_PREFIX_PATH="$qtRoot" `
        -DLB_BUILD_UI=ON
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
}

& $cmake --build "$root\build"
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Output "Build OK: $root\build\ui\lb_ui.exe"