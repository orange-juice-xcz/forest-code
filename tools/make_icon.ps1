#requires -version 5
<#
  Forest Code - icon generator (ASCII only; PS 5.1 reads .ps1 as ANSI)
  Draws a multi-size .ico: forest-green rounded tile + mint pine tree.
  Usage: powershell -File tools\make_icon.ps1 -Preview
#>
param([switch]$Preview)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$Root = Split-Path $PSScriptRoot -Parent
$OutIco = Join-Path $Root 'res\forestcode.ico'
$OutPreview = Join-Path $Root 'build\icon_preview.png'

function New-IconBitmap([int]$S) {
    $bmp = New-Object System.Drawing.Bitmap($S, $S, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.Clear([System.Drawing.Color]::Transparent)

    $f = $S / 256.0

    # rounded tile with vertical gradient
    $r = [float](48 * $f)
    $d = $r * 2
    $w = $S
    $h = $S
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc(($w - $d), 0, $d, $d, 270, 90)
    $path.AddArc(($w - $d), ($h - $d), $d, $d, 0, 90)
    $path.AddArc(0, ($h - $d), $d, $d, 90, 90)
    $path.CloseFigure()

    $cTop = [System.Drawing.Color]::FromArgb(255, 0x1C, 0x4A, 0x2D)
    $cBot = [System.Drawing.Color]::FromArgb(255, 0x08, 0x16, 0x0E)
    $rect = New-Object System.Drawing.Rectangle(0, 0, $S, $S)
    $grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect, $cTop, $cBot, [System.Drawing.Drawing2D.LinearGradientMode]::Vertical)
    $g.FillPath($grad, $path)

    $penW = [Math]::Max(1.0, 4 * $f)
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(160, 0x3C, 0x8F, 0x5C), $penW)
    $g.DrawPath($pen, $path)

    # pine tree: trunk + three tiers
    $mint = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 0x4E, 0xD1, 0x7E))
    $mint2 = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 0x8A, 0xEA, 0xB0))
    $trunkB = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 0x2A, 0x6E, 0x45))

    $tw = 46 * $f
    $g.FillRectangle($trunkB, [float](($S - $tw) / 2), [float](162 * $f), [float]$tw, [float](52 * $f))

    $tiers = @(
        @{ apexY = 118; baseY = 168; halfW = 88; brush = $mint },
        @{ apexY = 84;  baseY = 136; halfW = 72; brush = $mint2 },
        @{ apexY = 50;  baseY = 100; halfW = 56; brush = $mint }
    )
    foreach ($t in $tiers) {
        $cx = $S / 2.0
        $pts = @(
            (New-Object System.Drawing.PointF([float]$cx, [float]($t.apexY * $f))),
            (New-Object System.Drawing.PointF([float]($cx - $t.halfW * $f), [float]($t.baseY * $f))),
            (New-Object System.Drawing.PointF([float]($cx + $t.halfW * $f), [float]($t.baseY * $f)))
        )
        $g.FillPolygon($t.brush, $pts)
    }

    $g.Dispose()
    $grad.Dispose(); $pen.Dispose(); $path.Dispose()
    $mint.Dispose(); $mint2.Dispose(); $trunkB.Dispose()
    return $bmp
}

$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$pngData = @()
foreach ($s in $sizes) {
    $bmp = New-IconBitmap $s
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngData += ,$ms.ToArray()
    $bmp.Dispose()
    $ms.Dispose()
}

$fs = [System.IO.File]::Create($OutIco)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]
    $dim = 0
    if ($s -lt 256) { $dim = $s }
    $bw.Write([byte]$dim)
    $bw.Write([byte]$dim)
    $bw.Write([byte]0)
    $bw.Write([byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$pngData[$i].Length)
    $bw.Write([UInt32]$offset)
    $offset += $pngData[$i].Length
}
foreach ($p in $pngData) { $bw.Write($p) }
$bw.Flush(); $bw.Close(); $fs.Close()

Write-Host ("icon written: {0} ({1} bytes, {2} sizes)" -f $OutIco, (Get-Item $OutIco).Length, $sizes.Count) -ForegroundColor Green

if ($Preview) {
    $pad = 16
    $W = ($sizes.Count * 256) + $pad * 2
    $H = 256 + $pad * 2 + 40
    $pv = New-Object System.Drawing.Bitmap($W, $H)
    $g = [System.Drawing.Graphics]::FromImage($pv)
    $g.Clear([System.Drawing.Color]::FromArgb(255, 0x0A, 0x15, 0x0F))
    $fnt = New-Object System.Drawing.Font('Segoe UI', 11)
    $br = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 0x9C, 0xC0, 0xAB))
    $x = $pad
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $ms = New-Object System.IO.MemoryStream($pngData[$i], $false)
        $b = [System.Drawing.Image]::FromStream($ms)
        $g.DrawImage($b, $x, $pad, 256, 256)
        $g.DrawString(("{0}px" -f $sizes[$i]), $fnt, $br, [float]$x, [float]($pad + 262))
        $b.Dispose(); $ms.Dispose()
        $x += 256
    }
    $g.Dispose(); $fnt.Dispose(); $br.Dispose()
    $pv.Save($OutPreview, [System.Drawing.Imaging.ImageFormat]::Png)
    $pv.Dispose()
    Write-Host "preview: $OutPreview" -ForegroundColor Green
}
