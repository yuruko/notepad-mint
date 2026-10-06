# px.ps1 - print pixel colors:  -In png  -Pts "x,y;x,y;..."
param([string]$In, [string]$Pts)
Add-Type -AssemblyName System.Drawing
$b = [System.Drawing.Bitmap]::FromFile($In)
foreach ($p in $Pts.Split(';')) {
    $xy = $p.Split(',')
    $c = $b.GetPixel([int]$xy[0], [int]$xy[1])
    Write-Output ("({0},{1}) = #{2:x2}{3:x2}{4:x2}" -f $xy[0], $xy[1], $c.R, $c.G, $c.B)
}
$b.Dispose()
