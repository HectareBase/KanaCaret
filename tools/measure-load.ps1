# インジケーターの負荷(CPU・メモリ・イベント数)を計測する。
#   powershell -ExecutionPolicy Bypass -File tools\measure-load.ps1 [-Seconds 60]
# 起動中のインジケーターをいったん終了し、--stats 付きで起動し直して計測する。
# 内訳は local\logs\stats-<日時>.log に 10 秒ごとに書かれる。計測後は通常の起動に戻す。
# 計測中は操作しないこと(何もしていないときの負荷を測るため)。入力中の負荷を測るときは、計測中に入力する。
param([int]$Seconds = 60)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\ime-indicator.exe"
if (-not (Test-Path $exe)) { throw "先に build.cmd でビルドしてください" }

$logDir = Join-Path $root "local\logs"
New-Item -ItemType Directory -Force $logDir | Out-Null
$log = Join-Path $logDir ("stats-" + (Get-Date -Format "yyyyMMdd-HHmmss") + ".log")

$wasRunning = [bool](Get-Process ime-indicator -ErrorAction SilentlyContinue)
Get-Process ime-indicator -ErrorAction SilentlyContinue | Stop-Process
Start-Sleep -Milliseconds 400

$p = Start-Process $exe -ArgumentList "--stats=`"$log`"" -PassThru
Start-Sleep -Seconds 2  # 起動時の初期化を計測に含めない
$cpu0 = $p.TotalProcessorTime
Start-Sleep -Seconds $Seconds
$p.Refresh()
$cpuMs = ($p.TotalProcessorTime - $cpu0).TotalMilliseconds

"計測時間: $Seconds 秒"
"CPU: {0:N1} ms(1 コアあたり {1:N3}%)" -f $cpuMs, ($cpuMs / ($Seconds * 10))
"メモリ: プライベート {0:N1} MB / ワーキングセット {1:N1} MB" -f ($p.PrivateMemorySize64 / 1MB), ($p.WorkingSet64 / 1MB)
"exe: {0:N0} バイト" -f (Get-Item $exe).Length
"内訳: $log"
if (Test-Path $log) { Get-Content $log }

Stop-Process -Id $p.Id
if ($wasRunning) {
    Start-Sleep -Milliseconds 400
    Start-Process $exe
}
