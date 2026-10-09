# ui_test.ps1 - functional GUI tests for "notepad mint": the real exe, driven ONLY with window messages
# (SendMessage / PostMessage to window handles). no SendKeys, no SetForegroundWindow, no real mouse: the user's own typing
# cannot leak into the app and the test cannot steal the keyboard.
#
#   powershell -NoProfile -File tests\ui\ui_test.ps1 [-Exe <path>] [-Only T2,T3] [-Dump] [-BigMB 20] [-Timeout 4000]
#
# - the exe is COPIED to a fresh temp dir first (the build may delete / rewrite build\notepad-mint.exe while this runs),
#   and %APPDATA% is pointed at a fresh temp dir per launch, so the real settings.ini is never touched.
# - the app is started with SW_SHOWNOACTIVATE: its window appears without taking the foreground.
# - one PASS / FAIL / SKIP line per check, a summary line at the end, exit code 1 if anything failed.
# - only the processes started here (their exe lives in the temp dir) are killed, never another "notepad-mint" instance.
# - command ids / control ids are parsed at run time from src\mp.h, src\find.c, src\filedlg.c, src\fontdlg.c, src\edit.c.
# - -Dump prints every child (class, id, enabled, rect, text) of each dialog the tests open.
# keep this file pure ascii (windows powershell 5.1 reads a bom-less file as ansi): non-ascii text is built from char codes.
param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\..\build\notepad-mint.exe'),
    [string]$Only = '',
    [switch]$Dump,
    [int]$BigMB = 20,
    [int]$Timeout = 3000,
    [switch]$Visible,                                                            # run the app on the real desktop (default: a private hidden desktop)
    [switch]$NoRun                                                              # dot-source the helpers only (for ad-hoc probes): . tests\ui\ui_test.ps1 -NoRun
)

$ErrorActionPreference = 'Stop'
$Root  = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Src   = Join-Path $Root 'src'
$Tests = Join-Path $Root 'tests'
if (-not (Test-Path -LiteralPath $Exe)) { Write-Output ('FATAL exe not found: ' + $Exe); exit 2 }
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$origAppData = $env:APPDATA

# ---------------------------------------------------------------------------------------------- win32 helper (c# 5)
$cs = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class U {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h, EnumProc p, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern bool IsWindowEnabled(IntPtr h);
    [DllImport("user32.dll")] static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr h);
    [DllImport("user32.dll")] static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern int GetWindowLongW(IntPtr h, int i);
    [StructLayout(LayoutKind.Sequential)] struct SCRINFO { public uint cbSize, fMask; public int nMin, nMax; public uint nPage; public int nPos, nTrackPos; }
    [DllImport("user32.dll")] static extern bool GetScrollInfo(IntPtr h, int bar, ref SCRINFO si);
    [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern IntPtr SendMessageTimeoutW(IntPtr h, uint m, IntPtr w, IntPtr l, uint fl, uint to, out IntPtr res);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageTimeoutW")] static extern IntPtr SendMessageTimeoutS(IntPtr h, uint m, IntPtr w, string l, uint fl, uint to, out IntPtr res);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageTimeoutW")] static extern IntPtr SendMessageTimeoutB(IntPtr h, uint m, IntPtr w, StringBuilder l, uint fl, uint to, out IntPtr res);
    [DllImport("user32.dll", EntryPoint = "SendMessageTimeoutW")] static extern IntPtr SendMessageTimeoutP(IntPtr h, uint m, ref uint w, ref uint l, uint fl, uint to, out IntPtr res);
    [DllImport("user32.dll")] static extern bool GetGUIThreadInfo(uint tid, ref GTI g);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll")] static extern bool InvalidateRect(IntPtr h, ref RECT r, bool erase);
    [DllImport("user32.dll")] static extern bool UpdateWindow(IntPtr h);
    [DllImport("user32.dll")] static extern uint GetGuiResources(IntPtr proc, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct GTI { public int cb, flags; public IntPtr active, focus, capture, menuOwner, moveSize, caret; public RECT rc; }

    public static int To = 8000;                                   // SendMessageTimeout timeout (ms) used by Snd / SetText / GetText
    static IntPtr H(long h) { return new IntPtr(h); }
    public static void Dpi() { SetProcessDPIAware(); }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern IntPtr CreateDesktopW(string name, string dev, IntPtr dm, uint flags, uint access, IntPtr sa);
    [DllImport("user32.dll", SetLastError = true)] static extern bool CloseDesktop(IntPtr h);
    [DllImport("user32.dll")] static extern bool EnumDesktopWindows(IntPtr hDesk, EnumProc p, IntPtr l);
    static IntPtr desk = IntPtr.Zero;
    public static string DeskName = null;                                        // non-null: the app runs on this private desktop (invisible, takes no input / focus from the user)
    public static bool MakeDesktop(string name) {
        desk = CreateDesktopW(name, null, IntPtr.Zero, 0, 0x10000000, IntPtr.Zero);          // GENERIC_ALL
        if (desk == IntPtr.Zero) return false;
        DeskName = name;
        return true;
    }
    public static void DropDesktop() { if (desk != IntPtr.Zero) { CloseDesktop(desk); desk = IntPtr.Zero; DeskName = null; } }
    public static long[] Tops(uint pid, bool vis) {
        List<long> l = new List<long>();
        EnumProc cb = delegate(IntPtr h, IntPtr p) { uint q; GetWindowThreadProcessId(h, out q); if (q == pid && (!vis || IsWindowVisible(h))) l.Add(h.ToInt64()); return true; };
        if (desk != IntPtr.Zero) EnumDesktopWindows(desk, cb, IntPtr.Zero); else EnumWindows(cb, IntPtr.Zero);
        return l.ToArray();
    }
    public static long[] Kids(long w) {
        List<long> l = new List<long>();
        EnumChildWindows(H(w), delegate(IntPtr h, IntPtr p) { l.Add(h.ToInt64()); return true; }, IntPtr.Zero);
        return l.ToArray();
    }
    public static long FindTop(uint pid, string cls, string title) {             // first visible top-level window of pid; "" = any
        foreach (long h in Tops(pid, true)) {
            if (!string.IsNullOrEmpty(cls) && !string.Equals(Cls(h), cls, StringComparison.OrdinalIgnoreCase)) continue;
            if (!string.IsNullOrEmpty(title) && Text(h) != title) continue;
            return h;
        }
        return 0;
    }
    public static string Cls(long h) { StringBuilder s = new StringBuilder(128); GetClassNameW(H(h), s, 128); return s.ToString(); }
    public static string Text(long h) {                                          // window caption (no message sent); WM_GETTEXT only when that is empty
        StringBuilder s = new StringBuilder(2048);
        GetWindowTextW(H(h), s, 2048);
        if (s.Length == 0) {
            IntPtr r; StringBuilder b = new StringBuilder(2048);
            if (SendMessageTimeoutB(H(h), 0x0D, H(2047), b, 2, 400, out r) != IntPtr.Zero) return b.ToString();
        }
        return s.ToString();
    }
    public static bool Alive(long h) { return IsWindow(H(h)); }
    public static bool Visible(long h) { return IsWindow(H(h)) && IsWindowVisible(H(h)); }
    public static bool Enabled(long h) { return IsWindowEnabled(H(h)); }
    public static bool Iconic(long h) { return IsIconic(H(h)); }
    public static int[] Scroll(long h, int bar) { SCRINFO s = new SCRINFO(); s.cbSize = 28; s.fMask = 7; GetScrollInfo(H(h), bar, ref s); return new int[] { s.nMin, s.nMax, (int)s.nPage, s.nPos }; }   // bar 0 = horizontal, 1 = vertical: min, max, page, pos
    public static int Style(long h) { return GetWindowLongW(H(h), -16); }          // GWL_STYLE: WS_VSCROLL 0x200000 / WS_HSCROLL 0x100000 = the bar is shown
    [DllImport("user32.dll")] static extern bool IsZoomed(IntPtr h);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr h);
    public static bool Zoomed(long h) { return IsZoomed(H(h)); }
    public static int Dpi(long h) { return (int)GetDpiForWindow(H(h)); }
    public static int Id(long h) { return GetDlgCtrlID(H(h)); }
    public static long Item(long d, int id) { return GetDlgItem(H(d), id).ToInt64(); }
    public static uint Tid(long h) { uint p; return GetWindowThreadProcessId(H(h), out p); }
    public static int[] WRect(long h) { RECT r; GetWindowRect(H(h), out r); return new int[] { r.L, r.T, r.R, r.B }; }
    public static int[] CRect(long h) { RECT r; GetClientRect(H(h), out r); return new int[] { r.L, r.T, r.R, r.B }; }
    public static bool Repaint(long h, int l, int t, int r, int b) { RECT dirty = new RECT { L = l, T = t, R = r, B = b }; return InvalidateRect(H(h), ref dirty, true) && UpdateWindow(H(h)); }
    public static uint GdiCount(System.Diagnostics.Process proc) { return GetGuiResources(proc.Handle, 0); }
    public static bool Post(long h, uint m, long w, long l) { return PostMessageW(H(h), m, H(w), H(l)); }

    public static long Snd(long h, uint m, long w, long l) {                     // synchronous; throws when the window does not answer in To ms
        IntPtr r;
        if (SendMessageTimeoutW(H(h), m, H(w), H(l), 0, (uint)To, out r) == IntPtr.Zero)
            throw new Exception("SendMessage 0x" + m.ToString("X") + " to window 0x" + h.ToString("X") + " got no answer within " + To + " ms");
        return unchecked((int)r.ToInt64());
    }
    public static bool Responds(long h, int ms, bool abortIfHung) {
        IntPtr r;
        return SendMessageTimeoutW(H(h), 0, IntPtr.Zero, IntPtr.Zero, abortIfHung ? 2u : 0u, (uint)ms, out r) != IntPtr.Zero;
    }
    public static long SndStr(long h, uint m, long w, string s) {                // lParam = a string (EM_REPLACESEL, LB_FINDSTRINGEXACT ...)
        IntPtr r;
        if (SendMessageTimeoutS(H(h), m, H(w), s, 0, (uint)To, out r) == IntPtr.Zero) throw new Exception("SendMessage(string) 0x" + m.ToString("X") + " got no answer");
        return unchecked((int)r.ToInt64());
    }
    public static string GetText(long h) {                                       // WM_GETTEXT (edit control contents)
        int n = (int)Snd(h, 0x0E, 0, 0);
        StringBuilder sb = new StringBuilder(n + 2);
        IntPtr r;
        if (SendMessageTimeoutB(H(h), 0x0D, H(n + 1), sb, 0, (uint)To, out r) == IntPtr.Zero) throw new Exception("WM_GETTEXT got no answer");
        return sb.ToString();
    }
    public static void SetText(long h, string s) {                               // WM_SETTEXT
        IntPtr r;
        if (SendMessageTimeoutS(H(h), 0x0C, IntPtr.Zero, s, 0, (uint)To, out r) == IntPtr.Zero) throw new Exception("WM_SETTEXT got no answer");
    }
    public static uint[] Sel(long h) {                                           // EM_GETSEL -> { start, end }
        uint s = 0, e = 0; IntPtr r;
        SendMessageTimeoutP(H(h), 0xB0, ref s, ref e, 0, (uint)To, out r);
        return new uint[] { s, e };
    }
    public static long[] Gui(uint tid) {                                         // { active window, focus window } of that thread's input queue
        GTI g = new GTI(); g.cb = Marshal.SizeOf(typeof(GTI));
        if (!GetGUIThreadInfo(tid, ref g)) return new long[] { 0, 0 };
        return new long[] { g.active.ToInt64(), g.focus.ToInt64() };
    }
    public static int ListFind(long lb, int data) {                              // index of the list item whose item data is `data`
        int n = (int)Snd(lb, 0x18B, 0, 0);
        for (int i = 0; i < n; i++) if ((int)Snd(lb, 0x199, i, 0) == data) return i;
        return -1;
    }
    public static string ListText(long lb, int i) {
        int n = (int)Snd(lb, 0x18A, i, 0);
        if (n < 0) return null;
        StringBuilder sb = new StringBuilder(n + 2); IntPtr r;
        if (SendMessageTimeoutB(H(lb), 0x189, H(i), sb, 0, (uint)To, out r) == IntPtr.Zero) return null;
        return sb.ToString();
    }
    public static System.Drawing.Bitmap Grab(long h) {                           // PrintWindow: the window's own pixels, even when covered
        RECT r; GetWindowRect(H(h), out r);
        System.Drawing.Bitmap bmp = new System.Drawing.Bitmap(r.R - r.L, r.B - r.T);
        using (System.Drawing.Graphics g = System.Drawing.Graphics.FromImage(bmp)) { IntPtr dc = g.GetHdc(); PrintWindow(H(h), dc, 2); g.ReleaseHdc(dc); }
        return bmp;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct SI { public int cb; public string reserved, desktop, title; public int x, y, xs, ys, xc, yc, fill, flags; public short show, cbRes2; public IntPtr res2, hin, hout, herr; }
    [StructLayout(LayoutKind.Sequential)] struct PI { public IntPtr hp, ht; public uint pid, tid; }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool CreateProcessW(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inh, uint fl, IntPtr env, string cwd, ref SI si, out PI pi);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    // every launched app joins a job object that kills its processes when this powershell goes away (even if it is killed): no stray (invisible) app is ever left behind
    [StructLayout(LayoutKind.Sequential)] struct JBASIC { public long perProc, perJob; public uint flags; public IntPtr minWs, maxWs; public uint activeLimit; public IntPtr affinity; public uint prio, sched; }
    [StructLayout(LayoutKind.Sequential)] struct JIO { public ulong a, b, c, d, e, f; }
    [StructLayout(LayoutKind.Sequential)] struct JEXT { public JBASIC basic; public JIO io; public IntPtr procMem, jobMem, peakProc, peakJob; }
    [DllImport("kernel32.dll")] static extern IntPtr CreateJobObjectW(IntPtr sa, string name);
    [DllImport("kernel32.dll")] static extern bool SetInformationJobObject(IntPtr job, int cls, ref JEXT info, int len);
    [DllImport("kernel32.dll")] static extern bool AssignProcessToJobObject(IntPtr job, IntPtr proc);
    [DllImport("kernel32.dll")] static extern uint ResumeThread(IntPtr t);
    static IntPtr job = IntPtr.Zero;
    static void EnsureJob() {
        if (job != IntPtr.Zero) return;
        IntPtr j = CreateJobObjectW(IntPtr.Zero, null);
        if (j == IntPtr.Zero) return;
        JEXT x = new JEXT(); x.basic.flags = 0x2000;                             // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if (!SetInformationJobObject(j, 9, ref x, Marshal.SizeOf(typeof(JEXT)))) { CloseHandle(j); return; }
        job = j;
    }
    public static uint Launch(string exe, string args, string cwd) {             // SW_SHOWNOACTIVATE (on the real desktop the app still takes the foreground by itself: see -Visible)
        SI si = new SI(); si.cb = Marshal.SizeOf(typeof(SI)); si.flags = 1; si.show = 4; si.desktop = DeskName;
        PI pi;
        EnsureJob();
        StringBuilder cmd = new StringBuilder("\"" + exe + "\"" + (string.IsNullOrEmpty(args) ? "" : " " + args));
        if (!CreateProcessW(null, cmd, IntPtr.Zero, IntPtr.Zero, false, job != IntPtr.Zero ? 4u : 0u, IntPtr.Zero, cwd, ref si, out pi)) throw new Exception("CreateProcess failed, error " + Marshal.GetLastWin32Error());   // 4 = CREATE_SUSPENDED
        if (job != IntPtr.Zero) { AssignProcessToJobObject(job, pi.hp); ResumeThread(pi.ht); }
        CloseHandle(pi.hp); CloseHandle(pi.ht);
        return pi.pid;
    }
}
'@
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition $cs
[U]::Dpi()
[U]::To = [Math]::Max($Timeout * 2, 8000)
$deskName = ''
if (-not $Visible) {                                                             # a private desktop: the app's windows never reach the user's screen, keyboard or foreground
    $deskName = 'npm_ui_' + [guid]::NewGuid().ToString('N').Substring(0, 8)
    if (-not [U]::MakeDesktop($deskName)) { $deskName = '' }
}

# win32 message / key constants (system values, not app values)
$WM_NULL = 0; $WM_CLOSE = 0x10; $WM_SETTEXT = 0xC; $WM_KEYDOWN = 0x100; $WM_COMMAND = 0x111
$BM_GETCHECK = 0xF0; $BM_CLICK = 0xF5
$EM_GETSEL = 0xB0; $EM_SETSEL = 0xB1; $EM_GETMODIFY = 0xB8; $EM_SETMODIFY = 0xB9; $EM_CANUNDO = 0xC6; $EM_UNDO = 0xC7
$EM_REPLACESEL = 0xC2; $EM_LINEFROMCHAR = 0xC9; $EM_EMPTYUNDOBUFFER = 0xCD
$LB_SETCURSEL = 0x186; $LB_GETCURSEL = 0x188; $LB_GETCOUNT = 0x18B; $LB_FINDSTRINGEXACT = 0x1A2
$VK_RETURN = 0x0D; $VK_ESCAPE = 0x1B; $VK_DOWN = 0x28
$IDOK = 1; $IDCANCEL = 2                                                         # standard dialog button ids (what IsDialogMessage / the app's esc + enter use)
$BOX_BTN2 = 102; $BOX_BTN3 = 103                                                 # MpAsk (dialog.c MsgProc): the 1st button is IDOK, the 2nd 100+2, the 3rd 100+3

# ---------------------------------------------------------------------------------------- ids parsed from the sources
function Eval-Expr([string]$e, $own, $base) {                                  # numbers, names and + / - (all the enums here need)
    $sum = 0; $sign = 1
    foreach ($m in [regex]::Matches($e, '0x[0-9a-fA-F]+|\d+|\w+|[+\-]')) {
        $v = $m.Value
        if ($v -eq '+') { $sign = 1; continue }
        if ($v -eq '-') { $sign = -1; continue }
        if ($v -match '^0x') { $x = [Convert]::ToInt32($v.Substring(2), 16) }
        elseif ($v -match '^\d+$') { $x = [int]$v }
        elseif ($own.ContainsKey($v)) { $x = $own[$v] }
        elseif ($base.ContainsKey($v)) { $x = $base[$v] }
        else { throw ("cannot evaluate '" + $e + "': unknown name " + $v) }
        $sum += $sign * $x; $sign = 1
    }
    return $sum
}
function Read-Consts([string]$path, $base = @{}) {                              # enum { A = 1, B, C } and #define N 12 -> name/value table
    $t = [IO.File]::ReadAllText($path)
    $t = [regex]::Replace($t, '/\*.*?\*/', ' ', 'Singleline')
    $t = [regex]::Replace($t, '//[^\r\n]*', ' ')
    $c = @{}
    foreach ($m in [regex]::Matches($t, '(?m)^\s*#define\s+(\w+)\s+(0x[0-9a-fA-F]+|\d+)\s*$')) { $c[$m.Groups[1].Value] = Eval-Expr $m.Groups[2].Value $c $base }
    foreach ($m in [regex]::Matches($t, 'enum\s*\w*\s*\{([^}]*)\}')) {
        $n = 0
        foreach ($item in $m.Groups[1].Value.Split(',')) {
            $s = $item.Trim()
            if (-not $s) { continue }
            if ($s -match '^(\w+)\s*=\s*(.+)$') { $name = $Matches[1]; $n = Eval-Expr $Matches[2] $c $base } else { $name = $s }
            $c[$name] = $n; $n++
        }
    }
    return $c
}
$IDM = Read-Consts (Join-Path $Src 'mp.h')                                       # IDM_*, ENC_*, EOL_*, FONT_MIN ...
$IDF = Read-Consts (Join-Path $Src 'find.c') $IDM                                # ID_WHAT ... ID_REPLACEALL, ID_LINE
$IDO = Read-Consts (Join-Path $Src 'filedlg.c') $IDM                             # ID_ENCLIST (the encoding picker; open / save as are native now)
$IDT = Read-Consts (Join-Path $Src 'fontdlg.c') $IDM                             # ID_LIST ... ID_RESET
$IDE = Read-Consts (Join-Path $Src 'edit.c') $IDM                                # IDC_EDIT
foreach ($need in @(@('IDM', $IDM, 'IDM_EDIT_FIND'), @('find.c', $IDF, 'ID_REPLACEALL'), @('filedlg.c', $IDO, 'ID_ENCLIST'), @('fontdlg.c', $IDT, 'ID_RESET'), @('edit.c', $IDE, 'IDC_EDIT'))) {
    if (-not $need[1].ContainsKey($need[2])) { Write-Output ('FATAL cannot parse ' + $need[0] + ': ' + $need[2] + ' missing'); exit 2 }
}
$mpText = [IO.File]::ReadAllText((Join-Path $Src 'mp.h'))
$MainClass = [regex]::Match($mpText, '#define\s+APP_CLASS\s+L"([^"]+)"').Groups[1].Value
$AppName   = [regex]::Match($mpText, '#define\s+APP_NAME\s+L"([^"]+)"').Groups[1].Value
if (-not $MainClass -or -not $AppName) { Write-Output 'FATAL cannot parse APP_CLASS / APP_NAME from mp.h'; exit 2 }

# ------------------------------------------------------------------------------------------------------- reporting
$script:cnt = @{ PASS = 0; FAIL = 0; SKIP = 0 }
function Show($s) {                                                              # printable ascii form of a string for messages
    if ($null -eq $s) { return '<null>' }
    $t = [string]$s
    if ($t.Length -gt 100) { $t = $t.Substring(0, 100) + '...(' + $t.Length + ' chars)' }
    $sb = New-Object Text.StringBuilder
    foreach ($ch in $t.ToCharArray()) {
        $c = [int]$ch
        if ($c -eq 13) { [void]$sb.Append('\r') } elseif ($c -eq 10) { [void]$sb.Append('\n') } elseif ($c -eq 9) { [void]$sb.Append('\t') }
        elseif ($c -lt 32 -or $c -gt 126) { [void]$sb.Append(('\u{0:X4}' -f $c)) } else { [void]$sb.Append($ch) }
    }
    return $sb.ToString()
}
function Pass([string]$n) { Write-Output ('PASS ' + $n); $script:cnt.PASS++ }
function Fail([string]$n, [string]$why) { Write-Output ('FAIL ' + $n + ': ' + $why); $script:cnt.FAIL++ }
function Skip([string]$n, [string]$why) { Write-Output ('SKIP ' + $n + ': ' + $why); $script:cnt.SKIP++ }
function Info([string]$s) { Write-Output ('INFO ' + $s) }
function Ck([string]$n, $ok, [string]$why = '') { if ($ok) { Pass $n } else { Fail $n $why } }
function CkEq([string]$n, $exp, $act) { if ($exp -ceq $act) { Pass $n } else { Fail $n ('expected [' + (Show $exp) + '] actual [' + (Show $act) + ']') } }
function CkText([string]$n, [string]$exp, [string]$act) {                        # long texts: report the first difference
    if ($exp -ceq $act) { Pass $n; return }
    $i = 0; $m = [Math]::Min($exp.Length, $act.Length)
    while ($i -lt $m -and $exp[$i] -ceq $act[$i]) { $i++ }
    $a = [Math]::Max(0, $i - 12)
    Fail $n ('texts differ, expected ' + $exp.Length + ' chars, actual ' + $act.Length + ' chars, first difference at ' + $i +
             ': expected [' + (Show $exp.Substring($a, [Math]::Min(30, $exp.Length - $a))) + '] actual [' + (Show $act.Substring($a, [Math]::Min(30, $act.Length - $a))) + ']')
}

# ----------------------------------------------------------------------------------------------- generic helpers
function WaitFor([scriptblock]$cond, [int]$ms = 0) {                             # polls $cond until it returns something truthy
    if ($ms -le 0) { $ms = $Timeout }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($true) {
        $r = & $cond
        if ($r) { return $r }
        if ($sw.ElapsedMilliseconds -ge $ms) { return $null }
        Start-Sleep -Milliseconds 20
    }
}
function Snd($h, $m, $w = 0, $l = 0) { [U]::Snd([long]$h, [uint32]$m, [long]$w, [long]$l) }
function Pst($h, $m, $w = 0, $l = 0) { [void][U]::Post([long]$h, [uint32]$m, [long]$w, [long]$l) }
function Trim-Px($app) { return [int][Math]::Round($IDM.SBAR_TRIM * ([U]::Dpi([long]$app.Main)) / 96, [MidpointRounding]::AwayFromZero) }   # the editor's window overhangs the visible area by this many px at the right and at the bottom (SBAR_TRIM: thinner scrollbars)
function Get-Edit($app) {                                                       # re-found every time: toggling word wrap re-creates the control
    foreach ($k in [U]::Kids([long]$app.Main)) { if ([U]::Cls($k) -eq 'Edit') { return [long]$k } }
    throw 'the editor (class EDIT) is not a child of the main window'
}
function Ed-Text($app) { [U]::GetText((Get-Edit $app)) }
function Ed-Set($app, [string]$t) { [U]::SetText((Get-Edit $app), $t) }
function Ed-Modified($app) { (Snd (Get-Edit $app) $EM_GETMODIFY) -ne 0 }
function Ed-Dirty($app, [string]$t = 'x') { [void][U]::SndStr((Get-Edit $app), $EM_REPLACESEL, 1, $t) }   # a real edit: sets the modified flag + EN_CHANGE
function Title($app) { [U]::Text([long]$app.Main) }
function Default-Name($app) {                                                    # the unsaved document's default name, read from the title: "mint-" + 4 characters of 0-9 a-z (or $null)
    if ((Title $app) -cmatch '^\*?(mint-[0-9a-z]{4}) - notepad mint$') { return $Matches[1] }
    return $null
}
function Wait-Title($app, [string]$t, [int]$ms = 0) { [bool](WaitFor { (Title $app) -ceq $t } $ms) }
function Expected-Text([string]$path, $enc = $null) {                            # file bytes -> text with CRLF line breaks (what the editor holds)
    if (-not $enc) { $enc = New-Object Text.UTF8Encoding($false) }
    return [regex]::Replace($enc.GetString([IO.File]::ReadAllBytes($path)), "\r\n|\r|\n", "`r`n")
}
function U8([string]$s) { (New-Object Text.UTF8Encoding($false)).GetBytes($s) }
function Chars([int[]]$codes) { -join ($codes | ForEach-Object { [char]$_ }) }    # string from utf-16 code units (keeps this file ascii)
function Read-Ini([string]$path) {                                               # utf-16 ini -> @{ section = @{ key = value } }
    $r = @{}
    if (-not (Test-Path -LiteralPath $path)) { return $r }
    $sec = ''
    $raw = $null
    for ($try = 0; $try -lt 15 -and $raw -eq $null; $try++) {                    # the app holds the file exclusively while it writes a key: retry
        try { $raw = [IO.File]::ReadAllText($path, [Text.Encoding]::Unicode) } catch { Start-Sleep -Milliseconds 30 }
    }
    if ($raw -eq $null) { return $r }
    foreach ($line in $raw -split "\r?\n") {
        if ($line -match '^\[(.+)\]\s*$') { $sec = $Matches[1]; if (-not $r.ContainsKey($sec)) { $r[$sec] = @{} } }
        elseif ($line -match '^([^=;]+)=(.*)$' -and $sec) { $r[$sec][$Matches[1].Trim()] = $Matches[2] }
    }
    return $r
}
function Ini-Val($app, [string]$sec, [string]$key, [string]$want = $null, [int]$ms = 15000) {  # polls settings.ini (written by the app, ~25 separate file rewrites) for key (= want); one run once saw a late italic=1 with a 2.5 s window, and a busy machine (other sessions rendering) made 6 s too short
    $v = $null
    $ok = WaitFor { $i = Read-Ini $app.Ini; if ($i.ContainsKey($sec) -and $i[$sec].ContainsKey($key)) { $script:iniV = $i[$sec][$key]; if ($want -eq $null -or $script:iniV -ceq $want) { $true } } } $ms
    if ($ok) { return $script:iniV }
    $i = Read-Ini $app.Ini
    if ($i.ContainsKey($sec) -and $i[$sec].ContainsKey($key)) { return $i[$sec][$key] }
    return $null
}

# ------------------------------------------------------------------------------------------------ app management
$work = Join-Path ([IO.Path]::GetTempPath()) ('npm_ui_' + [guid]::NewGuid().ToString('N').Substring(0, 8))
[void](New-Item -ItemType Directory -Force -Path $work)
$exeCopy = Join-Path $work 'mint_ui_test.exe'                                    # (not "notepad-mint.exe": the native file dialogs remember their last folder per exe NAME in the registry, the real app's entry must stay untouched)
for ($try = 0; $try -lt 8; $try++) {                                             # the lead's build may be rewriting the exe right now
    try { Copy-Item -LiteralPath $Exe -Destination $exeCopy -Force; if ((Get-Item -LiteralPath $exeCopy).Length -gt 50000) { break } } catch {}
    Start-Sleep -Milliseconds 500
}
$script:apps = @()
$script:launchN = 0

function Start-App([string]$file = '', [string]$appdata = '') {
    if (-not $appdata) { $script:launchN++; $appdata = Join-Path $work ('appdata' + $script:launchN) }
    [void](New-Item -ItemType Directory -Force -Path $appdata)
    $env:APPDATA = $appdata                                                      # the app reads %APPDATA%\notepad-mint\settings.ini (prefs.c IniLocate)
    $argLine = ''
    if ($file) { $argLine = '"' + $file + '"' }
    $procId = [U]::Launch($exeCopy, $argLine, $work)
    $proc = $null
    try { $proc = [Diagnostics.Process]::GetProcessById([int]$procId) } catch { throw 'the exe exited right after it was launched (not a gui app, or it crashed at startup)' }
    $app = [pscustomobject]@{ Pid = [uint32]$procId; Proc = $proc; AppData = $appdata
                              Ini = (Join-Path $appdata 'notepad-mint\settings.ini'); Main = [long]0; Tid = [uint32]0 }
    $script:apps += $app
    $main = WaitFor { [U]::FindTop($app.Pid, $MainClass, '') } 8000
    if (-not $main) { throw ('the main window (class ' + $MainClass + ') did not appear') }
    $app.Main = [long]$main
    $app.Tid = [U]::Tid([long]$main)
    return $app
}

function Cmd($app, [string]$name) {                                              # WM_COMMAND(IDM_x): posted (many commands run a modal loop)
    if (-not $IDM.ContainsKey($name)) { throw ('unknown command ' + $name + ' (not in mp.h)') }
    Pst $app.Main $WM_COMMAND $IDM[$name] 0
}
function Dump-Tree([long]$h) {
    $r = [U]::WRect($h)
    Write-Host ('DUMP window 0x{0:X} class={1} title=[{2}] rect={3},{4},{5},{6}' -f $h, [U]::Cls($h), (Show ([U]::Text($h))), $r[0], $r[1], $r[2], $r[3])
    foreach ($k in [U]::Kids($h)) {
        $cl = [U]::Cls($k); $x = [U]::WRect($k); $t = ''; $extra = ''
        try {
            if ($cl -eq 'Edit') { $t = [U]::GetText($k) }
            elseif ($cl -eq 'ListBox') { $t = '(' + (Snd $k $LB_GETCOUNT) + ' items)' }
            else { $t = [U]::Text($k) }
            if ($cl -eq 'mp_btn') { $extra = ' check=' + (Snd $k $BM_GETCHECK) }
        } catch { $t = '<' + $_.Exception.Message + '>' }
        Write-Host ('DUMP   {0,-10} id={1,-5} en={2} vis={3} rel=({4},{5} {6}x{7}){8} text=[{9}]' -f $cl, [U]::Id($k), [int][U]::Enabled($k), [int][U]::Visible($k),
                      ($x[0] - $r[0]), ($x[1] - $r[1]), ($x[2] - $x[0]), ($x[3] - $x[1]), $extra, (Show $t))
    }
}
function Wait-Win($app, [string]$cls, [string]$title, [int]$ms = 0) {            # a visible top-level window of the app; throws (= a clear FAIL) when it never shows
    $h = WaitFor { [U]::FindTop($app.Pid, $cls, $title) } $ms
    if (-not $h) { throw ("window '" + $title + "' (class " + $cls + ") did not appear") }
    if ($Dump) { Dump-Tree ([long]$h) }
    return [long]$h
}
function Gone([long]$h, [int]$ms = 0) { [bool](WaitFor { -not [U]::Visible($h) } $ms) }
function Press($dlg, [int]$id) {                                                 # BM_CLICK (posted): what a click does in the app's own mp_btn buttons
    $c = [U]::Item([long]$dlg, $id)
    if (-not $c) { throw ('control id ' + $id + ' not found in window 0x' + ([long]$dlg).ToString('X')) }
    [void][U]::Post([long]$c, $BM_CLICK, 0, 0)
}
function Ctl($dlg, [int]$id) {
    $c = [U]::Item([long]$dlg, $id)
    if (-not $c) { throw ('control id ' + $id + ' not found in window 0x' + ([long]$dlg).ToString('X')) }
    return [long]$c
}
function Set-Field($dlg, [int]$id, [string]$t) { [U]::SetText((Ctl $dlg $id), $t) }
function Get-Field($dlg, [int]$id) { [U]::GetText((Ctl $dlg $id)) }
function Chk($dlg, [int]$id) { [int](Snd (Ctl $dlg $id) $BM_GETCHECK) }
function Is-On($dlg, [int]$id) { [U]::Enabled((Ctl $dlg $id)) }
function Wait-Box($app, [string]$title, [int]$ms = 0) {                          # an app message box (MpAsk): class mp_msg
    $h = Wait-Win $app 'mp_msg' $title $ms
    $txt = ''
    foreach ($k in [U]::Kids($h)) { if ([U]::Cls($k) -eq 'Static') { $txt = [U]::Text($k); break } }
    return [pscustomobject]@{ H = [long]$h; Text = $txt }
}
function Box-Press($box, [int]$id) {                                             # buttons: 1 = first (IDOK), 102 = second, 103 = third (dialog.c MsgProc)
    Press $box.H $id
    return (Gone $box.H)
}
function Wait-Sel($ed, [int]$s, [int]$e, [int]$ms = 1500) {
    $sw = [Diagnostics.Stopwatch]::StartNew(); $x = $null
    do {
        $x = [U]::Sel([long]$ed)
        if ($x[0] -eq $s -and $x[1] -eq $e) { return $true }
        Start-Sleep -Milliseconds 20
    } while ($sw.ElapsedMilliseconds -lt $ms)
    $script:lastSel = '(' + $x[0] + ',' + $x[1] + ')'
    return $false
}
function CkSel([string]$n, $app, [int]$s, [int]$e) {
    if (Wait-Sel (Get-Edit $app) $s $e) { Pass $n } else { Fail $n ('expected selection (' + $s + ',' + $e + ') actual ' + $script:lastSel) }
}

function Close-Dialogs($app) {                                                   # leaves only the main window: esc for menu popups, WM_CLOSE for the rest
    for ($i = 0; $i -lt 14; $i++) {
        $others = @([U]::Tops($app.Pid, $true) | Where-Object { $_ -ne $app.Main -and ([U]::Cls($_) -like 'mp_*' -or [U]::Cls($_) -eq '#32770') })
        if ($others.Count -eq 0) { return }
        foreach ($w in $others) {
            if ([U]::Cls($w) -eq 'mp_popup') { Pst $w $WM_KEYDOWN $VK_ESCAPE 0 } else { Pst $w $WM_CLOSE 0 0 }
        }
        Start-Sleep -Milliseconds 150
    }
}
function Stop-App($app) {                                                        # clean shutdown (no save prompt), else kill
    try {
        if ($app.Proc.HasExited) { return }
        Close-Dialogs $app
        try { [void](Snd (Get-Edit $app) $EM_SETMODIFY 0 0) } catch {}
        Pst $app.Main $WM_CLOSE 0 0
        if (-not $app.Proc.WaitForExit(2500)) { $app.Proc.Kill(); [void]$app.Proc.WaitForExit(2000) }
    } catch { try { $app.Proc.Kill() } catch {} }
}
function Stop-All() { foreach ($a in $script:apps) { Stop-App $a }; $script:apps = @() }
function Run-Case([string]$id) {
    if ($Only -and (($Only -split '[ ,]+') -notcontains $id)) { return }
    Write-Output ('---- ' + $id)
    try { & ('Test-' + $id) } catch { Fail $id ('exception: ' + $_.Exception.Message + ' (script line ' + $_.InvocationInfo.ScriptLineNumber + ')') }
    finally { try { Stop-All } catch {} }
}
function Kill-Mine() {                                                           # any leftover process whose exe lives in our temp dir
    Get-Process -Name 'mint_ui_test' -ErrorAction SilentlyContinue | Where-Object { try { $_.Path -like ($work + '*') } catch { $false } } | ForEach-Object { try { $_.Kill() } catch {} }
}

$sampleMulti = Join-Path $Tests 'multilingual-sample.txt'
$sampleWrap  = Join-Path $Tests 'wordwrap-sample.txt'

function CkChk([string]$n, $dlg, [int]$id, [int]$want) {                        # a checkbox / radio reaches the wanted state (BM_CLICK is posted)
    if (WaitFor { (Chk $dlg $id) -eq $want } 1500) { Pass $n } else { Fail $n ('expected check state ' + $want + ' actual ' + (Chk $dlg $id)) }
}
function CkEdText([string]$n, $app, [string]$exp, [int]$ms = 2000) {            # the editor text reaches the expected text
    [void](WaitFor { (Ed-Text $app) -ceq $exp } $ms)
    CkText $n $exp (Ed-Text $app)
}
function Reset-Doc($app, [string]$t) {                                           # back to a known text: no undo history, caret at 0, clean
    $ed = Get-Edit $app
    [U]::SetText($ed, $t)
    [void](Snd $ed $EM_EMPTYUNDOBUFFER 0 0)
    [void](Snd $ed $EM_SETSEL 0 0)
    [void](Snd $ed $EM_SETMODIFY 0 0)
}
function Line-Starts([string]$t) {                                               # char index where each line starts
    $s = New-Object System.Collections.ArrayList
    [void]$s.Add(0)
    for ($i = 0; $i -lt $t.Length; $i++) { if ($t[$i] -eq "`n") { [void]$s.Add($i + 1) } }
    return , @($s)
}

# ============================================================================================================ T1
function Test-T1 {                                                               # startup: DocRead
    $app = Start-App $sampleMulti
    [void](Wait-Title $app 'multilingual-sample.txt - notepad mint')
    CkEq 'T1.1 title is "multilingual-sample.txt - notepad mint"' 'multilingual-sample.txt - notepad mint' (Title $app)
    CkEq 'T1.2 main window class' $MainClass ([U]::Cls($app.Main))
    CkEq 'T1.3 the editor is the EDIT child with the edit control id' $IDE.IDC_EDIT ([U]::Id((Get-Edit $app)))
    $exp = Expected-Text $sampleMulti
    Ck 'T1.4 (sanity of the expectation) decoded sample holds japanese + an emoji surrogate pair + CRLF breaks' (
        $exp.Contains((Chars 0x65E5, 0x672C, 0x8A9E)) -and $exp.Contains([char]::ConvertFromUtf32(0x1F600)) -and $exp.Contains("`r`n") -and -not $exp.Contains("`n`n")) 'the expected text was not built as intended'
    CkText 'T1.5 editor text == the file decoded as utf-8, LF -> CRLF (DocRead)' $exp (Ed-Text $app)
    Ck 'T1.6 the loaded document is not modified' (-not (Ed-Modified $app)) 'EM_GETMODIFY is set after loading'
    CkSel 'T1.7 caret at the start of the text' $app 0 0
}

# ============================================================================================================ T2
function Test-T2 {                                                               # find
    $app = Start-App $sampleMulti
    [void](Wait-Title $app 'multilingual-sample.txt - notepad mint')
    $ed = Get-Edit $app
    $txt = Ed-Text $app
    $ci = [StringComparison]::OrdinalIgnoreCase
    $iq = $txt.IndexOf('quick', [StringComparison]::Ordinal)
    $i1 = $txt.IndexOf('the', $ci); $i2 = $txt.IndexOf('the', $i1 + 3, $ci)
    Ck 'T2.0 (sanity) the sample has "quick" once and "the" at least twice' ($iq -ge 0 -and $i1 -ge 0 -and $i2 -gt $i1) ('quick at ' + $iq + ', the at ' + $i1 + ' / ' + $i2)

    Cmd $app 'IDM_EDIT_FIND'
    $dlg = Wait-Win $app 'mp_find' 'find'
    Pass 'T2.1 find dialog opens (class mp_find, title "find")'
    Ck 'T2.2 "find next" (IDOK) is disabled while the box is empty' (-not (Is-On $dlg $IDOK)) 'IDOK is enabled with an empty find box'
    Set-Field $dlg $IDF.ID_WHAT 'quick'
    Ck 'T2.3 "find next" becomes enabled once the box has text' ([bool](WaitFor { Is-On $dlg $IDOK } 1500)) 'IDOK is still disabled'
    CkEq 'T2.4 wrap around is checked by default' 1 (Chk $dlg $IDF.ID_WRAP)
    Press $dlg $IDOK
    CkSel 'T2.5 find next selects the first "quick"' $app $iq ($iq + 5)
    [void](Snd $ed $EM_SETSEL ($iq + 15) ($iq + 15))                             # caret past the only match: the next find has to wrap
    Press $dlg $IDOK
    CkSel 'T2.6 find next again wraps around to the same match' $app $iq ($iq + 5)

    Press $dlg $IDF.ID_CASE
    CkChk 'T2.7 match case checkbox toggles on (BM_CLICK on the mp_btn)' $dlg $IDF.ID_CASE 1
    Set-Field $dlg $IDF.ID_WHAT 'QUICK'
    Press $dlg $IDOK
    $box = Wait-Box $app $AppName
    Ck 'T2.8 match case on: "QUICK" is not found, a message box says cannot find "QUICK"' ($box.Text -like '*cannot find "QUICK"*') ('box text [' + (Show $box.Text) + ']')
    Ck 'T2.9 the message box closes with its ok button' (Box-Press $box $IDOK) 'box still visible'
    Press $dlg $IDF.ID_CASE
    CkChk 'T2.10 match case toggles off again' $dlg $IDF.ID_CASE 0
    Set-Field $dlg $IDF.ID_WHAT 'QUICK'
    [void](Snd $ed $EM_SETSEL 0 0)
    Press $dlg $IDOK
    CkSel 'T2.11 match case off: "QUICK" finds "quick"' $app $iq ($iq + 5)

    Set-Field $dlg $IDF.ID_WHAT 'the'
    Press $dlg $IDF.ID_UP
    CkChk 'T2.12 direction up radio on' $dlg $IDF.ID_UP 1
    CkChk 'T2.13 direction down radio off (radio group)' $dlg $IDF.ID_DOWN 0
    [void](Snd $ed $EM_SETSEL $txt.Length $txt.Length)
    Press $dlg $IDOK
    CkSel 'T2.14 up: first find from the end of the text selects the last "the"' $app $i2 ($i2 + 3)
    Press $dlg $IDOK
    CkSel 'T2.15 up: the next find moves to the previous match (the selection start decreases)' $app $i1 ($i1 + 3)
    Press $dlg $IDOK
    CkSel 'T2.16 up: one more wraps around to the last match' $app $i2 ($i2 + 3)

    Press $dlg $IDF.ID_WRAP
    CkChk 'T2.17 wrap around toggles off' $dlg $IDF.ID_WRAP 0
    [void](Snd $ed $EM_SETSEL 0 0)
    Press $dlg $IDOK
    $box = Wait-Box $app $AppName
    Ck 'T2.18 wrap around off: an up search from the start finds nothing (cannot find "the")' ($box.Text -like '*cannot find "the"*') ('box text [' + (Show $box.Text) + ']')
    [void](Box-Press $box $IDOK)
    Press $dlg $IDF.ID_WRAP
    CkChk 'T2.19 wrap around toggles on again' $dlg $IDF.ID_WRAP 1

    [void](Snd $ed $EM_SETSEL $i2 ($i2 + 3))
    Press $dlg $IDCANCEL
    Ck 'T2.20 cancel closes the find dialog' (Gone $dlg) 'the dialog is still visible'
    $focusOk = [bool](WaitFor { ([U]::Gui($app.Tid))[1] -eq $ed } 1500)           # GetGUIThreadInfo of the app thread: its own idea of the focus window
    if ($focusOk) { Pass 'T2.21 the editor regains the keyboard focus after the dialog closes' }
    elseif (([U]::Gui($app.Tid))[0] -eq 0) { Skip 'T2.21 the editor regains the keyboard focus after the dialog closes' 'the app has no active window in its input queue (on the private desktop nothing activates it, so there is no focus to look at): run with -Visible' }
    else { Fail 'T2.21 the editor regains the keyboard focus after the dialog closes' ('focus is 0x' + ([U]::Gui($app.Tid))[1].ToString('X') + ', the editor is 0x' + $ed.ToString('X')) }

    Cmd $app 'IDM_EDIT_FINDNEXT'
    CkSel 'T2.22 f3 (find next) with the dialog closed uses the remembered text and wraps' $app $i1 ($i1 + 3)
    Cmd $app 'IDM_EDIT_FINDPREV'
    CkSel 'T2.23 shift+f3 (find previous) searches up and wraps' $app $i2 ($i2 + 3)
}

# ============================================================================================================ T3
function Test-T3 {                                                               # replace
    $app = Start-App $sampleMulti
    [void](Wait-Title $app 'multilingual-sample.txt - notepad mint')
    $ed = Get-Edit $app
    $orig = Ed-Text $app
    $ci = [StringComparison]::OrdinalIgnoreCase
    $i1 = $orig.IndexOf('the', $ci); $i2 = $orig.IndexOf('the', $i1 + 3, $ci)

    Cmd $app 'IDM_EDIT_REPLACE'
    $dlg = Wait-Win $app 'mp_find' 'replace'
    Pass 'T3.1 replace dialog opens (class mp_find, title "replace")'
    Set-Field $dlg $IDF.ID_WHAT 'the'
    Set-Field $dlg $IDF.ID_WITH 'THE'
    Ck 'T3.2 "replace" and "replace all" are enabled once "find what" has text' ([bool](WaitFor { (Is-On $dlg $IDF.ID_REPLACE) -and (Is-On $dlg $IDF.ID_REPLACEALL) } 1500)) 'a button is still disabled'
    Press $dlg $IDF.ID_REPLACE
    CkSel 'T3.3 replace with no match selected only selects the first match' $app $i1 ($i1 + 3)
    CkText 'T3.4 ... and does not change the text' $orig (Ed-Text $app)
    Press $dlg $IDF.ID_REPLACE
    CkSel 'T3.5 replace #2 replaced the selected match, then moved on to the next one' $app $i2 ($i2 + 3)
    CkText 'T3.6 ... the first "the" became "THE", the rest is untouched' ($orig.Substring(0, $i1) + 'THE' + $orig.Substring($i1 + 3)) (Ed-Text $app)

    Reset-Doc $app $orig
    Press $dlg $IDF.ID_REPLACEALL
    $expAll = [regex]::Replace($orig, 'the', 'THE', 'IgnoreCase')
    CkEdText 'T3.7 replace all (match case off) gives the expected text' $app $expAll
    Ck 'T3.8 replace all left something to undo' ((Snd $ed $EM_CANUNDO) -ne 0) 'EM_CANUNDO is 0'
    [void](Snd $ed $EM_UNDO)
    CkEdText 'T3.9 ONE undo restores the original text completely (replace all is a single undo step)' $app $orig

    Reset-Doc $app $orig
    Set-Field $dlg $IDF.ID_WITH 'X'
    [void](Snd $ed $EM_SETSEL $orig.Length $orig.Length)
    Press $dlg $IDF.ID_REPLACEALL
    $expX = [regex]::Replace($orig, 'the', 'X', 'IgnoreCase')
    CkEdText 'T3.10 replace all with a shorter replacement' $app $expX
    CkSel 'T3.11 the caret keeps its place (the end of the text) although the text got shorter' $app $expX.Length $expX.Length

    Reset-Doc $app $orig
    Set-Field $dlg $IDF.ID_WHAT 'zzzqqq'
    Press $dlg $IDF.ID_REPLACEALL
    $box = Wait-Box $app $AppName
    Ck 'T3.12 replace all with zero matches: cannot find "zzzqqq"' ($box.Text -like '*cannot find "zzzqqq"*') ('box text [' + (Show $box.Text) + ']')
    [void](Box-Press $box $IDOK)
    Press $dlg $IDF.ID_REPLACE
    $box = Wait-Box $app $AppName
    Ck 'T3.13 replace with zero matches: cannot find "zzzqqq"' ($box.Text -like '*cannot find "zzzqqq"*') ('box text [' + (Show $box.Text) + ']')
    [void](Box-Press $box $IDOK)
    CkText 'T3.14 zero matches leave the text alone' $orig (Ed-Text $app)
    Press $dlg $IDCANCEL
    Ck 'T3.15 cancel closes the replace dialog' (Gone $dlg) 'the dialog is still visible'
}

# ============================================================================================================ T4
function Test-T4 {                                                               # go to line
    $app = Start-App $sampleMulti
    [void](Wait-Title $app 'multilingual-sample.txt - notepad mint')
    $ed = Get-Edit $app
    $txt = Ed-Text $app
    $st = Line-Starts $txt
    $lines = $st.Count
    [void](Snd $ed $EM_SETSEL ($st[2] + 4) ($st[2] + 4))                         # inside line 3

    Cmd $app 'IDM_EDIT_GOTO'
    $dlg = Wait-Win $app 'mp_goto' 'go to line'
    Pass 'T4.1 go to dialog opens (class mp_goto, title "go to line")'
    CkEq 'T4.2 the edit is prefilled with the current line (3)' '3' (Get-Field $dlg $IDF.ID_LINE)
    Set-Field $dlg $IDF.ID_LINE '5'
    Press $dlg $IDOK
    Ck 'T4.3 ok closes the dialog' (Gone $dlg) 'the dialog is still visible'
    CkSel 'T4.4 the caret is at the start of line 5' $app $st[4] $st[4]
    CkEq 'T4.5 EM_LINEFROMCHAR of the caret says line 5 (0-based 4)' 4 (Snd (Get-Edit $app) $EM_LINEFROMCHAR (-1))

    Cmd $app 'IDM_EDIT_GOTO'
    $dlg = Wait-Win $app 'mp_goto' 'go to line'
    CkEq 'T4.6 reopened: prefilled with the new current line (5)' '5' (Get-Field $dlg $IDF.ID_LINE)
    Set-Field $dlg $IDF.ID_LINE '99999'
    Press $dlg $IDOK
    $box = Wait-Box $app 'go to line'
    CkEq 'T4.7 line 99999: message "the line number is beyond the total number of lines"' 'the line number is beyond the total number of lines' $box.Text
    Ck 'T4.8 the message box closes with ok' (Box-Press $box $IDOK) 'box still visible'
    Ck 'T4.9 ... and the go to dialog stays open' ([U]::Visible($dlg)) 'the go to dialog went away'
    Set-Field $dlg $IDF.ID_LINE ([string]($lines + 1))
    Press $dlg $IDOK
    $box = Wait-Box $app 'go to line'
    Ck ('T4.10 line ' + ($lines + 1) + ' (one past the last of ' + $lines + ') is beyond the end') ($box.Text -like '*beyond the total number of lines*') ('box text [' + (Show $box.Text) + ']')
    [void](Box-Press $box $IDOK)
    Press $dlg $IDCANCEL
    Ck 'T4.11 cancel closes the dialog' (Gone $dlg) 'the dialog is still visible'
    CkSel 'T4.12 cancel (after the failed tries) leaves the caret where it was (line 5)' $app $st[4] $st[4]

    Cmd $app 'IDM_EDIT_GOTO'
    $dlg = Wait-Win $app 'mp_goto' 'go to line'
    Set-Field $dlg $IDF.ID_LINE ([string]$lines)
    Press $dlg $IDOK
    Ck ('T4.13 the last line (' + $lines + ') is a valid target: the dialog closes') (Gone $dlg) 'the dialog is still visible'
    CkSel 'T4.14 ... and the caret is at the start of the last line' $app $st[$lines - 1] $st[$lines - 1]
}

function Read-Bytes([string]$p) { try { return , [IO.File]::ReadAllBytes($p) } catch { return $null } }   # null while the app still has the file open
function Hex($b, [int]$n = 16) { (@($b) | Select-Object -First $n | ForEach-Object { '{0:X2}' -f $_ }) -join ' ' }
function Same-Bytes($a, $b) { $a -ne $null -and $b -ne $null -and ([Convert]::ToBase64String([byte[]]$a) -ceq [Convert]::ToBase64String([byte[]]$b)) }
function CkBytes([string]$n, $exp, [string]$path, [int]$ms = 2500) {             # the file on disk reaches the expected bytes
    [void](WaitFor { Same-Bytes $exp (Read-Bytes $path) } $ms)
    $act = Read-Bytes $path
    if (Same-Bytes $exp $act) { Pass $n }
    elseif ($act -eq $null) { Fail $n ('cannot read ' + $path) }
    else { Fail $n ('expected ' + @($exp).Count + ' bytes [' + (Hex $exp 20) + ' ...] actual ' + @($act).Count + ' bytes [' + (Hex $act 20) + ' ...]') }
}

# ------------------------------------------------------------------------------------ native open / save as (comdlg32)
# open and save as are the windows file dialogs (GetOpenFileNameW / GetSaveFileNameW): a top-level window of class #32770 owned by the main
# window, running inside the app's own process (the app's own boxes are mp_msg, its own dialogs mp_*). their texts are localized: nothing
# below looks at a caption or a label, only at classes, control ids, ownership and what the app does afterwards. a message box / task dialog
# the file dialog puts up (file not found, replace the file?) is another #32770, owned by the file dialog.
# the file name field is an Edit: in the open dialog inside a ComboBoxEx32 (control id 1148) > ComboBox, in the save as dialog (id 1001) inside
# a ComboBox of its DirectUI view; the address band has a hidden Edit too. ok / open / save = the child with control id 1, cancel = id 2.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Nd {
    [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr h, uint cmd);
    public static long Owner(long h) { return GetWindow(new IntPtr(h), 4).ToInt64(); }       // GW_OWNER (0 = unowned)
    [DllImport("user32.dll")] static extern uint GetClassLongW(IntPtr h, int i);
    public static uint ClassStyle(long h) { return GetClassLongW(new IntPtr(h), -26); }      // GCL_STYLE
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    public static bool Size(long h, int w, int hh) { return SetWindowPos(new IntPtr(h), IntPtr.Zero, 0, 0, w, hh, 0x16); }   // outer size; SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
}
'@
$TDM_CLICK_BUTTON = 0x466                                                        # WM_USER + 102: the overwrite prompt of save as is a task dialog, its buttons have no control ids
$IDYES = 6; $IDNO = 7
function Nat-Owned($app, [long]$owner) {                                         # the visible top-level #32770 windows of the app that are owned by $owner
    foreach ($w in @([U]::Tops($app.Pid, $true))) { if ([U]::Cls($w) -eq '#32770' -and [Nd]::Owner($w) -eq $owner) { [long]$w } }
}
function Nat-Name([long]$dlg) {                                                  # the file name field (an Edit): the known ids first, else the first visible Edit; 0 when there is none
    $first = [long]0
    foreach ($k in [U]::Kids($dlg)) {
        if ([U]::Cls($k) -ne 'Edit' -or -not [U]::Visible($k)) { continue }      # (the address band has a hidden one)
        if (@(1148, 1001) -contains [U]::Id($k)) { return [long]$k }
        if (-not $first) { $first = [long]$k }
    }
    return $first
}
function Nat-Find($app) {                                                        # the file dialog if one is up (0 if none): a visible #32770 owned by the main window that has a name field
    foreach ($w in @(Nat-Owned $app $app.Main)) { if (Nat-Name $w) { return [long]$w } }
    return [long]0
}
function Nat-Wait($app, [int]$ms = 20000) {                                      # waits for it (the first one of a process can be slow); throws (= a clear FAIL) when it never shows
    $h = WaitFor { Nat-Find $app } $ms
    if (-not $h) { throw 'the file dialog (a #32770 owned by the main window, with a file name field) did not appear' }
    if ($Dump) { Dump-Tree ([long]$h) }
    return [long]$h
}
function Nat-Text([long]$dlg) { [U]::GetText((Nat-Name $dlg)) }                  # what the name field holds
function Nat-Type([long]$dlg, [string]$t) { [U]::SetText((Nat-Name $dlg), $t) }  # WM_SETTEXT on it
function Nat-Press([long]$dlg, [int]$id) {                                       # BM_CLICK (posted) on the button with that control id: 1 = open / save, 2 = cancel
    $b = [U]::Item($dlg, $id)
    if (-not $b) { throw ('the file dialog has no button with control id ' + $id) }
    [void](WaitFor { [U]::Enabled([long]$b) } 2000)
    [void][U]::Post([long]$b, $BM_CLICK, 0, 0)
}
function Nat-Closed($app, [long]$dlg) {                                          # the dialog goes away; when it does not, the leftovers are closed so that the next step starts clean
    if (Gone $dlg 8000) { return $true }
    foreach ($b in @(Nat-Owned $app $dlg)) { [void][U]::Post([long]$b, $TDM_CLICK_BUTTON, $IDNO, 0) }     # (a task dialog ignores WM_CLOSE: answer it "no")
    Close-Dialogs $app
    return $false
}
function Nat-Box($app, [long]$dlg, [int]$ms = 5000) {                            # the message box / task dialog the file dialog put up (a #32770 owned by it); 0 when none shows within $ms
    return [long](WaitFor { @(Nat-Owned $app $dlg) | Select-Object -First 1 } $ms)
}
function Nat-Answer([long]$box, [int]$id) {                                      # press the button with that id (IDOK 1, IDYES 6, IDNO 7) of such a box: a message box has real control ids, a task dialog is clicked with TDM_CLICK_BUTTON; returns whether the box went away
    $b = [U]::Item($box, $id)
    if ($b) { [void][U]::Post([long]$b, $BM_CLICK, 0, 0) } else { [void][U]::Post($box, $TDM_CLICK_BUTTON, $id, 0) }
    return (Gone $box 3000)
}

# ============================================================================================================ T5
function Test-T5 {                                                               # open: the native comdlg32 dialog
    # first, while this exe copy has not handed a start folder to windows yet, the dialog starts in the folder of the open file. (windows honors the first start folder
    # an exe passes in and swaps a repeat of it for the last folder picked; that folder is remembered for every instance of the exe name, but not when it is under
    # %TEMP%, where the test files live.) so another instance picks a file from the src folder; then a fresh instance that opened its file from the command line must
    # still come up in tests: a bare file name resolves there only if the app passed that folder in
    $seed = Start-App
    Cmd $seed 'IDM_FILE_OPEN'
    $dlgS = Nat-Wait $seed
    Nat-Type $dlgS (Join-Path $Src 'mp.h')
    Nat-Press $dlgS $IDOK
    Ck 'T5.0 (setup) another instance picks a file from the src folder (a folder windows remembers)' (Wait-Title $seed 'mp.h - notepad mint' 8000) ('title [' + (Title $seed) + ']')
    Stop-App $seed
    $app3 = Start-App $sampleWrap
    [void](Wait-Title $app3 'wordwrap-sample.txt - notepad mint' 8000)
    Cmd $app3 'IDM_FILE_OPEN'
    $dlg3 = Nat-Wait $app3
    Nat-Type $dlg3 'multilingual-sample.txt'
    Nat-Press $dlg3 $IDOK
    Ck 'T5.0a the open dialog starts in the folder of the open file: a bare file name opens (title "multilingual-sample.txt - notepad mint")' (Wait-Title $app3 'multilingual-sample.txt - notepad mint' 8000) ('title [' + (Title $app3) + ']')
    CkEdText 'T5.0b ... and the editor text equals that file' $app3 (Expected-Text $sampleMulti)
    Stop-App $app3

    $app = Start-App                                                             # untitled
    Cmd $app 'IDM_FILE_OPEN'
    $dlg = Nat-Wait $app
    Pass 'T5.1 open dialog opens (a native #32770 owned by the main window, with a file name field)'
    Ck 'T5.2 it is modal: the main window is disabled while it is up' (-not [U]::Enabled($app.Main)) 'the main window is enabled'
    CkEq 'T5.3 the file name field starts empty (open proposes nothing)' '' (Nat-Text $dlg)
    Nat-Type $dlg $sampleWrap
    Nat-Press $dlg $IDOK
    Ck 'T5.4 ok on the full path of wordwrap-sample.txt closes the dialog' (Nat-Closed $app $dlg) 'the dialog is still visible'
    Ck 'T5.5 the title becomes "wordwrap-sample.txt - notepad mint"' (Wait-Title $app 'wordwrap-sample.txt - notepad mint' 8000) ('title [' + (Title $app) + ']')
    CkEdText 'T5.6 the editor text equals the file (utf-8, LF -> CRLF)' $app (Expected-Text $sampleWrap)
    Ck 'T5.7 the main window is enabled again and the document is not modified' ([bool](WaitFor { [U]::Enabled($app.Main) } 2000) -and -not (Ed-Modified $app)) 'the main window is disabled or the document is modified'

    Cmd $app 'IDM_FILE_OPEN'
    $dlg = Nat-Wait $app
    Nat-Type $dlg $sampleMulti
    Nat-Press $dlg $IDOK
    Ck 'T5.8 opening another file replaces the document: title "multilingual-sample.txt - notepad mint"' (Wait-Title $app 'multilingual-sample.txt - notepad mint' 8000) ('title [' + (Title $app) + ']')
    CkEdText 'T5.9 ... and the editor text equals that file' $app (Expected-Text $sampleMulti)
    [void](Nat-Closed $app $dlg)                                                 # (a failed open must not leave a dialog in the way of the next step)

    Cmd $app 'IDM_FILE_OPEN'
    $dlg = Nat-Wait $app
    Nat-Type $dlg 'no_such_file_xyz.txt'
    Nat-Press $dlg $IDOK
    $box = Nat-Box $app $dlg
    Ck 'T5.10 a file that does not exist: the dialog puts up a message of its own (a #32770 owned by it)' ($box -ne 0) 'no message appeared within 5 s'
    if ($box) { Ck 'T5.11 ... it closes with ok' (Nat-Answer $box $IDOK) 'the message is still visible' }
    Ck 'T5.12 ... the open dialog stays' ([U]::Visible($dlg)) 'the dialog went away'
    CkEq 'T5.13 ... and the title is unchanged' 'multilingual-sample.txt - notepad mint' (Title $app)
    $t0 = Title $app; $x0 = Ed-Text $app
    Nat-Type $dlg $sampleWrap                                                    # typed, then cancelled
    Nat-Press $dlg $IDCANCEL
    Ck 'T5.14 cancel closes the dialog' (Nat-Closed $app $dlg) 'the dialog is still visible'
    Start-Sleep -Milliseconds 250
    CkEq 'T5.15 ... the title is unchanged' $t0 (Title $app)
    CkText 'T5.16 ... the text is unchanged' $x0 (Ed-Text $app)
    Cmd $app 'IDM_FILE_OPEN'
    $dlg = Nat-Wait $app
    Pst $dlg $WM_CLOSE 0 0                                                       # what the window's x / alt+f4 sends (Close-Dialogs relies on it)
    Ck 'T5.17 WM_CLOSE on the dialog cancels it' (Nat-Closed $app $dlg) 'the dialog is still visible'
    Start-Sleep -Milliseconds 250
    Ck 'T5.18 ... the document is unchanged and the main window is enabled again' (((Title $app) -ceq $t0) -and [bool](WaitFor { [U]::Enabled($app.Main) } 2000)) ('title [' + (Title $app) + ']')

    Ed-Dirty $app 'x'
    CkEq 'T5.19 an edit puts "*" in front of the title' '*multilingual-sample.txt - notepad mint' (Title $app)
    Cmd $app 'IDM_FILE_OPEN'
    $box = Wait-Box $app $AppName
    CkEq 'T5.20 open while modified asks (box title "notepad mint")' 'do you want to save changes to multilingual-sample.txt?' $box.Text
    Ck 'T5.21 ... answer cancel (3rd button): the box closes' (Box-Press $box $BOX_BTN3) 'box still visible'
    Start-Sleep -Milliseconds 700                                                # (the native dialog takes ~0.2 s to show: long enough to notice one that wrongly follows)
    Ck 'T5.22 ... no open dialog follows and the document stays modified' (((Nat-Find $app) -eq 0) -and (Ed-Modified $app)) 'an open dialog appeared or the modified flag was lost'
    Cmd $app 'IDM_FILE_OPEN'
    $box = Wait-Box $app $AppName
    Ck 'T5.23 ... asked again; answer "don''t save" (2nd button): the box closes' (Box-Press $box $BOX_BTN2) 'box still visible'
    $dlg = Nat-Wait $app
    Pass 'T5.24 ... the open dialog follows'
    Nat-Type $dlg $sampleWrap
    Nat-Press $dlg $IDOK
    Ck 'T5.25 the file opens, the unsaved edit is discarded (title "wordwrap-sample.txt - notepad mint")' (Wait-Title $app 'wordwrap-sample.txt - notepad mint' 8000) ('title [' + (Title $app) + ']')
    CkEdText 'T5.26 ... editor text equals the file' $app (Expected-Text $sampleWrap)
    Ck 'T5.27 ... not modified' (-not (Ed-Modified $app)) 'EM_GETMODIFY is set'

    # the path and the encoding the dialog hands over are what "save" writes back to (no dialog: the document has a file now)
    $txtE = 'caf' + (Chars 0xE9) + ' ' + (Chars 0x65E5, 0x672C, 0x8A9E) + "`r`nsecond line"
    $u8 = New-Object Text.UTF8Encoding($false)
    $acpEnc = [Text.Encoding]::GetEncoding([int](Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Nls\CodePage').ACP)     # what "ansi" means for the app (the system code page)
    $bomU8 = [byte[]]@(0xEF, 0xBB, 0xBF); $bomU16 = [byte[]]@(0xFF, 0xFE)
    $ansiBytes = [byte[]]@(0x61, 0x93, 0xFA, 0x96, 0x7B, 0x0D, 0x0A, 0x62)       # not valid utf-8, and every byte is defined in the common ansi code pages (shift-jis: two kanji), so the text survives the round trip
    $specs = @(
        @{ Label = 'a utf-8 with bom'; Name = 'ui_t5_bom.txt'; Raw = [byte[]]($bomU8 + $u8.GetBytes($txtE)); Text = $txtE; Bom = $bomU8; Enc = $u8 },
        @{ Label = 'a utf-16 le'; Name = 'ui_t5_u16.txt'; Raw = [byte[]]($bomU16 + [Text.Encoding]::Unicode.GetBytes($txtE)); Text = $txtE; Bom = $bomU16; Enc = [Text.Encoding]::Unicode },
        @{ Label = 'an ansi'; Name = 'ui_t5_ansi.txt'; Raw = $ansiBytes; Text = $null; Bom = [byte[]]@(); Enc = $acpEnc })
    $k = 28
    foreach ($s in $specs) {
        $p = Join-Path $work $s.Name
        [IO.File]::WriteAllBytes($p, $s.Raw)
        Cmd $app 'IDM_FILE_OPEN'
        $dlg = Nat-Wait $app
        Nat-Type $dlg $p
        Nat-Press $dlg $IDOK
        Ck ('T5.' + $k + ' open ' + $s.Label + ' file: the title is its name') (Wait-Title $app ($s.Name + ' - notepad mint') 8000) ('title [' + (Title $app) + ']')
        if ($s.Text -ne $null) { CkEdText ('T5.' + ($k + 1) + ' ... the editor text is the decoded text (no bom character)') $app $s.Text }
        else { Ck ('T5.' + ($k + 1) + ' ... the editor text is read with the system code page (no replacement characters)') (-not (Ed-Text $app).Contains([string][char]0xFFFD)) 'the text holds U+FFFD: read as utf-8?' }
        Ed-Dirty $app 'x'
        $after = Ed-Text $app
        Cmd $app 'IDM_FILE_SAVE'
        CkBytes ('T5.' + ($k + 2) + ' ... save writes it back to the same file, same encoding') ([byte[]]($s.Bom + $s.Enc.GetBytes($after))) $p
        [void](Snd (Get-Edit $app) $EM_SETMODIFY 0 0)                            # (a failed save must not leave a prompt in the way of the next open)
        $k += 3
    }
}

# ============================================================================================================ T6
function Test-T6 {                                                               # save as: the native comdlg32 dialog
    $app = Start-App
    $text1 = 'h' + (Chars 0xE9) + 'llo ' + (Chars 0x65E5, 0x672C, 0x8A9E) + ' ' + [char]::ConvertFromUtf32(0x1F600) + "`r`n" + 'second line'
    Ed-Set $app $text1
    $base = Join-Path $work 'ui_t6'
    $f = $base + '.txt'
    $def = Default-Name $app                                                     # the unsaved document's name: what the dialog proposes

    Cmd $app 'IDM_FILE_SAVEAS'
    $dlg = Nat-Wait $app
    Pass 'T6.1 save as dialog opens (a native #32770 owned by the main window, with a file name field)'
    $prop = Nat-Text $dlg
    Ck 'T6.2 it proposes the default document name (mintXXXX, with or without ".txt")' (($def -ne $null) -and ($prop -cmatch ('^' + [regex]::Escape($def) + '(\.txt)?$'))) ('name field [' + (Show $prop) + '], default name [' + $def + ']')
    Nat-Type $dlg $base                                                          # full path, no extension
    Nat-Press $dlg $IDOK
    Ck 'T6.3 no extension typed: ".txt" is appended, title becomes "ui_t6.txt - notepad mint"' (Wait-Title $app 'ui_t6.txt - notepad mint' 8000) ('title [' + (Title $app) + ']')
    Ck 'T6.4 the dialog is gone' (Nat-Closed $app $dlg) 'the dialog is still visible'
    Ck 'T6.5 the file ui_t6.txt exists' (Test-Path -LiteralPath $f) ('missing: ' + $f)
    CkBytes 'T6.6 the file holds the text as utf-8 without bom, CRLF line breaks' (U8 $text1) $f
    Ck 'T6.7 saving cleared the modified flag' (-not (Ed-Modified $app)) 'EM_GETMODIFY is set'

    $text2 = 'second version ' + (Chars 0x65E5)
    Ed-Set $app $text2
    Cmd $app 'IDM_FILE_SAVEAS'
    $dlg = Nat-Wait $app
    $prop = Nat-Text $dlg
    Ck 'T6.8 the name field proposes the current file name (ui_t6, with or without ".txt")' ($prop -cmatch '^ui_t6(\.txt)?$') ('name field [' + (Show $prop) + ']')
    Nat-Type $dlg $f
    Nat-Press $dlg $IDOK
    $box = Nat-Box $app $dlg
    Ck 'T6.9 saving over an existing file asks first (a #32770 owned by the dialog)' ($box -ne 0) 'no confirmation appeared within 5 s'
    if ($box) { Ck 'T6.10 answer no: the box closes' (Nat-Answer $box $IDNO) 'the box is still visible' }
    Ck 'T6.11 ... the save as dialog stays open' ([U]::Visible($dlg)) 'the dialog went away'
    Start-Sleep -Milliseconds 250
    CkBytes 'T6.12 ... and the file on disk is unchanged' (U8 $text1) $f 300
    Nat-Press $dlg $IDOK
    $box = Nat-Box $app $dlg
    Ck 'T6.13 ok again asks again; answer yes: the box closes' (($box -ne 0) -and (Nat-Answer $box $IDYES)) 'no confirmation appeared, or it is still visible'
    Ck 'T6.14 ... the dialog closes' (Nat-Closed $app $dlg) 'the dialog is still visible'
    CkBytes 'T6.15 ... and the new text is written' (U8 $text2) $f

    # the encoding and the line ending are the document's own (format menu / status bar): the dialog has no pickers for them
    $text3 = 'h' + (Chars 0xE9) + "llo`r`nline two"
    Ed-Set $app $text3
    Cmd $app 'IDM_ENC_UTF16LE'
    Cmd $app 'IDM_EOL_LF'
    Cmd $app 'IDM_FILE_SAVEAS'
    $dlg = Nat-Wait $app
    $base2 = Join-Path $work 'ui_t6b'
    Nat-Type $dlg $base2
    Nat-Press $dlg $IDOK
    Ck 'T6.16 save as with the document set to utf-16 le + unix (lf): title becomes "ui_t6b.txt - notepad mint"' (Wait-Title $app 'ui_t6b.txt - notepad mint' 8000) ('title [' + (Title $app) + ']')
    $exp16 = [byte[]]([byte[]]@(0xFF, 0xFE) + [Text.Encoding]::Unicode.GetBytes(($text3 -replace "`r`n", "`n")))
    CkBytes 'T6.17 the file is utf-16 le: FF FE bom, LF only' $exp16 ($base2 + '.txt')

    # a name that has an extension keeps it
    $fLog = Join-Path $work 'ui_t6c.log'
    Cmd $app 'IDM_FILE_SAVEAS'
    $dlg = Nat-Wait $app
    Nat-Type $dlg $fLog
    Nat-Press $dlg $IDOK
    Ck 'T6.18 a name with an extension keeps it (no ".txt" added): title "ui_t6c.log - notepad mint"' (Wait-Title $app 'ui_t6c.log - notepad mint' 8000) ('title [' + (Title $app) + ']')
    Ck 'T6.19 ... the file ui_t6c.log exists, ui_t6c.log.txt does not' ((Test-Path -LiteralPath $fLog) -and -not (Test-Path -LiteralPath ($fLog + '.txt'))) 'wrong file names on disk'

    # cancel: nothing is written, the document keeps its name and its modified state
    Ed-Dirty $app 'y'
    $t0 = Title $app
    $noFile = Join-Path $work 'ui_t6_cancel'
    Cmd $app 'IDM_FILE_SAVEAS'
    $dlg = Nat-Wait $app
    Nat-Type $dlg $noFile                                                        # typed, then cancelled
    Nat-Press $dlg $IDCANCEL
    Ck 'T6.20 cancel closes the dialog' (Nat-Closed $app $dlg) 'the dialog is still visible'
    Start-Sleep -Milliseconds 300
    Ck 'T6.21 ... no file is written' (-not (Test-Path -LiteralPath $noFile) -and -not (Test-Path -LiteralPath ($noFile + '.txt'))) 'a file was created'
    CkEq 'T6.22 ... the title is unchanged' $t0 (Title $app)
    Ck 'T6.23 ... and the document is still modified' (Ed-Modified $app) 'the modified flag was lost'
}

# ============================================================================================================ T7
function Test-T7 {                                                               # reopen with encoding
    $f = Join-Path $work 'ui_t7.txt'
    $bytes = [byte[]]@(0x63, 0x61, 0x66, 0xE9, 0x20, 0x61, 0x75, 0x20, 0x6C, 0x61, 0x69, 0x74, 0x0D, 0x0A, 0x6E, 0x61, 0xEF, 0x76, 0x65, 0x0D, 0x0A)   # "caf<e acute> au lait", "na<i diaeresis>ve" in windows-1252
    [IO.File]::WriteAllBytes($f, $bytes)
    $exp1252 = [regex]::Replace([Text.Encoding]::GetEncoding(1252).GetString($bytes), "\r\n|\r|\n", "`r`n")
    Ck 'T7.0 (sanity) the test file is not valid utf-8' ($true -eq $(try { (New-Object Text.UTF8Encoding($false, $true)).GetString($bytes) | Out-Null; $false } catch { $true })) 'the bytes decode as utf-8'

    $app = Start-App $f
    Ck 'T7.1 the file opens (title "ui_t7.txt - notepad mint")' (Wait-Title $app 'ui_t7.txt - notepad mint') ('title [' + (Title $app) + ']')
    $acp = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Nls\CodePage').ACP                   # what CP_ACP means for the app (not the user culture's code page)
    Info ('T7 first decode (detected as ansi = the system code page ' + $acp + '): ' + (Show (Ed-Text $app)))
    Cmd $app 'IDM_ENC_REOPEN'
    $dlg = Wait-Win $app 'mp_enc' 'reopen with encoding'
    Pass 'T7.2 reopen with encoding dialog opens (class mp_enc, title "reopen with encoding")'
    $lb = Ctl $dlg $IDO.ID_ENCLIST
    $ok = $true; for ($i = 0; $i -lt $IDM.ENC_COUNT; $i++) { if ((Snd $lb 0x199 $i 0) -ne $i) { $ok = $false } }
    Ck 'T7.3 the list starts with the encoding ids 0..4 (utf-8, utf-8 bom, utf-16 le/be, ansi) as item data' $ok 'LB_GETITEMDATA of items 0..4 is not 0..4'
    $i1252 = [U]::ListFind($lb, 1252)
    Ck 'T7.4 the list offers code page 1252' ($i1252 -ge 0) 'no item with data 1252'
    CkEq 'T7.5 the dialog preselects the current encoding (ansi)' $IDM.ENC_ANSI (Snd $lb 0x199 (Snd $lb $LB_GETCURSEL) 0)
    [void](Snd $lb $LB_SETCURSEL $i1252 0)
    Press $dlg $IDOK
    Ck 'T7.6 ok closes the dialog' (Gone $dlg) 'the dialog is still visible'
    CkEdText 'T7.7 reopened as windows-1252: the text is "cafe acute au lait / naive diaeresis"' $app $exp1252 3000
    Ck 'T7.8 ... not modified' (-not (Ed-Modified $app)) 'EM_GETMODIFY is set'

    Cmd $app 'IDM_ENC_REOPEN'
    $dlg = Wait-Win $app 'mp_enc' 'reopen with encoding'
    $lb = Ctl $dlg $IDO.ID_ENCLIST
    CkEq 'T7.9 the dialog now preselects windows-1252' 1252 (Snd $lb 0x199 (Snd $lb $LB_GETCURSEL) 0)
    [void](Snd $lb $LB_SETCURSEL ([U]::ListFind($lb, $IDM.ENC_UTF8)) 0)
    Press $dlg $IDOK
    $box = Wait-Box $app $AppName
    Ck 'T7.10 invalid utf-8 is not opened by accident: the app asks first' ($box.Text -like '*some bytes of this file are not valid in its encoding*') $box.Text
    [void](Box-Press $box $BOX_BTN2)                                             # cancel
    CkEdText 'T7.10a cancel preserves the previous document' $app $exp1252
    CkBytes 'T7.10b ... and the file on disk' $bytes $f
    Cmd $app 'IDM_ENC_REOPEN'
    $dlg = Wait-Win $app 'mp_enc' 'reopen with encoding'
    $lb = Ctl $dlg $IDO.ID_ENCLIST
    [void](Snd $lb $LB_SETCURSEL ([U]::ListFind($lb, $IDM.ENC_UTF8)) 0)
    Press $dlg $IDOK
    $box = Wait-Box $app $AppName
    [void](Box-Press $box $IDOK)                                                 # open anyway
    CkEdText 'T7.10c "open" opens it anyway, each bad byte as U+FFFD' $app ('caf' + (Chars @(0xFFFD)) + " au lait`r`nna" + (Chars @(0xFFFD)) + "ve`r`n") 3000
    CkBytes 'T7.10d ... the file on disk is untouched' $bytes $f
    Cmd $app 'IDM_ENC_REOPEN'                                                    # back to windows-1252 for the rest
    $dlg = Wait-Win $app 'mp_enc' 'reopen with encoding'
    $lb = Ctl $dlg $IDO.ID_ENCLIST
    [void](Snd $lb $LB_SETCURSEL ([U]::ListFind($lb, 1252)) 0)
    Press $dlg $IDOK
    CkEdText 'T7.10e ... and reopening it as windows-1252 gives the real text again' $app $exp1252 3000

    Cmd $app 'IDM_ENC_REOPEN'
    $dlg = Wait-Win $app 'mp_enc' 'reopen with encoding'
    Press $dlg $IDCANCEL
    Ck 'T7.11 cancel closes the dialog' (Gone $dlg) 'the dialog is still visible'
    Start-Sleep -Milliseconds 250
    CkText 'T7.12 ... and the text is unchanged' $exp1252 (Ed-Text $app)

    Ed-Dirty $app 'zz'                                                           # a modified document: reopening asks first, before the encoding is picked
    $dirtyText = Ed-Text $app
    Cmd $app 'IDM_ENC_REOPEN'
    $box = Wait-Box $app $AppName
    Ck 'T7.13 reopening a modified document warns first: "reopening the file will discard your unsaved changes."' ($box.Text -like 'reopening the file will discard your unsaved changes.*') ('box text [' + (Show $box.Text) + ']')
    Ck 'T7.14 ... answer no (2nd button): the box closes' (Box-Press $box $BOX_BTN2) 'box still visible'
    Start-Sleep -Milliseconds 250
    CkText 'T7.15 ... the edited text is kept' $dirtyText (Ed-Text $app)
    Cmd $app 'IDM_ENC_REOPEN'
    $box = Wait-Box $app $AppName
    Ck 'T7.16 ... asked again; answer yes (1st button): the box closes' (Box-Press $box $IDOK) 'box still visible'
    $dlg = Wait-Win $app 'mp_enc' 'reopen with encoding'
    [void](Snd (Ctl $dlg $IDO.ID_ENCLIST) $LB_SETCURSEL ([U]::ListFind((Ctl $dlg $IDO.ID_ENCLIST), 1252)) 0)
    Press $dlg $IDOK
    CkEdText 'T7.17 ... the file is reloaded as windows-1252' $app $exp1252 3000
    Ck 'T7.18 ... and the document is clean again' (-not (Ed-Modified $app)) 'EM_GETMODIFY is set'

    $app2 = Start-App                                                            # untitled: nothing to reopen (the menu item is grayed)
    Cmd $app2 'IDM_ENC_REOPEN'
    $box = $null
    try { $box = Wait-Box $app2 $AppName 800 } catch { $box = $null }
    Ck 'T7.19 an untitled document has nothing to reopen: no dialog, no box' (-not $box -and -not [U]::FindTop($app2.Pid, 'mp_enc', '')) 'something opened'
}

function Read-Palette() {                                                        # the two palettes from the g_themes table in ui.c: face + editor background
    $t = [IO.File]::ReadAllText((Join-Path $Src 'ui.c'))
    $m = [regex]::Match($t, 'g_themes\[2\]\s*=\s*\{(.*?)\n\};', 'Singleline')
    if (-not $m.Success) { return $null }
    $c = @([regex]::Matches($m.Groups[1].Value, 'RGB\(\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*\)') | ForEach-Object {
        , @([Convert]::ToInt32($_.Groups[1].Value.Substring(2), 16), [Convert]::ToInt32($_.Groups[2].Value.Substring(2), 16), [Convert]::ToInt32($_.Groups[3].Value.Substring(2), 16)) })
    if ($c.Count -ne 28) { return $null }                                        # 2 themes x 14 colours (struct Palette in mp.h)
    return @{ dark = @{ face = $c[3]; face2 = $c[4]; edit = $c[13]; accent = $c[0]; onAccent = $c[2]; text = $c[10] }; light = @{ face = $c[17]; face2 = $c[18]; edit = $c[27]; accent = $c[14]; onAccent = $c[16]; text = $c[24] } }
}
function Get-Pixel($app, [string]$where) {                                       # a colour from PrintWindow of the main window (works when covered)
    $w = [U]::WRect([long]$app.Main)
    if ($where -eq 'edit') { $e = [U]::WRect((Get-Edit $app)); $fx = 0.85; $fy = 0.85 }
    else {
        $bar = $null
        foreach ($k in [U]::Kids([long]$app.Main)) { if ([U]::Cls($k) -eq 'mp_menubar') { $bar = $k } }
        if (-not $bar) { return $null }
        $e = [U]::WRect([long]$bar); $fx = 0.7; $fy = 0.4                        # the free part of the menu bar (right of the menus, left of the two buttons): plain chrome face
    }
    $bmp = [U]::Grab([long]$app.Main)
    try {
        $x = [int](($e[0] - $w[0]) + ($e[2] - $e[0]) * $fx); $y = [int](($e[1] - $w[1]) + ($e[3] - $e[1]) * $fy)
        if ($x -lt 0 -or $y -lt 0 -or $x -ge $bmp.Width -or $y -ge $bmp.Height) { return $null }
        $c = $bmp.GetPixel($x, $y)
        return [pscustomobject]@{ R = [int]$c.R; G = [int]$c.G; B = [int]$c.B; Info = ('rgb(' + $c.R + ',' + $c.G + ',' + $c.B + ') at ' + $x + ',' + $y) }
    } finally { $bmp.Dispose() }
}
function Near($c, $rgb, [int]$tol = 10) { $c -ne $null -and [Math]::Abs($c.R - $rgb[0]) -le $tol -and [Math]::Abs($c.G - $rgb[1]) -le $tol -and [Math]::Abs($c.B - $rgb[2]) -le $tol }
function CkPix([string]$n, $app, [string]$where, $rgb) {
    if (WaitFor { Near (Get-Pixel $app $where) $rgb } 3000) { Pass $n; return }
    $c = Get-Pixel $app $where
    $a = '<no pixel>'; if ($c) { $a = $c.Info }
    Fail $n ('expected rgb(' + ($rgb -join ',') + ') actual ' + $a)
}
function Strip-Section([string]$ini, [string]$sec) {                             # drop a [section] from the utf-16 ini (keeps a relaunch on the no-saved-placement path)
    $out = New-Object System.Collections.ArrayList; $skip = $false
    foreach ($l in ([IO.File]::ReadAllText($ini, [Text.Encoding]::Unicode) -split "\r?\n")) {
        if ($l -match '^\[(.+)\]\s*$') { $skip = ($Matches[1] -eq $sec) }
        if (-not $skip) { [void]$out.Add($l) }
    }
    [IO.File]::WriteAllText($ini, ($out -join "`r`n"), [Text.Encoding]::Unicode)
}

# ============================================================================================================ T8
function Test-T8 {                                                               # theme
    $pal = Read-Palette
    $app = Start-App
    $ri = Read-Ini $app.Ini                                                      # (the only thing a start writes: the last default name handed out, [name] last)
    Ck 'T8.0 no settings yet in the fresh %APPDATA% (isolation)' (@($ri.Keys | Where-Object { $_ -ne 'name' }).Count -eq 0) ('unexpected sections in ' + $app.Ini + ': ' + (@($ri.Keys) -join ','))
    if ($pal) { CkPix 'T8.1 default theme is dark: the editor area is black' $app 'edit' $pal.dark.edit; CkPix 'T8.2 ... and the chrome (menu bar) has the dark face colour' $app 'bar' $pal.dark.face }
    else { Skip 'T8.1 pixel checks' 'could not parse the g_themes palette table in src\ui.c' }

    Cmd $app 'IDM_THEME_LIGHT'
    CkEq 'T8.3 view > theme > light writes theme=light under [view] of settings.ini' 'light' (Ini-Val $app 'view' 'theme' 'light')
    if ($pal) { CkPix 'T8.4 light theme: the editor area is white' $app 'edit' $pal.light.edit; CkPix 'T8.5 ... and the chrome has the light face colour' $app 'bar' $pal.light.face }
    Cmd $app 'IDM_THEME_DARK'
    CkEq 'T8.6 view > theme > dark writes theme=dark' 'dark' (Ini-Val $app 'view' 'theme' 'dark')
    if ($pal) { CkPix 'T8.7 dark again: the editor area is black' $app 'edit' $pal.dark.edit }

    Cmd $app 'IDM_THEME_LIGHT'                                                   # persistence: save on exit, restore on the next start
    [void](Ini-Val $app 'view' 'theme' 'light')
    Pst $app.Main $WM_CLOSE 0 0
    Ck 'T8.8 the app exits on WM_CLOSE (settings are written)' ($app.Proc.WaitForExit(3000)) 'the process is still running'
    CkEq 'T8.9 settings.ini keeps theme=light after exit' 'light' (Ini-Val $app 'view' 'theme' 'light' 500)
    Strip-Section $app.Ini 'window'
    $app2 = Start-App '' $app.AppData
    if ($pal) { CkPix 'T8.10 a relaunch restores the light theme (white editor area)' $app2 'edit' $pal.light.edit }
    else { Ck 'T8.10 relaunch keeps theme=light in the ini' ((Ini-Val $app2 'view' 'theme' 'light' 500) -eq 'light') 'theme lost' }
    Cmd $app2 'IDM_THEME_DARK'
    CkEq 'T8.11 and switching back writes theme=dark' 'dark' (Ini-Val $app2 'view' 'theme' 'dark')
}

# ============================================================================================================ T9
function Open-Font($app) { Cmd $app 'IDM_FMT_FONT'; return (Wait-Win $app 'mp_font' 'font' 20000) }   # the first open measures every installed family: can be slow
function Line-Height($app) {                                                     # distance between two text lines of the editor, in pixels (grows with the font size)
    $ed = Get-Edit $app
    $p0 = Snd $ed 0xD6 0 0                                                       # EM_POSFROMCHAR of char 0 and of char 3 ("a\r\nb": the start of line 2)
    $p1 = Snd $ed 0xD6 3 0
    return ((($p1 -shr 16) -band 0xFFFF) - (($p0 -shr 16) -band 0xFFFF))
}
function Test-T9 {                                                               # font dialog
    function PtText($v) { if ($v % 10) { '{0}.{1}' -f [math]::Floor($v / 10), ($v % 10) } else { [string][math]::Floor($v / 10) } }   # tenths of a point -> the size box text
    $sizes = @(([regex]::Match([IO.File]::ReadAllText((Join-Path $Src 'fontdlg.c')), 'g_sizes\[\d+\]\s*=\s*\{([^}]*)\}').Groups[1].Value -split '\s*,\s*') | ForEach-Object { [int]$_.Trim() })
    $app = Start-App
    Ed-Set $app ("a`r`nb")
    $lh12 = Line-Height $app
    $dlg = Open-Font $app
    Pass 'T9.1 font dialog opens (class mp_font, title "font")'
    $defPt = [regex]::Match([IO.File]::ReadAllText((Join-Path $Src 'prefs.c')), 'g_pf\.pt\s*=\s*(\d+)\s*;').Groups[1].Value     # PrefsDefaults
    CkEq 'T9.2 the size box starts with the default size (PrefsDefaults)' (PtText ([int]$defPt)) (Get-Field $dlg $IDT.ID_SIZE)
    CkChk 'T9.3 "monospaced fonts only" is on by default' $dlg $IDT.ID_MONO 1
    Set-Field $dlg $IDT.ID_SIZE '18'
    Press $dlg $IDOK
    Ck 'T9.4 ok closes the dialog' (Gone $dlg) 'the dialog is still visible'
    CkEq 'T9.5 settings.ini has size10=180 under [editor] (18 pt, tenths)' '180' (Ini-Val $app 'editor' 'size10' '180')
    $lh18 = 0; [void](WaitFor { $script:lh = Line-Height $app; $script:lh -gt $lh12 } 2000); $lh18 = $script:lh
    Ck 'T9.6 the editor font really changed: the line height grew (12 pt -> 18 pt)' ($lh18 -gt $lh12) ('line height ' + $lh12 + ' px at 12 pt, ' + $lh18 + ' px at 18 pt')

    $dlg = Open-Font $app
    CkEq 'T9.7 reopened: the size box shows 18' '18' (Get-Field $dlg $IDT.ID_SIZE)
    Set-Field $dlg $IDT.ID_SIZE '5'
    Press $dlg $IDOK
    CkEq 'T9.8 size 5 clamps up to FONT_MIN on ok' ([string]$IDM.FONT_MIN) (Ini-Val $app 'editor' 'size10' ([string]$IDM.FONT_MIN))
    $dlg = Open-Font $app
    Set-Field $dlg $IDT.ID_SIZE '500'
    Press $dlg $IDOK
    CkEq 'T9.9 size 500 clamps down to FONT_MAX on ok' ([string]$IDM.FONT_MAX) (Ini-Val $app 'editor' 'size10' ([string]$IDM.FONT_MAX))
    $lh96 = 0; [void](WaitFor { $script:lh = Line-Height $app; $script:lh -gt $lh18 } 2000); $lh96 = $script:lh
    Ck 'T9.10 ... and the editor line height grew again at 70 pt' ($lh96 -gt $lh18) ('line height ' + $lh18 + ' px at 18 pt, ' + $lh96 + ' px at FONT_MAX pt')

    $dlg = Open-Font $app
    Set-Field $dlg $IDT.ID_SIZE '30'
    Press $dlg $IDCANCEL
    Ck 'T9.11 cancel closes the dialog' (Gone $dlg) 'the dialog is still visible'
    CkEq 'T9.12 ... and leaves the saved size unchanged (FONT_MAX)' ([string]$IDM.FONT_MAX) (Ini-Val $app 'editor' 'size10' '300' 1000)

    $dlg = Open-Font $app                                                        # a preset button, then face / bold / italic
    $p = 6
    Press $dlg ($IDT.ID_PRESET + $p)
    Ck ('T9.13 preset button #' + $p + ' puts ' + $sizes[$p] + ' in the size box') ([bool](WaitFor { (Get-Field $dlg $IDT.ID_SIZE) -eq (PtText $sizes[$p]) } 1500)) ('size box [' + (Get-Field $dlg $IDT.ID_SIZE) + ']')
    $lb = Ctl $dlg $IDT.ID_LIST
    $idx = [U]::SndStr($lb, $LB_FINDSTRINGEXACT, -1, 'Courier New')
    if ($idx -lt 0) { Skip 'T9.14 pick the face Courier New + bold + italic' 'Courier New is not in the (monospaced only) family list of this machine' }
    else {
        [void](Snd $lb $LB_SETCURSEL $idx 0)
        Press $dlg $IDT.ID_BOLD; Press $dlg $IDT.ID_ITALIC
        CkChk 'T9.14 bold checkbox toggles on' $dlg $IDT.ID_BOLD 1
        CkChk 'T9.15 italic checkbox toggles on' $dlg $IDT.ID_ITALIC 1
    }
    Press $dlg $IDOK
    CkEq ('T9.16 ok writes the preset size ' + $sizes[$p]) ([string]$sizes[$p]) (Ini-Val $app 'editor' 'size10' ([string]$sizes[$p]))
    if ($idx -ge 0) {
        CkEq 'T9.17 ... face Courier New' 'Courier New' (Ini-Val $app 'editor' 'font' 'Courier New')
        CkEq 'T9.18 ... bold=1' '1' (Ini-Val $app 'editor' 'bold' '1')
        CkEq 'T9.19 ... italic=1' '1' (Ini-Val $app 'editor' 'italic' '1')
    }

    $dlg = Open-Font $app
    Press $dlg $IDT.ID_RESET
    Ck 'T9.20 "reset": the size box shows 10' ([bool](WaitFor { (Get-Field $dlg $IDT.ID_SIZE) -eq '10' } 1500)) ('size box [' + (Get-Field $dlg $IDT.ID_SIZE) + ']')
    CkChk 'T9.21 ... bold is off again' $dlg $IDT.ID_BOLD 0
    Press $dlg $IDOK
    CkEq 'T9.22 ok after reset: size10=100' '100' (Ini-Val $app 'editor' 'size10' '100')
    CkEq 'T9.23 ... font=Consolas' 'Consolas' (Ini-Val $app 'editor' 'font' 'Consolas')
    CkEq 'T9.24 ... bold=0' '0' (Ini-Val $app 'editor' 'bold' '0')
    CkEq 'T9.25 ... italic=0' '0' (Ini-Val $app 'editor' 'italic' '0')
}

# ============================================================================================================ T10
function Test-T10 {                                                              # other dialogs: smoke
    $app = Start-App
    $specs = @(
        @('T10.1', 'other code page', 'IDM_ENC_OTHER', 'mp_enc', 'other code page'),
        @('T10.2', 'about', 'IDM_HELP_ABOUT', 'mp_about', 'about notepad mint'),
        @('T10.3', 'help topics', 'IDM_HELP_TOPICS', 'mp_help', 'help topics'))
    foreach ($s in $specs) {
        Cmd $app $s[2]
        $h = Wait-Win $app $s[3] $s[4]
        Pass ($s[0] + ' ' + $s[1] + ' opens (class ' + $s[3] + ', title "' + $s[4] + '")')
        Ck ($s[0] + 'a the app answers SendMessageTimeout while it is open') ([U]::Responds($app.Main, 2000, $false)) 'no answer within 2 s'
        if ($s[2] -eq 'IDM_HELP_TOPICS') {
            $t = ''
            foreach ($k in [U]::Kids($h)) { if ([U]::Cls($k) -eq 'Edit') { $t = [U]::GetText($k) } }
            Ck ($s[0] + 'b the help text is there') ($t.StartsWith('notepad mint - keyboard shortcuts and tips')) ('help text starts [' + (Show $t) + ']')
            $kids = @([U]::Kids($h) | Where-Object { [U]::Cls($_) -ne 'mp_sbar' })    # (the scrollbar overlays are windows of ours too) no ok button, and the text fills the whole window (no padding)
            Ck ($s[0] + 'e the help window has one child, the text (no button)') ($kids.Count -eq 1 -and [U]::Cls($kids[0]) -eq 'Edit') ('children: ' + (($kids | ForEach-Object { [U]::Cls($_) }) -join ', '))
            $cr = [U]::CRect($h); $er = [U]::WRect($kids[0])
            Ck ($s[0] + 'f ... and it is as big as the client area') ($er[2] - $er[0] -ge $cr[2] - $cr[0] -and $er[3] - $er[1] -ge $cr[3] - $cr[1]) ('edit ' + ($er -join ',') + ' client ' + ($cr -join ','))
            Ck ($s[0] + 'g ... and no line of the text is broken by hand mid sentence (every text line is a title, a key row or a whole paragraph)') (-not ($t -split "`r`n" | Where-Object { $_ -ne '' -and $_ -notmatch '^(  \S|[a-z][a-z ]*$|notepad mint - |[a-z][a-z ]*: )' })) 'a line that is none of those'
        }
        Pst $h $WM_CLOSE 0 0
        Ck ($s[0] + 'c it closes on WM_CLOSE (esc / cancel)') (Gone $h) 'the window is still visible'
        Ck ($s[0] + 'd the app is still running and answers') ((-not $app.Proc.HasExited) -and [U]::Responds($app.Main, 2000, $false)) 'the app died or hangs'
    }
    Cmd $app 'IDM_HELP_TOPICS'                                                   # the help window has no button: esc (read by the dialog loop) closes it
    $h = Wait-Win $app 'mp_help' 'help topics'
    $ed = 0; foreach ($k in [U]::Kids($h)) { if ([U]::Cls($k) -eq 'Edit') { $ed = $k } }
    Pst $ed $WM_KEYDOWN $VK_ESCAPE 0
    Ck 'T10.3h esc closes the help window' (Gone $h) 'the window is still visible after esc'
    $before = @([U]::Tops($app.Pid, $true))                                      # page setup: the native comdlg32 dialog (title is localized)
    Cmd $app 'IDM_FILE_PAGESETUP'
    $h = WaitFor { @([U]::Tops($app.Pid, $true) | Where-Object { ($before -notcontains $_) -and (@('#32770', 'mp_msg') -contains [U]::Cls($_)) }) | Select-Object -First 1 } 10000   # (system overlay / ime windows also show up in the app process: ignore them)
    if (-not $h) { Skip 'T10.4 page setup' 'no new window appeared within 10 s (PageSetupDlg shows nothing when there is no usable printer)' }
    elseif ([U]::Cls($h) -eq 'mp_msg') {
        $txt = ''; foreach ($k in [U]::Kids($h)) { if ([U]::Cls($k) -eq 'Static') { $txt = [U]::Text($k) } }
        Skip 'T10.4 page setup' ('the app showed a message instead of the dialog: ' + (Show $txt))
        Pst $h $WM_CLOSE 0 0
    } else {
        Info ('T10 page setup window: class ' + [U]::Cls($h) + ', title [' + (Show ([U]::Text($h))) + ']')
        Ck 'T10.4 page setup opens (a native dialog, class #32770)' ([U]::Cls($h) -eq '#32770') ('class ' + [U]::Cls($h))
        Ck 'T10.4a the app answers SendMessageTimeout while it is open' ([U]::Responds($app.Main, 2000, $false)) 'no answer within 2 s'
        Pst $h $WM_CLOSE 0 0
        Ck 'T10.4b it closes on WM_CLOSE (cancel)' (Gone $h 6000) 'the window is still visible'
        Ck 'T10.4c the app is still running and answers' ((-not $app.Proc.HasExited) -and [U]::Responds($app.Main, 2000, $false)) 'the app died or hangs'
    }
}

# ============================================================================================================ T11
function Test-T11 {                                                              # big file + responsiveness
    $big = Join-Path $work 'big.txt'
    $jp = Chars 0x65E5, 0x672C, 0x8A9E
    $sb = New-Object Text.StringBuilder
    $lines = 0
    while ($sb.Length -lt 60000) { [void]$sb.Append($lines).Append(' the quick brown fox jumps over the lazy dog 0123456789 ').Append($jp).Append("`r`n"); $lines++ }
    $block = (New-Object Text.UTF8Encoding($false)).GetBytes($sb.ToString())
    $target = [long]$BigMB * 1MB; $written = [long]0; $blocks = 0
    $fs = [IO.File]::Create($big)
    try { while ($written -lt $target) { $fs.Write($block, 0, $block.Length); $written += $block.Length; $blocks++ } } finally { $fs.Dispose() }
    $chars = [long]$sb.Length * $blocks; $totalLines = [long]$lines * $blocks + 1

    $sw = [Diagnostics.Stopwatch]::StartNew()
    $app = Start-App $big
    $state = WaitFor { if ($app.Proc.HasExited) { 'exited' } elseif ((Title $app) -ceq 'big.txt - notepad mint') { 'ok' } } 60000
    $ms = $sw.ElapsedMilliseconds
    Info ('T11 ' + [Math]::Round($written / 1MB, 1) + ' MB (' + $written + ' bytes, ' + $chars + ' chars, ' + $totalLines + ' lines) loaded in ' + $ms + ' ms (process start until the title shows the file name)')
    if ($state -eq 'exited') { Fail 'T11.1 the title shows the file name within 60 s' ('the app process exited during the load after ' + $ms + ' ms, exit code 0x' + ('{0:X}' -f $app.Proc.ExitCode) + ': a crash, or killed from outside (another harness killing "notepad-mint" by name?)'); return }
    if ($state -ne 'ok') { Fail 'T11.1 the title shows the file name within 60 s' ('title [' + (Title $app) + '] after ' + $ms + ' ms'); return }
    Pass 'T11.1 the title shows the file name within 60 s'
    $sw.Restart()
    $r = [U]::Responds($app.Main, 2000, $false)
    Info ('T11 first answer to SendMessageTimeout after the load: ' + $sw.ElapsedMilliseconds + ' ms')
    Ck 'T11.2 the app answers SendMessageTimeout within 2 s after the load' $r 'no answer within 2000 ms'
    $ed = Get-Edit $app
    CkEq 'T11.3 the editor holds every character (WM_GETTEXTLENGTH)' $chars (Snd $ed 0x0E 0 0)
    CkEq 'T11.4 ... and every line (EM_GETLINECOUNT)' $totalLines (Snd $ed 0xBA 0 0)
    Ck 'T11.5 not modified after loading' (-not (Ed-Modified $app)) 'EM_GETMODIFY is set'
    $sw.Restart()
    Pst $app.Main $WM_CLOSE 0 0
    $exited = $app.Proc.WaitForExit(15000)
    Info ('T11 closing the big document took ' + $sw.ElapsedMilliseconds + ' ms')
    Ck 'T11.6 the app closes the big document and exits' $exited 'still running 15 s after WM_CLOSE'
}

# ============================================================================================================ T12
function Test-T12 {                                                              # exit
    $app = Start-App $sampleMulti
    [void](Wait-Title $app 'multilingual-sample.txt - notepad mint')
    Pst $app.Main $WM_CLOSE 0 0
    Ck 'T12.1 WM_CLOSE on an unmodified document: the process exits within 3 s' ($app.Proc.WaitForExit(3000)) 'still running 3 s after WM_CLOSE'
    Ck 'T12.2 ... settings.ini was written on exit' (Test-Path -LiteralPath $app.Ini) ('missing ' + $app.Ini)
    $ini = Read-Ini $app.Ini
    Ck 'T12.3 ... with [window] / [editor] / [view] sections' ($ini.ContainsKey('window') -and $ini.ContainsKey('editor') -and $ini.ContainsKey('view')) ('sections: ' + (($ini.Keys | Sort-Object) -join ','))

    $app = Start-App
    $name = Default-Name $app                                                    # the unsaved document is called mintXXXX (see T13)
    Ed-Dirty $app 'unsaved'
    CkEq 'T12.4 an edit marks the unsaved document modified (title "*mintXXXX - notepad mint")' ('*' + $name + ' - notepad mint') (Title $app)
    Pst $app.Main $WM_CLOSE 0 0
    $box = Wait-Box $app $AppName
    CkEq 'T12.5 WM_CLOSE on a modified document asks (box title "notepad mint") and names the default name' ('do you want to save changes to ' + $name + '?') $box.Text
    Ck 'T12.6 answer cancel (3rd button): the box closes' (Box-Press $box $BOX_BTN3) 'box still visible'
    Start-Sleep -Milliseconds 400
    Ck 'T12.7 ... the app stays: process alive, window visible, document still modified' ((-not $app.Proc.HasExited) -and [U]::Visible($app.Main) -and (Ed-Modified $app)) 'the app exited or lost the document'
    Pst $app.Main $WM_CLOSE 0 0
    $box = Wait-Box $app $AppName
    Ck 'T12.8 asked again; answer "don''t save" (2nd button): the box closes' (Box-Press $box $BOX_BTN2) 'box still visible'
    Ck 'T12.9 ... and the process exits within 3 s' ($app.Proc.WaitForExit(3000)) 'still running 3 s after "don''t save"'

    $app = Start-App
    Cmd $app 'IDM_FILE_EXIT'
    Ck 'T12.10 file > exit on a clean untitled document exits within 3 s' ($app.Proc.WaitForExit(3000)) 'still running 3 s after IDM_FILE_EXIT'
}

# =========================================================================================================== T13
function Expected-Name([DateTime]$t) {                                           # DocNameClock + DefaultDocName (util.c): "mint-" + base 36 of (days since 2000-01-01, year mod 100) * 45 + (seconds since midnight) / 1920
    $days = ([DateTime]::new(2000 + ($t.Year % 100), $t.Month, $t.Day) - [DateTime]::new(2000, 1, 1)).Days
    $v = $days * 45 + [int][Math]::Floor(($t.Hour * 3600 + $t.Minute * 60 + $t.Second) / 1920)
    $d = '0123456789abcdefghijklmnopqrstuvwxyz'
    $s = ''
    for ($i = 0; $i -lt 4; $i++) { $s = [string]$d[$v % 36] + $s; $v = [int][Math]::Floor($v / 36) }
    return 'mint-' + $s
}
function Name-Value([string]$n) {                                                # "mint-XXXX" -> the number behind it
    $d = '0123456789abcdefghijklmnopqrstuvwxyz'; $v = 0
    foreach ($c in $n.Substring(5).ToCharArray()) { $v = $v * 36 + $d.IndexOf([string]$c) }
    return $v
}
function Test-T13 {                                                              # the default document name "mintXXXX"
    $t0 = Get-Date
    $app = Start-App
    $t1 = Get-Date
    $name = Default-Name $app
    Ck 'T13.1 an unsaved document is called "mint-" + 4 characters from 0-9 a-z (title "mint-XXXX - notepad mint")' ($name -ne $null) ('title [' + (Title $app) + ']')
    $cands = @()                                                                 # the app computed the name some time between just before the launch and the first title
    for ($t = $t0.AddSeconds(-1); $t -le $t1.AddSeconds(1); $t = $t.AddSeconds(1)) { $cands += (Expected-Name $t) }
    Ck 'T13.2 ... with no name handed out before it is the clock: base 36 of days since 2000-01-01 * 45 + seconds since midnight / 1920 (local time, +-1 s)' ($cands -contains $name) ('name ' + $name + ', the formula gives ' + (($cands | Select-Object -Unique) -join ' '))
    $script:iniLast = $null
    [void](WaitFor { $ri = Read-Ini $app.Ini; if ($ri.ContainsKey('name')) { $script:iniLast = $ri['name']['last'] }; $script:iniLast } 3000)
    CkEq 'T13.2b ... and settings.ini [name] last holds its number' ([string](Name-Value $name)) ([string]$script:iniLast)

    Cmd $app 'IDM_FILE_NEW'                                                      # (same 32 minute step: the counter, not the clock, makes it higher)
    $new = $null
    [void](WaitFor { $script:n2 = Default-Name $app; $script:n2 -and ($script:n2 -cne $name) } 3000)
    $new = $script:n2
    Ck 'T13.3 file > new gives the new document a higher default name, one more when the clock has not moved on' (($new -ne $null) -and ((Name-Value $new) -gt (Name-Value $name)) -and ((Name-Value $new) -eq (Name-Value $name) + 1 -or $cands -contains $new -or (Name-Value $new) -eq (Name-Value (Expected-Name (Get-Date))))) ('before [' + $name + '] after [' + $new + ']')
    $app2 = Start-App '' $app.AppData                                             # a second window that shares the settings
    $other = Default-Name $app2
    Ck 'T13.3b a second window (same settings.ini) gets a higher name than the last one handed out' (($other -ne $null) -and ((Name-Value $other) -gt (Name-Value $new))) ('last [' + $new + '] second window [' + $other + ']')
    Stop-App $app2

    Cmd $app 'IDM_FILE_SAVEAS'                                                   # the native save as dialog (see the helpers above T5)
    $dlg = Nat-Wait $app
    $prop = Nat-Text $dlg                                                        # what the name field proposes
    $esc = [regex]::Escape([string]$new)
    Ck 'T13.4 the save as dialog proposes the default name (mintXXXX, with or without ".txt")' ($prop -cmatch ('^' + $esc + '(\.txt)?$')) ('name field [' + (Show $prop) + '], default name [' + $new + ']')
    Nat-Type $dlg (Join-Path $work $prop)                                        # accept the proposal, but in a folder we know (a native dialog starts where windows remembers the last one)
    Nat-Press $dlg $IDOK                                                         # ".txt" is added when the proposal came without one
    $f = Join-Path $work ($new + '.txt')
    Ck 'T13.5 accepting it saves "mintXXXX.txt" and the title shows the file name' (Wait-Title $app ($new + '.txt - notepad mint') 8000) ('title [' + (Title $app) + ']')
    Ck 'T13.6 ... the file exists in the folder that was picked' (Test-Path -LiteralPath $f) ('missing: ' + $f)
}

# =========================================================================================================== T15
function Bars($app) { $st = [U]::Style((Get-Edit $app)); return @{ V = [bool]($st -band 0x00200000); H = [bool]($st -band 0x00100000) } }
function CkBars([string]$n, $app, [bool]$v, [bool]$h) {                           # the edit's scrollbars reach the wanted state (they follow posted messages) and STAY there:
    $sw = [Diagnostics.Stopwatch]::StartNew()                                    # 3 samples in a row, 40 ms apart (UpdateBars shows the horizontal bar for a few ms to probe its range, then hides it again:
    $streak = 0; $b = $null                                                      # a single sample can land inside that probe)
    while ($true) {
        $b = Bars $app
        if (($b.V -eq $v) -and ($b.H -eq $h)) { $streak++ } else { $streak = 0 }
        if (($streak -ge 3) -or ($sw.ElapsedMilliseconds -ge 3000)) { break }
        Start-Sleep -Milliseconds 40
    }
    $sh = [U]::Scroll((Get-Edit $app), 0)
    $extra = ''
    if ($streak -lt 3) {                                                         # a failure: how stable is the state? (samples 100 ms apart) + the edit's client size
        $s = @(); for ($k = 0; $k -lt 6; $k++) { $x = Bars $app; $s += ('V' + [int]$x.V + 'H' + [int]$x.H); Start-Sleep -Milliseconds 100 }
        $cr = [U]::CRect((Get-Edit $app))
        $extra = ' samples ' + ($s -join ',') + ' client ' + (($cr[2] - $cr[0]) -as [string]) + 'x' + (($cr[3] - $cr[1]) -as [string])
    }
    Ck $n ($streak -ge 3) ('wanted vertical=' + $v + ' horizontal=' + $h + ', got vertical=' + $b.V + ' horizontal=' + $b.H + ' (horizontal range min/max/page/pos ' + ($sh -join '/') + ')' + $extra)
}
function Test-T15 {                                                              # scrollbars only when needed (edit.c UpdateBars)
    $app = Start-App
    $wide = 'wide text ' * 60                                                    # one line far wider than the window
    $tall = (1..150 | ForEach-Object { 'line ' + $_ }) -join "`r`n"
    CkBars 'T15.1 an empty document has no scrollbars' $app $false $false
    Ed-Set $app $wide
    CkBars 'T15.2 one very wide line: the horizontal bar shows, no vertical bar' $app $false $true
    Ed-Set $app 'short'
    CkBars 'T15.3 ... short text again: no bars' $app $false $false
    Ed-Set $app $tall
    CkBars 'T15.4 150 short lines: the vertical bar shows, no horizontal bar' $app $true $false
    Ed-Set $app ($tall + "`r`n" + $wide)
    CkBars 'T15.5 150 lines and a wide one: both bars' $app $true $true
    Cmd $app 'IDM_FMT_WRAP'
    CkBars 'T15.6 word wrap on: the wide line wraps, only the vertical bar' $app $true $false
    Cmd $app 'IDM_FMT_WRAP'
    CkBars 'T15.7 word wrap off again: both bars' $app $true $true
    Ed-Set $app $wide
    CkBars 'T15.8 wide line only (wrap off): horizontal bar only' $app $false $true
    Cmd $app 'IDM_FMT_WRAP'
    CkBars 'T15.9 wide line only, wrap on: no bars (it wraps into a few rows)' $app $false $false
}

# =========================================================================================================== T16
function Test-T16 {                                                              # dirty state by content (app_state.c AppIsDirty): the title's "*" and the close prompt follow it
    # Own the line-ending fixture: a maintainer may edit the multilingual sample.
    $dirtyFile = Join-Path $work 'dirty-state-lf.txt'
    [IO.File]::WriteAllText($dirtyFile, "alpha`nbeta`nlast", (New-Object Text.UTF8Encoding($false)))
    $EM_BACK = 8                                                                 # WM_CHAR backspace: a real keystroke-like deletion (the control's own modified flag stays set)
    $app = Start-App                                                             # --- a blank unsaved document
    $name = Default-Name $app
    $clean = $name + ' - notepad mint'
    CkEq 'T16.1 a blank document is not modified (no "*" in the title)' $clean (Title $app)
    Ed-Dirty $app 'x'
    Ck 'T16.2 typing a character marks it modified ("*mintXXXX - notepad mint")' (Wait-Title $app ('*' + $clean)) ('title [' + (Title $app) + ']')
    [void](Snd (Get-Edit $app) 0x102 $EM_BACK 0)                                 # WM_CHAR backspace
    Ck 'T16.3 deleting it again: blank again = not modified' (Wait-Title $app $clean) ('title [' + (Title $app) + ']')
    Ed-Dirty $app 'abc'
    Ck 'T16.4 typing three characters: modified' (Wait-Title $app ('*' + $clean)) ('title [' + (Title $app) + ']')
    [void](Snd (Get-Edit $app) $EM_SETSEL 0 -1)
    Ed-Dirty $app ''                                                             # EM_REPLACESEL with '' = delete the selection
    Ck 'T16.5 select all + delete: blank again = not modified' (Wait-Title $app $clean) ('title [' + (Title $app) + ']')
    Cmd $app 'IDM_EOL_LF'
    Start-Sleep -Milliseconds 300
    CkEq 'T16.6 another line ending on an empty unsaved document: nothing to save, still not modified' $clean (Title $app)
    Pst $app.Main $WM_CLOSE 0 0
    Ck 'T16.7 WM_CLOSE on it exits within 3 s with no save prompt' ($app.Proc.WaitForExit(3000)) 'still running 3 s after WM_CLOSE (a prompt?)'

    $app = Start-App $dirtyFile                                                # --- a file document (lf line endings)
    $fn = 'dirty-state-lf.txt - notepad mint'
    Ck 'T16.8 a loaded file is not modified' (Wait-Title $app $fn) ('title [' + (Title $app) + ']')
    Ed-Dirty $app 'x'
    Ck 'T16.9 typing a character: modified' (Wait-Title $app ('*' + $fn)) ('title [' + (Title $app) + ']')
    [void](Snd (Get-Edit $app) $EM_UNDO 0 0)
    Ck 'T16.10 undo back to the original text: not modified' (Wait-Title $app $fn) ('title [' + (Title $app) + ']')
    CkEdText 'T16.11 ... and the text is the file again' $app (Expected-Text $dirtyFile)
    Ed-Dirty $app 'x'
    [void](Wait-Title $app ('*' + $fn))
    [void](Snd (Get-Edit $app) 0x102 $EM_BACK 0)
    Ck 'T16.12 type and delete: not modified' (Wait-Title $app $fn) ('title [' + (Title $app) + ']')
    Cmd $app 'IDM_EOL_CRLF'
    Ck 'T16.13 a new line ending on a file document: modified (it needs a save)' (Wait-Title $app ('*' + $fn)) ('title [' + (Title $app) + ']')
    Cmd $app 'IDM_EOL_LF'
    Ck 'T16.14 ... and back to the original line ending: not modified' (Wait-Title $app $fn) ('title [' + (Title $app) + ']')
    Ed-Dirty $app 'x'
    [void](Snd (Get-Edit $app) $EM_UNDO 0 0)
    [void](Wait-Title $app $fn)
    Pst $app.Main $WM_CLOSE 0 0
    Ck 'T16.15 WM_CLOSE after an undone edit exits within 3 s with no save prompt' ($app.Proc.WaitForExit(3000)) 'still running 3 s after WM_CLOSE (a prompt?)'

    $app = Start-App $dirtyFile                                                # --- the opposite: a real change still asks
    [void](Wait-Title $app $fn)
    Ed-Dirty $app 'x'
    [void](Wait-Title $app ('*' + $fn))
    Pst $app.Main $WM_CLOSE 0 0
    $box = Wait-Box $app $AppName
    Ck 'T16.16 WM_CLOSE on a really modified document still asks to save' ($box -ne $null) 'no save prompt'
    if ($box) { [void](Box-Press $box $BOX_BTN2) }                               # "don't save"
    Ck 'T16.17 "don''t save" exits the app' ($app.Proc.WaitForExit(3000)) 'still running 3 s after "don''t save"'
}

# =========================================================================================================== T17
function Test-T17 {                                                              # the about box links to yuru.be (about.c mp_link; never clicked: that opens a browser)
    $app = Start-App
    Cmd $app 'IDM_HELP_ABOUT'
    $h = Wait-Win $app 'mp_about' 'about notepad mint'
    $link = $null
    foreach ($k in [U]::Kids($h)) { if ([U]::Cls($k) -eq 'mp_link') { $link = [long]$k } }
    Ck 'T17.1 the about box has a link control (class mp_link)' ($link -ne $null) 'no mp_link child in the about box'
    if ($link) {
        CkEq 'T17.2 ... it reads "yuru.be"' 'yuru.be' ([U]::Text($link))
        $lr = [U]::WRect($link); $dr = [U]::WRect($h)                            # left, top, right, bottom (screen)
        Ck 'T17.3 ... it is visible and lies inside the dialog' ([U]::Visible($link) -and $lr[0] -ge $dr[0] -and $lr[1] -ge $dr[1] -and $lr[2] -le $dr[2] -and $lr[3] -le $dr[3]) ('link ' + ($lr -join ',') + ' dialog ' + ($dr -join ','))
    }
    $said = '(no label)'                                                         # the version of the exe (FILEVERSION in src\notepad_mint.rc), not a stale one
    foreach ($k in [U]::Kids($h)) { if ([U]::Text($k) -match '^version (\S+)') { $said = $Matches[1] } }
    $fv = [regex]::Match([IO.File]::ReadAllText((Join-Path $Src 'notepad_mint.rc')), 'FILEVERSION (\d+),(\d+),(\d+),(\d+)')
    CkEq 'T17.5 the about box says "version x.y.z" with the FILEVERSION of notepad_mint.rc' ($fv.Groups[1].Value + '.' + $fv.Groups[2].Value + '.' + $fv.Groups[3].Value) $said
    Pst $h $WM_CLOSE 0 0
    Ck 'T17.4 the about box closes on WM_CLOSE' (Gone $h) 'the window is still visible'
}

# =========================================================================================================== T18
# the classic scrollbars (sbar.c): our own window of class mp_sbar lies over each native bar strip of the editor, paints a classic bar in the
# palette and drives the editor with the messages the native bar would send. all of it is driven by posted / sent mouse messages here.
function Sbars($app) {                                                           # the VISIBLE overlays (children of the main window): @{ V = hwnd or 0; H = hwnd or 0 }
    $r = @{ V = [long]0; H = [long]0 }
    foreach ($k in [U]::Kids([long]$app.Main)) {
        if ([U]::Cls($k) -ne 'mp_sbar' -or -not [U]::Visible($k)) { continue }
        $w = [U]::WRect($k)
        if (($w[3] - $w[1]) -gt ($w[2] - $w[0])) { $r.V = [long]$k } else { $r.H = [long]$k }
    }
    return $r
}
function CkSb([string]$n, $app, [bool]$v, [bool]$h) {                             # the overlays show exactly when the native bars do, and stay that way (3 samples in a row)
    $sw = [Diagnostics.Stopwatch]::StartNew(); $streak = 0; $s = $null; $b = $null
    while ($true) {
        $s = Sbars $app; $b = Bars $app
        if (([bool]$s.V -eq $v) -and ([bool]$s.H -eq $h) -and ($b.V -eq $v) -and ($b.H -eq $h)) { $streak++ } else { $streak = 0 }
        if (($streak -ge 3) -or ($sw.ElapsedMilliseconds -ge 3500)) { break }
        Start-Sleep -Milliseconds 60
    }
    Ck $n ($streak -ge 3) ('wanted vertical=' + $v + ' horizontal=' + $h + ', overlays vertical=' + [bool]$s.V + ' horizontal=' + [bool]$s.H + ', native bars vertical=' + $b.V + ' horizontal=' + $b.H)
}
function Mouse($ov, [int]$msg, [int]$x, [int]$y, [int]$keys = 0) { [void](Snd $ov $msg $keys ((([long]$y) -shl 16) -bor ([long]($x -band 0xFFFF)))) }
function Click-Sb($ov, [int]$x, [int]$y) { Mouse $ov 0x201 $x $y 1; Mouse $ov 0x202 $x $y 0 }     # WM_LBUTTONDOWN + WM_LBUTTONUP
function Sb-Thumb($app, $ov) {                                                   # where the classic algorithm puts the vertical thumb: a0 (start), len, centre (overlay client coordinates)
    $si = [U]::Scroll((Get-Edit $app), 1); $c = [U]::CRect($ov); $w = $c[2]; $h = $c[3]
    $range = $si[1] - $si[0] + 1; $page = $si[2]; $tl = $h - 2 * $w
    $len = [Math]::Max($w, [int][Math]::Round($tl * $page / $range, [MidpointRounding]::AwayFromZero))
    $maxp = $range - $page
    $a0 = $w + $(if ($maxp -gt 0) { [int][Math]::Round(($tl - $len) * ($si[3] - $si[0]) / $maxp, [MidpointRounding]::AwayFromZero) } else { 0 })
    return @{ A0 = $a0; Len = $len; Centre = $a0 + [int]($len / 2); W = $w; H = $h; Tl = $tl; Range = $range; Page = $page; MaxP = $maxp }
}
function Sb-Face($app, $ov) {                                                    # the colour inside the first arrow button of an overlay, from PrintWindow of the main window
    $w = [U]::WRect([long]$app.Main); $o = [U]::WRect([long]$ov)
    $bmp = [U]::Grab([long]$app.Main)
    try {
        $x = ($o[0] - $w[0]) + 3; $y = ($o[1] - $w[1]) + 3
        if ($x -lt 0 -or $y -lt 0 -or $x -ge $bmp.Width -or $y -ge $bmp.Height) { return $null }
        $c = $bmp.GetPixel($x, $y)
        return [pscustomobject]@{ R = [int]$c.R; G = [int]$c.G; B = [int]$c.B; Info = ('rgb(' + $c.R + ',' + $c.G + ',' + $c.B + ') at ' + $x + ',' + $y) }
    } finally { $bmp.Dispose() }
}
function Test-T18 {
    $app = Start-App
    $wide = 'wide text ' * 60
    $tall = (1..150 | ForEach-Object { 'line ' + $_ }) -join "`r`n"
    CkSb 'T18.1 an empty document: no classic bars (the native ones are hidden as well)' $app $false $false
    Ed-Set $app $wide
    CkSb 'T18.2 one very wide line: the horizontal bar only' $app $false $true
    Ed-Set $app $tall
    CkSb 'T18.3 150 short lines: the vertical bar only' $app $true $false
    Ed-Set $app ($tall + "`r`n" + $wide)
    CkSb 'T18.4 150 lines and a wide one: both bars' $app $true $true
    $ed = Get-Edit $app; $wr = [U]::WRect($ed); $cr = [U]::CRect($ed); $sb = Sbars $app
    if ($sb.V -and $sb.H) {
        $vr = [U]::WRect($sb.V); $hr = [U]::WRect($sb.H)
        $vs = ($wr[2] - $wr[0]) - ($cr[2] - $cr[0]); $hs = ($wr[3] - $wr[1]) - ($cr[3] - $cr[1]); $tr = Trim-Px $app
        Ck 'T18.5 the vertical bar covers the editor''s whole right strip up to the overhang (the corner square included): SBAR_TRIM px thinner than the strip' (($vr[0] -eq $wr[2] - $vs) -and ($vr[2] -eq $wr[2] - $tr) -and ($vr[1] -eq $wr[1]) -and ($vr[3] -eq $wr[3] - $tr)) ('overlay ' + ($vr -join ',') + ' editor ' + ($wr -join ',') + ' strip ' + $vs + ' trim ' + $tr)
        Ck 'T18.6 the horizontal bar covers the bottom strip up to the corner and the overhang' (($hr[1] -eq $wr[3] - $hs) -and ($hr[3] -eq $wr[3] - $tr) -and ($hr[0] -eq $wr[0]) -and ($hr[2] -eq $vr[0])) ('overlay ' + ($hr -join ',') + ' editor ' + ($wr -join ',') + ' strip ' + $hs + ' trim ' + $tr)
    } else { Fail 'T18.5 / T18.6 geometry' 'an overlay is missing' }
    Cmd $app 'IDM_FMT_WRAP'
    CkSb 'T18.7 word wrap on: the vertical bar only' $app $true $false
    Cmd $app 'IDM_FMT_WRAP'
    CkSb 'T18.8 word wrap off again: both bars' $app $true $true

    Reset-Doc $app $tall                                                         # --- driving the vertical bar with mouse messages
    CkSb 'T18.9 (setup) 150 lines: the vertical bar only' $app $true $false
    $ed = Get-Edit $app; $v = (Sbars $app).V
    if (-not $v) { Fail 'T18.10 - T18.17 interaction' 'no vertical overlay'; return }
    $c = [U]::CRect($v); $w = $c[2]; $h = $c[3]
    CkEq 'T18.10 the document starts at the first line' 0 (Snd $ed 0xCE 0 0)
    Click-Sb $v ([int]($w / 2)) ($h - [int]($w / 2))                              # the down arrow
    CkEq 'T18.11 the down arrow scrolls one line' 1 (Snd $ed 0xCE 0 0)
    Click-Sb $v ([int]($w / 2)) ([int]($w / 2))                                   # the up arrow
    CkEq 'T18.12 the up arrow scrolls it back' 0 (Snd $ed 0xCE 0 0)
    [void](Snd $ed 0x115 3 0)                                                    # what the native bar does for a click on its track: WM_VSCROLL SB_PAGEDOWN
    $page = [int](Snd $ed 0xCE 0 0)
    [void](Snd $ed 0xB6 0 (-$page))                                              # back to line 0
    Click-Sb $v ([int]($w / 2)) ($h - $w - 3)                                     # the track below the thumb
    CkEq 'T18.13 a click on the track below the thumb scrolls one page (the same as the native bar)' $page (Snd $ed 0xCE 0 0)
    [void](Snd $ed 0xB6 0 (-$page))
    $t = Sb-Thumb $app $v
    Mouse $v 0x201 ([int]($w / 2)) $t.Centre 1                                    # grab the thumb ...
    Mouse $v 0x200 ([int]($w / 2)) ($t.Centre + [int]($t.Tl / 2)) 1               # ... half way down the track
    Mouse $v 0x200 ([int]($w / 2)) ($h - 1) 1                                     # ... and far past the end
    Mouse $v 0x202 ([int]($w / 2)) ($h - 1) 0
    $si = [U]::Scroll($ed, 1)
    CkEq 'T18.14 dragging the thumb past the end of the track stops at the last page' ($si[1] - $si[2] + 1) $si[3]
    $before = [int](Snd $ed 0xCE 0 0)
    [void](Snd $v 0x20A ([long](((-120) -band 0xFFFF) -shl 16)) 0)                # the wheel over the bar goes to the editor
    Ck 'T18.15 the mouse wheel over the bar is forwarded to the editor (it scrolls)' ((Snd $ed 0xCE 0 0) -ne $before -or $before -eq ($si[1] - $si[2] + 1)) 'the first visible line did not change'

    $pal = Read-Palette                                                          # --- the classic bar is drawn in the palette, not by windows
    if ($pal) {
        $ok = WaitFor { Near (Sb-Face $app $v) $pal.dark.face } 3000
        Ck 'T18.16 dark: the arrow button face is the palette face colour (the native bar does not show through)' $ok ('actual ' + (Sb-Face $app $v).Info)
        Cmd $app 'IDM_THEME_LIGHT'
        $ok = WaitFor { Near (Sb-Face $app $v) $pal.light.face } 3000
        Ck 'T18.17 light: ... and follows the theme switch' $ok ('actual ' + (Sb-Face $app $v).Info)
        Cmd $app 'IDM_THEME_DARK'
    } else { Skip 'T18.16 / T18.17 pixels' 'could not parse the g_themes palette table in src\ui.c' }

    $big = (1..70000 | ForEach-Object { 'x' }) -join "`r`n"                        # --- a document with more lines than a 16 bit scroll position holds
    Reset-Doc $app $big
    CkSb 'T18.18 (setup) 70000 lines: the vertical bar only' $app $true $false
    $ed = Get-Edit $app; $v = (Sbars $app).V
    $t = Sb-Thumb $app $v; $w = $t.W
    Mouse $v 0x201 ([int]($w / 2)) $t.Centre 1
    Mouse $v 0x200 ([int]($w / 2)) ($t.Centre + [int]($t.Tl / 2)) 1
    Mouse $v 0x202 ([int]($w / 2)) ($t.Centre + [int]($t.Tl / 2)) 0
    $first = [int](Snd $ed 0xCE 0 0)
    $want = [int]($t.MaxP * ($t.Tl / 2) / ($t.Tl - $t.Len))                      # the thumb moved by half the track length: that share of the range
    Ck 'T18.19 dragging the thumb half way along the track lands there in 70000 lines (within 1%): the 16 bit scroll position does not limit it' ([Math]::Abs($first - $want) -le 700) ('first visible line ' + $first + ', wanted about ' + $want)
}

# =========================================================================================================== T19
# the main window's chrome: no frame around the editor but an 8 px padding inside it, the chrome font is static, the title strip is a flat tint of the accent
function Chrome-Kid($app, [string]$cls) { foreach ($k in [U]::Kids([long]$app.Main)) { if ([U]::Cls($k) -eq $cls) { return [long]$k } }; return [long]0 }
function Accent-Colours() {                                                      # the accents of the two themes, from the g_themes table in ui.c (first colour of each theme)
    $t = [IO.File]::ReadAllText((Join-Path $Src 'ui.c'))
    $m = [regex]::Match($t, 'g_themes\[2\]\s*=\s*\{(.*?)\n\};', 'Singleline')
    $c = @([regex]::Matches($m.Groups[1].Value, 'RGB\(\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*\)') | ForEach-Object {
        , @([Convert]::ToInt32($_.Groups[1].Value.Substring(2), 16), [Convert]::ToInt32($_.Groups[2].Value.Substring(2), 16), [Convert]::ToInt32($_.Groups[3].Value.Substring(2), 16)) })
    if ($c.Count -ne 28) { return $null }
    return @{ dark = $c[0]; light = $c[14] }
}
function Strip-Pixel($app, [int]$x, [int]$y) {                                   # a pixel of the title strip (window coordinates, from PrintWindow)
    $bmp = [U]::Grab([long]$app.Main)
    try {
        if ($x -ge $bmp.Width -or $y -ge $bmp.Height) { return $null }
        $c = $bmp.GetPixel($x, $y)
        return [pscustomobject]@{ R = [int]$c.R; G = [int]$c.G; B = [int]$c.B; Info = ('rgb(' + $c.R + ',' + $c.G + ',' + $c.B + ') at ' + $x + ',' + $y) }
    } finally { $bmp.Dispose() }
}
function Test-T19 {
    $app = Start-App
    $dpi = [U]::Dpi([long]$app.Main)
    $pad = [int][Math]::Round($IDM.EDIT_PAD * $dpi / 96, [MidpointRounding]::AwayFromZero)           # left / right / bottom (8 px at 96 dpi)
    $padTop = [int][Math]::Round($IDM.EDIT_PAD_TOP * $dpi / 96, [MidpointRounding]::AwayFromZero)    # top (4 px at 96 dpi: 4 less than the others since 2026-10-06)
    $bar = Chrome-Kid $app 'mp_menubar'; $st = Chrome-Kid $app 'mp_status'; $ed = Get-Edit $app
    $b = [U]::WRect($bar); $s = [U]::WRect($st); $e = [U]::WRect($ed); $w = [U]::WRect([long]$app.Main)
    $tr = Trim-Px $app
    Ck 'T19.1 no frame around the editor: it fills the space between the menu bar and the status bar (its window overhangs by SBAR_TRIM px at the right and at the bottom: under the status bar, clipped by the window)' (($e[1] -eq $b[3]) -and ($e[3] -eq $s[1] + $tr) -and ($e[0] -eq $b[0]) -and ($e[2] -eq $b[2] + $tr)) ('menu bar ' + ($b -join ',') + ' editor ' + ($e -join ',') + ' status bar ' + ($s -join ',') + ' trim ' + $tr)
    Reset-Doc $app 'x'
    $p = [long](Snd $ed 0xD6 0 0)                                                # EM_POSFROMCHAR of the first character: the client position of the text
    Ck 'T19.2 the text starts 8 px from the left and 4 px from the top (dpi scaled) of the editor: the padding (EDIT_PAD, EDIT_PAD_TOP)' ((($p -band 0xFFFF) -eq $pad) -and ((($p -shr 16) -band 0xFFFF) -eq $padTop)) ('first character at ' + ($p -band 0xFFFF) + ',' + (($p -shr 16) -band 0xFFFF) + ', wanted ' + $pad + ',' + $padTop)

    $h0 = @(($b[3] - $b[1]), ($s[3] - $s[1]))                                    # the chrome font is static: zooming the editor leaves the bars alone
    foreach ($i in 1..4) { Cmd $app 'IDM_ZOOM_IN'; Start-Sleep -Milliseconds 150 }
    Start-Sleep -Milliseconds 400
    $b = [U]::WRect($bar); $s = [U]::WRect($st)
    Ck 'T19.3 zooming the editor in four steps leaves the menu bar and the status bar heights alone (static chrome font)' ((($b[3] - $b[1]) -eq $h0[0]) -and (($s[3] - $s[1]) -eq $h0[1])) ('heights before ' + ($h0 -join ',') + ' after ' + ($b[3] - $b[1]) + ',' + ($s[3] - $s[1]))
    foreach ($i in 1..4) { Cmd $app 'IDM_ZOOM_OUT'; Start-Sleep -Milliseconds 100 }

    Ck 'T19.3a the status bar repaints all of itself on a size change (class styles CS_HREDRAW | CS_VREDRAW: its panels are right aligned)' (([Nd]::ClassStyle($st) -band 3) -eq 3) ('class style ' + [Nd]::ClassStyle($st))
    $acc = Accent-Colours; $pal = Read-Palette
    if ($acc -and $pal) {                                                        # the title strip is flat (no gradient): 9% of the accent (21% in the light theme), 7 points less in an inactive window (clamped at 0)
        foreach ($th in @('dark', 'light')) {
            if ($th -eq 'light') { Cmd $app 'IDM_THEME_LIGHT'; Start-Sleep -Milliseconds 500 }
            $face = $pal[$th].face; $a = $acc[$th]
            $pA = 9; $pI = 2                                                           # percent of the accent: active / inactive
            if ($th -eq 'light') { $pA = 21; $pI = 14 }                                 # +12 points in the light theme
            $want = @(0, 1, 2 | ForEach-Object { [int]($face[$_] + ($a[$_] - $face[$_]) * [Math]::Floor($pA * 255 / 100) / 255) })     # (the app tints with alpha = percent * 255 / 100, integer)
            $wantI = @(0, 1, 2 | ForEach-Object { [int]($face[$_] + ($a[$_] - $face[$_]) * [Math]::Floor($pI * 255 / 100) / 255) })
            $bw = [int](($w[2] - $w[0] - [U]::CRect([long]$app.Main)[2]) / 2)                  # the sizing border left of the client area (the strip starts after it)
            $btn = [int](($w[2] - $w[0]) - 3 * [Math]::Round(38 * $dpi / 96) - 4)              # where the window buttons start
            $mid = [int](($w[2] - $w[0]) / 2)
            $left = Strip-Pixel $app ($bw + 2) 3
            Ck ('T19.4 ' + $th + ': the title strip is a flat ' + $pA + '% of the accent: at its left edge (rgb ' + ($want -join ',') + ')') (Near $left $want 3) ('actual ' + $left.Info)
            $m = Strip-Pixel $app $mid 3; $right = Strip-Pixel $app $btn 3
            Ck ('T19.5 ' + $th + ': ... the same in the middle and where the window buttons start (no gradient)') ((Near $m $want 3) -and (Near $right $want 3)) ('middle ' + $m.Info + ', buttons ' + $right.Info)
            $under = Strip-Pixel $app ($btn + 8) 3                                       # inside the minimize button, away from its glyph: transparent at rest, the strip shows through
            Ck ('T19.6 ' + $th + ': ... and under the window buttons (they are transparent at rest: no seam)') (Near $under $want 3) ('actual ' + $under.Info)
            [void](Snd $app.Main 0x6 0 0)                                                # WM_ACTIVATE(WA_INACTIVE): 7 points less of the accent (clamped at 0)
            $okL = WaitFor { Near (Strip-Pixel $app ($bw + 2) 3) $wantI 3 } 3000
            Ck ('T19.7 ' + $th + ': an inactive window: a flat ' + $pI + '% of the accent at the left edge (rgb ' + ($wantI -join ',') + ')') $okL ('actual ' + (Strip-Pixel $app ($bw + 2) 3).Info)
            Ck ('T19.8 ' + $th + ': ... and the same in the middle and where the window buttons start') ((Near (Strip-Pixel $app $mid 3) $wantI 3) -and (Near (Strip-Pixel $app $btn 3) $wantI 3)) ('middle ' + (Strip-Pixel $app $mid 3).Info + ', buttons ' + (Strip-Pixel $app $btn 3).Info)
            [void](Snd $app.Main 0x6 1 0)                                                # active again
            Ck ('T19.9 ' + $th + ': active again: back to ' + $pA + '% at the left edge') (WaitFor { Near (Strip-Pixel $app ($bw + 2) 3) $want 3 } 3000) ('actual ' + (Strip-Pixel $app ($bw + 2) 3).Info)
        }
    } else { Skip 'T19.4 / T19.5 title strip pixels' 'could not parse the g_themes palette table in src\ui.c' }

    # the strip's geometry, through WM_NCHITTEST at screen points (messages only: no real mouse): the icon area is HTCLIENT (ours), the rest of the strip HTCAPTION
    $w = [U]::WRect([long]$app.Main); $bw = [int](($w[2] - $w[0] - [U]::CRect([long]$app.Main)[2]) / 2); $b = [U]::WRect($bar)
    function Px([int]$n) { [int][Math]::Round($n * $dpi / 96, [MidpointRounding]::AwayFromZero) }
    function Hit([int]$cx, [int]$cy) { [int](Snd $app.Main 0x84 0 ([long]((($w[1] + $cy) -shl 16) -bor (($w[0] + $bw + $cx) -band 0xFFFF)))) }   # WM_NCHITTEST at a client point
    $iconR = (Px 8) + (Px 16) + (Px 6); $capH = (Px 24) - (Px 1)
    $yIn = [int]($capH / 2)
    Ck 'T19.10 the strip is 1 px shorter than it was (24 px at 96 dpi -> 23): the menu bar starts right under it'(($b[1] - $w[1]) -eq $capH) ('menu bar top is ' + ($b[1] - $w[1]) + ' px below the window top, wanted ' + $capH)
    Ck ('T19.11 the icon area ends ' + $iconR + ' px from the left edge of the client: the icon sits 8 px from it (10 px before) and the title follows') ((Hit ($iconR - 1) $yIn) -eq 1 -and (Hit $iconR $yIn) -eq 2) ('hit at x ' + ($iconR - 1) + ' = ' + (Hit ($iconR - 1) $yIn) + ' (wanted 1 HTCLIENT), at x ' + $iconR + ' = ' + (Hit $iconR $yIn) + ' (wanted 2 HTCAPTION)')
    Ck 'T19.12 the strip''s last row (y = height - 1) is still caption, the row under it belongs to the menu bar (client)'((Hit 200 ($capH - 1)) -eq 2 -and (Hit 200 $capH) -eq 1) ('hit at y ' + ($capH - 1) + ' = ' + (Hit 200 ($capH - 1)) + ' (wanted 2), at y ' + $capH + ' = ' + (Hit 200 $capH) + ' (wanted 1)')

    # the title: the NAME is bold, " - notepad mint" is not. a file called "notepad mint.txt" makes the title "notepad mint.txt - notepad mint": the same twelve glyphs
    # "notepad mint" once in the bold name and once in the regular tail, so the ink of the two (the pixels' distance from the flat strip colour) compares the weights
    # (measured: the bold copy has about 1.2 x the ink of the regular one; two regular copies would be 1.0)
    $nm = Join-Path $work 'notepad mint.txt'; [IO.File]::WriteAllText($nm, 'x')
    $app2 = Start-App $nm
    [void](Wait-Title $app2 'notepad mint.txt - notepad mint')
    Start-Sleep -Milliseconds 400
    $w2 = [U]::WRect([long]$app2.Main); $bw2 = [int](($w2[2] - $w2[0] - [U]::CRect([long]$app2.Main)[2]) / 2)
    $bmp = [U]::Grab([long]$app2.Main)
    try {
        function Lum($c) { 0.299 * $c.R + 0.587 * $c.G + 0.114 * $c.B }
        function Ink([int]$xa, [int]$xb, [int]$ya, [int]$yb, [double]$bg) { $s = 0.0; for ($yy = $ya; $yy -lt $yb; $yy++) { for ($xx = $xa; $xx -lt $xb; $xx++) { $s += [Math]::Abs((Lum ($bmp.GetPixel($xx, $yy))) - $bg) } }; $s }
        $x0 = $bw2 + $iconR + (Px 4); $xEnd = ($w2[2] - $w2[0]) - 3 * (Px 38) - (Px 8)
        $bg = Lum ($bmp.GetPixel(200, 2))                                                # the strip is flat: any pixel above the text is its colour
        $right = -1
        for ($xx = $xEnd; $xx -gt $x0 -and $right -lt 0; $xx--) { if ((Ink $xx ($xx + 1) (Px 5) (Px 19) $bg) -gt 40) { $right = $xx } }     # the last inked column = the end of the title
        $cell = ($right - $x0 + 1) / 31.0                                                # 31 characters, one advance each (a monospaced face)
        $head = Ink $x0 ([int]($x0 + 12 * $cell)) (Px 4) (Px 20) $bg                     # "notepad mint" in the name
        $tail = Ink ([int]($x0 + 19 * $cell)) ([int]($x0 + 31 * $cell)) (Px 4) (Px 20) $bg   # "notepad mint" after " - "
        $ratio = if ($tail -gt 0) { $head / $tail } else { 0 }
        Ck 'T19.13 the name in the title is bold, " - notepad mint" is not: the same twelve glyphs have at least 10% more ink in the name' ($ratio -ge 1.10) ('ink ratio name / tail = ' + [Math]::Round($ratio, 3) + ' (head ' + [Math]::Round($head) + ', tail ' + [Math]::Round($tail) + ', text from x ' + $x0 + ' to ' + $right + ', cell ' + [Math]::Round($cell, 2) + ')')
    } finally { $bmp.Dispose() }

    # three things whose values cannot be read from outside the process (a caret's colour, a font's size) or are plain palette numbers: checked in the sources.
    # the caret is the edit control's own, an inverting rectangle (pure white on the dark editor's black, pure black on the light editor's white);
    # the menu bar, popups, status bar and title strip all use the one chrome font, CHROME_PX = 11 (a 10 px menu / status font was tried and went back)
    $mpH = [IO.File]::ReadAllText((Join-Path $Src 'mp.h')); $uiSrc = [IO.File]::ReadAllText((Join-Path $Src 'ui.c')); $frSrc = [IO.File]::ReadAllText((Join-Path $Src 'frame.c'))
    $allSrc = ((Get-ChildItem -LiteralPath $Src -Filter *.c | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n")
    Ck 'T19.14 the caret is the edit control''s own inverting one (pure white on the dark theme, pure black on the light one): no source creates a caret' ($allSrc -notmatch '\bCreateCaret\s*\(') 'CreateCaret( found in src\*.c'
    Ck 'T19.15 menu bar, popups, status bar and title strip all use the one chrome font, CHROME_PX = 11 (g_fontMenu, bold twin g_fontMenuB for the title)' (($mpH -match '#define\s+CHROME_PX\s+11\b') -and ($mpH -notmatch 'MENU_PX') -and ($uiSrc -match '(?:g_fontMenu|c\.font)\s*=\s*FacePx\(\s*g_chromeFace\s*,\s*CHROME_PX\s*,\s*FW_NORMAL\s*\)') -and ($uiSrc -match '(?:g_fontMenuB|c\.bold)\s*=\s*FacePx\(\s*g_chromeFace\s*,\s*CHROME_PX\s*,\s*FW_BOLD\s*\)') -and ($frSrc -match 'SelectObject\(mdc,\s*g_fontMenuB\)')) 'the font sizes are not wired as expected (src\mp.h, src\ui.c, src\frame.c)'
    if ($pal) { Ck 'T19.16 the light face is #dfdee1 = rgb(223,222,225) (it was #e4e1da: 5 less red, 3 less green, 7 more blue)' (($pal['light'].face -join ',') -eq '223,222,225') ('light face is ' + ($pal['light'].face -join ',')) }
}

# =========================================================================================================== T20
# the minimum size is a size of the ENTIRE window (frame included): 320 px wide (the status panels may ask for more) and 140 px high, dpi scaled
function Test-T20 {
    $app = Start-App
    $dpi = [U]::Dpi([long]$app.Main)
    $wantW = [int][Math]::Round(320 * $dpi / 96, [MidpointRounding]::AwayFromZero)
    $wantH = [int][Math]::Round(140 * $dpi / 96, [MidpointRounding]::AwayFromZero)
    [void][Nd]::Size([long]$app.Main, 100, 50)                                   # far too small: the window stops at its minimum
    $w = WaitFor { $r = [U]::WRect([long]$app.Main); if (($r[2] - $r[0]) -le 2000) { $r } } 3000
    $gw = $w[2] - $w[0]; $gh = $w[3] - $w[1]
    Ck ('T20.1 shrunk as far as it goes the whole window is ' + $wantW + ' px wide (320 px at 96 dpi; the status panels need less)') ($gw -eq $wantW) ('width ' + $gw + ', wanted ' + $wantW + ' (dpi ' + $dpi + ')')
    Ck ('T20.2 ... and ' + $wantH + ' px high (140 px at 96 dpi), title strip and frame included') ($gh -eq $wantH) ('height ' + $gh + ', wanted ' + $wantH + ' (dpi ' + $dpi + ')')
    $bar = Chrome-Kid $app 'mp_menubar'; $st = Chrome-Kid $app 'mp_status'; $ed = Get-Edit $app
    $b = [U]::WRect($bar); $s = [U]::WRect($st); $e = [U]::WRect($ed)
    Ck 'T20.3 at that size the menu bar, the editor (at least one pixel high) and the status bar still stack inside the window without overlapping' (($e[3] - (Trim-Px $app) -gt $e[1]) -and ($b[3] -le $e[1]) -and ($e[3] - (Trim-Px $app) -le $s[1]) -and ($s[3] -le $w[3])) ('menu bar ' + ($b -join ',') + ' editor ' + ($e -join ',') + ' status bar ' + ($s -join ',') + ' window ' + ($w -join ','))
    [void][Nd]::Size([long]$app.Main, 900, 600)                                  # and it still grows
    $g = WaitFor { $r = [U]::WRect([long]$app.Main); if (($r[2] - $r[0]) -eq 900 -and ($r[3] - $r[1]) -eq 600) { $r } } 3000
    Ck 'T20.4 the window can still be made bigger again (900 x 600)' ($g -ne $null) ('size now ' + (([U]::WRect([long]$app.Main)) -join ','))
}

# =========================================================================================================== T21
# ctrl+w closes the window like file > exit. an accelerator needs real keyboard state (the message loop asks the thread's key state, which posted
# messages never change, and a private desktop gets no injected input): the table entry and the menu label are checked in the sources, the help
# line in the real help window, the command itself (exit, with the unsaved prompt) is T12's
function Test-T21 {
    $main = [IO.File]::ReadAllText((Join-Path $Src 'main.c'))
    $menu = [IO.File]::ReadAllText((Join-Path $Src 'menu_defs.c'))
    Ck 'T21.1 the accelerator table maps ctrl+w to file > exit' ($main -match 'FVIRTKEY\s*\|\s*FCONTROL\s*,\s*''W''\s*,\s*IDM_FILE_EXIT\s*\}') 'no { FVIRTKEY | FCONTROL, ''W'', IDM_FILE_EXIT } entry in src\main.c'
    Ck 'T21.2 file > exit shows "ctrl+w" as its shortcut' ($menu -match 'IT\(L"e&xit",\s*L"ctrl\+w",\s*IDM_FILE_EXIT\)') 'the exit item in src\menu_defs.c has no ctrl+w label'
    $app = Start-App
    Cmd $app 'IDM_HELP_TOPICS'
    $h = Wait-Win $app 'mp_help' 'help topics'
    $t = ''
    foreach ($k in [U]::Kids($h)) { if ([U]::Cls($k) -eq 'Edit') { $t = [U]::GetText($k) } }
    Ck 'T21.3 the help topics list ctrl+w' ($t -match 'ctrl\+w\s+close the window') ('help text [' + (Show $t) + ']')
    Ck 'T21.4 ... and alt+x (theme; ctrl+t is gone) and ctrl+u (status bar)' (($t -match 'alt\+x\s+switch between the dark and light theme') -and ($t -notmatch 'ctrl\+t') -and ($t -match 'ctrl\+u\s+show / hide the status bar')) ('help text [' + (Show $t) + ']')
    Pst $h $WM_CLOSE 0 0
    [void](Gone $h)
    Ck 'T21.5 the accelerator table maps alt+x to the theme toggle (ctrl+t no longer does) and ctrl+u to the status bar command' (($main -match 'FVIRTKEY\s*\|\s*FALT\s*,\s*''X''\s*,\s*IDM_THEME_TOGGLE\s*\}') -and ($main -notmatch 'FCONTROL\s*,\s*''T''\s*,') -and ($main -match 'FVIRTKEY\s*\|\s*FCONTROL\s*,\s*''U''\s*,\s*IDM_VIEW_STATUS\s*\}')) 'entries missing in src\main.c'
    Ck 'T21.6 the menus show them: view > status bar "ctrl+u", view > theme > toggle "alt+x"' (($menu -match 'IT\(L"&status bar",\s*L"ctrl\+u",\s*IDM_VIEW_STATUS\)') -and ($menu -match 'IT\(L"&toggle",\s*L"alt\+x",\s*IDM_THEME_TOGGLE\)')) 'labels missing in src\menu_defs.c'
    Cmd $app 'IDM_THEME_TOGGLE'                                                  # (the commands the two accelerators run)
    CkEq 'T21.7 the theme toggle switches dark -> light (settings.ini says theme=light)' 'light' (Ini-Val $app 'view' 'theme' 'light')
    Cmd $app 'IDM_THEME_TOGGLE'
    CkEq 'T21.8 ... and light -> dark' 'dark' (Ini-Val $app 'view' 'theme' 'dark')
    $st = Chrome-Kid $app 'mp_status'
    Ck 'T21.9 the status bar is shown at the start' ([U]::Visible($st)) 'the status bar window is not visible'
    Cmd $app 'IDM_VIEW_STATUS'
    Ck 'T21.10 the status bar command hides it' (WaitFor { -not [U]::Visible($st) } 3000) 'still visible 3 s after IDM_VIEW_STATUS'
    Cmd $app 'IDM_VIEW_STATUS'
    Ck 'T21.11 ... and shows it again' (WaitFor { [U]::Visible($st) } 3000) 'still hidden 3 s after the second IDM_VIEW_STATUS'
    Ck 'T21.12 the accelerator table maps alt+z to word wrap (the command T15 / T18 run through format > word wrap)' ($main -match 'FVIRTKEY\s*\|\s*FALT\s*,\s*''Z''\s*,\s*IDM_FMT_WRAP\s*\}') 'no { FVIRTKEY | FALT, ''Z'', IDM_FMT_WRAP } entry in src\main.c'
    Ck 'T21.13 format > word wrap shows "alt+z" as its shortcut' ($menu -match 'IT\(L"&word wrap",\s*L"alt\+z",\s*IDM_FMT_WRAP\)') 'the word wrap item in src\menu_defs.c has no alt+z label'
    Ck 'T21.14 the help topics list alt+z (word wrap on / off)' ($t -match 'alt\+z\s+word wrap on / off') ('help text [' + (Show $t) + ']')
}

# =========================================================================================================== T22
# the status bar text: "line:col[ [N L N B]] | lines | size in bytes | line ending | encoding" (what WM_GETTEXT of the mp_status child answers).
# the size is what a save would write: the encoding's bytes (+ bom), a line break as long as the chosen line ending; the lines are the number of the last line.
# a selection shows the lines it covers and the bytes a save would write for it (same encoding and line ending, no bom): "1:3 [1 L 2 B]"
function Status-Text($app) { [U]::Text((Chrome-Kid $app 'mp_status')) }
function Status-Has($app, [string]$pat) { [bool](WaitFor { (Status-Text $app) -match $pat } 3000) }
function Test-T22 {
    $app = Start-App
    $ed = Get-Edit $app
    Reset-Doc $app 'abc'
    CkEq 'T22.1 "abc": position 1:1, then 1 L, 3 B, crlf, utf8, in that order (the lines sit before the size, both before the line ending)' '1:1 | 1 L | 3 B | crlf | utf8' (Status-Text $app)
    [void](Snd $ed $EM_SETSEL 0 2)
    Ck 'T22.2 two characters selected: "1:3 [1 L 2 B]" after the position' ((Status-Text $app) -match '^1:\d+ \[1 L 2 B\] \| 1 L \| 3 B \| crlf \| utf8$') ('status [' + (Status-Text $app) + ']')
    [void](Snd $ed $EM_SETSEL 1 1)
    Ck 'T22.3 nothing selected any more: the position is back to just line:column' ((Status-Text $app) -match '^1:2 \| 1 L \| 3 B \|') ('status [' + (Status-Text $app) + ']')

    Reset-Doc $app ("ab`r`ncd")                                                  # a b CR LF c d
    [void](Snd $ed $EM_SETSEL 1 5)                                               # "b", the line break, "c"
    Ck 'T22.4 "b", the break and "c": 2 L, 4 B (the break is two bytes in a crlf file)' ((Status-Text $app) -match ' \[2 L 4 B\] \|') ('status [' + (Status-Text $app) + ']')
    [void](Snd $ed $EM_SETSEL 0 -1)
    Ck 'T22.5 select all of "ab", break, "cd": 2 L, 6 B selected, and 2 L, 6 B in all' ((Status-Text $app) -match ' \[2 L 6 B\] \| 2 L \| 6 B \| crlf') ('status [' + (Status-Text $app) + ']')
    Cmd $app 'IDM_EOL_LF'
    Ck 'T22.6 line ending lf: the same text is 5 B (still 2 L), the selection too' (Status-Has $app ' \[2 L 5 B\] \| 2 L \| 5 B \| lf \|') ('status [' + (Status-Text $app) + ']')
    Cmd $app 'IDM_EOL_CR'
    Ck 'T22.7 line ending cr: 5 B' (Status-Has $app ' \[2 L 5 B\] \| 2 L \| 5 B \| cr \|') ('status [' + (Status-Text $app) + ']')
    Cmd $app 'IDM_EOL_CRLF'
    Ck 'T22.8 back to crlf: 6 B' (Status-Has $app ' \[2 L 6 B\] \| 2 L \| 6 B \| crlf \|') ('status [' + (Status-Text $app) + ']')

    Reset-Doc $app ('a' + [char]::ConvertFromUtf32(0x1F600) + 'b')               # a, a surrogate pair (one emoji), b
    Ck 'T22.9 an emoji is 4 bytes in utf-8: 6 B in all' (Status-Has $app '^1:1 \| 1 L \| 6 B \| crlf \| utf8$') ('status [' + (Status-Text $app) + ']')
    [void](Snd $ed $EM_SETSEL 0 -1)
    Ck 'T22.10 select all of "a", the emoji, "b": 1 L, 6 B (the pair is 4 bytes, not 2 x 3)' ((Status-Text $app) -match ' \[1 L 6 B\] \|') ('status [' + (Status-Text $app) + ']')
    Cmd $app 'IDM_ENC_UTF8BOM'
    Ck 'T22.11 utf-8 with bom: the file is 3 bytes more (9 B), the selection is not (the mark belongs to the file): 6 B' (Status-Has $app ' \[1 L 6 B\] \| 1 L \| 9 B \| crlf \| utf8 bom$') ('status [' + (Status-Text $app) + ']')
    Cmd $app 'IDM_ENC_UTF16LE'
    Ck 'T22.12 utf-16 le: the file is the mark (2) + 4 units * 2 = 10 B, the selection 4 units = 8 B' (Status-Has $app ' \[1 L 8 B\] \| 1 L \| 10 B \| crlf \| utf16 le$') ('status [' + (Status-Text $app) + ']')
    Cmd $app 'IDM_ENC_UTF16BE'
    Ck 'T22.13 utf-16 be: 10 B and 8 B' (Status-Has $app ' \[1 L 8 B\] \| 1 L \| 10 B \| crlf \| utf16 be$') ('status [' + (Status-Text $app) + ']')
    Cmd $app 'IDM_ENC_UTF8'
    Ck 'T22.14 back to utf-8: 6 B and 6 B' (Status-Has $app ' \[1 L 6 B\] \| 1 L \| 6 B \| crlf \| utf8$') ('status [' + (Status-Text $app) + ']')

    Reset-Doc $app 'e'                                                           # an edit that keeps the length but not the size ("e" -> "e acute": 1 -> 2 bytes)
    CkEq 'T22.15 "e": 1 B' '1:1 | 1 L | 1 B | crlf | utf8' (Status-Text $app)
    [void](Snd $ed $EM_SETSEL 0 -1)
    [void][U]::SndStr($ed, $EM_REPLACESEL, 1, [string][char]0xE9)
    Ck 'T22.16 replaced by an accented e (same length, 2 bytes in utf-8): the size follows' ((Status-Text $app) -match '\| 1 L \| 2 B \| crlf') ('status [' + (Status-Text $app) + ']')
    Ed-Dirty $app 'xyz'
    Ck 'T22.17 typing 3 more characters: 5 B' ((Status-Text $app) -match '\| 1 L \| 5 B \| crlf') ('status [' + (Status-Text $app) + ']')

    Reset-Doc $app ''                                                            # the lines: the number of the last line
    CkEq 'T22.18 an empty document is 0 B in 1 L' '1:1 | 1 L | 0 B | crlf | utf8' (Status-Text $app)
    Reset-Doc $app ("a`r`nb`r`nc`r`nd`r`ne")
    Ck 'T22.19 five lines of text: 5 L, 13 B (5 letters + 4 breaks of 2 bytes)' (Status-Has $app '\| 5 L \| 13 B \| crlf') ('status [' + (Status-Text $app) + ']')
    Reset-Doc $app ("a`r`n")
    Ck 'T22.20 a text that ends with a line break has an empty last line: 2 L (the caret can sit on it)' (Status-Has $app '\| 2 L \| 3 B \| crlf') ('status [' + (Status-Text $app) + ']')
    Ed-Dirty $app ("x`r`n")                                                      # typing a line break (EN_CHANGE): one more line
    Ck 'T22.21 inserting "x" and a break at the start: 3 L' (Status-Has $app '\| 3 L \| 6 B \| crlf') ('status [' + (Status-Text $app) + ']')

    # the lines of a selection: every line break in it ends one line, the text after the last break is one more, unless the selection ends right at the start of a line
    Reset-Doc $app ("a`r`nb`r`nc`r`n")                                           # a CR LF b CR LF c CR LF = chars 0..8
    [void](Snd $ed $EM_SETSEL 0 6)                                               # "a", break, "b", break: ends at the start of line 3
    Ck 'T22.22 two whole lines (the selection ends at the start of the third): 2 L, not 3; 6 B' (Status-Has $app ' \[2 L 6 B\] \|') ('status [' + (Status-Text $app) + ']')
    [void](Snd $ed $EM_SETSEL 0 7)                                               # ... one more character: "c" is in it
    Ck 'T22.23 one character further, "c" is [3 L 7 B]' (Status-Has $app ' \[3 L 7 B\] \|') ('status [' + (Status-Text $app) + ']')
    [void](Snd $ed $EM_SETSEL 1 3)                                               # nothing but the first line break
    Ck 'T22.24 just a line break: 1 L, 2 B' (Status-Has $app ' \[1 L 2 B\] \|') ('status [' + (Status-Text $app) + ']')
    [void](Snd $ed $EM_SETSEL 1 7)                                               # from the end of line 1 into line 3: break, "b", break, "c"
    Ck 'T22.25 from the end of one line into the third: 3 L, 6 B' (Status-Has $app ' \[3 L 6 B\] \|') ('status [' + (Status-Text $app) + ']')
    Cmd $app 'IDM_EOL_LF'
    [void](Snd $ed $EM_SETSEL 0 7)
    Ck 'T22.26 line ending lf: "a", break, "b", break, "c" is 5 B' (Status-Has $app ' \[3 L 5 B\] \| 4 L \| 6 B \| lf \|') ('status [' + (Status-Text $app) + ']')
    Cmd $app 'IDM_EOL_CRLF'

    $cjk = -join ([char]0x65E5, [char]0x672C, [char]0x8A9E)                      # three cjk characters: 3 bytes each in utf-8
    Reset-Doc $app $cjk
    [void](Snd $ed $EM_SETSEL 0 -1)
    Ck 'T22.27 three cjk characters: 9 B in utf-8' (Status-Has $app ' \[1 L 9 B\] \| 1 L \| 9 B \| crlf \| utf8$') ('status [' + (Status-Text $app) + ']')
    [void](Snd $ed $EM_SETSEL 1 2)
    Ck 'T22.28 one of them: 3 B' (Status-Has $app ' \[1 L 3 B\] \| 1 L \| 9 B \|') ('status [' + (Status-Text $app) + ']')

    Reset-Doc $app 'ab'                                                          # the same range after an edit that keeps its length: the numbers are not kept from before
    [void](Snd $ed $EM_SETSEL 0 1)
    Ck 'T22.29 "a" selected: 1 B' (Status-Has $app ' \[1 L 1 B\] \| 1 L \| 2 B \|') ('status [' + (Status-Text $app) + ']')
    [void](Snd $ed $EM_SETSEL 0 1)
    [void][U]::SndStr($ed, $EM_REPLACESEL, 1, [string][char]0xE9)                # "a" -> accented e, same length, 2 bytes
    [void](Snd $ed $EM_SETSEL 0 1)
    Ck 'T22.30 the same range after replacing "a" by an accented e: 2 B' (Status-Has $app ' \[1 L 2 B\] \| 1 L \| 3 B \|') ('status [' + (Status-Text $app) + ']')
}

# =========================================================================================================== T23
# selected line breaks (edit.c "selected line breaks"). the stock control highlights characters only, so an empty line inside a selection - or the end of a
# selected line - showed nothing. every selected HARD line break now gets a block (a space wide, a row high, the system selection colour) where the row's
# text ends. T23.1-T23.12 read PrintWindow of the main window (the repaint path: WM_PRINT -> WM_PRINTCLIENT). the control paints a CHANGED selection straight
# onto its window, not through WM_PAINT, so T23.13+ read the window itself: that needs the probe build (tools\probe.bat /DSHOTDC -> a message that dumps the
# editor's own DC; a process on another desktop cannot read it) and is skipped otherwise:
#   tools\probe.bat /DSHOTDC   then   tests\ui\ui_test.ps1 -Exe build\probe\notepad-mint.exe -Only T23
function S16([long]$v) { $v = $v -band 0xFFFF; if ($v -ge 32768) { return [int]($v - 65536) } else { return [int]$v } }
function Ed-Pos($app, [int]$i) { $p = [long](Snd (Get-Edit $app) 0xD6 $i 0); return @((S16 $p), (S16 ($p -shr 16))) }     # EM_POSFROMCHAR: client x, y of a character
function Ed-Shot($app) {                                                         # PrintWindow of the main window + where the editor's client area starts in it
    $mw = [U]::WRect([long]$app.Main); $ew = [U]::WRect((Get-Edit $app))
    return @{ Bmp = [U]::Grab([long]$app.Main); Ox = ($ew[0] - $mw[0]); Oy = ($ew[1] - $mw[1]) }
}
function Is-Hl($c) { $h = [System.Drawing.SystemColors]::Highlight; return ([Math]::Abs($c.R - $h.R) + [Math]::Abs($c.G - $h.G) + [Math]::Abs($c.B - $h.B)) -le 12 }
function Hl-At($shot, [int]$x, [int]$y) { return (Is-Hl ($shot.Bmp.GetPixel($shot.Ox + $x, $shot.Oy + $y))) }
function Hl-Run($shot, [int]$x0, [int]$y) { $n = 0; while ((Hl-At $shot ($x0 + $n) $y) -and $n -lt 600) { $n++ }; return $n }   # highlight pixels in a row from x0 to the right
function Ed-Direct($app, [int]$w, [int]$h, [int]$y0 = 0, [int]$msg = (0x8000 + 90)) { # the editor's own pixels, w x h from row y0 (SHOTDC probe messages; about 20 us a pixel), $null when the exe has none
    $f = Join-Path $env:TEMP 'mint_dc.ppm'
    Remove-Item -LiteralPath $f -ErrorAction SilentlyContinue
    [void](Snd (Get-Edit $app) $msg $w ($h -bor ($y0 -shl 16)))
    if (-not (Test-Path -LiteralPath $f)) { return $null }
    $b = [IO.File]::ReadAllBytes($f)
    $nl = 0; $i = 0
    while ($nl -lt 3 -and $i -lt 64) { if ($b[$i] -eq 10) { $nl++ }; $i++ }     # "P6\n<w> <h>\n255\n"
    return @{ B = $b; Off = $i; W = $w; H = $h; Y0 = $y0 }
}
function Px-Hl($d, [int]$x, [int]$y) { $k = $d.Off + ($y * $d.W + $x) * 3; return (Is-Hl ([pscustomobject]@{ R = [int]$d.B[$k]; G = [int]$d.B[$k + 1]; B = [int]$d.B[$k + 2] })) }
function Direct-Run($d, [int]$x0, [int]$y) { $n = 0; while ((Px-Hl $d ($x0 + $n) $y) -and ($x0 + $n) -lt ($d.W - 1)) { $n++ }; return $n }
function Px-HlAny($d, [int]$x0, [int]$x1, [int]$y) { for ($x = $x0; $x -le $x1; $x++) { if (Px-Hl $d $x $y) { return $true } }; return $false }   # a selected glyph's own pixels are not the selection colour (the stem of an "l" can sit on any one probed pixel): look at a whole run
function Test-T23 {
    $app = Start-App
    $ed = Get-Edit $app
    $t = "alpha`r`n`r`nbeta"                                                     # rows: "alpha" / "" / "beta"
    Reset-Doc $app $t
    $st = Line-Starts $t                                                         # 0, 7, 9
    $cell = (Ed-Pos $app 1)[0] - (Ed-Pos $app 0)[0]                              # one character cell (the editor font is monospace)
    $p0 = Ed-Pos $app 5; $p1 = Ed-Pos $app $st[1]; $p2 = Ed-Pos $app ($st[2] + 3)
    $pitch = $p1[1] - $p0[1]
    $half = [int]($pitch / 2)
    $x0 = $p0[0] + 3                                                             # just past the end of "alpha" ...
    $xe = $p2[0] + $cell + 3                                                     # ... and just past the end of "beta"
    Info ('editor font: cell ' + $cell + ' px, row pitch ' + $pitch + ' px')
    [void](Snd $ed $EM_SETSEL 0 -1)
    $s = Ed-Shot $app
    Ck 'T23.1 everything selected: a block follows the text of the first line ("alpha" ends at the line break)' (Hl-At $s $x0 ($p0[1] + $half)) ('no selection colour at ' + $x0 + ',' + ($p0[1] + $half))
    Ck 'T23.2 ... the empty line in the middle is highlighted (before: nothing to see)' (Hl-At $s ($p1[0] + 3) ($p1[1] + $half)) ('no selection colour at ' + ($p1[0] + 3) + ',' + ($p1[1] + $half))
    Ck 'T23.3 ... the last line has no line break after it: nothing past the end of "beta"' (-not (Hl-At $s $xe ($p1[1] + $pitch + $half))) ('selection colour at ' + $xe + ',' + ($p1[1] + $pitch + $half))
    Ck ('T23.4 the block on the empty line is one character cell (' + $cell + ' px) wide') ((Hl-Run $s $p1[0] ($p1[1] + $half)) -eq $cell) ('run of ' + (Hl-Run $s $p1[0] ($p1[1] + $half)) + ' px')
    [void](Snd $ed $EM_SETSEL 0 $st[1])                                          # ends at the START of the empty line: the first break is selected, the empty line's is not
    $s = Ed-Shot $app
    Ck 'T23.5 a selection that ends where the next line starts: the first line shows its block ...' (Hl-At $s $x0 ($p0[1] + $half)) 'no block after "alpha"'
    Ck 'T23.6 ... the line it ends in does not (its own break is not selected)' (-not (Hl-At $s ($p1[0] + 3) ($p1[1] + $half))) 'a block on the empty line'
    [void](Snd $ed $EM_SETSEL 1 3)                                               # inside one line: no break selected
    $s = Ed-Shot $app
    Ck 'T23.7 a selection inside one line has no block' (-not (Hl-At $s $x0 ($p0[1] + $half))) 'a block after "alpha"'
    [void](Snd $ed $EM_SETSEL $st[1] $st[2])                                     # exactly the break of the empty line
    $s = Ed-Shot $app
    Ck 'T23.8 only the empty line''s break selected: its block shows, the line before it shows none' ((Hl-At $s ($p1[0] + 3) ($p1[1] + $half)) -and -not (Hl-At $s $x0 ($p0[1] + $half))) 'blocks wrong'
    Ck 'T23.9 ... and the block is exactly one row high: the colour covers the first and the last pixel row of the line, not the row above or below' ((Hl-At $s ($p1[0] + 3) $p1[1]) -and (Hl-At $s ($p1[0] + 3) ($p1[1] + $pitch - 1)) -and -not (Hl-At $s ($p1[0] + 3) ($p1[1] - 1)) -and -not (Hl-At $s ($p1[0] + 3) ($p1[1] + $pitch))) ('rows ' + ($p1[1] - 1) + ',' + $p1[1] + ',' + ($p1[1] + $pitch - 1) + ',' + ($p1[1] + $pitch) + ': ' + (Hl-At $s ($p1[0] + 3) ($p1[1] - 1)) + ' ' + (Hl-At $s ($p1[0] + 3) $p1[1]) + ' ' + (Hl-At $s ($p1[0] + 3) ($p1[1] + $pitch - 1)) + ' ' + (Hl-At $s ($p1[0] + 3) ($p1[1] + $pitch)))
    [void](Snd $ed $EM_SETSEL 2 2)
    $s = Ed-Shot $app
    Ck 'T23.10 no selection: no block anywhere' ((-not (Hl-At $s $x0 ($p0[1] + $half))) -and (-not (Hl-At $s ($p1[0] + 3) ($p1[1] + $half)))) 'a block with an empty selection'

    [void](Snd $ed $EM_SETSEL 0 -1)                                              # zoom: the block follows the font
    for ($i = 0; $i -lt 3; $i++) { Cmd $app 'IDM_ZOOM_IN'; Start-Sleep -Milliseconds 150 }
    [void](WaitFor { ((Ed-Pos $app 1)[0] - (Ed-Pos $app 0)[0]) -gt $cell } 3000)
    $cell2 = (Ed-Pos $app 1)[0] - (Ed-Pos $app 0)[0]; $q1 = Ed-Pos $app $st[1]
    $s = Ed-Shot $app
    Ck ('T23.11 zoomed in (cell ' + $cell + ' -> ' + $cell2 + ' px): the block on the empty line is one new cell wide') (($cell2 -gt $cell) -and ((Hl-Run $s $q1[0] ($q1[1] + 8)) -eq $cell2)) ('cell ' + $cell2 + ', run ' + (Hl-Run $s $q1[0] ($q1[1] + 8)))
    Cmd $app 'IDM_ZOOM_RESET'

    Cmd $app 'IDM_FMT_WRAP'                                                      # word wrap on: a soft wrap is not a line break
    Start-Sleep -Milliseconds 500
    $ed = Get-Edit $app
    $para = ('word ' * 60).TrimEnd()                                             # 299 characters: several rows at any sane window width
    Reset-Doc $app ($para + "`r`nz")
    [void](Snd $ed $EM_SETSEL 0 -1)
    $rows = [int](Snd $ed 0xBA 0 0)                                              # EM_GETLINECOUNT
    $len0 = [int](Snd $ed 0xC1 0 0)                                              # EM_LINELENGTH: the first row
    $a = Ed-Pos $app ($len0 - 1)
    $cell = (Ed-Pos $app 1)[0] - (Ed-Pos $app 0)[0]
    $s = Ed-Shot $app
    Ck ('T23.12 word wrap on, a long line over ' + ($rows - 1) + ' rows: no block where a row only wraps (the highlight stops at the text, one extra pixel at most)') (($rows -ge 3) -and ((Hl-Run $s 8 ($a[1] + 8)) -le ($len0 * $cell + 2))) ('rows ' + $rows + ', first row ' + $len0 + ' chars, highlight run ' + (Hl-Run $s 8 ($a[1] + 8)) + ' px, text ' + ($len0 * $cell) + ' px')
    $brk = Ed-Pos $app $para.Length                                              # the paragraph's real line break
    Ck 'T23.13 ... but the real line break at the end of the paragraph has its block' (Hl-At $s ($brk[0] + 3) ($brk[1] + $half)) ('no selection colour at ' + ($brk[0] + 3) + ',' + ($brk[1] + $half))
    Cmd $app 'IDM_FMT_WRAP'                                                      # (back to the default for the direct paint checks)
    Start-Sleep -Milliseconds 500

    # ---- what the control left on the window itself
    $ed = Get-Edit $app
    Reset-Doc $app $t
    $d = Ed-Direct $app 60 70
    if (-not $d) { Skip 'T23.14-T23.23 the control''s direct painting' 'this exe has no WM_APP + 90 (build the probe: tools\probe.bat /DSHOTDC, run with -Exe build\probe\notepad-mint.exe)'; return }
    $cell = (Ed-Pos $app 1)[0] - (Ed-Pos $app 0)[0]
    $p0 = Ed-Pos $app 5; $p1 = Ed-Pos $app $st[1]
    $x0 = $p0[0] + 3; $y0 = $p0[1] + $half; $y1 = $p1[1] + $half; $y2 = $y1 + $pitch
    $xe = (Ed-Pos $app ($st[2] + 3))[0] + $cell + 3
    [void](Snd $ed $EM_SETSEL 0 -1)
    $d = Ed-Direct $app 100 70
    Ck 'T23.14 on the window itself (EM_SETSEL: the control paints it straight onto the window): blocks after "alpha" and on the empty line, none past "beta"' ((Px-Hl $d $x0 $y0) -and (Px-Hl $d ($p1[0] + 3) $y1) -and -not (Px-Hl $d $xe $y2)) ('alpha ' + (Px-Hl $d $x0 $y0) + ', empty ' + (Px-Hl $d ($p1[0] + 3) $y1) + ', beta ' + (Px-Hl $d $xe $y2))
    Ck ('T23.15 ... the block on the empty line is ' + $cell + ' px wide') ((Direct-Run $d $p1[0] $y1) -eq $cell) ('run ' + (Direct-Run $d $p1[0] $y1))
    [void](Snd $ed $EM_SETSEL 2 2)
    $d = Ed-Direct $app 100 70
    Ck 'T23.16 the selection collapses: the blocks are gone (not left behind on the window)' ((-not (Px-Hl $d $x0 $y0)) -and (-not (Px-Hl $d ($p1[0] + 3) $y1))) 'a block is still on the window'
    [void](Snd $ed $EM_SETSEL 0 -1)
    [void](Snd $ed $EM_SETSEL 3 ($st[1] + 0))                                    # shrinks: the empty line's break is no longer selected
    $d = Ed-Direct $app 100 70
    Ck 'T23.17 the selection shrinks: the block on the line it no longer reaches is gone, the one on "alpha" stays' ((Px-Hl $d $x0 $y0) -and (-not (Px-Hl $d ($p1[0] + 3) $y1))) ('alpha ' + (Px-Hl $d $x0 $y0) + ', empty ' + (Px-Hl $d ($p1[0] + 3) $y1))
    [void](Snd $ed $EM_SETSEL 0 $st[2])                                          # grows again
    $d = Ed-Direct $app 100 70
    Ck 'T23.18 ... and comes back when it grows again' ((Px-Hl $d $x0 $y0) -and (Px-Hl $d ($p1[0] + 3) $y1)) 'blocks missing'
    [void](Snd $ed $EM_SETSEL 0 $st[1])                                          # ends at the start of the empty line: the break after "alpha" is the last thing selected
    $d = Ed-Direct $app 100 70
    $had = Px-Hl $d $x0 $y0
    [void](Snd $ed $EM_SETSEL 0 5)                                               # now it stops right after "alpha": only the line break leaves the selection (a change without any width for the control)
    $d = Ed-Direct $app 100 70
    Ck 'T23.19 when only the line break leaves the selection, its block goes with it (the control repaints nothing visible for that change: our own cleanup)' ($had -and (-not (Px-Hl $d $x0 $y0))) ('block before ' + $had + ', after ' + (Px-Hl $d $x0 $y0))
    [void](Snd $ed $EM_SETSEL 0 0)                                               # a mouse drag: down on "alpha", out to the last line, back
    [void](Snd $ed 0x201 1 (([long]($p0[1] + 4) -shl 16) -bor [long]($p0[0] - 20)))
    [void](Snd $ed 0x200 1 (([long]($p2[1] + 4) -shl 16) -bor [long]($p2[0] + 4)))
    $d = Ed-Direct $app 100 70
    Ck 'T23.20 dragging the selection down over the lines: blocks on the first two lines' ((Px-Hl $d $x0 $y0) -and (Px-Hl $d ($p1[0] + 3) $y1)) 'blocks missing while dragging'
    [void](Snd $ed 0x200 1 (([long]($p0[1] + 4) -shl 16) -bor [long]($p0[0] - 10)))
    $d = Ed-Direct $app 100 70
    Ck 'T23.21 ... and dragging back up takes them away again' ((-not (Px-Hl $d $x0 $y0)) -and (-not (Px-Hl $d ($p1[0] + 3) $y1))) 'a block is left behind after dragging back'
    [void](Snd $ed 0x202 0 (([long]($p0[1] + 4) -shl 16) -bor [long]($p0[0] - 10)))

    $sb = New-Object Text.StringBuilder                                           # scrolling: every third line empty, 90 lines
    for ($i = 1; $i -le 90; $i++) { if ($i % 3 -eq 0) { [void]$sb.Append("`r`n") } else { [void]$sb.Append('line ' + $i + "`r`n") } }
    Reset-Doc $app $sb.ToString()
    [void](Snd $ed $EM_SETSEL 0 -1)
    [void](Snd $ed 0xB6 0 7)                                                     # EM_LINESCROLL: seven rows down
    $first = [int](Snd $ed 0xCE 0 0)                                             # EM_GETFIRSTVISIBLELINE
    $d = Ed-Direct $app 100 400
    $ok = $true; $why = ''
    for ($r = $first; $r -lt $first + 12; $r++) {                                # an empty line is row r when (r + 1) % 3 == 0: its block must be there
        $ci = [int](Snd $ed 0xBB $r 0)                                           # EM_LINEINDEX
        $pp = Ed-Pos $app $ci
        if ((($r + 1) % 3) -eq 0) { if (-not (Px-Hl $d ($pp[0] + 3) ($pp[1] + $half))) { $ok = $false; $why += ' row ' + $r + ' empty line has no block;' } }
        elseif (-not (Px-HlAny $d $pp[0] ($pp[0] + $cell - 1) ($pp[1] + $half))) { $ok = $false; $why += ' row ' + $r + ' text has no highlight;' }       # (any pixel of the first cell: the first letter's ink may sit on a single probed one)
    }
    Ck ('T23.22 scrolled ' + $first + ' rows down with everything selected: every visible empty line has its block') $ok $why
    [void](Snd $ed $EM_SETSEL 40 40)
    $d = Ed-Direct $app 100 400
    $any = $false
    for ($r = $first; $r -lt $first + 12; $r++) { $pp = Ed-Pos $app ([int](Snd $ed 0xBB $r 0)); if (Px-Hl $d ($pp[0] + 3) ($pp[1] + $half)) { $any = $true } }
    Ck 'T23.23 the selection collapses: the blocks of the scrolled view are all gone' (-not $any) 'a block is left on the window'
}

# =========================================================================================================== T24
# files and folders dropped on the window go into the text as paths, at the caret (replacing a selection), one per line, plain. the drop is a WM_DROPFILES
# with an HDROP built here (the system hands that handle to the app's process), what the shell posts after a real drop. shift held while dropping keeps the old
# behaviour (open the files): that reads the real key state, which messages cannot set, so it is not tested here
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class Dp {
    [DllImport("kernel32.dll")] static extern IntPtr GlobalAlloc(uint f, UIntPtr n);
    [DllImport("kernel32.dll")] static extern IntPtr GlobalLock(IntPtr h);
    [DllImport("kernel32.dll")] static extern bool GlobalUnlock(IntPtr h);
    public static long Make(string[] paths) {                                    // an HDROP (DROPFILES + a double nul terminated list of wide strings)
        StringBuilder sb = new StringBuilder();
        foreach (string q in paths) { sb.Append(q); sb.Append('\0'); }
        sb.Append('\0');
        byte[] data = Encoding.Unicode.GetBytes(sb.ToString());
        IntPtr h = GlobalAlloc(0x2042, new UIntPtr((uint)(20 + data.Length)));    // GHND
        IntPtr p = GlobalLock(h);
        Marshal.WriteInt32(p, 0, 20);                                             // pFiles
        Marshal.WriteInt32(p, 16, 1);                                             // fWide
        Marshal.Copy(data, 0, new IntPtr(p.ToInt64() + 20), data.Length);
        GlobalUnlock(h);
        return h.ToInt64();
    }
}
'@
function Drop-Paths($app, [string[]]$paths) { [void][U]::Post([long]$app.Main, 0x233, [long][Dp]::Make($paths), 0) }     # WM_DROPFILES
function Test-T24 {
    $app = Start-App
    $ed = Get-Edit $app
    $title0 = Title $app
    $exe = 'C:\Windows\notepad.exe'
    Reset-Doc $app 'ab'
    [void](Snd $ed $EM_SETSEL 1 1)
    Drop-Paths $app @($exe)
    CkEdText 'T24.1 a file dropped on the window: its full path goes into the text at the caret (between "a" and "b")' $app ('a' + $exe + 'b')
    $e = 1 + $exe.Length
    CkSel 'T24.2 the caret ends up right after the inserted path' $app $e $e
    Ck 'T24.3 nothing was opened: the window title is as it was, plus the change mark of the edit' (WaitFor { (Title $app) -match '^\*' } 3000) ('title [' + (Title $app) + '] (was [' + $title0 + '])')
    [void](Snd $ed $EM_UNDO 0 0)
    CkEdText 'T24.4 one undo takes the whole drop back' $app 'ab'

    Reset-Doc $app 'axyzb'
    [void](Snd $ed $EM_SETSEL 1 4)
    Drop-Paths $app @('C:\Windows')
    CkEdText 'T24.5 a folder works too, the selection is replaced, no backslash added: a, C:\Windows, b' $app 'aC:\Windowsb'

    Reset-Doc $app ''
    Drop-Paths $app @('C:\one\a.txt', 'C:\two words\b.txt', 'D:\three')
    CkEdText 'T24.6 several items: one path per line, plain (no quotes, spaces kept), no line break after the last' $app ("C:\one\a.txt`r`nC:\two words\b.txt`r`nD:\three")

    Reset-Doc $app 'x'
    $jp = Chars @(0x30C6, 0x30B9, 0x30C8); $jf = Chars @(0x30D5, 0x30A1, 0x30A4, 0x30EB)
    Reset-Doc $app ''
    Drop-Paths $app @(('C:\' + $jp + '\' + $jf + '.txt'))
    CkEdText 'T24.7 a path with non-ascii characters (katakana) comes through intact' $app ('C:\' + $jp + '\' + $jf + '.txt')

    $long = 'C:\' + ('a' * 1500) + '\file.txt'
    Reset-Doc $app ''
    Drop-Paths $app @($long)
    CkEdText ('T24.8 a path of ' + $long.Length + ' characters (longer than the old 1024 limit) is inserted whole') $app $long

    Reset-Doc $app 'keep'
    Drop-Paths $app @()
    Start-Sleep -Milliseconds 300
    CkEdText 'T24.9 an empty drop changes nothing' $app 'keep'

    $sb = New-Object Text.StringBuilder                                           # the inserted text is scrolled into view
    for ($i = 1; $i -le 150; $i++) { [void]$sb.Append('line ' + $i + "`r`n") }
    Reset-Doc $app $sb.ToString()
    [void](Snd $ed $EM_SETSEL 100000 100000)                                      # (the end; a message does not scroll)
    $f0 = [int](Snd $ed 0xCE 0 0)
    Drop-Paths $app @('C:\a', 'C:\b')
    Ck 'T24.10 dropped at the end of a long document: the caret is scrolled into view (the view moved down)' (WaitFor { [int](Snd $ed 0xCE 0 0) -gt $f0 } 3000) ('first visible row stayed ' + $f0)
    $t = Ed-Text $app
    Ck 'T24.11 ... and the text ends with the two paths' ($t.EndsWith("C:\a`r`nC:\b")) ('tail [' + (Show $t.Substring([Math]::Max(0, $t.Length - 30))) + ']')
}

# =========================================================================================================== T25
# partly visible rows (edit.c "partly visible rows"). the stock control draws whole rows only and rounds its formatting rectangle down to whole rows; below the
# last whole row is a band (the bottom padding + what is left of the height) in which the app draws the next row(s), cut off at the editor's bottom edge.
# the check is a comparison with the control itself: scroll one row, and the row that was cut off in the band is a whole row, drawn by the control: the band's
# pixels must be those of the top of that row, exactly (PrintWindow of the main window both times: the repaint path, WM_PRINT -> WM_PRINTCLIENT).
# T25.40+ read the window itself after the control scrolled / changed / was resized (the control paints straight onto its window): that needs the SHOTDC probe build
# (tools\probe.bat /DSHOTDC, run with -Exe build\probe\notepad-mint.exe) and is skipped otherwise.
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class Bx {
    // w x h pixels of a (from ax, ay) against those of b (from bx, by): { different pixels, first x, first y (in the region), biggest channel difference }
    public static int[] Diff(Bitmap a, int ax, int ay, Bitmap b, int bx, int by, int w, int h) {
        BitmapData da = a.LockBits(new Rectangle(0, 0, a.Width, a.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        BitmapData db = b.LockBits(new Rectangle(0, 0, b.Width, b.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        int n = 0, fx = -1, fy = -1, mx = 0;
        try {
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    int pa = Marshal.ReadInt32(da.Scan0, (ay + y) * da.Stride + (ax + x) * 4) & 0xFFFFFF;
                    int pb = Marshal.ReadInt32(db.Scan0, (by + y) * db.Stride + (bx + x) * 4) & 0xFFFFFF;
                    if (pa == pb) continue;
                    n++;
                    if (fx < 0) { fx = x; fy = y; }
                    int d = Math.Max(Math.Abs(((pa >> 16) & 255) - ((pb >> 16) & 255)), Math.Max(Math.Abs(((pa >> 8) & 255) - ((pb >> 8) & 255)), Math.Abs((pa & 255) - (pb & 255))));
                    if (d > mx) mx = d;
                }
        } finally { a.UnlockBits(da); b.UnlockBits(db); }
        return new int[] { n, fx, fy, mx };
    }
    // pixels of the region of a that are not exactly colour c (0xRRGGBB): { count, first x, first y }
    public static int[] NotColor(Bitmap a, int ax, int ay, int w, int h, int c) {
        BitmapData da = a.LockBits(new Rectangle(0, 0, a.Width, a.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        int n = 0, fx = -1, fy = -1;
        try {
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    int pa = Marshal.ReadInt32(da.Scan0, (ay + y) * da.Stride + (ax + x) * 4) & 0xFFFFFF;
                    if (pa == c) continue;
                    n++;
                    if (fx < 0) { fx = x; fy = y; }
                }
        } finally { a.UnlockBits(da); }
        return new int[] { n, fx, fy };
    }
    // the same for a binary ppm (what the SHOTDC probe writes: pw pixels wide, the pixels start at byte off, its first row is row py0 of the editor) against a bitmap:
    // the region (x0, y0, w, h) of the editor, which is at (bx + x0, by + y0) of the bitmap
    public static int[] DiffPpm(byte[] p, int off, int pw, int py0, Bitmap b, int bx, int by, int x0, int y0, int w, int h) {
        BitmapData db = b.LockBits(new Rectangle(0, 0, b.Width, b.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        int n = 0, fx = -1, fy = -1, mx = 0;
        try {
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    int k = off + ((y0 + y - py0) * pw + x0 + x) * 3;
                    int pa = (p[k] << 16) | (p[k + 1] << 8) | p[k + 2];
                    int pb = Marshal.ReadInt32(db.Scan0, (by + y0 + y) * db.Stride + (bx + x0 + x) * 4) & 0xFFFFFF;
                    if (pa == pb) continue;
                    n++;
                    if (fx < 0) { fx = x; fy = y; }
                    int d = Math.Max(Math.Abs(((pa >> 16) & 255) - ((pb >> 16) & 255)), Math.Max(Math.Abs(((pa >> 8) & 255) - ((pb >> 8) & 255)), Math.Abs((pa & 255) - (pb & 255))));
                    if (d > mx) mx = d;
                }
        } finally { b.UnlockBits(db); }
        return new int[] { n, fx, fy, mx };
    }
}
'@
function Band-Geo($app) {                                                        # where the whole rows end and the band begins (editor client coordinates), from what the control says about its rows
    $ed = Get-Edit $app
    $cr = [U]::CRect($ed)
    $wrE = [U]::WRect($ed); $tr = Trim-Px $app
    if (($wrE[2] - $wrE[0]) -eq $cr[2]) { $cr[2] -= $tr }                       # no vertical bar: the window's overhang is client area, out of sight (SBAR_TRIM)
    if (($wrE[3] - $wrE[1]) -eq $cr[3]) { $cr[3] -= $tr }                       # ... and no horizontal bar
    $first = [int](Snd $ed 0xCE 0 0)                                            # EM_GETFIRSTVISIBLELINE
    $p0 = Ed-Pos $app ([int](Snd $ed 0xBB $first 0))                             # EM_LINEINDEX, EM_POSFROMCHAR
    $p1 = Ed-Pos $app ([int](Snd $ed 0xBB ($first + 1) 0))
    $pitch = $p1[1] - $p0[1]
    $pad = $p0[1]                                                                # the top padding: where the first row starts (EDIT_PAD_TOP)
    $bot = [int][Math]::Round($IDM.EDIT_PAD * ([U]::Dpi([long]$app.Main)) / 96, [MidpointRounding]::AwayFromZero)   # the bottom padding (EDIT_PAD): not the same as the top one
    $rows = [int][Math]::Floor(($cr[3] - $pad - $bot) / $pitch)                  # the control keeps whole rows only
    $top = $pad + $rows * $pitch
    return @{ Ed = $ed; W = $cr[2]; H = $cr[3]; First = $first; Pad = $pad; Bot = $bot; Pitch = $pitch; Rows = $rows; Top = $top; BandH = ($cr[3] - $top) }
}
function Band-Cmp($app, [int]$j = 0, [bool]$noCaret = $false, [int]$tol = 0, [int]$tries = 1) {   # band row j against the same row drawn whole by the control (after scrolling j + 1 rows); $tol: the biggest difference of a colour channel that still counts as the same
    $g = Band-Geo $app                                                           # $noCaret: the control is told it lost the focus (no caret: after the scroll the caret may be in a whole row, where it shows, in the band it never does; the selection stays visible)
    $ya = $g.Top + $j * $g.Pitch                                                 # $tries: the control's OWN row is the flaky side with shaped / fallback-font text (the control draws a row without most of its glyphs now and then, also in the build without the band, measured):
    $h = [Math]::Min($g.Pitch, $g.H - $ya)                                       # it is captured again, up to $tries times, until it is the same as the band's (an equal pair can not be a false pass)
    if ($h -lt 1) { return $null }
    if ($noCaret) { [void](Snd $g.Ed 0x8 0 0) }                                  # WM_KILLFOCUS
    $a = Ed-Shot $app
    $bgc = $a.Bmp.GetPixel($a.Ox + $g.W - 2, $a.Oy + $ya + 1)                   # the right padding: the background
    $bg = ([int]$bgc.R -shl 16) -bor ([int]$bgc.G -shl 8) -bor [int]$bgc.B
    $ink = [Bx]::NotColor($a.Bmp, $a.Ox, $a.Oy + $ya, $g.W, $h, $bg)
    $d = $null
    for ($k = 0; $k -lt $tries; $k++) {
        [void](Snd $g.Ed 0xB6 0 ($j + 1))                                        # EM_LINESCROLL
        $b = Ed-Shot $app
        [void](Snd $g.Ed 0xB6 0 (-($j + 1)))
        $d = [Bx]::Diff($a.Bmp, $a.Ox, $a.Oy + $ya, $b.Bmp, $b.Ox, $b.Oy + $g.Top - $g.Pitch, $g.W, $h)
        $b.Bmp.Dispose()
        if ($d[0] -eq 0 -or $d[3] -le $tol) { break }
    }
    if ($noCaret) { [void](Snd $g.Ed 0x7 0 0) }                                  # WM_SETFOCUS
    $why = ('band row ' + $j + ' (' + $h + ' px of ' + $g.Pitch + ', ' + $g.Rows + ' whole rows, pad top ' + $g.Pad + ' bottom ' + $g.Bot + ') differs from the control''s own row in ' + $d[0] + ' px, first at x ' + $d[1] + ' y ' + $d[2] + ', biggest channel difference ' + $d[3] + '; ink px ' + $ink[0] + ' (' + ($k + 1) + ' tries)')
    $a.Bmp.Dispose()
    return @{ Same = (($d[0] -eq 0) -or ($d[3] -le $tol)); Ink = $ink[0]; Why = $why; G = $g }
}
function Band-Ck([string]$n, $app, [int]$j = 0, [bool]$blank = $false, [bool]$noCaret = $false, [int]$tol = 0, [int]$tries = 1) {   # a band row equals the whole row; $blank = the row is expected to have no ink at all
    $r = Band-Cmp $app $j $noCaret $tol $tries
    if (-not $r) { Fail $n 'there is no such band row (the band is too low)'; return }
    $ok = $r.Same -and ($blank -or $r.Ink -gt 0)
    Ck $n $ok $r.Why
}
function Band-Px($shot, [int]$x, [int]$y) {                                      # a pixel of a PrintWindow shot as 0xRRGGBB
    $c = $shot.Bmp.GetPixel($x, $y)
    return (([int]$c.R -shl 16) -bor ([int]$c.G -shl 8) -bor [int]$c.B)
}
function Band-Bg($app, [string]$n, [bool]$rtl = $false) {                        # the whole band is the plain background
    $g = Band-Geo $app
    $a = Ed-Shot $app
    $ox = $a.Ox
    if ($rtl) { $ew = [U]::WRect($g.Ed); $ox += ($ew[2] - $ew[0]) - $g.W }       # (the scroll bar is on the left: the client area starts after it)
    $bg = Band-Px $a ($ox + $g.W - 2) ($a.Oy + $g.Top + 1)
    $x = [Bx]::NotColor($a.Bmp, $ox, $a.Oy + $g.Top, $g.W, $g.BandH, $bg)
    $a.Bmp.Dispose()
    Ck $n ($x[0] -eq 0) ('the band (' + $g.BandH + ' px high) has ' + $x[0] + ' px that are not the background, first at x ' + $x[1] + ' y ' + $x[2])
}
function Band-Doc([int]$n, [string]$fill = '') {                                 # n numbered lines (distinct rows), each with the same filler
    $sb = New-Object Text.StringBuilder
    for ($i = 1; $i -le $n; $i++) { [void]$sb.Append('row ' + $i + ' the quick brown fox jumps over the lazy dog ' + ('x' * ($i % 7)) + $fill + "`r`n") }
    return $sb.ToString()
}
function Band-Size($app, [int]$bandH) {                                          # resize the window until the band is $bandH px high (the editor's height follows the window's one for one; a band is the bottom padding plus less than a row)
    $g = Band-Geo $app
    $w = [U]::WRect([long]$app.Main)
    $delta = $g.Pad + $g.Pitch * $g.Rows + $bandH - $g.H
    if ($delta -ne 0) {
        [void][Nd]::Size([long]$app.Main, ($w[2] - $w[0]), ($w[3] - $w[1] + $delta))
        [void](WaitFor { (Band-Geo $app).BandH -eq $bandH } 3000)
    }
    return (Band-Geo $app)
}
function Test-T25 {
    $app = Start-App
    $ed = Get-Edit $app
    $t = Band-Doc 120
    $st = Line-Starts $t
    Reset-Doc $app $t
    $g = Band-Geo $app
    $i = $g.First + $g.Rows                                                      # the line (= row) in the band
    Info ('editor client ' + $g.W + ' x ' + $g.H + ', row pitch ' + $g.Pitch + ' px, ' + $g.Rows + ' whole rows, band ' + $g.BandH + ' px high (padding top ' + $g.Pad + ', bottom ' + $g.Bot + ')')
    Ck 'T25.1 a document longer than the view leaves a band below the whole rows (at least the bottom padding)' ($g.BandH -ge $g.Bot -and $g.BandH -gt 0) ('band ' + $g.BandH + ' px')
    Band-Ck 'T25.2 plain text: the band shows the top of the next row, pixel for pixel what the control draws when that row is whole' $app 0
    [void](Snd $ed 0xB6 0 5)                                                     # scrolled: another row in the band
    Band-Ck 'T25.3 ... after scrolling five rows too (the text under the band moves with it)' $app 0
    [void](Snd $ed 0xB6 0 -5)

    Reset-Doc $app (Band-Doc 120 ("`t" + 'tab' + "`t" + 'stops' + "`t" + '!'))
    Band-Ck 'T25.4 tabs: the default tab stops, the same as the control''s' $app 0 $false $false 0 4      # (4 tries: the control's own row is the flaky side, see Band-Cmp)
    foreach ($ts in 8, 2) {                                                      # format > tab size: the band's own rows follow the control's tab stops
        Cmd $app ('IDM_TAB_' + $ts); Start-Sleep -Milliseconds 500
        Band-Ck ('T25.4' + $(if ($ts -eq 8) { 'b' } else { 'c' }) + ' tab size ' + $ts + ': the same as the control''s') $app 0 $false $false 0 4
    }
    Cmd $app 'IDM_TAB_4'; Start-Sleep -Milliseconds 500

    $jp = Chars @(0x65E5, 0x672C, 0x8A9E, 0x306E, 0x30C6, 0x30B9, 0x30C8, 0x3067, 0x3059, 0xFF21, 0xFF22)       # kanji, hiragana, katakana, full width latin
    Reset-Doc $app (Band-Doc 120 ($jp + ' ' + $jp))
    Band-Ck 'T25.5 japanese text (wide characters, another font for them): the same as the control''s' $app 0 $false $false 0 4

    Reset-Doc $app $t
    [void](Snd $ed $EM_SETSEL ($st[$i] + 4) ($st[$i] + 12))
    Band-Ck 'T25.6 a selection inside the band row: the highlight is the control''s own (colours, width)' $app 0 $false $true
    [void](Snd $ed $EM_SETSEL ($st[$i - 2] + 6) ($st[$i + 1] + 5))
    Band-Ck 'T25.7 a selection that runs through the band row (its line break is selected: a block after its text)' $app 0
    [void](Snd $ed $EM_SETSEL 0 -1)
    Band-Ck 'T25.8 everything selected' $app 0
    [void](Snd $ed $EM_SETSEL 0 0)

    $lines = $t -split "`r`n"
    $lines[$i] = ''                                                              # an empty line in the band
    $tEmpty = $lines -join "`r`n"
    $st2 = Line-Starts $tEmpty
    Reset-Doc $app $tEmpty
    [void](Snd $ed $EM_SETSEL $st2[$i - 1] ($st2[$i + 1] + 3))
    Band-Ck 'T25.9 an empty line in the band, selected: its line break is a block, the same as the control draws' $app 0 $true

    Reset-Doc $app (Band-Doc 120 (' ' + ('0123456789' * 30)))                    # long lines, scrolled sideways
    [void](Snd $ed 0xB6 40 0)                                                    # EM_LINESCROLL: 40 columns to the right
    Band-Ck 'T25.10 scrolled sideways: the band row is cut off at the same place and starts at the same column' $app 0
    [void](Snd $ed $EM_SETSEL 3000 3400)
    Band-Ck 'T25.11 ... with a selection' $app 0
    [void](Snd $ed 0xB6 -100000 0)

    Reset-Doc $app (Band-Doc 6)                                                  # nothing below the last whole row
    Band-Bg $app 'T25.12 a document shorter than the view: the band is plain background'

    $g = Band-Size $app ($g.Pitch + 6)                                           # a taller band: one whole row and 6 px of the next
    Info ('window resized: client ' + $g.W + ' x ' + $g.H + ', band ' + $g.BandH + ' px high')
    Reset-Doc $app $t
    Band-Ck 'T25.13 a band over a row high: its first row' $app 0
    Band-Ck 'T25.14 ... and the 6 px of the second row' $app 1
    [void](Snd $ed $EM_SETSEL ($st[$i - 2] + 6) ($st[$i + 2] + 5))
    Band-Ck 'T25.15 a selection through both band rows: the first (with its block) ...' $app 0
    Band-Ck 'T25.16 ... and the second' $app 1
    [void](Snd $ed $EM_SETSEL 0 0)

    $g = Band-Size $app ($g.Pitch - 2)
    $tEnd = $t.TrimEnd([char]13, [char]10)                                       # the last line has no line break after it
    Reset-Doc $app $tEnd
    $total = [int](Snd $ed 0xBA 0 0)                                             # EM_GETLINECOUNT
    [void](Snd $ed 0xB6 0 ($total - $g.Rows - 1))                                # scrolled so that the last line is the band's first row
    Info ('end of the document: first visible row ' + [int](Snd $ed 0xCE 0 0) + ' of ' + $total + ', ' + $g.Rows + ' whole rows')
    Band-Ck 'T25.17 the last line of the document in the band' $app 0
    [void](Snd $ed $EM_SETSEL 0 -1)
    Band-Ck 'T25.18 ... all selected: no block after the last line (it has no line break)' $app 0 $false $true
    [void](Snd $ed 0xB6 0 -100000)

    Cmd $app 'IDM_THEME_LIGHT'
    Start-Sleep -Milliseconds 400
    Reset-Doc $app $t
    [void](Snd $ed $EM_SETSEL ($st[$i] + 2) ($st[$i + 1] + 3))
    Band-Ck 'T25.19 the light theme (black on white, the same highlight)' $app 0 $false $false 40
    Cmd $app 'IDM_THEME_DARK'
    Start-Sleep -Milliseconds 400

    for ($z = 0; $z -lt 4; $z++) { Cmd $app 'IDM_ZOOM_IN'; Start-Sleep -Milliseconds 150 }
    Start-Sleep -Milliseconds 300
    Reset-Doc $app $t
    $g = Band-Geo $app
    Info ('zoomed in: row pitch ' + $g.Pitch + ' px, ' + $g.Rows + ' whole rows, band ' + $g.BandH + ' px')
    Band-Ck 'T25.20 zoomed in (a bigger font: the band row follows it)' $app 0
    Cmd $app 'IDM_ZOOM_RESET'
    Start-Sleep -Milliseconds 300

    Cmd $app 'IDM_FMT_WRAP'                                                      # word wrap on: the control is re-created
    Start-Sleep -Milliseconds 500
    $ed = Get-Edit $app
    $para = ('wrapping words go on and on ' * 14).TrimEnd()
    $sb = New-Object Text.StringBuilder
    for ($k = 1; $k -le 60; $k++) { [void]$sb.Append($k.ToString() + ' ' + $para + "`r`n") }
    # at the 10 pt default the wrapped row's edge antialiases one pixel differently from the control (biggest channel 58): tol 60, as T25.19
    Reset-Doc $app $sb.ToString()
    Band-Ck 'T25.21 word wrap on (rows of wrapped lines): the band shows the next row of the paragraph' $app 0 $false $false 60
    [void](Snd $ed $EM_SETSEL 20 700)
    Band-Ck 'T25.22 ... with a selection over several rows (a soft wrap has no line break block)' $app 0 $false $false 60
    Cmd $app 'IDM_FMT_WRAP'
    Start-Sleep -Milliseconds 500
    $ed = Get-Edit $app

    Reset-Doc $app $t
    Cmd $app 'IDM_RTL'                                                           # a right to left editor keeps its blank band
    Start-Sleep -Milliseconds 400
    Band-Bg $app 'T25.23 right to left: the band stays plain background (its rows are not drawn there)' $true
    Cmd $app 'IDM_RTL'
    Start-Sleep -Milliseconds 400
    Band-Ck 'T25.24 ... and left to right again: the band is back' $app 0

    $ar = Chars @(0x0627, 0x0644, 0x0639, 0x0631, 0x0628, 0x064A, 0x0629)         # arabic: shaped and reordered by the language pack, the same call draws it in the band
    $td = Band-Doc 120 (' ' + $ar + ' ' + $ar); $sd = Line-Starts $td
    Reset-Doc $app $td
    Band-Ck 'T25.25 a row with arabic in it, nothing selected: the same as the control''s' $app 0 $false $false 0 8
    $he = Chars @(0x05E9, 0x05DC, 0x05D5, 0x05DD)
    $td2 = Band-Doc 120 (' abc ' + $he + ' def'); Reset-Doc $app $td2
    Band-Ck 'T25.26 hebrew between latin words (two directions in one row)' $app 0 $false $false 0 8
    Reset-Doc $app $td
    $p = $td.IndexOf($ar[0], $sd[$i])
    [void](Snd $ed $EM_SETSEL ($p + 1) ($p + 4))
    Band-Bg $app 'T25.27 ... with part of the arabic selected the row stays blank (the highlight in reordered text is not a rectangle to measure)'
    [void](Snd $ed $EM_SETSEL 0 0)
    $pf = Chars @(0xFB50, 0xFE80, 0xFEFB)                                          # arabic presentation forms: plain GDI draws them differently
    Reset-Doc $app (Band-Doc 120 (' ' + $pf))
    Band-Bg $app 'T25.28 a row with presentation forms stays blank'
    $th = Chars @(0x0E2A, 0x0E27, 0x0E31, 0x0E2A, 0x0E14, 0x0E35)                 # thai: shaped, but left to right
    $td = Band-Doc 120 (' ' + $th + ' ' + $th); $sd = Line-Starts $td
    Reset-Doc $app $td
    Band-Ck 'T25.29 thai: the same as the control''s' $app 0 $false $false 0 8
    $p = $td.IndexOf($th[0], $sd[$i])
    [void](Snd $ed $EM_SETSEL ($p + 1) ($p + 4))
    Band-Ck 'T25.30 ... with part of it selected (the control draws the text of a shaped row inside the highlight another way: up to one level of one colour channel off)' $app 0 $false $true 1 8
    $dv = Chars @(0x0928, 0x092E, 0x0938, 0x094D, 0x0924, 0x0947)                 # devanagari
    $td = Band-Doc 120 (' ' + $dv + ' ' + $dv); $sd = Line-Starts $td
    Reset-Doc $app $td
    Band-Ck 'T25.31 devanagari: the same as the control''s' $app 0 $false $false 0 8

    $acc = Chars @(0x00E9, 0x00FC, 0x00F1, 0x00DF, 0x03B1, 0x03B2, 0x0416, 0x0434)                              # e acute, u umlaut, n tilde, sharp s, alpha, beta, zhe, de
    Reset-Doc $app (Band-Doc 120 (' ' + $acc))
    Band-Ck 'T25.32 accented latin, greek and cyrillic: the same as the control''s' $app 0 $false $false 0 4
    $cmb = 'e' + (Chars @(0x0301)) + 'a' + (Chars @(0x0308)) + 'o' + (Chars @(0x0302, 0x0323))                    # letters with combining accents
    Reset-Doc $app (Band-Doc 120 (' ' + $cmb))
    Band-Ck 'T25.33 combining accents' $app 0 $false $false 0 4
    $emo = Chars @(0xD83D, 0xDE00, 0x0020, 0xD83C, 0xDF89)                                                        # two emoji (surrogate pairs)
    Reset-Doc $app (Band-Doc 120 (' ' + $emo))
    Band-Ck 'T25.34 emoji (surrogate pairs)' $app 0 $false $false 0 8
    $odd = Chars @(0xFF71, 0xFF72, 0xFF73, 0x0001, 0x2026, 0x2014, 0x00A0, 0x3000, 0x2603)                       # half width katakana, a control character (a box), ellipsis, dash, no-break space, ideographic space, snowman
    Reset-Doc $app (Band-Doc 120 (' ' + $odd))
    Band-Ck 'T25.35 half width katakana, a control character, punctuation, spaces of other widths, a symbol' $app 0 $false $false 0 4

    # ---- the window's own pixels (the control paints straight onto its window, and scrolling moves only the rows)
    Reset-Doc $app $t
    $ed = Get-Edit $app
    $d = Ed-Direct $app 8 8
    if (-not $d) { Skip 'T25.40-T25.46 the window itself after scrolls, edits and resizes' 'this exe has no WM_APP + 90 (build the probe: tools\probe.bat /DSHOTDC, run with -Exe build\probe\notepad-mint.exe)'; return }
    $script:bandDirect = { param($name, $app)                                    # the band on the window against the band of a repaint (PrintWindow) of the same state
        $g = Band-Geo $app
        $dd = Ed-Direct $app $g.W $g.BandH $g.Top
        $s = Ed-Shot $app
        $r = [Bx]::DiffPpm($dd.B, $dd.Off, $dd.W, $dd.Y0, $s.Bmp, $s.Ox, $s.Oy, 0, $g.Top, $g.W, $g.BandH)
        $s.Bmp.Dispose()
        Ck $name ($r[0] -eq 0) ('the band on the window differs from a repaint in ' + $r[0] + ' px, first at x ' + $r[1] + ' y ' + $r[2] + ' (in the band), biggest channel difference ' + $r[3])
    }
    [void](Snd $ed $EM_SETSEL ($st[$i] + 3) ($st[$i] + 9))
    & $script:bandDirect 'T25.40 the window as painted: the band is what a repaint gives' $app
    [void](Snd $ed 0xB6 0 3)                                                     # EM_LINESCROLL: the control moves the rows, the band's text changes
    & $script:bandDirect 'T25.41 after EM_LINESCROLL (three rows down) the band on the window shows the new row' $app
    Pst $ed 0x115 1 0                                                            # WM_VSCROLL SB_LINEDOWN
    Start-Sleep -Milliseconds 200
    & $script:bandDirect 'T25.42 after a scroll bar message (one row down)' $app
    [void](Snd $ed 0x20A ((([long](-120)) -band 0xFFFF) -shl 16) 0)               # WM_MOUSEWHEEL, one notch down (no keys)
    Start-Sleep -Milliseconds 200
    & $script:bandDirect 'T25.43 after a mouse wheel notch' $app
    [void](Snd $ed $EM_SETSEL 0 0)
    [void][U]::SndStr($ed, $EM_REPLACESEL, 1, "new first line`r`n")             # an extra line at the top: every row below moves down one
    & $script:bandDirect 'T25.44 after a line was inserted above (every row moved)' $app
    [void](Snd $ed $EM_SETSEL ($st[$i] + 1) ($st[$i + 1] + 4))
    & $script:bandDirect 'T25.45 after the selection was changed into the band rows' $app
    [void](Snd $ed $EM_SETSEL 100000 100000)                                     # resize reveals this caret; direct captures exclude its blink, like PrintWindow
    $w0 = [U]::WRect([long]$app.Main)
    $ok = $true; $why = ''
    foreach ($dh in @(-37, 21, 53, -9, 14)) {                                    # resize steps: the window shrinks and grows
        $wc = [U]::WRect([long]$app.Main)
        [void][Nd]::Size([long]$app.Main, ($w0[2] - $w0[0]), ($wc[3] - $wc[1] + $dh))
        Start-Sleep -Milliseconds 300
        $caretHidden = Snd $ed (0x8000 + 97) 0 0                                 # keep both captures free of the native caret, including child-window composition
        try {
            $g = Band-Geo $app
            $y0 = $g.Top - 2 * $g.Pitch                                          # the last two whole rows and the band
            $dd = Ed-Direct $app $g.W ($g.H - $y0) $y0
            $s = Ed-Shot $app
            try { $r = [Bx]::DiffPpm($dd.B, $dd.Off, $dd.W, $dd.Y0, $s.Bmp, $s.Ox, $s.Oy, 0, $y0, $g.W, $g.H - $y0) }
            finally { $s.Bmp.Dispose() }
        } finally {
            if ($caretHidden) { [void](Snd $ed (0x8000 + 97) 1 0) }
        }
        if ($r[0] -ne 0) { $ok = $false; $why += ' height ' + $g.H + ': ' + $r[0] + ' px differ (first x ' + $r[1] + ' y ' + $r[2] + ' from row ' + $y0 + ');' }
    }
    Ck 'T25.46 after resizing the window five times the last rows and the band on the window are what a repaint gives (nothing stale is left in or around the band)' $ok $why
}

# =========================================================================================================== T26
# ctrl+k = edit > clear line: the caret's logical line loses its text, keeps its line break, one undo step. like ctrl+w / alt+z (T21) the accelerator
# needs real keyboard state: the table entry, the menu label and the help line are checked in the sources / the help window, the command runs here
function Test-T26 {
    $main = [IO.File]::ReadAllText((Join-Path $Src 'main.c'))
    $menu = [IO.File]::ReadAllText((Join-Path $Src 'menu_defs.c'))
    Ck 'T26.1 the accelerator table maps ctrl+k to clear line' ($main -match 'FVIRTKEY\s*\|\s*FCONTROL\s*,\s*''K''\s*,\s*IDM_EDIT_CLEARLINE\s*\}') 'no { FVIRTKEY | FCONTROL, ''K'', IDM_EDIT_CLEARLINE } entry in src\main.c'
    Ck 'T26.2 edit > clear line shows "ctrl+k" as its shortcut' ($menu -match 'IT\(L"cl&ear line",\s*L"ctrl\+k",\s*IDM_EDIT_CLEARLINE\)') 'the clear line item in src\menu_defs.c has no ctrl+k label'
    $app = Start-App
    Cmd $app 'IDM_HELP_TOPICS'
    $h = Wait-Win $app 'mp_help' 'help topics'
    $t = ''
    foreach ($k in [U]::Kids($h)) { if ([U]::Cls($k) -eq 'Edit') { $t = [U]::GetText($k) } }
    Ck 'T26.3 the help topics list ctrl+k (clear the current line)' ($t -match 'ctrl\+k\s+clear the current line') ('help text [' + (Show $t) + ']')
    Pst $h $WM_CLOSE 0 0
    [void](Gone $h)

    $ed = Get-Edit $app
    Reset-Doc $app "alpha`r`nbeta`r`ngamma"
    [void](Snd $ed $EM_SETSEL 9 9)                                               # inside "beta" (line 2 starts at 7)
    Cmd $app 'IDM_EDIT_CLEARLINE'
    CkEdText 'T26.4 a middle line: its text is gone, the line (and the breaks around it) stay' $app "alpha`r`n`r`ngamma"
    CkSel 'T26.5 ... and the caret sits at the start of that line' $app 7 7
    Ck 'T26.6 ... the document is modified' ([bool](WaitFor { Ed-Modified $app } 2000)) 'EM_GETMODIFY is 0'
    [void](Snd $ed $EM_UNDO 0 0)
    CkEdText 'T26.7 one undo brings the whole line back' $app "alpha`r`nbeta`r`ngamma"

    Reset-Doc $app "one`r`ntwo"
    [void](Snd $ed $EM_SETSEL 8 8)                                               # the end of the last line (no line break after it)
    Cmd $app 'IDM_EDIT_CLEARLINE'
    CkEdText 'T26.8 the last line (no line break after it): cleared, the break before it stays' $app "one`r`n"
    CkSel 'T26.9 ... the caret is at the start of the now empty last line' $app 5 5
    Reset-Doc $app "one`r`ntwo"
    Cmd $app 'IDM_EDIT_CLEARLINE'                                                # the caret is at 0: the first line
    CkEdText 'T26.10 the first line' $app "`r`ntwo"
    CkSel 'T26.11 ... the caret stays at 0' $app 0 0

    Reset-Doc $app "one`r`n`r`ntwo"
    [void](Snd $ed $EM_SETSEL 5 5)                                               # on the empty line 2
    Cmd $app 'IDM_EDIT_CLEARLINE'
    Start-Sleep -Milliseconds 250
    CkEdText 'T26.12 an empty line: nothing changes' $app "one`r`n`r`ntwo"
    Ck 'T26.13 ... the document stays clean and the caret stays where it was' ((-not (Ed-Modified $app)) -and (Wait-Sel $ed 5 5)) ('modified ' + (Ed-Modified $app) + ', selection ' + $script:lastSel)

    $emoji = [char]::ConvertFromUtf32(0x1F600)
    $cjk = -join ([char]0x65E5, [char]0x672C, [char]0x8A9E)
    Reset-Doc $app ($emoji + $cjk + "abc`t d`r`nx")
    [void](Snd $ed $EM_SETSEL 3 3)
    Cmd $app 'IDM_EDIT_CLEARLINE'
    CkEdText 'T26.14 surrogate pairs, cjk, a tab: the whole line goes' $app "`r`nx"

    Reset-Doc $app "aa`r`nbb`r`ncc"
    [void](Snd $ed $EM_SETSEL 1 6)                                               # a selection from line 1 into line 2: the caret is at its end, in line 2
    Cmd $app 'IDM_EDIT_CLEARLINE'
    CkEdText 'T26.15 a selection over two lines: the line of the caret (what the status bar shows) is cleared, line 1 stays' $app "aa`r`n`r`ncc"
    CkSel 'T26.16 ... and the selection is gone, the caret at the start of that line' $app 4 4

    Cmd $app 'IDM_FMT_WRAP'                                                      # word wrap on: the control is re-created
    Start-Sleep -Milliseconds 500
    $ed = Get-Edit $app
    $para = ('wrapping words go on and on ' * 14).TrimEnd()                      # one logical line, several rows
    Reset-Doc $app ($para + "`r`nnext")
    [void](Snd $ed $EM_SETSEL 300 300)
    Ck 'T26.17 word wrap on: the caret is in a later row of a wrapped paragraph' ((Snd $ed $EM_LINEFROMCHAR 300 0) -ge 2) ('the caret is in row ' + (Snd $ed $EM_LINEFROMCHAR 300 0))
    Cmd $app 'IDM_EDIT_CLEARLINE'
    CkEdText 'T26.18 ... the whole paragraph is cleared (a wrapped row is not a line), the next line stays' $app "`r`nnext"
    CkSel 'T26.19 ... the caret is at the start of the paragraph' $app 0 0
    Cmd $app 'IDM_FMT_WRAP'                                                      # back to the default (off)
    Start-Sleep -Milliseconds 500
}

# =========================================================================================================== T28
# the theme button, the right-most of the two buttons at the right end of the menu bar (menu.c "theme button"): no frame, no fill, no sunken state. its icon is the theme in use (a moon
# in the dark theme, a sun in the light one), so a click just swaps the icons. the icon is BAR_BTN_OPACITY percent opaque (20) at rest and fully opaque, with the
# accent behind it, while hovered. a hover can only be looked at in the probe build (tools\probe.bat /DMENU_NO_TRACK: the bar asks the system for a mouse-leave message
# and the system, finding the real pointer elsewhere, sends it at once, which ends a synthetic hover before anything can look at it): skipped otherwise
function Bar-Geo($app) {                                                         # the bar and its two buttons: the theme button is the last BAR_BTN_W px of the bar, the word wrap button the BAR_BTN_W px before it (dpi scaled); Off = how far the button we look at is from the right edge (0 = the theme button; T29 sets Bw: the wrap button); Ox / Oy = where the bar starts in a PrintWindow shot of the main window
    $bar = Chrome-Kid $app 'mp_menubar'
    $dpi = [U]::Dpi([long]$app.Main)
    $r = [U]::WRect($bar); $w = [U]::WRect([long]$app.Main)
    return @{ Bar = $bar; W = ($r[2] - $r[0]); H = ($r[3] - $r[1]); Bw = [int][Math]::Round($IDM.BAR_BTN_W * $dpi / 96, [MidpointRounding]::AwayFromZero); Ox = ($r[0] - $w[0]); Oy = ($r[1] - $w[1]); Off = 0 }
}
function Btn-Look1($app, $g, $bg) {                                              # one shot of the button as PrintWindow shows it: the pixels that differ from $bg, the one that differs most (and its colour), whether the rim of the button is plain $bg
    $bmp = [U]::Grab([long]$app.Main)
    try {
        $x0 = $g.W - $g.Bw - $g.Off
        $mask = New-Object 'bool[]' ($g.Bw * $g.H)
        $best = 0; $bc = @(0, 0, 0); $rim = $true; $n = 0
        for ($y = 0; $y -lt $g.H - 1; $y++) {                                    # (the bar's last row is a 1 px margin under the button: always bar colour)
            for ($x = 0; $x -lt $g.Bw; $x++) {
                $c = $bmp.GetPixel($g.Ox + $x0 + $x, $g.Oy + $y)
                $d = [Math]::Abs([int]$c.R - $bg[0]) + [Math]::Abs([int]$c.G - $bg[1]) + [Math]::Abs([int]$c.B - $bg[2])
                if ($d -gt 12) { $mask[$y * $g.Bw + $x] = $true; $n++ }
                if ($d -gt $best) { $best = $d; $bc = @([int]$c.R, [int]$c.G, [int]$c.B) }
                if (($x -lt 3 -or $x -ge $g.Bw - 3 -or $y -lt 3 -or $y -ge $g.H - 4) -and $d -gt 3) { $rim = $false }
            }
        }
        return @{ Mask = $mask; Count = $n; Best = $bc; Rim = $rim; Bw = $g.Bw; H = $g.H }
    } finally { $bmp.Dispose() }
}
function Btn-Look($app, $g, $bg) {                                               # the same, but a window that has only just started can still show an unpainted (all black) button in its first shots: look again, for up to 3 s, until the rim is plain bar colour. a real frame or fill stays wrong and fails after the 3 s
    $sw = [Diagnostics.Stopwatch]::StartNew()
    do { $l = Btn-Look1 $app $g $bg; if ($l.Rim) { break }; Start-Sleep -Milliseconds 150 } while ($sw.ElapsedMilliseconds -lt 3000)
    return $l
}
function Look-Asym($look) {                                                      # the pixels of the icon whose mirror image (in the icon's own bounding box) is not part of it: ~0 for the sun, many for the moon
    $minx = 9999; $maxx = -1
    for ($y = 0; $y -lt $look.H; $y++) { for ($x = 0; $x -lt $look.Bw; $x++) { if ($look.Mask[$y * $look.Bw + $x]) { if ($x -lt $minx) { $minx = $x }; if ($x -gt $maxx) { $maxx = $x } } } }
    $miss = 0
    for ($y = 0; $y -lt $look.H; $y++) { for ($x = $minx; $x -le $maxx; $x++) { if ($look.Mask[$y * $look.Bw + $x] -ne $look.Mask[$y * $look.Bw + ($minx + $maxx - $x)]) { $miss++ } } }
    return $miss
}
function Mix-Rgb($bg, $fg, [int]$pct) { , @(0, 1, 2 | ForEach-Object { [int][Math]::Round($bg[$_] + ($fg[$_] - $bg[$_]) * $pct / 100) }) }
function Near-Rgb($a, $b, [int]$tol) { [Math]::Abs($a[0] - $b[0]) -le $tol -and [Math]::Abs($a[1] - $b[1]) -le $tol -and [Math]::Abs($a[2] - $b[2]) -le $tol }
function Bar-Click($g, [int]$x) { $y = [int]($g.H / 2); Mouse $g.Bar 0x201 $x $y 1; Mouse $g.Bar 0x202 $x $y 0 }   # WM_LBUTTONDOWN + WM_LBUTTONUP at bar client x
function Test-T28 {
    $pal = Read-Palette
    if (-not $pal) { Skip 'T28 theme button' 'could not parse the g_themes palette table in src\ui.c'; return }
    $op = $IDM.BAR_BTN_OPACITY
    $app = Start-App                                                             # dark: the moon
    $g = Bar-Geo $app
    $edit0 = Get-Edit $app                                                       # (a click on the theme button must not toggle word wrap: that re-creates the editor)
    Ck 'T28.1 the bar has room for the menus and the button (a 900 px window)' ($g.W -gt 3 * $g.Bw) ('bar ' + $g.W + ' px wide, button ' + $g.Bw)
    $rest = $pal.dark.face
    $look = Btn-Look $app $g $rest
    Ck 'T28.2 dark theme: an icon is there, and nothing else: the rim of the button is plain bar colour (no frame, no fill, no sunken state)' (($look.Count -gt 20) -and ($look.Count -lt 200) -and $look.Rim) ('differing pixels ' + $look.Count + ', rim plain ' + $look.Rim)
    $want = Mix-Rgb $rest $pal.dark.text $op
    Ck ('T28.3 ... drawn at ' + $op + '% opacity: its strongest pixel is that share of the text colour over the bar colour') (Near-Rgb $look.Best $want 4) ('strongest pixel rgb(' + ($look.Best -join ',') + '), wanted about rgb(' + ($want -join ',') + ')')
    $asymMoon = Look-Asym $look
    Ck 'T28.4 ... and it is the moon (a crescent is not mirror symmetric)' ($asymMoon -gt 12) ('mirror mismatches ' + $asymMoon)

    Bar-Click $g ($g.W - $g.Off - [int]($g.Bw / 2))                                       # the middle of the button
    CkEq 'T28.5 a click on the button switches to the light theme (settings.ini: theme=light)' 'light' (Ini-Val $app 'view' 'theme' 'light')
    Ck 'T28.5b ... it is the theme button, not the word wrap one (word wrap would have re-created the editor: it is still the same window)' ((Get-Edit $app) -eq $edit0) 'the editor was re-created: the click toggled word wrap'
    Start-Sleep -Milliseconds 400
    $rest = $pal.light.face
    $look = Btn-Look $app $g $rest
    Ck 'T28.6 light theme: the same one button, nothing around the icon' (($look.Count -gt 20) -and ($look.Count -lt 250) -and $look.Rim) ('differing pixels ' + $look.Count + ', rim plain ' + $look.Rim)
    $want = Mix-Rgb $rest $pal.light.text $op
    Ck ('T28.7 ... also at ' + $op + '% opacity') (Near-Rgb $look.Best $want 4) ('strongest pixel rgb(' + ($look.Best -join ',') + '), wanted about rgb(' + ($want -join ',') + ')')
    $asymBulb = Look-Asym $look
    Ck 'T28.8 ... and the icon is now the sun (mirror symmetric: the moon was not)' (($asymBulb -le 4) -and ($asymBulb -lt $asymMoon)) ('mirror mismatches: sun ' + $asymBulb + ', moon was ' + $asymMoon)

    Bar-Click $g ($g.W - 2 * $g.Bw - 6)                                              # just left of the two buttons: free bar, nothing there
    Start-Sleep -Milliseconds 500
    CkEq 'T28.9 a click just left of the two buttons (free bar) does nothing: still the light theme' 'light' (Ini-Val $app 'view' 'theme' 'light' 300)
        Start-Sleep -Milliseconds 500
    Bar-Click $g ($g.W - $g.Off - 3)                                                      # the very right edge: still the button
    CkEq 'T28.10 the button reaches the right edge of the bar: a click 3 px from it switches back to dark' 'dark' (Ini-Val $app 'view' 'theme' 'dark')
    Start-Sleep -Milliseconds 400
    $look = Btn-Look $app $g $pal.dark.face
    Ck 'T28.11 and the icon is the moon again' ((Look-Asym $look) -gt 12) ('mirror mismatches ' + (Look-Asym $look))

    $mark = [long](Snd $g.Bar (0x8000 + 91) 0 0)                                 # WM_APP + 91: only the probe build answers
    if ($mark -ne 0x4D494E54) { Skip 'T28.12-T28.18 the hover' 'this exe has no WM_APP + 91 (build the probe: tools\probe.bat /DSHOTDC /DMENU_NO_TRACK, run with -Exe build\probe\notepad-mint.exe)'; return }
    $y = [int]($g.H / 2)
    foreach ($th in @('dark', 'light')) {
        if ($th -eq 'light') { Bar-Click $g ($g.W - $g.Off - [int]($g.Bw / 2)); [void](Ini-Val $app 'view' 'theme' 'light'); Start-Sleep -Milliseconds 400; Mouse $g.Bar 0x200 100 $y 0 }
        $p = $pal.$th
        Mouse $g.Bar 0x200 ($g.W - $g.Off - [int]($g.Bw / 2)) $y 0                        # the pointer moves onto the button
        Start-Sleep -Milliseconds 300
        $look = Btn-Look $app $g $p.accent
        $why = 'differing pixels ' + $look.Count + ', rim plain ' + $look.Rim + ', strongest rgb(' + ($look.Best -join ',') + ')'
        Ck ('T28.12 ' + $th + ': hovered, the button is filled with the accent (the same as a hovered menu) ...') ($look.Rim -and $look.Count -gt 20 -and $look.Count -lt 250) $why
        Ck ('T28.13 ' + $th + ': ... and the icon is at full strength in the on-accent colour, not faded') (Near-Rgb $look.Best $p.onAccent 2) $why
        Mouse $g.Bar 0x200 ($g.W - 2 * $g.Bw - 40) $y 0                              # away from it (free bar)
        Start-Sleep -Milliseconds 300
        $look = Btn-Look $app $g $p.face
        $want = Mix-Rgb $p.face $p.text $op
        Ck ('T28.14 ' + $th + ': the pointer left: the accent is gone and the icon is back to ' + $op + '%') ($look.Rim -and (Near-Rgb $look.Best $want 4)) ('rim plain ' + $look.Rim + ', strongest rgb(' + ($look.Best -join ',') + '), wanted about rgb(' + ($want -join ',') + ')')
    }
    Mouse $g.Bar 0x200 ($g.W - $g.Off - [int]($g.Bw / 2)) $y 0                            # hover, then click: the new icon comes up already hovered (light -> dark here)
    Bar-Click $g ($g.W - $g.Off - [int]($g.Bw / 2))
    [void](Ini-Val $app 'view' 'theme' 'dark')
    Start-Sleep -Milliseconds 400
    $look = Btn-Look $app $g $pal.dark.accent
    Ck 'T28.15 clicking while hovered: the other icon comes up at full strength on the accent (dark: the moon in the on-accent colour)' ($look.Rim -and (Near-Rgb $look.Best $pal.dark.onAccent 2) -and ((Look-Asym $look) -gt 12)) ('rim plain ' + $look.Rim + ', strongest rgb(' + ($look.Best -join ',') + '), mirror mismatches ' + (Look-Asym $look))
    Mouse $g.Bar 0x200 100 $y 0
}

# =========================================================================================================== T29
# the word wrap button: the left one of the menu bar's two buttons (the theme button is flush right, right of it). one click = alt+z (word wrap on / off); its icon is
# BAR_BTN_OPACITY percent opaque while wrap is off and BAR_BTN_OPACITY_ON while it is on. and the resize: the edit re-wraps once per size, a drag of a big
# wrapped document does not block for it every step (the last size gets its wrap after a pause)
function Test-T29 {
    $pal = Read-Palette
    $app = Start-App
    $g = Bar-Geo $app
    $g.Off = $g.Bw                                                                  # the wrap button is the one left of the theme button
    $edit0 = Get-Edit $app
    $look0 = Btn-Look $app $g $pal.dark.face
    Ck 'T29.1 word wrap off: an icon at the very right, nothing around it' (($look0.Count -gt 15) -and $look0.Rim) ('differing pixels ' + $look0.Count + ', rim plain ' + $look0.Rim)
    $want = Mix-Rgb $pal.dark.face $pal.dark.text $IDM.BAR_BTN_OPACITY
    Ck 'T29.2 ... drawn at the resting opacity' (Near-Rgb $look0.Best $want 4) ('strongest pixel rgb(' + ($look0.Best -join ',') + '), wanted about rgb(' + ($want -join ',') + ')')
    if ([U]::Dpi([long]$app.Main) -eq 96) {                                      # (the rows of the icon are worked out here for 96 dpi)
        $row0 = -1
        for ($y = 0; $y -lt $look0.H -and $row0 -lt 0; $y++) { for ($x = 0; $x -lt $look0.Bw; $x++) { if ($look0.Mask[$y * $look0.Bw + $x]) { $row0 = $y; break } } }
        $wantRow = [int][Math]::Floor(($g.H - 1 - $IDM.BAR_BTN_ICON) / 2) + $IDM.BAR_WRAP_ICON_DY + 1
        Ck 'T29.2b ... and it sits BAR_WRAP_ICON_DY px below the middle of its button (first icon row = the box centred in the button + the offset + 1: the first line is the second row of the box)' ($row0 -eq $wantRow) ('first icon row ' + $row0 + ' px below the top of the bar, wanted ' + $wantRow)
    }
    Bar-Click $g ($g.W - $g.Off - [int]($g.Bw / 2))
    Start-Sleep -Milliseconds 700
    Ck 'T29.3 a click switches word wrap on (the editor was re-created: another window)' ((Get-Edit $app) -ne $edit0) 'the editor is the same window'
    $look = Btn-Look $app $g $pal.dark.face
    $want = Mix-Rgb $pal.dark.face $pal.dark.text $IDM.BAR_BTN_OPACITY_ON
    Ck 'T29.4 ... and the icon is brighter now (word wrap on)' (Near-Rgb $look.Best $want 4) ('strongest pixel rgb(' + ($look.Best -join ',') + '), wanted about rgb(' + ($want -join ',') + ')')
    Bar-Click $g ($g.W - $g.Off - [int]($g.Bw / 2))
    Start-Sleep -Milliseconds 700
    $look = Btn-Look $app $g $pal.dark.face
    $want = Mix-Rgb $pal.dark.face $pal.dark.text $IDM.BAR_BTN_OPACITY
    Ck 'T29.5 a second click switches it off again: the resting opacity' (Near-Rgb $look.Best $want 4) ('strongest pixel rgb(' + ($look.Best -join ',') + '), wanted about rgb(' + ($want -join ',') + ')')
    Ck 'T29.6 the wrap button is not the theme button: the theme stays dark' ((Ini-Val $app 'view' 'theme' 'dark' 300) -ne 'light') 'theme changed'

    Cmd $app 'IDM_FMT_WRAP'                                                      # word wrap on through the command: the button follows
    Start-Sleep -Milliseconds 700
    $look = Btn-Look $app $g $pal.dark.face
    Ck 'T29.7 word wrap switched on by alt+z / the menu: the button shows it too' (Near-Rgb $look.Best (Mix-Rgb $pal.dark.face $pal.dark.text $IDM.BAR_BTN_OPACITY_ON) 4) ('strongest pixel rgb(' + ($look.Best -join ',') + ')')

    # resize: a wrapped document is laid out for the size the window ends at
    $ed = Get-Edit $app
    $piece = 'the quick brown fox jumps over the lazy dog 0123456789 '
    Reset-Doc $app ((1..1500 | ForEach-Object { 'line ' + $_ + ' ' + $piece + $piece }) -join "`r`n")
    $w0 = [U]::WRect([long]$app.Main)
    $ww = $w0[2] - $w0[0]; $hh = $w0[3] - $w0[1]
    $rows0 = [int](Snd $ed 0xBA 0 0)                                             # EM_GETLINECOUNT: rows at this width
    foreach ($i in 1..12) { [void][Nd]::Size([long]$app.Main, ($ww - 40 * $i), $hh) }          # a quick drag narrower
    $rowsFast = [int](Snd $ed 0xBA 0 0)
    $ok = [bool](WaitFor { [int](Snd $ed 0xBA 0 0) -gt $rows0 } 4000)
    Ck 'T29.8 a quick drag narrower: afterwards the text has more rows (it was wrapped for the final width)' $ok ('rows ' + $rows0 + ' before, ' + $rowsFast + ' right after, ' + (Snd $ed 0xBA 0 0) + ' later')
    Start-Sleep -Milliseconds 400
    $rowsA = [int](Snd $ed 0xBA 0 0)
    [void][Nd]::Size([long]$app.Main, $ww, $hh)
    $ok = [bool](WaitFor { [int](Snd $ed 0xBA 0 0) -eq $rows0 } 4000)
    Ck 'T29.9 back to the first width: the very same number of rows as at the start' $ok ('rows ' + $rows0 + ' at the start, ' + (Snd $ed 0xBA 0 0) + ' now (narrow: ' + $rowsA + ')')
}

# =========================================================================================================== T30
# the tooltips of the two bar buttons (menu.c "tooltips"): once the pointer has rested BAR_TIP_DELAY ms on a button, a small popup (class mp_tip) shows right under the bar with its right
# edge on the button's: what the button does + its key combo (read from the menu item that runs the same command). it looks like a menu popup (the face2 fill, the text colour), never takes
# the mouse, follows the pointer from one button to the other, and goes when the pointer leaves, on a click (and then stays away until the pointer has left and returned), and after BAR_TIP_SHOW ms.
# like the hover of T28 it needs the probe build (tools\probe.bat /DSHOTDC /DMENU_NO_TRACK): the bar asks the system for a mouse-leave message and the system sends it at once for a synthetic
# hover on a private desktop, which would end every hover before the delay is over: skipped on a normal exe
function Tip-Win($app) { [long][U]::FindTop($app.Pid, 'mp_tip', '') }           # the tip window (0 = none up)
function Tip-Look($tip, $bg) {                                                   # PrintWindow shot of a tip: the pixel inside the bevel (the popup fill) and the pixel that differs most from $bg (the text: its colour)
    $bmp = [U]::Grab([long]$tip)
    try {
        $in = $bmp.GetPixel(3, 3)
        $best = 0; $bc = @(0, 0, 0)
        for ($y = 3; $y -lt $bmp.Height - 3; $y++) {
            for ($x = 3; $x -lt $bmp.Width - 3; $x++) {
                $c = $bmp.GetPixel($x, $y)
                $d = [Math]::Abs([int]$c.R - $bg[0]) + [Math]::Abs([int]$c.G - $bg[1]) + [Math]::Abs([int]$c.B - $bg[2])
                if ($d -gt $best) { $best = $d; $bc = @([int]$c.R, [int]$c.G, [int]$c.B) }
            }
        }
        return @{ In = @([int]$in.R, [int]$in.G, [int]$in.B); Best = $bc }
    } finally { $bmp.Dispose() }
}
function Test-T30 {
    $pal = Read-Palette
    if (-not $pal) { Skip 'T30 button tooltips' 'could not parse the g_themes palette table in src\ui.c'; return }
    $app = Start-App                                                             # dark
    $g = Bar-Geo $app
    if ([long](Snd $g.Bar (0x8000 + 91) 0 0) -ne 0x4D494E54) { Skip 'T30 button tooltips' 'this exe has no WM_APP + 91 (build the probe: tools\probe.bat /DSHOTDC /DMENU_NO_TRACK, run with -Exe build\probe\notepad-mint.exe)'; return }
    $delay = $IDM.BAR_TIP_DELAY; $show = $IDM.BAR_TIP_SHOW
    $y = [int]($g.H / 2)
    $xWrap = $g.W - $g.Bw - [int]($g.Bw / 2)                                     # the middle of the word wrap button (left) ...
    $xTheme = $g.W - [int]($g.Bw / 2)                                            # ... and of the theme button (flush right)
    $away = $g.W - 2 * $g.Bw - 40                                                # free bar, left of both buttons
    $bar = [U]::WRect($g.Bar)                                                    # the bar in screen coordinates

    Mouse $g.Bar 0x200 20 $y 0                                                   # a menu title: no tip for it
    Start-Sleep -Milliseconds ($delay + 400)
    Ck 'T30.1 a menu title gets no tip' ((Tip-Win $app) -eq 0) 'a tip showed over a menu title'
    Mouse $g.Bar 0x200 $xWrap $y 0                                               # the pointer arrives on the word wrap button
    Start-Sleep -Milliseconds ([int]($delay / 4))
    Ck 'T30.2 a quarter of the delay later there is no tip yet' ((Tip-Win $app) -eq 0) 'a tip was up almost at once'
    $tip = WaitFor { Tip-Win $app } ($delay + 2000)
    Ck 'T30.3 ... and after the delay the tip shows' ([bool]$tip) 'no mp_tip window'
    if (-not $tip) { return }
    CkEq 'T30.4 word wrap button: what it does and its key combo (the one the menu shows)' 'toggle word wrap (alt+z)' ([U]::Text([long]$tip))
    $t = [U]::WRect([long]$tip)
    Ck 'T30.5 ... right under the bar' (($t[1] -ge $bar[3]) -and ($t[1] -le $bar[3] + 6)) ('tip top ' + $t[1] + ', bar bottom ' + $bar[3])
    Ck 'T30.6 ... with its right edge on the right edge of the button' ([Math]::Abs($t[2] - ($bar[2] - $g.Bw)) -le 1) ('tip right ' + $t[2] + ', button right ' + ($bar[2] - $g.Bw))
    CkEq 'T30.7 ... and it never takes the mouse (WM_NCHITTEST says HTTRANSPARENT)' -1 (Snd $tip 0x84 0 0)
    $look = Tip-Look $tip $pal.dark.face2
    Ck 'T30.8 dark theme: the tip is filled like a popup menu (face2) ...' (Near-Rgb $look.In $pal.dark.face2 2) ('inside rgb(' + ($look.In -join ',') + '), wanted rgb(' + ($pal.dark.face2 -join ',') + ')')
    Ck 'T30.9 ... and its text is in the text colour' (Near-Rgb $look.Best $pal.dark.text 40) ('strongest pixel rgb(' + ($look.Best -join ',') + '), wanted about rgb(' + ($pal.dark.text -join ',') + ')')
    Cmd $app 'IDM_THEME_TOGGLE'                                                  # the theme changes while the tip is up
    Start-Sleep -Milliseconds 400
    $look = Tip-Look $tip $pal.light.face2
    Ck 'T30.10 after a theme switch the tip shows the new palette (light: face2 and the text colour)' ((Near-Rgb $look.In $pal.light.face2 2) -and (Near-Rgb $look.Best $pal.light.text 40)) ('inside rgb(' + ($look.In -join ',') + '), strongest rgb(' + ($look.Best -join ',') + ')')

    Mouse $g.Bar 0x200 $xTheme $y 0                                              # on to the theme button: its tip at once (one was up)
    $tip2 = WaitFor { $h = Tip-Win $app; if ($h -and ([U]::Text($h) -like 'toggle dark*')) { $h } } 1500
    Ck 'T30.11 moving to the other button while a tip is up shows that button''s tip at once' ([bool]$tip2) ('the tip text is now [' + (Show ([U]::Text((Tip-Win $app)))) + ']')
    if (-not $tip2) { return }
    CkEq 'T30.12 theme button: what it does and its key combo' 'toggle dark / light theme (alt+x)' ([U]::Text([long]$tip2))
    $t = [U]::WRect([long]$tip2)
    Ck 'T30.13 ... with its right edge on the right edge of the bar (the theme button is flush right)' ([Math]::Abs($t[2] - $bar[2]) -le 1) ('tip right ' + $t[2] + ', bar right ' + $bar[2])
    Ck 'T30.14 ... and the first tip is gone (one tip at a time)' (-not [U]::Visible([long]$tip)) 'the first tip window is still visible'

    Mouse $g.Bar 0x200 $away $y 0                                                # the pointer leaves the buttons
    Ck 'T30.15 the pointer left: the tip is gone' (Gone ([long]$tip2) 1000) 'still visible 1 s after the pointer left'
    Mouse $g.Bar 0x200 $xWrap $y 0
    $tip3 = WaitFor { Tip-Win $app } ($delay + 2000)
    Bar-Click $g $xWrap                                                          # a click: word wrap switches (the button's own job), and the tip goes at once
    Ck 'T30.16 a click on the button ends its tip at once' (([bool]$tip3) -and (Gone ([long]$tip3) 600)) 'there was no tip to click, or it is still there'
    Start-Sleep -Milliseconds ($delay + 500)
    Ck 'T30.17 ... and it stays away while the pointer stays on the button' ((Tip-Win $app) -eq 0) 'the tip came back without the pointer leaving the button'
    Mouse $g.Bar 0x200 $away $y 0
    Mouse $g.Bar 0x200 $xWrap $y 0                                               # the pointer leaves and returns: the tip comes back
    $tip4 = WaitFor { Tip-Win $app } ($delay + 2000)
    Ck 'T30.18 once the pointer has left and returned the tip shows again' ([bool]$tip4) 'no tip after the pointer returned'
    if ($tip4) { Ck ('T30.19 the tip goes by itself after ' + $show + ' ms') (Gone ([long]$tip4) ($show + 2500)) 'still visible long after BAR_TIP_SHOW' }
    Mouse $g.Bar 0x200 $away $y 0
}

# =========================================================================================================== T31
# file > recent: the last 9 files opened or saved, newest first, in settings.ini ([recent] 1 .. 9); a file that is opened again moves to the top; the menu items open them
function Recent-List($ini) { $i = Read-Ini $ini; $l = @(); if ($i.ContainsKey('recent')) { foreach ($k in 1..12) { if ($i['recent'].ContainsKey([string]$k)) { $l += $i['recent'][[string]$k] } } }; return $l }
function Test-T31 {
    $ad = Join-Path $work 'recent_appdata'
    $ini = Join-Path $ad 'notepad-mint\settings.ini'
    $f = @(); foreach ($n in 1..11) { $p = Join-Path $work ('rec' + $n + '.txt'); [IO.File]::WriteAllText($p, 'file ' + $n); $f += $p }
    foreach ($n in 1..11) {
        $a = Start-App $f[$n - 1] $ad
        [void](Wait-Title $a ('rec' + $n + '.txt - notepad mint') 8000)
        Stop-App $a
    }
    $l = Recent-List $ini
    Ck 'T31.1 eleven files opened one after the other: nine are remembered' ($l.Count -eq 9) ('list ' + ($l -join ' | '))
    $want = @(11, 10, 9, 8, 7, 6, 5, 4, 3 | ForEach-Object { $f[$_ - 1] })
    Ck 'T31.2 ... newest first, the two oldest are gone' ((($l -join '|') -ieq ($want -join '|'))) ('list ' + ($l -join ' | '))
    $a = Start-App $f[5] $ad                                                     # rec6 again: it moves to the top, nothing is listed twice
    [void](Wait-Title $a 'rec6.txt - notepad mint' 8000)
    $l = Recent-List $ini
    $want = @(6, 11, 10, 9, 8, 7, 5, 4, 3 | ForEach-Object { $f[$_ - 1] })
    Ck 'T31.3 a file opened again moves to the top, no duplicate' ((($l -join '|') -ieq ($want -join '|'))) ('list ' + ($l -join ' | '))
    Pst $a.Main $WM_COMMAND ($IDM.IDM_RECENT_BASE + 2) 0                          # the third item: rec10
    $ok = Wait-Title $a 'rec10.txt - notepad mint' 8000
    Ck 'T31.4 the third item of file > recent opens that file (rec10)' $ok ('title [' + (Title $a) + ']')
    $l = Recent-List $ini
    Ck 'T31.5 ... and it moves to the top of the list' ($l.Count -eq 9 -and $l[0] -ieq $f[9] -and $l[1] -ieq $f[5]) ('list ' + ($l -join ' | '))
    Pst $a.Main $WM_COMMAND $IDM.IDM_RECENT_CLEAR 0                               # file > recent > clear list (the last item, after a separator)
    $ok = WaitFor { (Recent-List $ini).Count -eq 0 } 3000
    Ck 'T31.6 "clear list" empties the list in settings.ini' $ok ('list ' + ((Recent-List $ini) -join ' | '))
    Stop-App $a
    $b = Start-App '' $ad
    Ck 'T31.7 ... a new window starts with an empty list (and the document is not touched)' ((Recent-List $ini).Count -eq 0) ('list ' + ((Recent-List $ini) -join ' | '))
    Stop-App $b
    $b = Start-App $f[0] $ad
    [void](Wait-Title $b 'rec1.txt - notepad mint' 8000)
    $l = @(Recent-List $ini)                                                     # (@: one entry would come back as a plain string)
    Ck 'T31.8 the list fills again after it was cleared' ($l.Count -eq 1 -and $l[0] -ieq $f[0]) ('list ' + ($l -join ' | '))
}

# =========================================================================================================== T32
# format > tab size: 2, 4 or 8 columns (4 until chosen otherwise), saved in settings.ini ([editor] tab), used by the control, by the rows edit.c draws itself and by printing
function Test-T32 {
    $app = Start-App
    $ed = Get-Edit $app
    $pad = [int][Math]::Round($IDM.EDIT_PAD * ([U]::Dpi([long]$app.Main)) / 96, [MidpointRounding]::AwayFromZero)
    Reset-Doc $app ("`tx")
    function TabW { param($a) $e = Get-Edit $a; return ([int](([long](Snd $e 0xD6 1 0)) -band 0xFFFF)) - $pad }       # EM_POSFROMCHAR of the "x": how far the tab reaches
    $w4 = TabW $app
    Ck 'T32.1 the default tab size is 4 columns (a tab reaches a positive multiple of 4 average characters)' ($w4 -gt 0 -and $w4 % 4 -eq 0) ('tab reaches ' + $w4 + ' px')
    Cmd $app 'IDM_TAB_8'
    $ok = WaitFor { (TabW $app) -ne $w4 } 3000
    $w8 = TabW $app
    Ck 'T32.2 tab size 8: a tab is twice as wide' ($ok -and $w4 * 2 -eq $w8) ('4: ' + $w4 + ' px, 8: ' + $w8 + ' px')
    Cmd $app 'IDM_TAB_2'
    $ok = WaitFor { (TabW $app) -ne $w8 } 3000
    $w2 = TabW $app
    Ck 'T32.3 tab size 2: half as wide as 4' ($ok -and $w2 * 2 -eq $w4) ('4: ' + $w4 + ' px, 2: ' + $w2 + ' px')
    Ck 'T32.4 the choice is saved (settings.ini [editor] tab=2)' (Ini-Val $app 'editor' 'tab' '2') ('tab=' + $script:iniV)
    $ad = $app.AppData
    Stop-App $app
    $b = Start-App '' $ad
    Reset-Doc $b ("`tx")
    Ck 'T32.5 a new window starts with the saved tab size' ((TabW $b) -eq $w2) ('tab reaches ' + (TabW $b) + ' px, wanted ' + $w2)
    Cmd $b 'IDM_TAB_4'
    $ok = WaitFor { (TabW $b) -eq $w4 } 3000
    Ck 'T32.6 back to 4 columns' $ok ('tab reaches ' + (TabW $b) + ' px, wanted ' + $w4)
}

# =========================================================================================================== T33
# Classic .LOG entries are appended, undoable and only reach disk on save.
function Test-T33 {
    $path = Join-Path $work 'journal.txt'
    $original = ".LOG`nentry"
    $normalized = ".LOG`r`nentry"
    [IO.File]::WriteAllText($path, $original)
    $app = Start-App $path
    Ck 'T33.1 opening .LOG marks the appended entry modified' (Wait-Title $app '*journal.txt - notepad mint') (Title $app)
    $stamped = Ed-Text $app
    Ck 'T33.2 timestamp follows the original text and ends in a new line' ($stamped.StartsWith($normalized + "`r`n") -and $stamped.EndsWith("`r`n") -and $stamped.Length -gt $normalized.Length + 8) (Show $stamped)
    CkSel 'T33.3 caret is after the new entry' $app $stamped.Length $stamped.Length
    CkText 'T33.4 opening a log does not write the file' $original ([IO.File]::ReadAllText($path))
    Cmd $app 'IDM_EDIT_UNDO'
    CkEdText 'T33.5 one undo restores the exact original text' $app $normalized
    Ck 'T33.6 undo restores the clean title' (Wait-Title $app 'journal.txt - notepad mint') (Title $app)
    Cmd $app 'IDM_EDIT_UNDO'
    CkEdText 'T33.7 native redo restores the complete timestamp' $app $stamped
    Cmd $app 'IDM_FILE_SAVE'
    Ck 'T33.8 save records the timestamp and clears modified state' (Wait-Title $app 'journal.txt - notepad mint') (Title $app)
    CkText 'T33.9 save preserves the original LF line endings' ($stamped.Replace("`r`n", "`n")) ([IO.File]::ReadAllText($path))
    Stop-App $app
    $lower = Join-Path $work 'lowercase-log.txt'
    [IO.File]::WriteAllText($lower, '.log')
    $app = Start-App $lower
    [void](Wait-Title $app 'lowercase-log.txt - notepad mint')
    CkEdText 'T33.10 the marker is case sensitive' $app '.log'
}

# =========================================================================================================== T35
# A save cannot silently overwrite changes made by another program.
function Test-T35 {
    $path = Join-Path $work 'conflict.txt'
    [IO.File]::WriteAllText($path, 'original')
    $app = Start-App $path
    [void](Wait-Title $app 'conflict.txt - notepad mint')
    [void](Snd (Get-Edit $app) $EM_SETSEL 0 -1)
    Ed-Dirty $app 'my edits'
    [IO.File]::WriteAllText($path, 'external changes')
    Cmd $app 'IDM_FILE_SAVE'
    $box = Wait-Box $app $AppName
    Ck 'T35.1 external edits trigger an overwrite warning' ($box.Text -like '*changed outside*') $box.Text
    [void](Box-Press $box 102)
    CkText 'T35.2 cancel preserves the external file' 'external changes' ([IO.File]::ReadAllText($path))
    CkEdText 'T35.3 cancel retains the unsaved editor text' $app 'my edits'
    Ck 'T35.4 cancel retains modified state' (Wait-Title $app '*conflict.txt - notepad mint') (Title $app)
    Cmd $app 'IDM_FILE_SAVE'
    $box = Wait-Box $app $AppName
    [void](Box-Press $box 1)
    Ck 'T35.5 explicit overwrite succeeds and clears modified state' (Wait-Title $app 'conflict.txt - notepad mint') (Title $app)
    CkText 'T35.6 explicit overwrite writes the editor text' 'my edits' ([IO.File]::ReadAllText($path))
    [void](Snd (Get-Edit $app) $EM_SETSEL 8 8)
    Ed-Dirty $app '!'
    Cmd $app 'IDM_FILE_SAVE'
    Ck 'T35.7 the next normal save uses the refreshed file stamp' (Wait-Title $app 'conflict.txt - notepad mint') (Title $app)
    CkText 'T35.8 the next save writes without another warning' 'my edits!' ([IO.File]::ReadAllText($path))
    [IO.File]::Delete($path)
    Cmd $app 'IDM_FILE_SAVE'
    $box = Wait-Box $app $AppName
    Ck 'T35.9 deleting the file externally also requires confirmation' ($box.Text -like '*no longer available*') $box.Text
    [void](Box-Press $box 102)
    Ck 'T35.10 cancel does not recreate a deleted file' (-not [IO.File]::Exists($path)) 'file recreated'
}

# =========================================================================================================== T36
# Limit only the isolated test process to force text-copy allocation failure during word-wrap recreation.
function Test-T36 {
    $app = Start-App
    $text = 'alpha beta gamma ' * 131072
    Reset-Doc $app $text
    $ed = Get-Edit $app
    [void](Snd $ed $EM_SETSEL 17 23)
    $flags = [Reflection.BindingFlags]'NonPublic,Static'
    $job = ([U].GetField('job', $flags)).GetValue($null)
    $extType = [U].GetNestedType('JEXT', 'NonPublic')
    $basicType = [U].GetNestedType('JBASIC', 'NonPublic')
    $setJob = [U].GetMethod('SetInformationJobObject', $flags)
    $ext = [Activator]::CreateInstance($extType)
    $basic = [Activator]::CreateInstance($basicType)
    $basic.flags = 0x2100
    $ext.basic = $basic
    $app.Proc.Refresh()
    $ext.procMem = [IntPtr]($app.Proc.PrivateMemorySize64 + 524288)
    $size = [Runtime.InteropServices.Marshal]::SizeOf($ext)
    $applied = $setJob.Invoke($null, @($job, [int]9, $ext, [int]$size))
    Ck 'T36.1 process memory limit is applied' $applied 'SetInformationJobObject failed'
    try {
        Cmd $app 'IDM_FMT_WRAP'
        Start-Sleep -Milliseconds 1200
    } finally {
        $basic.flags = 0x2000
        $ext.basic = $basic
        $ext.procMem = [IntPtr]::Zero
        $cleared = $setJob.Invoke($null, @($job, [int]9, $ext, [int]$size))
        Ck 'T36.1b process memory limit is removed' $cleared 'SetInformationJobObject failed'
    }
    $box = Wait-Box $app $AppName
    Ck 'T36.2 failed wrap reports memory exhaustion' ($box.Text -like '*not enough memory*') $box.Text
    [void](Box-Press $box 1)
    Ck 'T36.3 failed wrap keeps the original editor window' ((Get-Edit $app) -eq $ed) 'editor replaced'
    CkEdText 'T36.4 failed wrap preserves every character' $app $text
    CkSel 'T36.5 failed wrap preserves selection' $app 17 23
    Cmd $app 'IDM_FMT_WRAP'
    Ck 'T36.6 wrapping succeeds after memory is available again' ([bool](WaitFor { (Get-Edit $app) -ne $ed } 12000)) 'editor not replaced'
    CkEdText 'T36.7 successful retry also preserves the text' $app $text 12000
}

# =========================================================================================================== T34
function Test-T34 {                                                             # whole-word find / replace, complete-document boundaries and persistence
    $app = Start-App
    $ed = Get-Edit $app
    $original = 'scatter cat cat_1 cat2 CAT cat' + (Chars 0x0301)
    Reset-Doc $app $original
    Cmd $app 'IDM_EDIT_FIND'
    $dlg = Wait-Win $app 'mp_find' 'find'
    CkChk 'T34.1 whole word defaults off' $dlg $IDF.ID_WORD 0
    Set-Field $dlg $IDF.ID_WHAT 'cat'
    Press $dlg $IDF.ID_WORD
    CkChk 'T34.2 whole word toggles on' $dlg $IDF.ID_WORD 1
    Press $dlg $IDOK
    CkSel 'T34.3 whole word skips an embedded prefix' $app 8 11
    Press $dlg $IDOK
    CkSel 'T34.4 whole word skips underscore and numeric suffixes' $app 23 26
    Press $dlg $IDOK
    CkSel 'T34.5 whole word skips a combining-mark suffix and wraps' $app 8 11
    Press $dlg $IDF.ID_UP
    CkChk 'T34.6 reverse direction selected' $dlg $IDF.ID_UP 1
    Press $dlg $IDOK
    CkSel 'T34.7 reverse wrap uses whole-word boundaries' $app 23 26
    Press $dlg $IDCANCEL
    [void](Gone $dlg)
    Cmd $app 'IDM_EDIT_REPLACE'
    $dlg = Wait-Win $app 'mp_find' 'replace'
    CkChk 'T34.8 replace remembers whole word' $dlg $IDF.ID_WORD 1
    Set-Field $dlg $IDF.ID_WHAT 'cat'
    Set-Field $dlg $IDF.ID_WITH 'fox'
    Press $dlg $IDF.ID_DOWN
    CkChk 'T34.9 forward direction selected' $dlg $IDF.ID_DOWN 1
    [void](Snd $ed $EM_SETSEL 1 4)
    Press $dlg $IDF.ID_REPLACE
    CkSel 'T34.10 replacing an embedded selection only finds the next whole word' $app 8 11
    CkText 'T34.11 embedded selection was not replaced' $original (Ed-Text $app)
    Press $dlg $IDF.ID_REPLACEALL
    $expected = 'scatter fox cat_1 cat2 fox cat' + (Chars 0x0301)
    CkEdText 'T34.12 replace all changes only complete words' $app $expected
    [void](Snd $ed $EM_UNDO)
    CkEdText 'T34.13 one undo restores whole-word replace all' $app $original
    Reset-Doc $app 'cat2 cat'
    Set-Field $dlg $IDF.ID_WITH 'x'
    [void](Snd $ed $EM_SETSEL 3 3)
    Press $dlg $IDF.ID_REPLACEALL
    CkEdText 'T34.14 caret inside cat2 does not turn its prefix into a match' $app 'cat2 x'
    CkSel 'T34.15 caret counting uses document boundary beyond the caret' $app 3 3
    Press $dlg $IDCANCEL
    [void](Gone $dlg)
    $ad = $app.AppData
    Stop-App $app
    CkEq 'T34.16 whole word is saved' '1' (Ini-Val $app 'find' 'wholeword' '1')
    $app = Start-App '' $ad
    Cmd $app 'IDM_EDIT_FIND'
    $dlg = Wait-Win $app 'mp_find' 'find'
    CkChk 'T34.17 a new process loads whole word' $dlg $IDF.ID_WORD 1
    Press $dlg $IDCANCEL
    [void](Gone $dlg)
}

# =========================================================================================================== T37
# resize can erase the native editor before a later WM_PAINT redraws its text. the SHOTDC probe sends that erase and dumps the same DC
# synchronously, so a repaint cannot hide the blank frame. a second probe poisons the client before a real erased repaint, checking that
# deferring standalone erasure still clears stale text and backgrounds. both themes, selected text and direct editing are covered.
function Test-T37 {
    $app = Start-App
    $ed = Get-Edit $app
    if ((Snd $ed (0x8000 + 92) 0 0) -ne 0x4D494E54) {
        Skip 'T37 resize erase / repaint regression' 'build the probe: tools\probe.bat /DSHOTDC, run with -Exe build\probe\notepad-mint.exe'
        return
    }
    $w = 320; $h = 100
    $text = "resize keeps this text visible`r`nselected text and empty space`r`nlast row"
    $idx = 0
    foreach ($theme in @('dark', 'light')) {
        if ($theme -eq 'light') { [void](Snd $app.Main $WM_COMMAND $IDM.IDM_THEME_LIGHT 0) }
        Reset-Doc $app $text
        [void](Snd $ed 0x8 0 0)                                                 # no caret blink in the captured region; ES_NOHIDESEL keeps selection visible
        $paint = Ed-Direct $app $w $h 0 (0x8000 + 93)
        $s = Ed-Shot $app
        try {
            $c = $s.Bmp.GetPixel($s.Ox + $w - 1, $s.Oy + $h - 1)
            $bg = ([int]$c.R -shl 16) -bor ([int]$c.G -shl 8) -bor [int]$c.B
            $ink = [Bx]::NotColor($s.Bmp, $s.Ox, $s.Oy, $w, $h, $bg)
            $idx++; Ck ('T37.' + $idx + ' ' + $theme + ': the reference contains text') ($ink[0] -gt 100) ('ink pixels ' + $ink[0])
            $d = [Bx]::DiffPpm($paint.B, $paint.Off, $w, 0, $s.Bmp, $s.Ox, $s.Oy, 0, 0, $w, $h)
            $idx++; Ck ('T37.' + $idx + ' ' + $theme + ': actual WM_PAINT clears the poisoned background and draws every row') ($d[0] -eq 0) ('different pixels ' + $d[0])
            foreach ($erase in @(1, 2)) {                                       # resize commonly sends two separate erase messages before painting
                $blank = Ed-Direct $app $w $h 0 (0x8000 + 92)
                $d = [Bx]::DiffPpm($blank.B, $blank.Off, $w, 0, $s.Bmp, $s.Ox, $s.Oy, 0, 0, $w, $h)
                $idx++; Ck ('T37.' + $idx + ' ' + $theme + ': standalone erase ' + $erase + ' preserves the painted text until WM_PAINT') ($d[0] -eq 0) ('different pixels ' + $d[0])
            }
        } finally { $s.Bmp.Dispose() }

        [void](Ed-Direct $app $w $h 0 (0x8000 + 93))                              # restore the baseline's deliberately erased pixels before testing direct selection
        [void](Snd $ed $EM_SETSEL 0 36)
        $sel = Ed-Direct $app $w $h
        $s = Ed-Shot $app
        try {
            $d = [Bx]::DiffPpm($sel.B, $sel.Off, $w, 0, $s.Bmp, $s.Ox, $s.Oy, 0, 0, $w, $h)
            $idx++; Ck ('T37.' + $idx + ' ' + $theme + ': direct selection painting matches a complete repaint') ($d[0] -eq 0) ('different pixels ' + $d[0])
            $sel = Ed-Direct $app $w $h 0 (0x8000 + 92)
            $d = [Bx]::DiffPpm($sel.B, $sel.Off, $w, 0, $s.Bmp, $s.Ox, $s.Oy, 0, 0, $w, $h)
            $idx++; Ck ('T37.' + $idx + ' ' + $theme + ': standalone erase preserves the selection and selected line break') ($d[0] -eq 0) ('different pixels ' + $d[0])
        } finally { $s.Bmp.Dispose() }

        [void][U]::SndStr($ed, $EM_REPLACESEL, 1, '')
        $deleted = Ed-Direct $app $w $h
        $s = Ed-Shot $app
        try {
            $d = [Bx]::DiffPpm($deleted.B, $deleted.Off, $w, 0, $s.Bmp, $s.Ox, $s.Oy, 0, 0, $w, $h)
            $idx++; Ck ('T37.' + $idx + ' ' + $theme + ': direct deletion clears the removed characters and selection') ($d[0] -eq 0) ('different pixels ' + $d[0])
            $deleted = Ed-Direct $app $w $h 0 (0x8000 + 93)
            $d = [Bx]::DiffPpm($deleted.B, $deleted.Off, $w, 0, $s.Bmp, $s.Ox, $s.Oy, 0, 0, $w, $h)
            $idx++; Ck ('T37.' + $idx + ' ' + $theme + ': erased repaint leaves no removed characters or stale background') ($d[0] -eq 0) ('different pixels ' + $d[0])
        } finally { $s.Bmp.Dispose() }
    }
}

# =========================================================================================================== T38
# unlike T37's standalone erase, this observes the actual erase inside an invalidated WM_PAINT. the probe reads the live window after
# native erasure and before native text drawing; no extra erase is sent by the test. mixed-script fallback fonts make that gap visible.
function Test-T38 {
    $probe = Start-App
    if ((Snd (Get-Edit $probe) (0x8000 + 94) 0 0) -ne 0x4D494E54) {
        Skip 'T38 text remains visible inside an erased repaint' 'build the probe: tools\probe.bat /DSHOTDC, run with -Exe build\probe\notepad-mint.exe'
        return
    }
    Stop-App $probe
    $plain = Join-Path $work 'resize-plain.txt'
    [IO.File]::WriteAllText($plain, "plain text resize control`r`ntext stays visible during painting`r`n0123456789 abcdefghijklmnopqrstuvwxyz`r`nlast row", (New-Object Text.UTF8Encoding($false)))
    $idx = 0
    foreach ($theme in @('dark', 'light')) {
        foreach ($wrap in @($false, $true)) {
            foreach ($file in @($plain, $sampleMulti)) {
                $app = Start-App $file                                         # fresh profile and native control for every document/theme/wrap combination
                $ed = Get-Edit $app
                if ($theme -eq 'light') { [void](Snd $app.Main $WM_COMMAND $IDM.IDM_THEME_LIGHT 0) }
                if ($wrap) { [void](Snd $app.Main $WM_COMMAND $IDM.IDM_FMT_WRAP 0); $ed = Get-Edit $app }
                [void](Snd $ed 0x8 0 0)                                       # stop the caret blink; only text/background pixels are compared
                Start-Sleep -Milliseconds 300
                $w = 320; $h = 80; $y0 = 0
                $name = 'plain'
                if ($file -eq $sampleMulti) {
                    $name = 'multilingual'
                    $y0 = (Ed-Pos $app ([int](Snd $ed 0xBB 4 0)))[1]          # the Japanese, Chinese and Korean rows, rather than only the ASCII heading
                }
                $label = $theme + ', wrap ' + $wrap + ', ' + $name
                [void](Snd $ed (0x8000 + 94) $w ($h -bor ($y0 -shl 16)))       # settle initial theme/layout painting with the same ordinary erased repaint
                $before = Ed-Direct $app $w $h $y0
                $bg = $before.Off + (($h - 1) * $w + $w - 1) * 3
                $ink = 0
                for ($p = $before.Off; $p -lt $before.B.Length; $p += 3) {
                    if ($before.B[$p] -ne $before.B[$bg] -or $before.B[$p + 1] -ne $before.B[$bg + 1] -or $before.B[$p + 2] -ne $before.B[$bg + 2]) { $ink++ }
                }
                $idx++; Ck ('T38.' + $idx + ' ' + $label + ': the live reference contains painted text') ($ink -gt 100) ('ink pixels ' + $ink)
                $mid = Ed-Direct $app $w $h $y0 (0x8000 + 94)
                $idx++; Ck ('T38.' + $idx + ' ' + $label + ': the actual native erase was observed') ([bool]$mid) 'no snapshot from the actual WM_ERASEBKGND'
                if ($mid) {
                    $expected = [Convert]::ToBase64String($before.B, $before.Off, ($w * $h * 3))
                    $observed = [Convert]::ToBase64String($mid.B, $mid.Off, ($w * $h * 3))
                    $idx++; Ck ('T38.' + $idx + ' ' + $label + ': text stays visible while the new frame is erased and drawn') ($observed -ceq $expected) 'the live window changed before the complete frame was ready'
                    $after = Ed-Direct $app $w $h $y0
                    $finished = [Convert]::ToBase64String($after.B, $after.Off, ($w * $h * 3))
                    $idx++; Ck ('T38.' + $idx + ' ' + $label + ': the completed repaint preserves the same text and background') ($finished -ceq $expected) 'completed repaint pixels differ from the original'
                }
                Stop-App $app
            }
        }
    }
    foreach ($theme in @('dark', 'light')) {
        $app = Start-App $plain
        $ed = Get-Edit $app
        $cap = (Snd $ed (0x8000 + 95) 0 0) -eq 0x4D494E54
        $idx++; Ck ('T38.' + $idx + ' ' + $theme + ': the partial repaint probe is available') $cap 'no WM_APP + 95 probe'
        if (-not $cap) { Stop-App $app; return }
        if ($theme -eq 'light') { [void](Snd $app.Main $WM_COMMAND $IDM.IDM_THEME_LIGHT 0) }
        [void](Snd $ed 0x8 0 0)
        [void][U]::Repaint($ed, 0, 0, 320, 100)
        $before = Ed-Direct $app 320 100
        $partial = Ed-Direct $app 320 100 0 (0x8000 + 95)
        [byte[]]$marked = $before.B.Clone()
        $px = $before.Off + (96 * 320 + 316) * 3
        $marked[$px] = 255; $marked[$px + 1] = 0; $marked[$px + 2] = 255
        $expected = [Convert]::ToBase64String($marked, $before.Off, (320 * 100 * 3))
        $actual = [Convert]::ToBase64String($partial.B, $partial.Off, (320 * 100 * 3))
        $idx++; Ck ('T38.' + $idx + ' ' + $theme + ': a partial dirty rectangle restores its text and preserves every exterior pixel') ($actual -ceq $expected) 'partial repaint changed an exterior pixel or shifted/clipped text at its nonzero origin'
        [void][U]::Repaint($ed, 0, 0, 320, 100)
        $clean = Ed-Direct $app 320 100
        $expected = [Convert]::ToBase64String($before.B, $before.Off, (320 * 100 * 3))
        $actual = [Convert]::ToBase64String($clean.B, $clean.Off, (320 * 100 * 3))
        $idx++; Ck ('T38.' + $idx + ' ' + $theme + ': a complete repaint clears the exterior marker') ($actual -ceq $expected) 'full repaint left a stale marker'
        for ($j = 0; $j -lt 20; $j++) { [void][U]::Repaint($ed, 17, 19, 157, 51) }
        $start = [U]::GdiCount($app.Proc)
        $ok = $true
        for ($j = 0; $j -lt 200; $j++) { if (-not [U]::Repaint($ed, 17, 19, 157, 51)) { $ok = $false } }
        $end = [U]::GdiCount($app.Proc)
        $idx++; Ck ('T38.' + $idx + ' ' + $theme + ': 200 partial repaints release their DCs and bitmaps') ($ok -and $start -gt 0 -and $end -eq $start) ('GDI objects before ' + $start + ', after ' + $end + ', paint calls succeeded ' + $ok)
        Stop-App $app
    }
}

# ====================================================================================================== run them all
. (Join-Path $PSScriptRoot 'font_preview_test.ps1')
. (Join-Path $PSScriptRoot 'caret_position_test.ps1')
. (Join-Path $PSScriptRoot 'caret_visibility_test.ps1')
. (Join-Path $PSScriptRoot 'caret_scroll_test.ps1')
. (Join-Path $PSScriptRoot 'ui_resources_test.ps1')
. (Join-Path $PSScriptRoot 'rtl_test.ps1')
if ($NoRun) { return }
if ($deskName) { Info ('the app runs on a private desktop (' + $deskName + '): nothing shows on your screen and no keystroke can reach it (-Visible: real desktop)') }
else { Info 'the app runs on the real desktop: its windows pop up and TAKE THE FOREGROUND (it activates itself at startup): do not type until the run is over' }
try {
    foreach ($c in @('T1', 'T2', 'T3', 'T4', 'T5', 'T6', 'T7', 'T8', 'T9', 'T10', 'T11', 'T12', 'T13', 'T15', 'T16', 'T17', 'T18', 'T19', 'T20', 'T21', 'T22', 'T23', 'T24', 'T25', 'T26', 'T28', 'T29', 'T30', 'T31', 'T32', 'T33', 'T34', 'T35', 'T36', 'T37', 'T38', 'T39', 'T40', 'T41', 'T42', 'T43', 'T44', 'T45')) { Run-Case $c }
} finally {
    try { Stop-All } catch {}
    Kill-Mine
    try { [CV]::Shutdown() } catch { Fail 'isolated desktop worker cleanup' $_.Exception.Message }
    [U]::DropDesktop()
    $env:APPDATA = $origAppData
    $cleanupPath = [IO.Path]::GetFullPath($work)
    $tempParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if ((Split-Path $cleanupPath -Parent) -ne $tempParent -or (Split-Path $cleanupPath -Leaf) -notmatch '^npm_ui_[0-9a-f]{8}$') {
        throw ('refusing to remove unexpected UI-test directory: ' + $cleanupPath)
    }
    for ($try = 0; $try -lt 4; $try++) {
        Start-Sleep -Milliseconds 300
        Remove-Item -LiteralPath $cleanupPath -Recurse -Force -ErrorAction SilentlyContinue
        if (-not (Test-Path -LiteralPath $work)) { break }
    }
}
Write-Output ('SUMMARY: ' + $script:cnt.PASS + ' passed, ' + $script:cnt.FAIL + ' failed, ' + $script:cnt.SKIP + ' skipped')
if ($script:cnt.FAIL -gt 0) { exit 1 }
exit 0
