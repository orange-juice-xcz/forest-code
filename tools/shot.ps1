#requires -version 5
<#
  Forest Code - window screenshot helper (for visual QA)
  Usage:
    powershell -File tools\shot.ps1 -Title "forest code" -Out build\shot.png
    powershell -File tools\shot.ps1 -Exe build\forest-code.exe -Title "Forest Code" -Out shot.png -Kill
#>
param(
    [string]$Exe = '',
    [string]$Title = '',
    [string]$Out = 'shot.png',
    [int]$WaitMs = 2500,
    [long]$Hwnd = 0,
    [switch]$Kill
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class FcWin {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@

[void][FcWin]::SetProcessDPIAware()

$proc = $null
if ($Exe) {
    if (-not (Test-Path $Exe)) { throw "exe not found: $Exe" }
    $proc = Start-Process -FilePath $Exe -PassThru
    Start-Sleep -Milliseconds $WaitMs
}

$target = [IntPtr]::Zero
if ($Hwnd -ne 0) { $target = [IntPtr]$Hwnd }
if ($target -eq [IntPtr]::Zero -and $Title) {
    for ($i = 0; $i -lt 40; $i++) {
        $target = [FcWin]::FindWindowW($null, $Title)
        if ($target -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 250
    }
}
if ($target -eq [IntPtr]::Zero -and $proc) {
    Start-Sleep -Milliseconds 500
    $proc.Refresh()
    $target = $proc.MainWindowHandle
}
if ($target -eq [IntPtr]::Zero -and $Title) {
    # 兜底：按标题对应的进程名查找
    $procs = Get-Process | Where-Object { $_.MainWindowTitle -eq $Title }
    if ($procs) { $target = $procs[0].MainWindowHandle }
}
if ($target -eq [IntPtr]::Zero) { throw "window not found (title='$Title')" }

[void][FcWin]::ShowWindow($target, 9)   # SW_RESTORE
[void][FcWin]::SetForegroundWindow($target)
Start-Sleep -Milliseconds 700

$r = New-Object FcWin+RECT
[void][FcWin]::GetWindowRect($target, [ref]$r)
$w = $r.Right - $r.Left
$h = $r.Bottom - $r.Top
if ($w -le 0 -or $h -le 0) { throw "bad window rect ${w}x${h}" }

$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
# PW_RENDERFULLCONTENT = 2  (works for composited windows)
$ok = [FcWin]::PrintWindow($target, $hdc, 2)
$g.ReleaseHdc($hdc)
$g.Dispose()

if (-not $ok) {
    # fallback: BitBlt from screen
    $bmp2 = New-Object System.Drawing.Bitmap($w, $h)
    $g2 = [System.Drawing.Graphics]::FromImage($bmp2)
    $g2.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $g2.Dispose()
    $bmp.Dispose()
    $bmp = $bmp2
}

$dir = Split-Path $Out -Parent
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host ("saved {0}  ({1}x{2})" -f $Out, $w, $h) -ForegroundColor Green

if ($Kill -and $proc) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
