# crop.ps1 - crop + integer-scale a region of a png (to inspect pixels)
param([string]$In, [string]$Out, [int]$Left, [int]$Top, [int]$Wid, [int]$Hei, [int]$Scale = 4)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Bitmap]::FromFile($In)
$dw = [int]($Wid * $Scale); $dh = [int]($Hei * $Scale)
$dst = New-Object System.Drawing.Bitmap($dw, $dh)
$g = [System.Drawing.Graphics]::FromImage($dst)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$g.DrawImage($src, (New-Object System.Drawing.Rectangle(0, 0, $dw, $dh)), $Left, $Top, $Wid, $Hei, [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose()
$dst.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$dst.Dispose(); $src.Dispose()
Write-Output "ok $Out"
