# Launch the app with isolated settings on a private desktop; capture its window.
#   powershell -NoProfile -File tools\smoke.ps1 [-Exe <path>] [-Sample <file>] [-Shot <png>] [-Wait <seconds>]
# Needs a desktop session. Screenshot failure is nonfatal.
# exit code 1 = the exe died, never showed a window, or the title is wrong.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\build\notepad-mint.exe"),
    [string]$Sample = (Join-Path $PSScriptRoot "..\tests\multilingual-sample.txt"),
    [string]$Shot = (Join-Path $PSScriptRoot "..\build\smoke.png"),
    [int]$Wait = 4
)

$ErrorActionPreference = 'Stop'
$Sample = (Resolve-Path -LiteralPath $Sample).Path
. (Join-Path $PSScriptRoot '..\tests\ui\ui_test.ps1') -NoRun -Exe $Exe
$result = 1
try {
    $app = Start-App $Sample
    Start-Sleep -Seconds ([Math]::Max(0, $Wait))
    $app.Proc.Refresh()
    $alive = -not $app.Proc.HasExited
    $title = if ($alive) { Title $app } else { '' }
    Write-Output ('alive: {0}  title: [{1}]' -f $alive, $title)
    if (-not $alive) { throw 'the exe exited' }
    if ($title -cne ((Split-Path $Sample -Leaf) + ' - notepad mint')) { throw 'unexpected window title' }
    try {
        $bmp = [U]::Grab([long]$app.Main)
        try { $bmp.Save($Shot, [System.Drawing.Imaging.ImageFormat]::Png) }
        finally { $bmp.Dispose() }
        Write-Output ('screenshot: ' + $Shot)
    } catch {
        Write-Output ('no screenshot: ' + $_.Exception.Message)
    }
    Write-Output 'smoke ok'
    $result = 0
} catch { Write-Output ('FAIL ' + $_.Exception.Message) }
finally {
    try { Stop-All } catch {}
    Kill-Mine
    [U]::DropDesktop()
    $env:APPDATA = $origAppData
    # Validate the temporary directory before removing it.
    $cleanupPath = [IO.Path]::GetFullPath($work)
    $tempParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if ((Split-Path $cleanupPath -Parent) -eq $tempParent -and (Split-Path $cleanupPath -Leaf) -match '^npm_ui_[0-9a-f]{8}$') {
        Remove-Item -LiteralPath $cleanupPath -Recurse -Force -ErrorAction SilentlyContinue
    }
}
exit $result
