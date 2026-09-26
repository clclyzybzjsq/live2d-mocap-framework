# 为 MinGW 生成 Live2D Cubism Core 的导入库。
#
# 背景：Cubism Core 官方只提供 MSVC 格式的 .lib（静态库与导入库都是 MSVC 格式），
# MinGW 无法直接链接。这里用 gendef + dlltool 从官方 dll 生成 MinGW 可用的
# 导入库（libLive2DCubismCore.a），运行时仍加载官方 Live2DCubismCore.dll。
#
# 产物: <build>/cubism_link/libLive2DCubismCore.a
# 用法: powershell -ExecutionPolicy Bypass -File scripts/build_cubism_core_lib.ps1
param(
    [string]$SdkDir = 'D:\deps\cubism',
    [string]$BuildDir = ''
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $root 'build' }

$dll = Join-Path $SdkDir 'Core\dll\windows\x86_64\Live2DCubismCore.dll'
if (-not (Test-Path $dll)) { throw "未找到 $dll（请确认 Cubism SDK 已解压到 $SdkDir）" }

$outDir = Join-Path $BuildDir 'cubism_link'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$def = Join-Path $outDir 'Live2DCubismCore.def'
$implib = Join-Path $outDir 'libLive2DCubismCore.a'

# MinGW 工具链（与主构建一致）
$mingwBin = 'D:\deps\mingw_msvcrt\mingw64\bin'
foreach ($tool in @('gendef.exe', 'dlltool.exe')) {
    if (-not (Test-Path (Join-Path $mingwBin $tool))) { throw "缺少 $tool（$mingwBin）" }
}
$gendef = Join-Path $mingwBin 'gendef.exe'
$dlltool = Join-Path $mingwBin 'dlltool.exe'

Write-Output "生成 def: $def"

# 注意两点：
# 1) gendef/dlltool 会向 stderr 写正常信息（如 "Found PE+ image"）。在
#    $ErrorActionPreference='Stop' 下这会被当成终止错误，故调用期间临时降级。
# 2) gendef 把 .def 输出到当前工作目录（其 -o 选项在此版本会报 failed to open），
#    因此切换工作目录调用，产物名固定为 <dll基名>.def。
$previousEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    Push-Location $outDir
    try {
        & $gendef $dll 2>&1 | Out-Null
    } finally {
        Pop-Location
    }
    if (Test-Path $def) {
        Write-Output "生成导入库: $implib"
        & $dlltool -d $def -l $implib -D 'Live2DCubismCore.dll' 2>&1 | Out-Null
    }
} finally {
    $ErrorActionPreference = $previousEap
}

if (-not (Test-Path $def)) { throw "gendef 未生成 $def" }
if (-not (Test-Path $implib)) { throw 'dlltool 生成导入库失败' }

$exportCount = (Select-String -Path $def -Pattern '^csm' -ErrorAction SilentlyContinue).Count
Write-Output "完成: $implib ($((Get-Item $implib).Length) 字节, 导出 $exportCount 个 csm* 符号)"
Write-Output "运行时需把 Live2DCubismCore.dll 放在 exe 同目录（构建会自动拷贝）"
