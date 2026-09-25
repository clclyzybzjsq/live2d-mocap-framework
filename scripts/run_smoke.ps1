# Pipeline smoke runner. Usage:
#   scripts/run_smoke.ps1 [-Device synthetic|camera] [-Frames 150] [-Width 640] [-Height 480]
# Sets the runtime PATH (mingw-msvcrt first, then Qt) and LB_MODEL_DIR so the exe
# and the tracker sidecar resolve correctly. Requires build\lb_smoke.exe
# (configure with -DLB_BUILD_SMOKE=ON; see docs/assessment-plan.md 10.1).
#
# Note: use fixed toolchain paths - file-probing cmdlets (Test-Path/Get-ChildItem)
# are unreliable inside .ps1 execution on this machine.
param(
    [string]$Device = "synthetic",
    [int]$Frames = 150,
    [int]$Width = 640,
    [int]$Height = 480
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# Runtime library order matters: GCC16 msvcrt toolchain first, then Qt GCC13.
$env:PATH = "D:\deps\mingw_msvcrt\mingw64\bin;D:\deps\qt\6.8.3\mingw_64\bin;$env:PATH"
$env:LB_MODEL_DIR = Join-Path $root "inference\models"

& (Join-Path $root "build\lb_smoke.exe") --device $Device --frames $Frames --width $Width --height $Height
Write-Output "smoke_exit=$LASTEXITCODE"