# 配布用の zip とインストーラーを作る。
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1
# dist\KanaCaret-<バージョン>.zip(KanaCaret.exe、README.md、LICENSE)と、
# dist\KanaCaret-<バージョン>-setup.exe(installer\KanaCaret.nsi。NSIS 3 が要る)を作る。
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

& cmd /c "`"$root\build.cmd`""
if ($LASTEXITCODE -ne 0) { throw "ビルドに失敗しました" }

$exe = Join-Path $root "build\KanaCaret.exe"
$version = (Get-Item $exe).VersionInfo.ProductVersion
$dist = Join-Path $root "dist"
$stage = Join-Path $dist "KanaCaret-$version"
$zip = "$stage.zip"
$setup = "$stage-setup.exe"

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe $stage
Copy-Item (Join-Path $root "README.md") $stage
Copy-Item (Join-Path $root "LICENSE") $stage
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Remove-Item -Recurse -Force $stage

# makensis は PATH か、既定のインストール先から探す
$makensis = (Get-Command makensis -ErrorAction SilentlyContinue).Source
if (-not $makensis) {
    $makensis = @("${env:ProgramFiles(x86)}\NSIS\makensis.exe", "$env:ProgramFiles\NSIS\makensis.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $makensis) { throw "makensis.exe が見つかりません(NSIS 3 を入れてください)" }
if (Test-Path $setup) { Remove-Item -Force $setup }
& $makensis /V2 /INPUTCHARSET UTF8 "/DVERSION=$version" "/DROOT=$root" "/DOUTFILE=$setup" (Join-Path $root "installer\KanaCaret.nsi")
if ($LASTEXITCODE -ne 0) { throw "インストーラーの作成に失敗しました" }

foreach ($f in $zip, $setup) {
    "作成しました: $f"
    "SHA256: $((Get-FileHash $f -Algorithm SHA256).Hash)"
}