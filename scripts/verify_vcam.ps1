# 虚拟摄像头 ffmpeg 验收：
#   powershell -ExecutionPolicy Bypass -File scripts/verify_vcam.ps1            # 单帧
#   powershell -ExecutionPolicy Bypass -File scripts/verify_vcam.ps1 -Frames    # 连续 3 秒 / 30 帧
# 步骤：枚举确认设备可见 -> 拉流出 PNG -> 校验格式与画面非空。
# 注册由 filter 自身完成（scripts/register_vcam.ps1）；现代 Windows 读
# CLSID\{VideoInputCategory}\Instance\{clsid}，HKCU 写入即可，无需管理员。
param(
    [switch]$Frames
)
$ErrorActionPreference = 'Stop'

$ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ffmpeg) { throw 'ffmpeg 不在 PATH（winget install Gyan.FFmpeg）' }
$device = 'LiveBridge Virtual Camera'

# ffmpeg 会向 stderr 写正常信息（设备列表、进度等）。PowerShell 会把这些当成
# NativeCommandError；这里临时降级 ErrorActionPreference，避免脚本被误终止。
function Invoke-Ffmpeg {
    param([string[]]$FfArgs)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { return (& $ffmpeg @FfArgs 2>&1 | Out-String) }
    finally { $ErrorActionPreference = $prev }
}

Write-Output '== 1) 枚举视频设备 =='
$enum = Invoke-Ffmpeg @('-hide_banner', '-list_devices', 'true', '-f', 'dshow', '-i', 'dummy')
if ($enum -match [regex]::Escape($device)) {
    Write-Output "  设备可见: $device"
} else {
    Write-Output '  设备不可见。请先运行 scripts/register_vcam.ps1 完成注册。'
    exit 1
}

Write-Output '== 2) 拉流验证 =='
$outDir = Join-Path $env:TEMP 'lb_vcam_verify'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$png = Join-Path $outDir 'frame.png'
Remove-Item $png -ErrorAction SilentlyContinue
$log = Invoke-Ffmpeg @('-hide_banner', '-f', 'dshow', '-i', "video=$device",
                       '-frames:v', '1', '-update', '1', $png, '-y')
if (-not (Test-Path $png)) {
    Write-Output '  VERIFY FAIL: 未能拉出画面'
    Write-Output $log
    exit 1
}
$size = (Get-Item $png).Length
if ($log -match 'Video: rawvideo \((\w+) / 0x[0-9A-F]+\), (\S+), (\d+)x(\d+)') {
    Write-Output ("  格式: {0} ({1}) {2}x{3}" -f $Matches[1], $Matches[2], $Matches[3], $Matches[4])
}
Write-Output "  输出: $png ($size 字节)"
if ($size -lt 200) {
    Write-Output '  VERIFY FAIL: 画面疑似空白'
    exit 1
}
Write-Output '  画面内容: 非空'

if ($Frames) {
    Write-Output '== 3) 连续拉流（3 秒 / 约 30 帧）=='
    Get-ChildItem (Join-Path $outDir 'f_*.png') -ErrorAction SilentlyContinue | Remove-Item -Force
    $seq = Join-Path $outDir 'f_%03d.png'
    Invoke-Ffmpeg @('-hide_banner', '-f', 'dshow', '-i', "video=$device",
                    '-t', '3', '-r', '10', $seq) | Out-Null
    $count = (Get-ChildItem (Join-Path $outDir 'f_*.png') -ErrorAction SilentlyContinue).Count
    Write-Output "  输出帧数: $count"
    if ($count -lt 10) { Write-Output '  VERIFY FAIL: 帧数不足'; exit 1 }
}

Write-Output 'VERIFY OK'
