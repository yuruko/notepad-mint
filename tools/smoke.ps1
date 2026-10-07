# smoke.ps1 - start the exe with a sample file, check it stays alive and shows the right title, grab a screenshot.
#   powershell -NoProfile -File tools\smoke.ps1 [-Exe <path>] [-Sample <file>] [-Shot <png>] [-Wait <seconds>]
# needs a desktop session (a hosted windows runner has one). the screenshot is best effort: no screen => no png, not a failure.
# exit code 1 = the exe died, never showed a window, or the title is wrong.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\build\notepad-mint.exe"),
    [string]$Sample = (Join-Path $PSScriptRoot "..\tests\multilingual-sample.txt"),
    [string]$Shot = (Join-Path $PSScriptRoot "..\build\smoke.png"),
    [int]$Wait = 4
)

$Exe = (Resolve-Path $Exe).Path
$Sample = (Resolve-Path $Sample).Path
$p = Start-Process -FilePath $Exe -ArgumentList ('"' + $Sample + '"') -PassThru
Start-Sleep -Seconds $Wait
$p.Refresh()
$alive = -not $p.HasExited
$title = $p.MainWindowTitle
Write-Host ("alive: {0}  title: [{1}]" -f $alive, $title)

if ($alive) {
    try {
        Add-Type -AssemblyName System.Windows.Forms
        Add-Type -AssemblyName System.Drawing
        $s = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        $bmp = New-Object System.Drawing.Bitmap($s.Width, $s.Height)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($s.Location, [System.Drawing.Point]::Empty, $s.Size)
        $g.Dispose()
        $bmp.Save($Shot, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        Write-Host ("screenshot: " + $Shot)
    } catch {
        Write-Host ("no screenshot: " + $_.Exception.Message)
    }
    Stop-Process -Id $p.Id -Force
}

if (-not $alive) { Write-Host "FAIL the exe exited"; exit 1 }
if ($title -ne 'multilingual-sample.txt - notepad mint') { Write-Host "FAIL unexpected window title"; exit 1 }
Write-Host "smoke ok"
