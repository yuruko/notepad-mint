# flicker_test.ps1 - measures flicker while the window is resized: the exe is shown on the REAL desktop (topmost, never activated, no keys / mouse
# are sent), resized by SetWindowPos over and over, and two small screen areas are captured in a tight loop the whole time:
#   * a patch of the editor text (top left of the text, which does not move when the window is resized): its bright (text) pixel count must stay
#     the same in every frame. a frame where it drops is a frame in which the control had erased its text and not painted it again yet.
#   * the title text in the title strip (the same: white text on the tinted strip).
#   * the status bar's left text is bottom anchored, so it moves: its patch follows the window's current bottom edge.
# a screen capture of the composed desktop sees what dwm shows, so this measures the thing the eye sees. it is statistical (the bad state lasts a
# millisecond or two per paint), so the numbers are only meaningful as a before / after: run it on the old and the new exe.
# calibration: tools\probe.bat /DFLICKER_PROBE builds an exe whose editor sleeps 8 ms between its erase and its paint (edit.c, compiled out of the normal build);
# this harness has to see that one (it catches about a third of the editor frames and a few percent of the title frames). a clean run on a normal exe means
# "no gap of a few milliseconds in what is watched", not "no flicker anywhere": it only looks at the patches named above, not at the window edges, the status bar or the scrollbars.
#   powershell -NoProfile -File tools\flicker_test.ps1 [-Exe build\notepad-mint.exe] [-Steps 600] [-Theme dark|light] [-Wrap] [-Save build\flicker_worst.png]
# the window is visible for the few seconds it runs (on top of your other windows, without taking the focus). run it when you are not about to read that spot.
param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\build\notepad-mint.exe'),
    [int]$Steps = 600,
    [string]$Theme = 'dark',
    [switch]$Wrap,
    [int]$StepMs = 10,
    [int]$Lines = 300,                             # lines in the document (22 or so: the scrollbars come and go while the window is resized)
    [int]$DW = 5,                                  # pixels the width / height change per step (a fast drag moves 20 to 60 per message)
    [int]$DH = 3,
    [string]$Save = '',
    [string]$Sheet = ''                            # a contact sheet of whole-window frames from the middle of the run (to look at with your own eyes)
)
$ErrorActionPreference = 'Stop'
$Exe = (Resolve-Path -LiteralPath $Exe).Path
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class F {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
    [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint fl);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool CreateProcessW(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inh, uint fl, IntPtr env, string cwd, ref SI si, out PI pi);
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] struct SI { public int cb; public string r, d, t; public int x, y, xs, ys, xc, yc, fa; public int fl; public short sw, res; public IntPtr r2, i, o, e; }
    [StructLayout(LayoutKind.Sequential)] struct PI { public IntPtr hp, ht; public int pid, tid; }

    public static IntPtr Launch(string exe, string arg, string cwd, out int pid) {
        SI si = new SI(); si.cb = Marshal.SizeOf(typeof(SI)); si.fl = 1; si.sw = 4;     // STARTF_USESHOWWINDOW, SW_SHOWNOACTIVATE: the window never takes the focus
        PI pi;
        StringBuilder cmd = new StringBuilder("\"" + exe + "\" \"" + arg + "\"");
        if (!CreateProcessW(null, cmd, IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, cwd, ref si, out pi)) throw new Exception("CreateProcess failed " + Marshal.GetLastWin32Error());
        pid = pi.pid;
        for (int i = 0; i < 100; i++) {
            IntPtr found = IntPtr.Zero;
            EnumWindows(delegate(IntPtr h, IntPtr l) {
                uint p; GetWindowThreadProcessId(h, out p);
                StringBuilder s = new StringBuilder(64); GetClassNameW(h, s, 64);
                if (p == (uint)pi.pid && s.ToString() == "notepad_mint" && IsWindowVisible(h)) { found = h; return false; }
                return true;
            }, IntPtr.Zero);
            if (found != IntPtr.Zero) return found;
            Thread.Sleep(100);
        }
        throw new Exception("no window");
    }

    static int Bright(byte[] px, int stride, int w, int h) {                     // pixels brighter than a mid grey (text is near white on the dark theme)
        int n = 0;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            int o = y * stride + x * 4;
            if (px[o] + px[o + 1] + px[o + 2] > 384) n++;
        }
        return n;
    }
    static int Dark(byte[] px, int stride, int w, int h) {                       // light theme: text is dark on a light ground
        int n = 0;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            int o = y * stride + x * 4;
            if (px[o] + px[o + 1] + px[o + 2] < 300) n++;
        }
        return n;
    }

    public class Cap {
        public IntPtr Main; public bool Light;
        public volatile bool Stop;
        public List<int> Edit = new List<int>(), Strip = new List<int>(), Status = new List<int>();
        public List<long> T = new List<long>();
        public Bitmap WorstEdit; public int WorstEditInk = int.MaxValue; public int Frames;
        public Thread Go() { Thread t = new Thread(Run); t.IsBackground = true; t.Start(); return t; }     // (a thread must be made here: a scriptblock on a foreign thread kills the powershell host)
        public List<Bitmap> Full; public int MaxFull = 90; public volatile bool Resizing;
        public void Sheet(string path, int cols, int rows, int pick0, int stride) {                           // frames pick0, pick0 + stride, ... as a grid, each scaled to 45%
            if (Full == null || Full.Count == 0) return;
            int cw = 500, ch = 350;
            Bitmap sheet = new Bitmap(cols * cw, rows * ch, PixelFormat.Format32bppArgb);
            using (Graphics g = Graphics.FromImage(sheet)) {
                g.Clear(Color.FromArgb(255, 255, 0, 255));
                for (int i = 0; i < cols * rows; i++) {
                    int k = pick0 + i * stride; if (k >= Full.Count) break;
                    g.DrawImage(Full[k], new Rectangle((i % cols) * cw, (i / cols) * ch, (int)(Full[k].Width * 0.45), (int)(Full[k].Height * 0.45)));
                }
            }
            sheet.Save(path, ImageFormat.Png);
        }
        // patch geometry relative to the window's top left (client = window + the 8 px frame, the strip is 23 px, the menu 25 px: measured on the window itself)
        public void Run() {
            Bitmap bEdit = new Bitmap(300, 120, PixelFormat.Format32bppArgb), bStrip = new Bitmap(170, 14, PixelFormat.Format32bppArgb), bStat = new Bitmap(60, 12, PixelFormat.Format32bppArgb);
            Stopwatch sw = Stopwatch.StartNew();
            byte[] pe = new byte[300 * 120 * 4], ps = new byte[170 * 14 * 4], pt = new byte[60 * 12 * 4];
            while (!Stop) {
                RECT r; if (!GetWindowRect(Main, out r)) break;
                using (Graphics g = Graphics.FromImage(bEdit)) g.CopyFromScreen(r.l + 8 + 10, r.t + 8 + 23 + 25 + 10, 0, 0, bEdit.Size);        // inside the text: the first lines
                using (Graphics g = Graphics.FromImage(bStrip)) g.CopyFromScreen(r.l + 8 + 40, r.t + 8 + 5, 0, 0, bStrip.Size);                 // the title text
                using (Graphics g = Graphics.FromImage(bStat)) g.CopyFromScreen(r.l + 8 + 6, r.b - 8 - 18, 0, 0, bStat.Size);                  // the status bar's "1:1" (bottom anchored)
                int ink = Count(bEdit, pe, 300, 120), si = Count(bStrip, ps, 170, 14), st = Count(bStat, pt, 60, 12);
                Edit.Add(ink); Strip.Add(si); Status.Add(st); T.Add(sw.ElapsedTicks);
                if (Full != null && Resizing && Full.Count < MaxFull) {                                       // whole window, for the contact sheet (only while the resize runs)
                    int fw = r.r - r.l, fh = r.b - r.t;
                    if (fw > 0 && fh > 0 && fw < 2000 && fh < 1500) {
                        Bitmap fb = new Bitmap(fw, fh, PixelFormat.Format32bppArgb);
                        using (Graphics g = Graphics.FromImage(fb)) g.CopyFromScreen(r.l, r.t, 0, 0, fb.Size);
                        Full.Add(fb);
                    }
                }
                if (ink < WorstEditInk && Frames > 20) { WorstEditInk = ink; if (WorstEdit != null) WorstEdit.Dispose(); WorstEdit = (Bitmap)bEdit.Clone(); }
                Frames++;
            }
        }
        int Count(Bitmap b, byte[] buf, int w, int h) {
            BitmapData d = b.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            Marshal.Copy(d.Scan0, buf, 0, w * h * 4);
            b.UnlockBits(d);
            return Light ? Dark(buf, w * 4, w, h) : Bright(buf, w * 4, w, h);
        }
    }

    public static void Move(IntPtr h, int x, int y, int w, int ht) { SetWindowPos(h, IntPtr.Zero, x, y, w, ht, 0x0004 | 0x0010); }          // SWP_NOZORDER | SWP_NOACTIVATE
    public static void Top(IntPtr h, int x, int y, int w, int ht) { SetWindowPos(h, new IntPtr(-1), x, y, w, ht, 0x0010); }                // HWND_TOPMOST, SWP_NOACTIVATE
    public static void Close(IntPtr h) { PostMessage(h, 0x0010, IntPtr.Zero, IntPtr.Zero); }
    public static void Init() { SetProcessDPIAware(); }
}
"@
[F]::Init()

$work = Join-Path $env:TEMP ('mint_flicker_' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
[void](New-Item -ItemType Directory -Force -Path $work)
$exeCopy = Join-Path $work 'mint_flicker_test.exe'
Copy-Item -LiteralPath $Exe -Destination $exeCopy -Force
$ad = Join-Path $work 'appdata'; $dir = Join-Path $ad 'notepad mint'
[void](New-Item -ItemType Directory -Force -Path $dir)
$wrapV = 0; if ($Wrap) { $wrapV = 1 }
$ini = "[view]`r`ntheme=" + $Theme + "`r`n[editor]`r`nwrap=" + $wrapV + "`r`n"                       # (no [window] section: the first show is SW_SHOWDEFAULT = SW_SHOWNOACTIVATE from the startup info)
[IO.File]::WriteAllBytes((Join-Path $dir 'settings.ini'), ([byte[]](0xFF, 0xFE)) + [Text.Encoding]::Unicode.GetBytes($ini))
$doc = Join-Path $work 'doc.txt'
$sb = New-Object System.Text.StringBuilder
for ($i = 1; $i -le $Lines; $i++) { [void]$sb.Append(('line {0:D3}  the quick brown fox jumps over the lazy dog 0123456789 ' -f $i)); [void]$sb.Append("`r`n") }
[IO.File]::WriteAllText($doc, $sb.ToString(), (New-Object Text.UTF8Encoding($false)))

$origAppData = $env:APPDATA
$env:APPDATA = $ad
$procId = 0
try {
    $main = [F]::Launch($exeCopy, $doc, $work, [ref]$procId)
    $env:APPDATA = $origAppData
    Start-Sleep -Milliseconds 800
    $x0 = 40; $y0 = 40
    [F]::Top($main, $x0, $y0, 900, 640)
    Start-Sleep -Milliseconds 700
    $cap = New-Object F+Cap
    $cap.Main = $main
    $cap.Light = ($Theme -eq 'light')
    if ($Sheet) { $cap.Full = New-Object 'System.Collections.Generic.List[System.Drawing.Bitmap]' }
    $th = $cap.Go()
    Start-Sleep -Milliseconds 400                                                                 # a few still frames: the reference counts
    $still = $cap.Frames
    $cap.Resizing = $true
    $w = 900; $h = 640; $dw = $DW; $dh = $DH
    for ($i = 0; $i -lt $Steps; $i++) {
        $w += $dw; $h += $dh
        if ($w -gt 1100 -or $w -lt 640) { $dw = -$dw; $w += 2 * $dw }
        if ($h -gt 760 -or $h -lt 420) { $dh = -$dh; $h += 2 * $dh }
        [F]::Move($main, $x0, $y0, $w, $h)
        Start-Sleep -Milliseconds $StepMs
    }
    Start-Sleep -Milliseconds 300
    $cap.Stop = $true
    [void]$th.Join(3000)
    if ($Sheet) { $cap.Sheet($Sheet, 3, 4, 2, 5); 'contact sheet saved to ' + $Sheet + ' (' + $cap.Full.Count + ' whole-window frames kept)' }
    function Summ($name, $list, $skip) {
        $a = @($list | Select-Object -Skip $skip)
        if ($a.Count -eq 0) { return }
        $sorted = $a | Sort-Object
        $med = $sorted[[int]($sorted.Count / 2)]
        $lo = [int]($med * 0.85)
        $bad = @($a | Where-Object { $_ -lt $lo }).Count
        $min = $sorted[0]
        '{0,-7} frames {1,5}  median ink {2,5}  min {3,5}  frames below 85% of the median: {4,4} ({5:N1}%)' -f $name, $a.Count, $med, $min, $bad, (100.0 * $bad / $a.Count)
    }
    '--- {0} ({1} resize steps, {2} ms apart, theme {3}{4}), {5} frames captured, {6} of them before the resize' -f (Split-Path $Exe -Leaf), $Steps, $StepMs, $Theme, $(if ($Wrap) { ', wrap' } else { '' }), $cap.Frames, $still
    Summ 'editor' $cap.Edit $still
    Summ 'title' $cap.Strip $still
    Summ 'status' $cap.Status $still
    if ($Save -and $cap.WorstEdit) { $cap.WorstEdit.Save($Save, [System.Drawing.Imaging.ImageFormat]::Png); 'worst editor patch saved to ' + $Save }
} finally {
    $env:APPDATA = $origAppData
    try { if ($main -ne [IntPtr]::Zero) { [F]::Close($main) } } catch {}
    Start-Sleep -Milliseconds 400
    try { if ($procId) { Stop-Process -Id $procId -Force -ErrorAction SilentlyContinue } } catch {}
    try { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue } catch {}
}
