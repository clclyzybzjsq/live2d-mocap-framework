# Build script: configure and compile the Windows project skeleton.
# Requires CMake, Ninja, MinGW-w64 (UCRT) and Qt6 mingw build -- see docs/assessment-plan.md.
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

# ---- locate toolchain (all under D:\deps and user dirs, no admin) ----
# Qt 官方 mingw 构建基于 msvcrt；为消除跨 CRT 堆冲突（0xC0000139 / 堆崩溃），
# 优先使用 msvcrt 版工具链（D:\deps\mingw_msvcrt），其次 UCRT 版。
$mingwCandidates = @("D:\deps\mingw_msvcrt", "D:\deps\mingw64")
$gxx = $null
foreach ($c in $mingwCandidates) {
    foreach ($sub in @("$c\mingw64\bin\g++.exe", "$c\bin\g++.exe", "$c\mingw32\bin\g++.exe")) {
        if (Test-Path $sub) { $gxx = $sub; break }
    }
    if ($gxx) { break }
}
if (-not $gxx) { throw "MinGW not found ($($mingwCandidates -join ', ')) (run scripts/bootstrap_toolchain.ps1 first)" }
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
# 运行时库顺序：mingw (GCC16) 必须在 Qt (GCC13) 之前。
# 否则加载器取到 Qt bin 里的旧版 libgcc/libstdc++/libwinpthread，
# 与 GCC16 编译产物不匹配，进程启动即 0xC0000139。
$qtBin = Split-Path $qt
$env:PATH = "$mingwBin;$qtBin;$ninjaDir;$env:PATH"

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

# ---- deploy runtime next to the UI exe ----
# 让 build\ui\lb_ui.exe 可以脱离 PATH 直接双击运行：Qt6*.dll + 平台插件 +
# MinGW 运行库（libgcc/libstdc++/libwinpthread）全部随包。
# 缺这一步时双击会报“找不到部分 DLL 运行库”。
$uiExe = "$root\build\ui\lb_ui.exe"
$deployQt = Join-Path $qtBin "windeployqt.exe"
if ((Test-Path $uiExe) -and (Test-Path $deployQt)) {
    & $deployQt --no-translations --compiler-runtime --no-system-d3d-compiler --no-opengl-sw $uiExe | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }
}

Write-Output "Build OK: $uiExe (Qt runtime deployed)"