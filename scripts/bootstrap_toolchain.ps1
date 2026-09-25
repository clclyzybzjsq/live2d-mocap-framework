# One-shot toolchain bootstrap (no admin required, everything on D:\deps and user dirs).
# Installs: CMake, Ninja, 7-Zip (winget), MinGW-w64 (winlibs), Qt6 mingw (aqt),
#           Python venv + mediapipe, MediaPipe task models.
$ErrorActionPreference = 'Stop'

Write-Output "==> 1/6 winget base tools (cmake, ninja, 7zip)"
winget install -e --id Kitware.CMake --accept-source-agreements --accept-package-agreements --disable-interactivity
winget install -e --id Ninja-build.Ninja --accept-source-agreements --accept-package-agreements --disable-interactivity
winget install -e --id 7zip.7zip --accept-source-agreements --accept-package-agreements --disable-interactivity

Write-Output "==> 2/6 aqtinstall (Qt downloader)"
python -m pip install --quiet aqtinstall

Write-Output "==> 3/6 MinGW-w64 (latest winlibs UCRT POSIX release)"
$rel = Invoke-RestMethod -Uri "https://api.github.com/repos/brechtsanders/winlibs_mingw/releases/latest" -Headers @{ 'User-Agent' = 'dsh-bootstrap' }
$asset = $rel.assets | Where-Object { $_.name -match '^winlibs-x86_64-posix-seh-gcc-' -and $_.name -match 'ucrt' -and $_.name -notmatch 'llvm' -and $_.name -match '\.7z$' } | Select-Object -First 1
$zip = "D:\deps\$($asset.name)"
if (-not (Test-Path $zip)) {
    curl.exe -L --retry 3 --connect-timeout 30 -o $zip $asset.browser_download_url
}
& D:\deps\7zr.exe x $zip "-oD:\deps\mingw64" -y | Select-Object -Last 1

Write-Output "==> 4/6 Qt 6.8.3 (win64_mingw)"
python -m aqt install-qt windows desktop 6.8.3 win64_mingw -O D:\deps\qt

Write-Output "==> 5/6 Python venv + mediapipe"
python -m venv D:\deps\venv
& D:\deps\venv\Scripts\python.exe -m pip install --quiet --upgrade pip
& D:\deps\venv\Scripts\python.exe -m pip install --quiet mediapipe

Write-Output "==> 6/6 MediaPipe task models"
Set-ExecutionPolicy -Scope Process Bypass -Force
& (Join-Path $PSScriptRoot "..\inference\fetch_models.ps1")

Write-Output "Done. Now run scripts\build.ps1"