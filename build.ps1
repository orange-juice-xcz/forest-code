#requires -version 5
<#
  Forest Code - build script (Windows PowerShell 5.1 compatible, ASCII only)

  Usage:
    powershell -File build.ps1              # incremental build
    powershell -File build.ps1 -Clean       # clean rebuild
    powershell -File build.ps1 -LibsOnly    # only Scintilla/Lexilla static libs
    powershell -File build.ps1 -Run         # build then launch
    powershell -File build.ps1 -GXX <path>  # explicit g++
#>
param(
    [switch]$Clean,
    [switch]$LibsOnly,
    [switch]$Run,
    [string]$GXX = ''
)

$ErrorActionPreference = 'Stop'
$Root  = $PSScriptRoot
$Build = Join-Path $Root 'build'
$ObjRoot = Join-Path $Build 'obj'

# ---------- toolchain ----------
if (-not $GXX) {
    $candidates = @(
        'C:\Qt\Tools\mingw1310_64\bin\g++.exe',
        'C:\msys64\mingw64\bin\g++.exe',
        'C:\Program Files (x86)\Dev-Cpp\MinGW64\bin\g++.exe'
    )
    foreach ($c in $candidates) { if (Test-Path $c) { $GXX = $c; break } }
}
if (-not $GXX -or -not (Test-Path $GXX)) { throw "g++ not found; pass -GXX <path>" }
$BinDir = Split-Path $GXX
$AR = Join-Path $BinDir 'ar.exe'
if (-not (Test-Path $AR)) { throw "ar.exe not found: $AR" }

$SciRoot = Join-Path $Root 'third_party\scintilla\scintilla'
$LexRoot = Join-Path $Root 'third_party\lexilla\lexilla'

# ---------- 第三方依赖（首次构建自动下载） ----------
$SciVer = '566'
$LexVer = '553'
function Ensure-Dep {
    param([string]$Name, [string]$Url)
    $marker = Join-Path $Root "third_party\$Name"
    if (Test-Path $marker) { return }
    Write-Host "[fetch] $Name (首次构建，自动下载源码)" -ForegroundColor Yellow
    $dl = Join-Path $Root 'third_party\_dl'
    New-Item -ItemType Directory -Force -Path $dl | Out-Null
    $zip = Join-Path $dl "$Name.zip"
    if (-not (Test-Path $zip)) {
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $Url -OutFile $zip -UseBasicParsing -TimeoutSec 180
    }
    Expand-Archive -Path $zip -DestinationPath (Join-Path $Root 'third_party') -Force
    Write-Host "  [OK] $marker" -ForegroundColor Green
}
Ensure-Dep -Name 'scintilla' -Url "https://www.scintilla.org/scintilla$SciVer.zip"
Ensure-Dep -Name 'lexilla'   -Url "https://www.scintilla.org/lexilla$LexVer.zip"

# ---------- flags ----------
$CXXFLAGS = @(
    '-std=c++17', '-O2', '-DNDEBUG',
    '-DWIN32', '-DUNICODE', '-D_UNICODE', '-D_WIN32_WINNT=0x0601',
    '-DSCI_LEXER'
)
$WARN = @('-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-cast-function-type', '-Wno-missing-field-initializers')

$SCI_INC = @("-I$SciRoot\include", "-I$SciRoot\src", "-I$SciRoot\win32")
$LEX_INC = @("-I$LexRoot\include", "-I$LexRoot\lexlib")
$SRC_INC = @("-I$Root\src", "-I$SciRoot\include", "-I$LexRoot\include")

$LDFLAGS = @('-mwindows', '-static', '-static-libgcc', '-static-libstdc++', '-s')
$LIBS = @(
    '-lgdi32', '-luser32', '-limm32', '-lole32', '-luuid', '-loleaut32',
    '-ladvapi32', '-lcomctl32', '-lcomdlg32', '-lshell32', '-lshlwapi',
    '-ldwmapi', '-lmsimg32', '-luxtheme', '-lgdiplus', '-lstdc++'
)

# ---------- helpers ----------
function Write-Ok($msg)   { Write-Host "  [OK] $msg" -ForegroundColor Green }
function Write-Fail($msg) { Write-Host "  [!!] $msg" -ForegroundColor Red }

$script:Failures = @()

# 在 PS 5.1 下把原生命令的 stderr 重定向到临时文件，避免被当成异常抛出
function Invoke-Native {
    param([string]$Exe, [string[]]$ArgList)
    $tmp = [System.IO.Path]::GetTempFileName()
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $Exe @ArgList 2>$tmp | Out-Null
    $code = $LASTEXITCODE
    $ErrorActionPreference = $old
    $text = ''
    if (Test-Path $tmp) { $text = [System.IO.File]::ReadAllText($tmp); Remove-Item $tmp -Force }
    return @{ Code = $code; Text = $text }
}

function Compile-One {
    param([string]$Src, [string]$ObjDir, [string[]]$Inc, [string[]]$ExtraDefs, [datetime]$HeaderStamp = [datetime]::MinValue)
    $name = [System.IO.Path]::GetFileNameWithoutExtension($Src)
    $obj  = Join-Path $ObjDir ($name + '.o')
    if ((Test-Path $obj) -and ((Get-Item $obj).LastWriteTime -gt (Get-Item $Src).LastWriteTime) -and
        ((Get-Item $obj).LastWriteTime -gt $HeaderStamp)) {
        return $obj
    }
    if (-not (Test-Path $ObjDir)) { New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null }
    $cargs = @($CXXFLAGS) + @($ExtraDefs) + $WARN + $Inc + @('-c', $Src, '-o', $obj)
    $r = Invoke-Native -Exe $GXX -ArgList $cargs
    if ($r.Code -ne 0 -or -not (Test-Path $obj)) {
        $script:Failures += "=== $name ===`n$($r.Text)"
        return $null
    }
    return $obj
}

function Build-Lib {
    param([string]$Name, [string[]]$Sources, [string[]]$Inc, [string[]]$ExtraDefs = @())
    $objDir = Join-Path $ObjRoot $Name
    $lib = Join-Path $Build ('lib' + $Name + '.a')
    Write-Host "[$Name] $($Sources.Count) sources" -ForegroundColor Cyan
    $objs = @()
    $i = 0
    foreach ($s in $Sources) {
        $i++
        Write-Progress -Activity "compile $Name" -Status "[$i/$($Sources.Count)] $(Split-Path $s -Leaf)" -PercentComplete ($i * 100 / $Sources.Count)
        $o = Compile-One -Src $s -ObjDir $objDir -Inc $Inc -ExtraDefs $ExtraDefs
        if ($o) { $objs += $o }
    }
    Write-Progress -Activity "compile $Name" -Completed
    if ($script:Failures.Count -gt 0) { return $null }
    if (Test-Path $lib) { Remove-Item $lib -Force }
    $r = Invoke-Native -Exe $AR -ArgList (@('rcs', $lib) + $objs)
    if ($r.Code -ne 0) { throw "ar failed for $Name : $($r.Text)" }
    Write-Ok $lib
    return $lib
}

# ---------- clean ----------
if ($Clean) {
    if (Test-Path $Build) { Remove-Item $Build -Recurse -Force }
    Write-Ok 'cleaned build/'
}
New-Item -ItemType Directory -Force -Path $Build | Out-Null

$t0 = Get-Date

# ---------- 1. Scintilla ----------
$sciSrc = @(Get-ChildItem (Join-Path $SciRoot 'src') -Filter *.cxx | Select-Object -ExpandProperty FullName)
$sciSrc += @(
    (Join-Path $SciRoot 'win32\PlatWin.cxx'),
    (Join-Path $SciRoot 'win32\ScintillaWin.cxx'),
    (Join-Path $SciRoot 'win32\ListBox.cxx'),
    (Join-Path $SciRoot 'win32\SurfaceGDI.cxx'),
    (Join-Path $SciRoot 'win32\SurfaceD2D.cxx'),
    (Join-Path $SciRoot 'win32\HanjaDic.cxx')
)
$sciLib = Build-Lib -Name 'scintilla' -Sources $sciSrc -Inc ($SCI_INC + $LEX_INC)

# ---------- 2. Lexilla (only needed lexers) ----------
$lexSrc = @(Get-ChildItem (Join-Path $LexRoot 'lexlib') -Filter *.cxx | Select-Object -ExpandProperty FullName)
$lexSrc += @(
    (Join-Path $LexRoot 'lexers\LexCPP.cxx'),
    (Join-Path $LexRoot 'lexers\LexMarkdown.cxx')
)
$lexLib = Build-Lib -Name 'lexilla' -Sources $lexSrc -Inc ($LEX_INC + $SCI_INC)

if ($script:Failures.Count -gt 0) {
    Write-Fail 'build failed:'
    foreach ($f in $script:Failures) { Write-Host $f -ForegroundColor Red }
    exit 1
}
if ($LibsOnly) {
    Write-Ok ("libs built in {0:N1}s" -f ((Get-Date) - $t0).TotalSeconds)
    exit 0
}

# ---------- 3. Forest Code app ----------
$appSrc = @(Get-ChildItem (Join-Path $Root 'src') -Filter *.cpp -Recurse | Select-Object -ExpandProperty FullName)
if ($appSrc.Count -eq 0) { Write-Fail 'no .cpp under src/'; exit 1 }
$appObjDir = Join-Path $ObjRoot 'app'
# 头文件时间戳：任一头文件改动则重编所有应用源文件
$headerStamp = [datetime]::MinValue
Get-ChildItem (Join-Path $Root 'src') -Filter *.h -Recurse | ForEach-Object {
    if ($_.LastWriteTime -gt $headerStamp) { $headerStamp = $_.LastWriteTime }
}
Write-Host "[forest-code] $($appSrc.Count) sources" -ForegroundColor Cyan
$appObjs = @()
$i = 0
foreach ($s in $appSrc) {
    $i++
    Write-Progress -Activity 'compile forest-code' -Status "[$i/$($appSrc.Count)] $(Split-Path $s -Leaf)" -PercentComplete ($i * 100 / $appSrc.Count)
    $o = Compile-One -Src $s -ObjDir $appObjDir -Inc ($SRC_INC + $SCI_INC + $LEX_INC) -ExtraDefs @('-DFC_VERSION="0.1.0"') -HeaderStamp $headerStamp
    if ($o) { $appObjs += $o }
}
Write-Progress -Activity 'compile forest-code' -Completed

if ($script:Failures.Count -gt 0) {
    Write-Fail 'build failed:'
    foreach ($f in $script:Failures) { Write-Host $f -ForegroundColor Red }
    exit 1
}

# resources (icon) - optional
$resObj = $null
$rcFile = Join-Path $Root 'res\forestcode.rc'
if (Test-Path $rcFile) {
    $windres = Join-Path $BinDir 'windres.exe'
    if (Test-Path $windres) {
        $resObj = Join-Path $appObjDir 'forestcode_res.o'
        $r = Invoke-Native -Exe $windres -ArgList @("-I$Root\res", $rcFile, $resObj)
        if ($r.Code -ne 0) { $resObj = $null; Write-Fail ("resource compile failed: " + $r.Text) }
    }
}

$exe = Join-Path $Build 'forest-code.exe'
$linkArgs = @($appObjs)
if ($resObj) { $linkArgs += @($resObj) }
$linkArgs += @($sciLib, $lexLib) + @('-o', $exe) + $LDFLAGS + $LIBS
Write-Host '[link] forest-code.exe' -ForegroundColor Cyan
$lr = Invoke-Native -Exe $GXX -ArgList $linkArgs
if ($lr.Code -ne 0) {
    Write-Fail 'link failed:'
    Write-Host $lr.Text -ForegroundColor Red
    exit 1
}
$size = [math]::Round((Get-Item $exe).Length / 1KB, 0)
Write-Ok ("built -> $exe  ($size KB, {0:N1}s)" -f ((Get-Date) - $t0).TotalSeconds)

if ($Run) { Start-Process -FilePath $exe }
