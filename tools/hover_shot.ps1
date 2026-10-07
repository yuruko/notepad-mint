# hover_shot.ps1 - a screenshot of notepad mint on the gui test's private desktop with the pointer "over" the theme button of the menu bar
# (a WM_MOUSEMOVE sent to the bar window: no real mouse, nothing shows on your screen). the bar asks for a mouse-leave message with
# TrackMouseEvent and the system sends it soon after (the real pointer is elsewhere): -Delay is how long to wait between the move and the capture.
#   powershell -NoProfile -File tools/hover_shot.ps1 -Out D:/x.png [-Theme dark|light] [-NoHover] [-Delay 40] [-Exe <path>]
# prints the pixel of the button just left of its icon: the accent when the hover was caught
param(
    [string]$Out = (Join-Path $PSScriptRoot '..\build\hover.png'),
    [string]$Theme = 'dark',
    [switch]$NoHover,
    [int]$Delay = 40,
    [string]$Exe = (Join-Path $PSScriptRoot '..\build\notepad-mint.exe')
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\tests\ui\ui_test.ps1') -NoRun -Exe $Exe
try {
    $ad = Join-Path $work 'hv_appdata'
    $dir = Join-Path $ad 'notepad-mint'
    [void](New-Item -ItemType Directory -Force -Path $dir)
    $ini = "[view]`r`ntheme=" + $Theme + "`r`n[window]`r`nx=100`r`ny=60`r`nw=900`r`nh=620`r`n"
    [IO.File]::WriteAllBytes((Join-Path $dir 'settings.ini'), ([byte[]](0xFF, 0xFE)) + [Text.Encoding]::Unicode.GetBytes($ini))
    $app = Start-App '' $ad
    Start-Sleep -Milliseconds 500
    $bar = Chrome-Kid $app 'mp_menubar'
    $r = [U]::WRect($bar); $w = [U]::WRect([long]$app.Main)
    $x = ($r[2] - $r[0]) - 12; $y = [int](($r[3] - $r[1]) / 2)                   # 12 px in from the right edge of the bar: on the button
    if (-not $NoHover) {
        Mouse $bar 0x200 100 $y 0                                                # (the menu items first, so that the move onto the button changes something)
        Mouse $bar 0x200 $x $y 0
        if ($Delay -gt 0) { Start-Sleep -Milliseconds $Delay }
    }
    $bmp = [U]::Grab([long]$app.Main)
    $px = $bmp.GetPixel(($r[2] - $w[0]) - 27, ($r[1] - $w[1]) + 4)
    $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Output ('saved ' + $Out + ' delay ' + $Delay + ' ms, pixel left of the icon rgb(' + $px.R + ',' + $px.G + ',' + $px.B + ')')
    $bmp.Dispose()
} finally {
    try { Stop-All } catch {}
    try { Kill-Mine } catch {}
    try { [U]::DropDesktop() } catch {}
    $env:APPDATA = $origAppData
}
