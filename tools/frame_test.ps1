# frame_test.ps1 - drives the custom title strip (FRAME_CUSTOM=1 builds) with real mouse input and checks the window state.
#   powershell -NoProfile -File tools\frame_test.ps1 [-Exe <path>] [-Shots <dir>]
# prints one PASS / FAIL line per check, exit code 1 if anything failed. it moves the real mouse for a few seconds:
# don't touch the mouse / keyboard while it runs.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\build\probe\notepad mint.exe"),
    [string]$Shots = (Join-Path $PSScriptRoot "..\build")
)

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class F {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint fl, int dx, int dy, uint data, UIntPtr ex);
  [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int a, out RECT r, int sz);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  public static void Click(int x, int y) { SetCursorPos(x, y); System.Threading.Thread.Sleep(120);
    mouse_event(2, 0, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(60); mouse_event(4, 0, 0, 0, UIntPtr.Zero); }
  public static void DblClick(int x, int y) { Click(x, y); System.Threading.Thread.Sleep(60); Click(x, y); }
  public static void Drag(int x, int y, int dx, int dy) { SetCursorPos(x, y); System.Threading.Thread.Sleep(150);
    mouse_event(2, 0, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(100);
    for (int i = 1; i <= 10; i++) { SetCursorPos(x + dx * i / 10, y + dy * i / 10); System.Threading.Thread.Sleep(30); }
    mouse_event(4, 0, 0, 0, UIntPtr.Zero); }
  [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte sc, uint fl, UIntPtr ex);
  public static void KeyDown(byte vk) { keybd_event(vk, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(40); }
  public static void KeyUp(byte vk) { keybd_event(vk, 0, 2, UIntPtr.Zero); System.Threading.Thread.Sleep(40); }
  public static int HitTest(IntPtr h, int x, int y) { return (int)SendMessageW(h, 0x84, IntPtr.Zero, (IntPtr)((y << 16) | (x & 0xffff))); }
}
"@
[void][F]::SetProcessDPIAware()

$script:fail = 0
function Check([string]$name, [bool]$ok, [string]$info = "") {
    if ($ok) { Write-Output ("PASS " + $name) } else { Write-Output ("FAIL " + $name + " " + $info); $script:fail++ }
}
function Rect([IntPtr]$h) { $r = New-Object F+RECT; [void][F]::DwmGetWindowAttribute($h, 9, [ref]$r, 16); return $r }
function Shot([IntPtr]$h, [string]$name) {
    $r = Rect $h; $w = $r.R - $r.L; $ht = $r.B - $r.T
    $bmp = New-Object System.Drawing.Bitmap($w, $ht); $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($w, $ht))); $g.Dispose()
    $bmp.Save((Join-Path $Shots $name), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}

$exeFull = (Resolve-Path -LiteralPath $Exe).Path
Get-Process -Name "notepad mint" -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exeFull } | Stop-Process -Force   # copies of THIS exe only (the gui tests run temp copies)
Start-Sleep -Milliseconds 300
$p = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Milliseconds 1800
$p.Refresh()
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Output "FAIL no main window"; exit 1 }
[void][F]::ShowWindow($h, 9); [void][F]::SetForegroundWindow($h); Start-Sleep -Milliseconds 400

# ---- geometry: the strip is the top of the client area; buttons are 46 px (96 dpi) wide, right aligned
$rc = New-Object F+RECT; [void][F]::GetClientRect($h, [ref]$rc)
$pt = New-Object F+POINT; $pt.X = 0; $pt.Y = 0; [void][F]::ClientToScreen($h, [ref]$pt)
$cx0 = $pt.X; $cy0 = $pt.Y; $cw = $rc.R
$dpi = [int][F]::GetDpiForWindow($h)
if ($dpi -lt 96) { $dpi = 96 }
$btn = [int](46 * $dpi / 96)
$capH = [int](30 * $dpi / 96)                       # FrameHeight(): at least 30 px (96 dpi); the chrome font at 13 pt is below that
$yMid = $cy0 + [int]($capH / 2)
$rc2 = New-Object F+RECT; [void][F]::GetClientRect($h, [ref]$rc2); $cw = $rc2.R
$xClose = $cx0 + $cw - [int]($btn / 2)
$xMax   = $cx0 + $cw - $btn - [int]($btn / 2)
$xMin   = $cx0 + $cw - 2 * $btn - [int]($btn / 2)
Write-Output ("info dpi=$dpi client=($cx0,$cy0) cw=$cw btn=$btn capH=$capH")

# ---- hit tests (WM_NCHITTEST at screen points)
$wr = Rect $h
Check "hittest caption middle = HTCAPTION(2)" ([F]::HitTest($h, $cx0 + [int]($cw / 2), $yMid) -eq 2) ("got " + [F]::HitTest($h, $cx0 + [int]($cw / 2), $yMid))
Check "hittest top edge = HTTOP(12)" ([F]::HitTest($h, $cx0 + [int]($cw / 2), $wr.T + 1) -eq 12) ("got " + [F]::HitTest($h, $cx0 + [int]($cw / 2), $wr.T + 1))
Check "hittest top-left corner = HTTOPLEFT(13)" ([F]::HitTest($h, $wr.L + 2, $wr.T + 1) -eq 13) ("got " + [F]::HitTest($h, $wr.L + 2, $wr.T + 1))
# the sizing border is mostly invisible and lies OUTSIDE the dwm bounds in $wr: use the real window rect for the frame checks
$wrr = New-Object F+RECT; [void][F]::GetWindowRect($h, [ref]$wrr)
Check "hittest left edge = HTLEFT(10)" ([F]::HitTest($h, $wrr.L + 2, $wrr.T + 300) -eq 10) ("got " + [F]::HitTest($h, $wrr.L + 2, $wrr.T + 300))
Check "hittest bottom-right corner = HTBOTTOMRIGHT(17) (resizing without a status bar grip)" ([F]::HitTest($h, $wrr.R - 2, $wrr.B - 2) -eq 17) ("got " + [F]::HitTest($h, $wrr.R - 2, $wrr.B - 2))
Check "hittest close button = HTCLIENT(1)" ([F]::HitTest($h, $xClose, $yMid) -eq 1) ("got " + [F]::HitTest($h, $xClose, $yMid))

# ---- maximize button -> zoomed, again -> restored
$before = Rect $h
[F]::Click($xMax, $yMid); Start-Sleep -Milliseconds 700
Check "max button maximizes" ([F]::IsZoomed($h))
Shot $h "ft_max.png"
# in the maximized state the strip is at the very top of the screen: re-derive the geometry
$pt.X = 0; $pt.Y = 0; [void][F]::ClientToScreen($h, [ref]$pt); $rc2 = New-Object F+RECT; [void][F]::GetClientRect($h, [ref]$rc2)
$mx = $pt.X + $rc2.R - $btn - [int]($btn / 2); $my = $pt.Y + [int]($capH / 2)
Check "maximized: strip starts at the top of the work area (y=$($pt.Y))" ($pt.Y -le 2) ("client origin y=" + $pt.Y)
[F]::Click($mx, $my); Start-Sleep -Milliseconds 700
Check "max button again restores" (-not [F]::IsZoomed($h))
$after = Rect $h
Check "restored to the old size" (($after.R - $after.L) -eq ($before.R - $before.L) -and ($after.B - $after.T) -eq ($before.B - $before.T)) ("before " + ($before.R - $before.L) + "x" + ($before.B - $before.T) + " after " + ($after.R - $after.L) + "x" + ($after.B - $after.T))

# ---- double click on the caption toggles maximize
$pt.X = 0; $pt.Y = 0; [void][F]::ClientToScreen($h, [ref]$pt)
[F]::DblClick($pt.X + [int]($cw / 2), $yMid); Start-Sleep -Milliseconds 700
Check "double click on the caption maximizes" ([F]::IsZoomed($h))
$pt.X = 0; $pt.Y = 0; [void][F]::ClientToScreen($h, [ref]$pt); $rc2 = New-Object F+RECT; [void][F]::GetClientRect($h, [ref]$rc2)
[F]::DblClick($pt.X + [int]($rc2.R / 2), $pt.Y + [int]($capH / 2)); Start-Sleep -Milliseconds 700
Check "double click again restores" (-not [F]::IsZoomed($h))

# ---- drag the caption: the window follows
$pt.X = 0; $pt.Y = 0; [void][F]::ClientToScreen($h, [ref]$pt)
$r0 = Rect $h
[F]::Drag($pt.X + 300, $yMid, 120, 60); Start-Sleep -Milliseconds 400
$r1 = Rect $h
Check "dragging the caption moves the window (dx=$($r1.L - $r0.L) dy=$($r1.T - $r0.T))" (($r1.L - $r0.L) -ge 100 -and ($r1.T - $r0.T) -ge 40)
Shot $h "ft_moved.png"

# ---- aero snap: win+left snaps to the left half of the work area (the system does it; our frame must not break it)
Add-Type -AssemblyName System.Windows.Forms
$wa = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
[void][F]::SetForegroundWindow($h); Start-Sleep -Milliseconds 250
[F]::KeyDown(0x5B); [F]::KeyDown(0x25); [F]::KeyUp(0x25); [F]::KeyUp(0x5B); Start-Sleep -Milliseconds 1000
$rs = New-Object F+RECT; [void][F]::GetWindowRect($h, [ref]$rs)
$sw = $rs.R - $rs.L; $sh = $rs.B - $rs.T
Check "win+left snaps to the left half of the work area (window rect ${sw}x${sh}, work area $($wa.Width)x$($wa.Height))" (($sw -ge [int]($wa.Width / 2 - 24)) -and ($sw -le [int]($wa.Width / 2 + 24)) -and ($sh -ge $wa.Height - 24) -and -not [F]::IsZoomed($h))
Shot $h "ft_snap.png"
[F]::KeyDown(0x5B); [F]::KeyDown(0x28); [F]::KeyUp(0x28); [F]::KeyUp(0x5B); Start-Sleep -Milliseconds 900     # win+down: what it does varies by windows version (win11 quarter-snaps), so no check, just un-snap for the rest

# ---- window menu: alt+space opens our popup (screenshot for a look), esc closes it
[void][F]::SetForegroundWindow($h); Start-Sleep -Milliseconds 200
Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.SendKeys]::SendWait("% "); Start-Sleep -Milliseconds 500
Shot $h "ft_sysmenu.png"
[System.Windows.Forms.SendKeys]::SendWait("{ESC}"); Start-Sleep -Milliseconds 300
Check "window still alive after the window menu" (-not $p.HasExited)

# ---- minimize button
$pt.X = 0; $pt.Y = 0; [void][F]::ClientToScreen($h, [ref]$pt); $rc2 = New-Object F+RECT; [void][F]::GetClientRect($h, [ref]$rc2)
$nx = $pt.X + $rc2.R - 2 * $btn - [int]($btn / 2); $ny = $pt.Y + [int]($capH / 2)
[F]::Click($nx, $ny); Start-Sleep -Milliseconds 700
Check "min button minimizes" ([F]::IsIconic($h))
[void][F]::ShowWindow($h, 9); Start-Sleep -Milliseconds 600
Check "restores from minimized" (-not [F]::IsIconic($h))

# ---- close button ends the process (unmodified document: no prompt)
$pt.X = 0; $pt.Y = 0; [void][F]::ClientToScreen($h, [ref]$pt); $rc2 = New-Object F+RECT; [void][F]::GetClientRect($h, [ref]$rc2)
[void][F]::SetForegroundWindow($h); Start-Sleep -Milliseconds 200
[F]::Click($pt.X + $rc2.R - [int]($btn / 2), $pt.Y + [int]($capH / 2)); Start-Sleep -Milliseconds 1200
$p.Refresh()
Check "close button exits" ($p.HasExited)
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }

if ($script:fail -gt 0) { Write-Output ("frame_test: " + $script:fail + " FAILED"); exit 1 }
Write-Output "frame_test: all passed"
