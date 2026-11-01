# インジケーターの表示を、実際のアプリで自動確認する。
# アプリを開いて文字を入力し、インジケーターの位置と周辺のスクリーンショットを記録して、アプリを閉じる。
#   powershell -ExecutionPolicy Bypass -File tools\check-app.ps1 -Target explorer -Ime
# Target: notepad / explorer / start / terminal / settings
# -Ime を付けると、入力後に IME をオンにして「あ」の表示も撮影する(終了時に元へ戻す)。
# スクリーンショットは local\checks\<日時>\<Target>-a.png / <Target>-kana.png に保存する(-OutDir で変更可)。
# 画面には他のアプリや個人の情報が写るので、local\ の外(公開するフォルダー)には置かないこと。
param(
    [Parameter(Mandatory)][ValidateSet("notepad", "explorer", "start", "terminal", "settings")][string]$Target,
    [string]$OutDir = "",
    [switch]$Ime
)
$ErrorActionPreference = "Stop"
if (-not $OutDir) {
    $OutDir = Join-Path (Split-Path -Parent $PSScriptRoot) ("local\checks\" + (Get-Date -Format "yyyyMMdd-HHmmss"))
}
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Collections.Generic; using System.Text;
public static class W {
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint tid, ref GTI g);
  [DllImport("imm32.dll")] public static extern IntPtr ImmGetDefaultIMEWnd(IntPtr h);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct GTI { public int cbSize; public int flags; public IntPtr active, focus, capture, menuOwner, moveSize, caret; public RECT rc; }
  public static List<IntPtr> Windows(string cls) { var list = new List<IntPtr>(); EnumWindows((h, l) => { var sb = new StringBuilder(128); GetClassName(h, sb, 128); if (sb.ToString() == cls) list.Add(h); return true; }, IntPtr.Zero); return list; }
  public static IntPtr FocusOfForeground() {
    uint pid; uint tid = GetWindowThreadProcessId(GetForegroundWindow(), out pid);
    GTI g = new GTI(); g.cbSize = Marshal.SizeOf(g); GetGUIThreadInfo(tid, ref g);
    return g.focus != IntPtr.Zero ? g.focus : GetForegroundWindow();
  }
}
"@
[void][W]::SetThreadDpiAwarenessContext([IntPtr](-4))  # 座標を物理ピクセルで扱う

function Get-Indicator {
    $h = [W]::FindWindow("HectareBase.KanaCaret.Indicator", $null)
    if ($h -eq [IntPtr]::Zero) { return $null }
    $r = New-Object W+RECT
    [void][W]::GetWindowRect($h, [ref]$r)
    [pscustomobject]@{ Visible = [W]::IsWindowVisible($h); Left = $r.L; Top = $r.T; Size = $r.R - $r.L }
}

function Save-Shot($name, $ind) {
    $x = 0; $y = 0
    if ($ind) { $x = [Math]::Max(0, $ind.Left - 250); $y = [Math]::Max(0, $ind.Top - 60) }
    $bmp = New-Object System.Drawing.Bitmap 560, 180
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
    $path = Join-Path $OutDir "$Target-$name.png"
    $bmp.Save($path); $g.Dispose(); $bmp.Dispose()
    $path
}

function Set-ImeOpen([bool]$open) {
    # IMC_SETOPENSTATUS = 0x0006。WM_IME_CONTROL はプロセス外の IME ウィンドウにも送れる
    $ime = [W]::ImmGetDefaultIMEWnd([W]::FocusOfForeground())
    [void][W]::SendMessage($ime, 0x0283, [IntPtr]6, [IntPtr]([int]$open))
}

function Press-Win {
    [W]::keybd_event(0x5B, 0, 0, [IntPtr]::Zero); [W]::keybd_event(0x5B, 0, 2, [IntPtr]::Zero)
}

if (-not (Get-Process KanaCaret -ErrorAction SilentlyContinue)) { throw "KanaCaret.exe が起動していません" }
New-Item -ItemType Directory -Force $OutDir | Out-Null

# 対象を開く(新しく開いたウィンドウだけを後で閉じる)
$cls = @{ notepad = "Notepad"; explorer = "CabinetWClass"; terminal = "CASCADIA_HOSTING_WINDOW_CLASS"; settings = "ApplicationFrameWindow" }[$Target]
$before = if ($cls) { [W]::Windows($cls) } else { @() }
switch ($Target) {
    "notepad"  { Start-Process notepad.exe }
    "explorer" { Start-Process explorer.exe $env:USERPROFILE }
    "terminal" { Start-Process wt.exe }
    "settings" { Start-Process "ms-settings:" }
    "start"    { Press-Win }
}
$win = $null
$keep = $false  # 前から開いていたウィンドウを使った場合は閉じない
if ($cls) {
    for ($i = 0; $i -lt 30 -and -not $win; $i++) {
        Start-Sleep -Milliseconds 300
        $win = [W]::Windows($cls) | Where-Object { $before -notcontains $_ } | Select-Object -First 1
    }
    if (-not $win -and $Target -eq "settings") {
        # 設定アプリは 1 つしか開けないので、開いていれば既存のウィンドウが手前に来る
        $win = [W]::GetForegroundWindow()
        $keep = $true
    }
    if (-not $win) { throw "$Target のウィンドウが見つかりません" }
    Start-Sleep -Seconds 2
    [void][W]::SetForegroundWindow($win)
} else {
    Start-Sleep -Seconds 2
}
Start-Sleep -Milliseconds 500

# キー入力の前に、対象が本当に前面にあるかを確かめる。利用者が他のアプリを操作していると、
# Windows は前面の切り替えを許さないので、そのまま打つと利用者のアプリに入力してしまう
function Test-TargetForeground {
    $fg = [W]::GetForegroundWindow()
    if ($win) { return $fg -eq $win }
    $fpid = 0
    [void][W]::GetWindowThreadProcessId($fg, [ref]$fpid)
    return (Get-Process -Id $fpid -ErrorAction SilentlyContinue).ProcessName -in "SearchHost", "StartMenuExperienceHost"
}
function Assert-TargetForeground {
    if (Test-TargetForeground) { return }
    if ($win -and -not $keep) { [void][W]::PostMessage($win, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }
    throw "$Target が前面にならなかったので中止しました(他のアプリへ入力しないため)。操作していない状態で実行し直してください"
}

# 入力欄にフォーカスを移して文字を打つ
Assert-TargetForeground
switch ($Target) {
    "explorer" { [System.Windows.Forms.SendKeys]::SendWait("^e"); Start-Sleep -Milliseconds 800 }
    "settings" { [System.Windows.Forms.SendKeys]::SendWait("^e"); Start-Sleep -Milliseconds 800 }
}
Assert-TargetForeground
[System.Windows.Forms.SendKeys]::SendWait("abc")
Start-Sleep -Milliseconds 1200
$ind = Get-Indicator
"[$Target] a: visible=$($ind.Visible) pos=($($ind.Left),$($ind.Top)) size=$($ind.Size)"
Save-Shot "a" $ind

if ($Ime -and (Test-TargetForeground)) {
    Set-ImeOpen $true
    Start-Sleep -Milliseconds 800
    $ind = Get-Indicator
    "[$Target] あ: visible=$($ind.Visible) pos=($($ind.Left),$($ind.Top)) size=$($ind.Size)"
    Save-Shot "kana" $ind
    Set-ImeOpen $false
}

# 後片付け
if ($Target -eq "start") { if (Test-TargetForeground) { [System.Windows.Forms.SendKeys]::SendWait("{ESC}") } }
elseif ($win) {
    # 入力した文字を消してから閉じる(未保存の確認を出さないため。メモ帳は他のウィンドウと
    # 同じプロセスのことがあるので、プロセスは終了しない)。前面でなければ打たない
    if ($Target -eq "notepad" -or $Target -eq "terminal" -or $keep) {
        [void][W]::SetForegroundWindow($win)
        if (Test-TargetForeground) { [System.Windows.Forms.SendKeys]::SendWait("{BS}{BS}{BS}") }
        Start-Sleep -Milliseconds 300
    }
    if (-not $keep) { [void][W]::PostMessage($win, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }
}
