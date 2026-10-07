# resize_bench.ps1 - how fast does the main window answer a resize? the exe runs on the gui test's private desktop (nothing shows, no input) and is resized by
# SetWindowPos over and over, the way a drag does it. per step two times are taken:
#   call  = how long SetWindowPos took to return: the app's own work in the messages the system SENDS (WM_NCCALCSIZE, WM_SIZE, the layout ...)
#   paint = from the same start until no window of the app has an update region left: the call plus everything painted afterwards
# (a drag feels laggy when these are long: the next mouse message waits for them)
#   powershell -NoProfile -File tools/resize_bench.ps1 [-Lines 3000] [-Steps 200] [-Wrap] [-DW 12] [-Exe <path>]
param(
    [int]$Lines = 3000,
    [int]$Steps = 200,
    [int]$DW = 12,                                 # pixels the width changes per step (the height changes by half as much)
    [switch]$Wrap,
    [string]$Exe = (Join-Path $PSScriptRoot '..\build\notepad-mint.exe')
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\tests\ui\ui_test.ps1') -NoRun -Exe $Exe
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class RB {
    [DllImport("user32.dll")] static extern bool GetUpdateRect(IntPtr h, IntPtr r, bool erase);
    public static bool Dirty(long h) { return GetUpdateRect(new IntPtr(h), IntPtr.Zero, false); }
}
'@
function Stat($a) {
    $s = @($a | Sort-Object)
    if ($s.Count -eq 0) { return 'n/a' }
    return ('median {0:N1} ms, p95 {1:N1} ms, max {2:N1} ms' -f $s[[int]($s.Count / 2)], $s[[int]($s.Count * 0.95)], $s[$s.Count - 1])
}
try {
    $app = Start-App
    if ($Wrap) { Cmd $app 'IDM_FMT_WRAP'; Start-Sleep -Milliseconds 700 }
    $piece = 'the quick brown fox jumps over the lazy dog 0123456789 '
    $text = (1..$Lines | ForEach-Object { 'line ' + $_ + ' ' + $piece + $piece }) -join "`r`n"
    Ed-Set $app $text
    Start-Sleep -Milliseconds 800
    $main = [long]$app.Main
    $wins = @($main) + @([U]::Kids($main) | ForEach-Object { [long]$_ })
    $r0 = [U]::WRect($main)
    $w = $r0[2] - $r0[0]; $h = $r0[3] - $r0[1]
    $w0 = $w; $h0 = $h
    $call = New-Object System.Collections.ArrayList
    $paint = New-Object System.Collections.ArrayList
    $dir = 1
    for ($i = 0; $i -lt $Steps; $i++) {
        $w += $dir * $DW; $h += $dir * [int]($DW / 2)
        if ($w -gt $w0 + 10 * $DW -or $w -lt $w0 - 10 * $DW) { $dir = -$dir }
        $sw = [Diagnostics.Stopwatch]::StartNew()
        [void][Nd]::Size($main, $w, $h)
        [void]$call.Add($sw.Elapsed.TotalMilliseconds)
        $dirty = $true
        while ($dirty -and $sw.ElapsedMilliseconds -lt 2000) {
            $dirty = $false
            foreach ($x in $wins) { if ([RB]::Dirty($x)) { $dirty = $true; break } }
        }
        [void]$paint.Add($sw.Elapsed.TotalMilliseconds)
        Start-Sleep -Milliseconds 4
    }
    Write-Output ('{0} lines{1}, {2} steps of {3} px: ' -f $Lines, $(if ($Wrap) { ', word wrap on' } else { '' }), $Steps, $DW)
    Write-Output ('  call : ' + (Stat $call))
    Write-Output ('  paint: ' + (Stat $paint))
} finally {
    try { Stop-All } catch {}
    try { Kill-Mine } catch {}
    try { [U]::DropDesktop() } catch {}
    $env:APPDATA = $origAppData
}
