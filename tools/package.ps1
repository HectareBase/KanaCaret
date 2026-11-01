# 配布用の zip を作る。
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1
# dist\ime-indicator-<バージョン>.zip に、ime-indicator.exe、README.md、LICENSE を入れる。
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

& cmd /c "`"$root\build.cmd`""
if ($LASTEXITCODE -ne 0) { throw "ビルドに失敗しました" }

$exe = Join-Path $root "build\ime-indicator.exe"
$version = (Get-Item $exe).VersionInfo.ProductVersion
$dist = Join-Path $root "dist"
$stage = Join-Path $dist "ime-indicator-$version"
$zip = "$stage.zip"

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe $stage
Copy-Item (Join-Path $root "README.md") $stage
Copy-Item (Join-Path $root "LICENSE") $stage
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Remove-Item -Recurse -Force $stage

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash
"作成しました: $zip"
"SHA256: $hash"
