# 虚拟摄像头 ffmpeg 验收（需先完成 HKLM 注册）：
#   powershell -ExecutionPolicy Bypass -File scripts/verify_vcam.ps1
# 步骤：枚举确认设备可见 -> 拉流 1 帧出 PNG（无应用喂帧时显示彩条）。
param(
    [switch]$Frames   # 拉 30 帧验证长时间流
)
$ErrorActionPreference = 'Stop'
$ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ffmpeg) { throw "ffmpeg 不在 PATH（winget install Gyan.FFmpeg）" }

Write-Output "== 1) 枚举视频设备 =="
$enum = & $ffmpeg -hide_banner -list_devices true -f dshow -i dummy 2>&1 | Out-String
if ($enum -match "LiveBridge Virtual Camera") {
    Write-Output "设备可见: LiveBridge Virtual Camera"
} else {
    Write-Output "设备不可见。请先注册（scripts/register_vcam.ps1 需管理员）"
    exit 1
}

Write-Output "== 2) 拉流验证 =="
$outDir = Join-Path $env:TEMP "lb_vcam_verify"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
if ($Frames) {
    $png = Join-Path $outDir "frame_%03d.png"
    & $ffmpeg -hide_banner -f dshow -i "video=LiveBridge Virtual Camera" -t 3 -r 10 $png 2>&1 | Select-Object -Last 3
    $count = (Get-ChildItem (Join-Path $outDir "frame_*.png") -ErrorAction SilentlyContinue).Count
    Write-Output "输出帧数: $count (目录 $outDir)"
    if ($count -ge 10) { Write-Output "VERIFY OK" } else { Write-Output "VERIFY FAIL" }
} else {
    $png = Join-Path $outDir "frame_001.png"
    & $ffmpeg -hide_banner -f dshow -i "video=LiveBridge Virtual Camera" -frames:v 1 $png 2>&1 | Select-Object -Last 3
    if (Test-Path $png) {
        Write-Output "输出: $png"
        Write-Output "VERIFY OK"
    } else {
        Write-Output "VERIFY FAIL: 未生成画面"
    }
}