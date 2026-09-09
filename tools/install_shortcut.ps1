#requires -version 5
<#
  Forest Code - create a desktop shortcut (ASCII only; PS 5.1 reads .ps1 as ANSI)
  Usage: powershell -File tools\install_shortcut.ps1
         powershell -File tools\install_shortcut.ps1 -Exe <path\to\forest-code.exe>
#>
param([string]$Exe = '')

$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent

if (-not $Exe) { $Exe = Join-Path $Root 'build\forest-code.exe' }
if (-not (Test-Path $Exe)) {
    Write-Host "not found: $Exe  (run build.ps1 first)" -ForegroundColor Red
    exit 1
}
$Exe = (Resolve-Path $Exe).Path

$desktop = [Environment]::GetFolderPath('Desktop')
$lnkPath = Join-Path $desktop 'Forest Code.lnk'

$shell = New-Object -ComObject WScript.Shell
$lnk = $shell.CreateShortcut($lnkPath)
$lnk.TargetPath = $Exe
$lnk.WorkingDirectory = (Split-Path $Exe)
$lnk.IconLocation = "$Exe,0"
$lnk.Description = 'Forest Code - competitive programming workbench'
$lnk.Save()

# refresh shell icon cache so the shortcut shows the embedded icon immediately
$ie4uinit = Join-Path $env:SystemRoot 'system32\ie4uinit.exe'
if (Test-Path $ie4uinit) { & $ie4uinit -show | Out-Null }

Write-Host "shortcut created: $lnkPath" -ForegroundColor Green
Write-Host "  target : $Exe"
