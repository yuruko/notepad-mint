# pw_shot.ps1 - a screenshot of notepad mint taken on the gui test's PRIVATE desktop with PrintWindow: nothing shows on your screen, it takes
# no foreground / mouse / keyboard (tools\shot.ps1 does, and stray keystrokes of the user land in the window it drives).
#   powershell -NoProfile -File tools/pw_shot.ps1 -Out D:/x.png [-File D:/doc.txt] [-Theme dark|light] [-Cmds IDM_FMT_WRAP,IDM_THEME_LIGHT]
#                                            [-Sel 10,40] [-Text "some text"] [-Wait 700] [-Size 900x620] [-Exe <path>]
# what it cannot do: real mouse hover, the caret blink, native dialogs (own windows), anything that needs real input.
param(
    [string]$Out = (Join-Path $PSScriptRoot '..\build\pw.png'),
    [string]$File = '',
    [string]$Theme = 'dark',
    [string]$Cmds = '',
    [string]$Sel = '',
    [string]$Text = '',
    [int]$Wait = 700,
    [string]$Size = '',
    [string]$Exe = (Join-Path $PSScriptRoot '..\build\notepad mint.exe')
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\tests\ui\ui_test.ps1') -NoRun -Exe $Exe
try {
    $ad = Join-Path $work 'pw_appdata'
    $dir = Join-Path $ad 'notepad mint'
    [void](New-Item -ItemType Directory -Force -Path $dir)
    $ini = "[view]`r`ntheme=" + $Theme + "`r`n[window]`r`nx=100`r`ny=60`r`nw=900`r`nh=620`r`n"
    [IO.File]::WriteAllBytes((Join-Path $dir 'settings.ini'), ([byte[]](0xFF, 0xFE)) + [Text.Encoding]::Unicode.GetBytes($ini))
    $app = Start-App $File $ad
    if ($Size -match '^(\d+)x(\d+)$') { [void][U]::Post([long]$app.Main, 0x0, 0, 0) }
    Start-Sleep -Milliseconds 400
    if ($Text) { Ed-Set $app $Text }
    foreach ($c in ($Cmds -split ',' | Where-Object { $_ })) { Cmd $app $c.Trim(); Start-Sleep -Milliseconds 400 }
    if ($Sel -match '^(\d+),(-?\d+)$') { [void](Snd (Get-Edit $app) $EM_SETSEL ([int]$Matches[1]) ([int]$Matches[2])) }
    Start-Sleep -Milliseconds $Wait
    $bmp = [U]::Grab([long]$app.Main)
    $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Output ('saved ' + $Out + ' (' + $bmp.Width + 'x' + $bmp.Height + ') title=[' + (Title $app) + ']')
    $bmp.Dispose()
} finally {
    try { Stop-All } catch {}
    try { Kill-Mine } catch {}
    try { [U]::DropDesktop() } catch {}
    $env:APPDATA = $origAppData
}
