# 下载 MediaPipe task 模型文件到 inference/models。
# 模型托管在 mediapipe-models 桶（ai.google.dev 文档地址），旧 mediapipe-assets 桶已停用部分文件。
$ErrorActionPreference = 'Stop'
$dir = Join-Path $PSScriptRoot "models"
New-Item -ItemType Directory -Force $dir | Out-Null

$files = @(
    @{ name = "face_landmarker.task"; url = "https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task" },
    @{ name = "pose_landmarker_full.task"; url = "https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_full/float16/1/pose_landmarker_full.task" },
    @{ name = "pose_landmarker_lite.task"; url = "https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_lite/float16/1/pose_landmarker_lite.task" }
)

foreach ($f in $files) {
    $out = Join-Path $dir $f.name
    if ((Test-Path $out) -and (Get-Item $out).Length -gt 100000) {
        Write-Output "已存在: $out"
        continue
    }
    curl.exe -L --retry 3 --connect-timeout 30 -o $out $f.url
    Write-Output "下载完成: $out ($([math]::Round((Get-Item $out).Length/1MB,1)) MB)"
}