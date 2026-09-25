# 虚拟摄像头注册/反注册（需管理员运行一次）。
# DirectShow 设备枚举只读 HKLM（ICreateDevEnum），注册必须写入 HKLM。
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts/register_vcam.ps1          # 注册
#   powershell -ExecutionPolicy Bypass -File scripts/register_vcam.ps1 -Unregister  # 卸载
param(
    [switch]$Unregister
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root "build\ui\lb_vcam_filter.dll"

if (-not (Test-Path $dll)) {
    $dll = Join-Path $root "build\platform\windows\lb_vcam_filter.dll"
}
if (-not (Test-Path $dll)) { throw "lb_vcam_filter.dll not found; run scripts/build.ps1 first" }

# 检查是否管理员
$isAdmin = ([Security.Principal.WindowsPrincipal]
    [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Error "需要以管理员身份运行（注册写入 HKLM）。请右键 PowerShell 选择"以管理员身份运行"。"
    exit 1
}

if ($Unregister) {
    & "$env:WINDIR\System32\regsvr32.exe" /s /u $dll
} else {
    & "$env:WINDIR\System32\regsvr32.exe" /s $dll
}
if ($LASTEXITCODE -ne 0) { throw "regsvr32 failed" }
Write-Output ("虚拟摄像头已" + $(if ($Unregister) { "卸载" } else { "注册" }) + ": $dll")
Write-Output "验证: ffmpeg -hide_banner -list_devices true -f dshow -i dummy"