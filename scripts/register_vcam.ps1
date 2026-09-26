# 虚拟摄像头注册/反注册。
# DirectShow 设备枚举（ICreateDevEnum）只读 HKLM，因此注册必须写入 HKLM，需要一次管理员授权。
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts/register_vcam.ps1              # 注册（非管理员会自动请求提权）
#   powershell -ExecutionPolicy Bypass -File scripts/register_vcam.ps1 -Unregister  # 卸载
param(
    [switch]$Unregister,
    [switch]$NoElevate
)

$ErrorActionPreference = 'Stop'
$clsid = '{2A17E0B2-2A8C-4E4A-9E5C-8B4A6D2F1C55}'
$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root 'build\ui\lb_vcam_filter.dll'
if (-not (Test-Path $dll)) {
    $dll = Join-Path $root 'build\platform\windows\lb_vcam_filter.dll'
}
if (-not (Test-Path $dll)) { throw 'lb_vcam_filter.dll 不存在，请先运行 scripts/build.ps1' }

# 管理员检查。注意不能用跨行的 [TypeA][TypeB]::Method() 写法：
# PowerShell 会把换行后的 [TypeB] 当成数组索引，直接解析失败。
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    if ($NoElevate) { throw '需要管理员权限（注册写入 HKLM）' }
    Write-Output '需要管理员权限，正在请求提权：请在弹出的 UAC 对话框中选择“是”'
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-NoElevate')
    if ($Unregister) { $argList += '-Unregister' }
    $proc = Start-Process -FilePath 'powershell.exe' -ArgumentList $argList -Verb RunAs -Wait -PassThru
    if ($proc.ExitCode -eq 0) { exit 0 }
    throw "提权运行失败（exit=$($proc.ExitCode)）；若 UAC 被取消，请手动以管理员身份运行本脚本"
}

$regsvr = Join-Path $env:WINDIR 'System32\regsvr32.exe'
if ($Unregister) {
    & $regsvr /s /u $dll
} else {
    & $regsvr /s $dll
}
if ($LASTEXITCODE -ne 0) { throw "regsvr32 执行失败（exit=$LASTEXITCODE）" }

# 结果核验：CLSID 的 InprocServer32 与 DirectShow 设备类别键都必须存在
$key = "HKLM:\Software\Classes\CLSID\$clsid\InprocServer32"
$cat = "HKLM:\Software\Classes\MediaCategories\{860BB310-5D01-11D0-BD3B-00A0C911CE86}\$clsid"
if ($Unregister) {
    if (Test-Path $key) { Write-Output '注意：注册表项仍存在，卸载可能不完整' }
    else { Write-Output '虚拟摄像头已卸载' }
} else {
    if (-not (Test-Path $key)) { throw '注册后未找到 InprocServer32 键，注册未生效' }
    $path = (Get-ItemProperty $key).'(default)'
    if ($path -ne $dll) { Write-Output "注意：注册路径为 $path" }
    Write-Output "虚拟摄像头已注册: $path"
    if (Test-Path $cat) { Write-Output '设备类别项: OK' }
    else { Write-Output '设备类别项: 缺失（设备枚举可能失败）' }
    Write-Output '验证命令: ffmpeg -hide_banner -list_devices true -f dshow -i dummy'
}
