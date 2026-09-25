# Retry download of winlibs x86_64 msvcrt toolchain until success.
$zip = "D:\deps\winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64msvcrt-14.0.0-r1.7z"
$url = "https://github.com/brechtsanders/winlibs_mingw/releases/download/16.2.0posix-14.0.0-msvcrt-r1/winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64msvcrt-14.0.0-r1.7z"
for ($i = 1; $i -le 20; $i++) {
    if (Test-Path $zip) {
        $size = (Get-Item $zip).Length
        if ($size -gt 100MB) { Write-Output "download complete: $([math]::Round($size/1MB,1))MB"; exit 0 }
        Remove-Item $zip -Force
    }
    Write-Output "attempt $i"
    curl.exe -L --connect-timeout 90 --max-time 1200 -o $zip $url
    if ($LASTEXITCODE -eq 0 -and (Test-Path $zip) -and (Get-Item $zip).Length -gt 100MB) {
        Write-Output "download complete: $([math]::Round((Get-Item $zip).Length/1MB,1))MB"
        exit 0
    }
    Start-Sleep -Seconds 15
}
exit 1