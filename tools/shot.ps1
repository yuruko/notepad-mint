# shot.ps1 - launch notepad mint, optionally drive it with keys, screenshot it, kill it.
#   -Out      png path
#   -Arg      command line argument handed to the exe (a file to open)
#   -Keys     SendKeys string(s), separated by '|' ; each step waits -Step ms. use "{WAIT:500}" for an extra pause
#   -PadR/-PadB  grow the capture rect to include popups that hang outside the window
#   -Cmd / -Mouse "x,y" / -Burst N / -Drag "x1,y1,x2,y2" [-DragHold]   a menu command by id, a parked mouse (hover), N captures in a row, a real mouse drag (selection)
#   -Keep     don't kill the process afterwards
# NB: it kills every running copy of the exe it launches (-Exe, default build\notepad mint.exe) before and after: close your own notepad mint first. the gui tests run temp copies, those are left alone.
param(
    [string]$Out = (Join-Path $PSScriptRoot "..\build\shot.png"),
    [string]$Arg = "",
    [string]$Keys = "",
    [int]$Delay = 1200,
    [int]$Step = 450,
    [int]$PadR = 0,
    [int]$PadB = 0,
    [string]$Size = "",          # "WxH": resize the window (outer size, device px) before the keys
    [int]$Cmd = 0,               # post WM_COMMAND with this IDM_* id (see src\mp.h) to the main window before the keys
    [string]$Mouse = "",         # "x,y": park the real mouse there (device px from the window's top-left, as in the screenshot) just before the capture
    [int]$Burst = 0,             # N > 0: take N captures 90 ms apart as <Out>_0.png ... (a blinking caret shows in some of them)
    [string]$Drag = "",          # "x1,y1,x2,y2": a real left-button drag between the two points (device px from the window's top-left), before the capture
    [switch]$DragHold,           # with -Drag: take the capture while the button is still down (the selection is being dragged), release afterwards
    [switch]$Keep,
    [string]$Exe = (Join-Path $PSScriptRoot "..\build\notepad mint.exe")
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint fl, UIntPtr ex);
  [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool AttachThreadInput(uint a, uint b, bool attach);
  [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr h);
  [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
  public static uint FgPid() { uint pid; GetWindowThreadProcessId(GetForegroundWindow(), out pid); return pid; }
  public static bool Force(IntPtr h) {
    for (int i = 0; i < 5; i++) {
      IntPtr fg = GetForegroundWindow(); uint pid;
      uint fgT = GetWindowThreadProcessId(fg, out pid); uint me = GetCurrentThreadId();
      keybd_event(0x7E, 0, 0, UIntPtr.Zero); keybd_event(0x7E, 0, 2, UIntPtr.Zero);
      if (fgT != me) AttachThreadInput(me, fgT, true);
      BringWindowToTop(h); SetForegroundWindow(h);
      if (fgT != me) AttachThreadInput(me, fgT, false);
      System.Threading.Thread.Sleep(150);
      if (GetForegroundWindow() == h) return true;
    }
    return false;
  }
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, UIntPtr ex);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int a, out RECT r, int sz);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
[void][W]::SetProcessDPIAware()

$exeFull = (Resolve-Path -LiteralPath $Exe).Path
function Kill-Same { Get-Process -Name "notepad mint" -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exeFull } | Stop-Process -Force }   # only copies of THIS exe (the gui tests run temp copies)
Kill-Same
Start-Sleep -Milliseconds 200
if ($Arg -ne "") { $p = Start-Process -FilePath $Exe -ArgumentList ('"' + $Arg + '"') -PassThru } else { $p = Start-Process -FilePath $Exe -PassThru }
Start-Sleep -Milliseconds $Delay
$p.Refresh()
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Output "no main window (process exited? HasExited=$($p.HasExited))"; exit 1 }
[void][W]::ShowWindow($h, 9)
[void][W]::SetWindowPos($h, [IntPtr]::new(-1), 0, 0, 0, 0, 3)   # topmost, no move/size
$fgok = [W]::Force($h)
if (-not $fgok) { Write-Output "WARNING: could not get the window into the foreground" }
Start-Sleep -Milliseconds 300
if ($Size -match '^(\d+)x(\d+)$') { [void][W]::SetWindowPos($h, [IntPtr]::new(-1), 0, 0, [int]$Matches[1], [int]$Matches[2], 0x0002); Start-Sleep -Milliseconds 500 }   # SWP_NOMOVE

if ($Cmd -ne 0) { [void][W]::PostMessage($h, 0x111, [IntPtr]$Cmd, [IntPtr]::Zero); Start-Sleep -Milliseconds 700 }

if ($Keys -ne "") {
    foreach ($k in $Keys.Split('|')) {
        if ($k -match '^\{WAIT:(\d+)\}$') { Start-Sleep -Milliseconds ([int]$Matches[1]); continue }
        # only reclaim the foreground when some other app has it: a dialog of ours (find, open ...) must keep the keys
        if ([W]::FgPid() -ne $p.Id) { if (-not [W]::Force($h)) { Write-Output "WARNING: lost foreground before '$k'" } }
        if ($k -match '^\{(ALT|CTRL|CTRLSHIFT):(0x[0-9A-Fa-f]+|.)\}$') {      # slow, real key events (SendKeys fires them with no gap); key = char or 0xVK
            $mods = @(); if ($Matches[1] -eq 'ALT') { $mods = @(0x12) } elseif ($Matches[1] -eq 'CTRL') { $mods = @(0x11) } else { $mods = @(0x11, 0x10) }
            $ks = [string]$Matches[2]
            if ($ks.Length -gt 1) { $vk = [byte][Convert]::ToInt32($ks.Substring(2), 16) } else { $vk = [byte][char]($ks.ToUpper()) }
            foreach ($m in $mods) { [W]::keybd_event([byte]$m, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60 }
            [W]::keybd_event($vk, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40
            [W]::keybd_event($vk, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60
            [array]::Reverse($mods); foreach ($m in $mods) { [W]::keybd_event([byte]$m, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40 }
            Start-Sleep -Milliseconds $Step
            continue
        }
        [System.Windows.Forms.SendKeys]::SendWait($k)
        Start-Sleep -Milliseconds $Step
    }
}

if ([W]::FgPid() -ne $p.Id) { [void][W]::Force($h) }
$r = New-Object W+RECT
[void][W]::DwmGetWindowAttribute($h, 9, [ref]$r, 16)
$dragUp = $false
if ($Drag -match '^(\d+),(\d+),(\d+),(\d+)$') {                     # selecting with the mouse: down, 12 small moves, up (or not, with -DragHold)
    $x1 = $r.L + [int]$Matches[1]; $y1 = $r.T + [int]$Matches[2]; $x2 = $r.L + [int]$Matches[3]; $y2 = $r.T + [int]$Matches[4]
    [void][W]::SetCursorPos($x1, $y1); Start-Sleep -Milliseconds 150
    [W]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 100
    for ($s = 1; $s -le 12; $s++) {
        [void][W]::SetCursorPos($x1 + [int](($x2 - $x1) * $s / 12), $y1 + [int](($y2 - $y1) * $s / 12)); Start-Sleep -Milliseconds 30
    }
    Start-Sleep -Milliseconds 200
    if ($DragHold) { $dragUp = $true } else { [W]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 200 }
}
if ($Mouse -match '^(\d+),(\d+)$') {                                # hover states: the real cursor, a few moves so the window sees it arrive
    $mx = $r.L + [int]$Matches[1]; $my = $r.T + [int]$Matches[2]
    [void][W]::SetCursorPos($mx - 5, $my - 5); Start-Sleep -Milliseconds 120
    [void][W]::SetCursorPos($mx, $my); Start-Sleep -Milliseconds 900
}
$w = $r.R - $r.L + $PadR
$ht = $r.B - $r.T + $PadB
$shots = 1; if ($Burst -gt 0) { $shots = $Burst }
for ($i = 0; $i -lt $shots; $i++) {
    $bmp = New-Object System.Drawing.Bitmap($w, $ht)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
    $g.Dispose()
    $name = $Out
    if ($Burst -gt 0) { $name = $Out -replace '\.png$', ("_" + $i + ".png") }
    $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    if ($i -lt $shots - 1) { Start-Sleep -Milliseconds 90 }
}
if ($dragUp) { [W]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero) }
Write-Output ("saved {0}  ({1}x{2})  title='{3}' responding={4}" -f $Out, $w, $ht, $p.MainWindowTitle, $p.Responding)

if (-not $Keep) { Kill-Same }
