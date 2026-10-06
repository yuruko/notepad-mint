# ui_test.ps1 - functional GUI tests for "notepad mint": the real exe, driven ONLY with window messages
# (SendMessage / PostMessage to window handles). no SendKeys, no SetForegroundWindow, no real mouse: the user's own typing
# cannot leak into the app and the test cannot steal the keyboard.
#
#   powershell -NoProfile -File tests\ui\ui_test.ps1 [-Exe <path>] [-Only T2,T3] [-Dump] [-BigMB 20] [-Timeout 4000]
#
# - the exe is COPIED to a fresh temp dir first (the build may delete / rewrite build\notepad mint.exe while this runs),
#   and %APPDATA% is pointed at a fresh temp dir per launch, so the real settings.ini is never touched.
# - the app is started with SW_SHOWNOACTIVATE: its window appears without taking the foreground.
# - one PASS / FAIL / SKIP line per check, a summary line at the end, exit code 1 if anything failed.
# - only the processes started here (their exe lives in the temp dir) are killed, never another "notepad mint" instance.
# - command ids / control ids are parsed at run time from src\mp.h, src\find.c, src\filedlg.c, src\fontdlg.c, src\edit.c.
# - -Dump prints every child (class, id, enabled, rect, text) of each dialog the tests open.
# keep this file pure ascii (windows powershell 5.1 reads a bom-less file as ansi): non-ascii text is built from char codes.
param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\..\build\notepad mint.exe'),
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
    [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern IntPtr SendMessageTimeoutW(IntPtr h, uint m, IntPtr w, IntPtr l, uint fl, uint to, out IntPtr res);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageTimeoutW")] static extern IntPtr SendMessageTimeoutS(IntPtr h, uint m, IntPtr w, string l, uint fl, uint to, out IntPtr res);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageTimeoutW")] static extern IntPtr SendMessageTimeoutB(IntPtr h, uint m, IntPtr w, StringBuilder l, uint fl, uint to, out IntPtr res);
    [DllImport("user32.dll", EntryPoint = "SendMessageTimeoutW")] static extern IntPtr SendMessageTimeoutP(IntPtr h, uint m, ref uint w, ref uint l, uint fl, uint to, out IntPtr res);
    [DllImport("user32.dll")] static extern bool GetGUIThreadInfo(uint tid, ref GTI g);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
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
    [DllImport("user32.dll")] static extern bool IsZoomed(IntPtr h);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr h);
    public static bool Zoomed(long h) { return IsZoomed(H(h)); }
    public static int Dpi(long h) { return (int)GetDpiForWindow(H(h)); }
    public static int Id(long h) { return GetDlgCtrlID(H(h)); }
    public static long Item(long d, int id) { return GetDlgItem(H(d), id).ToInt64(); }
    public static uint Tid(long h) { uint p; return GetWindowThreadProcessId(H(h), out p); }
    public static int[] WRect(long h) { RECT r; GetWindowRect(H(h), out r); return new int[] { r.L, r.T, r.R, r.B }; }
    public static int[] CRect(long h) { RECT r; GetClientRect(H(h), out r); return new int[] { r.L, r.T, r.R, r.B }; }
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
$BOX_BTN2 = 102; $BOX_BTN3 = 103                                                 # MpAsk (ui.c MsgProc): the 1st button is IDOK, the 2nd 100+2, the 3rd 100+3

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
$IDO = Read-Consts (Join-Path $Src 'filedlg.c') $IDM                             # ID_PATH ... ID_ENCLIST, MI_*
$IDT = Read-Consts (Join-Path $Src 'fontdlg.c') $IDM                             # ID_LIST ... ID_RESET
$IDE = Read-Consts (Join-Path $Src 'edit.c') $IDM                                # IDC_EDIT
foreach ($need in @(@('IDM', $IDM, 'IDM_EDIT_FIND'), @('find.c', $IDF, 'ID_REPLACEALL'), @('filedlg.c', $IDO, 'MI_EOL'), @('fontdlg.c', $IDT, 'ID_RESET'), @('edit.c', $IDE, 'IDC_EDIT'))) {
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
function Get-Edit($app) {                                                        # re-found every time: toggling word wrap re-creates the control
    foreach ($k in [U]::Kids([long]$app.Main)) { if ([U]::Cls($k) -eq 'Edit') { return [long]$k } }
    throw 'the editor (class EDIT) is not a child of the main window'
}
function Ed-Text($app) { [U]::GetText((Get-Edit $app)) }
function Ed-Set($app, [string]$t) { [U]::SetText((Get-Edit $app), $t) }
function Ed-Modified($app) { (Snd (Get-Edit $app) $EM_GETMODIFY) -ne 0 }
function Ed-Dirty($app, [string]$t = 'x') { [void][U]::SndStr((Get-Edit $app), $EM_REPLACESEL, 1, $t) }   # a real edit: sets the modified flag + EN_CHANGE
function Title($app) { [U]::Text([long]$app.Main) }
function Default-Name($app) {                                                    # the unsaved document's default name, read from the title: "mint" + 4 characters of 0-9 a-z (or $null)
    if ((Title $app) -cmatch '^\*?(mint[0-9a-z]{4}) - notepad mint$') { return $Matches[1] }
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
function Ini-Val($app, [string]$sec, [string]$key, [string]$want = $null, [int]$ms = 6000) {   # polls settings.ini (written by the app, ~25 separate file rewrites) for key (= want); one run once saw a late italic=1 with a 2.5 s window
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
$exeCopy = Join-Path $work 'notepad mint.exe'
for ($try = 0; $try -lt 8; $try++) {                                             # the lead's build may be rewriting the exe right now
    try { Copy-Item -LiteralPath $Exe -Destination $exeCopy -Force; if ((Get-Item -LiteralPath $exeCopy).Length -gt 50000) { break } } catch {}
    Start-Sleep -Milliseconds 500
}
$script:apps = @()
$script:launchN = 0

function Start-App([string]$file = '', [string]$appdata = '') {
    if (-not $appdata) { $script:launchN++; $appdata = Join-Path $work ('appdata' + $script:launchN) }
    [void](New-Item -ItemType Directory -Force -Path $appdata)
    $env:APPDATA = $appdata                                                      # the app reads %APPDATA%\notepad mint\settings.ini (main.c IniLocate)
    $argLine = ''
    if ($file) { $argLine = '"' + $file + '"' }
    $procId = [U]::Launch($exeCopy, $argLine, $work)
    $proc = $null
    try { $proc = [Diagnostics.Process]::GetProcessById([int]$procId) } catch { throw 'the exe exited right after it was launched (not a gui app, or it crashed at startup)' }
    $app = [pscustomobject]@{ Pid = [uint32]$procId; Proc = $proc; AppData = $appdata
                              Ini = (Join-Path $appdata 'notepad mint\settings.ini'); Main = [long]0; Tid = [uint32]0 }
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
function Box-Press($box, [int]$id) {                                             # buttons: 1 = first (IDOK), 102 = second, 103 = third (ui.c MsgProc)
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
    Get-Process -Name 'notepad mint' -ErrorAction SilentlyContinue | Where-Object { try { $_.Path -like ($work + '*') } catch { $false } } | ForEach-Object { try { $_.Kill() } catch {} }
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

# ============================================================================================================ T5
function Test-T5 {                                                               # open
    $app = Start-App                                                             # untitled; the app's cwd is $work (what the open dialog starts in)
    Cmd $app 'IDM_FILE_OPEN'
    $dlg = Wait-Win $app 'mp_file' 'open'
    Pass 'T5.1 open dialog opens (class mp_file, title "open")'
    $look = Get-Field $dlg $IDO.ID_PATH
    Ck 'T5.2 the look-in edit starts at the current directory' ($look -ieq $work) ('look-in [' + $look + '] expected [' + $work + ']')
    Set-Field $dlg $IDO.ID_NAME $sampleWrap
    Press $dlg $IDOK
    Ck 'T5.3 ok on the full path of wordwrap-sample.txt closes the dialog' (Gone $dlg) 'the dialog is still visible'
    Ck 'T5.4 the title becomes "wordwrap-sample.txt - notepad mint"' (Wait-Title $app 'wordwrap-sample.txt - notepad mint') ('title [' + (Title $app) + ']')
    CkEdText 'T5.5 the editor text equals the file (utf-8, LF -> CRLF)' $app (Expected-Text $sampleWrap)

    Cmd $app 'IDM_FILE_OPEN'
    $dlg = Wait-Win $app 'mp_file' 'open'
    CkEq 'T5.6 the dialog starts in the folder of the open file' ((Split-Path $sampleWrap -Parent).ToLowerInvariant()) ((Get-Field $dlg $IDO.ID_PATH).ToLowerInvariant())
    Set-Field $dlg $IDO.ID_NAME 'no_such_file_xyz.txt'
    Press $dlg $IDOK
    $box = Wait-Box $app 'open'
    Ck 'T5.7 a missing file name: the message box says "file not found"' ($box.Text -like '*file not found*') ('box text [' + (Show $box.Text) + ']')
    Ck 'T5.8 ... the box closes with ok' (Box-Press $box $IDOK) 'box still visible'
    Ck 'T5.9 ... the open dialog stays' ([U]::Visible($dlg)) 'the dialog went away'
    CkEq 'T5.10 ... and the title is unchanged' 'wordwrap-sample.txt - notepad mint' (Title $app)
    Set-Field $dlg $IDO.ID_NAME $Tests
    Press $dlg $IDOK
    Ck 'T5.11 a directory path navigates: the look-in edit shows that folder' ([bool](WaitFor { (Get-Field $dlg $IDO.ID_PATH) -ieq $Tests } 2000)) ('look-in [' + (Get-Field $dlg $IDO.ID_PATH) + '] expected [' + $Tests + ']')
    Ck 'T5.12 ... the dialog stays open and the name box is cleared' ([U]::Visible($dlg) -and (Get-Field $dlg $IDO.ID_NAME) -eq '') ('name box [' + (Get-Field $dlg $IDO.ID_NAME) + ']')
    $lb = Ctl $dlg $IDO.ID_LIST
    Ck 'T5.13 ... and the list now shows that folder (the two samples + the ui and unit folders)' ((Snd $lb $LB_GETCOUNT) -ge 4) ('list has ' + (Snd $lb $LB_GETCOUNT) + ' items')
    Ck 'T5.14 ... including multilingual-sample.txt' (([U]::SndStr($lb, $LB_FINDSTRINGEXACT, -1, 'multilingual-sample.txt')) -ge 0) 'LB_FINDSTRINGEXACT did not find it'
    Set-Field $dlg $IDO.ID_NAME 'multilingual-sample.txt'
    Press $dlg $IDOK
    Ck 'T5.15 ok on a bare file name resolves it in the folder shown: title "multilingual-sample.txt - notepad mint"' (Wait-Title $app 'multilingual-sample.txt - notepad mint') ('title [' + (Title $app) + ']')
    CkEdText 'T5.16 ... and the editor text equals that file' $app (Expected-Text $sampleMulti)

    $t0 = Title $app; $x0 = Ed-Text $app
    Cmd $app 'IDM_FILE_OPEN'
    $dlg = Wait-Win $app 'mp_file' 'open'
    Set-Field $dlg $IDO.ID_NAME $sampleWrap                                      # typed, then cancelled
    Press $dlg $IDCANCEL
    Ck 'T5.17 cancel closes the dialog' (Gone $dlg) 'the dialog is still visible'
    Start-Sleep -Milliseconds 250
    CkEq 'T5.18 ... the title is unchanged' $t0 (Title $app)
    CkText 'T5.19 ... the text is unchanged' $x0 (Ed-Text $app)

    Ed-Dirty $app 'x'
    CkEq 'T5.20 an edit puts "*" in front of the title' '*multilingual-sample.txt - notepad mint' (Title $app)
    Cmd $app 'IDM_FILE_OPEN'
    $box = Wait-Box $app $AppName
    CkEq 'T5.21 open while modified asks (box title "notepad mint")' 'do you want to save changes to multilingual-sample.txt?' $box.Text
    Ck 'T5.22 ... answer cancel (3rd button): the box closes' (Box-Press $box $BOX_BTN3) 'box still visible'
    Start-Sleep -Milliseconds 300
    Ck 'T5.23 ... no open dialog follows and the document stays modified' (-not [U]::FindTop($app.Pid, 'mp_file', 'open') -and (Ed-Modified $app)) 'an open dialog appeared or the modified flag was lost'
    Cmd $app 'IDM_FILE_OPEN'
    $box = Wait-Box $app $AppName
    Ck 'T5.24 ... asked again; answer "don''t save" (2nd button): the box closes' (Box-Press $box $BOX_BTN2) 'box still visible'
    $dlg = Wait-Win $app 'mp_file' 'open'
    Pass 'T5.25 ... the open dialog follows'
    Set-Field $dlg $IDO.ID_NAME $sampleWrap
    Press $dlg $IDOK
    Ck 'T5.26 the file opens, the unsaved edit is discarded (title "wordwrap-sample.txt - notepad mint")' (Wait-Title $app 'wordwrap-sample.txt - notepad mint') ('title [' + (Title $app) + ']')
    CkEdText 'T5.27 ... editor text equals the file' $app (Expected-Text $sampleWrap)
    Ck 'T5.28 ... not modified' (-not (Ed-Modified $app)) 'EM_GETMODIFY is set'
}

# ============================================================================================================ T6
function Test-T6 {                                                               # save as
    $app = Start-App
    $text1 = 'h' + (Chars 0xE9) + 'llo ' + (Chars 0x65E5, 0x672C, 0x8A9E) + ' ' + [char]::ConvertFromUtf32(0x1F600) + "`r`n" + 'second line'
    Ed-Set $app $text1
    $base = Join-Path $work 'ui_t6'
    $f = $base + '.txt'

    Cmd $app 'IDM_FILE_SAVEAS'
    $dlg = Wait-Win $app 'mp_file' 'save as'
    Pass 'T6.1 save as dialog opens (class mp_file, title "save as")'
    Set-Field $dlg $IDO.ID_NAME $base                                            # full path, no extension
    Press $dlg $IDOK
    Ck 'T6.2 no extension typed: ".txt" is appended, title becomes "ui_t6.txt - notepad mint"' (Wait-Title $app 'ui_t6.txt - notepad mint') ('title [' + (Title $app) + ']')
    Ck 'T6.3 the file ui_t6.txt exists' (Test-Path -LiteralPath $f) ('missing: ' + $f)
    CkBytes 'T6.4 the file holds the text as utf-8 without bom, CRLF line breaks' (U8 $text1) $f
    Ck 'T6.5 saving cleared the modified flag' (-not (Ed-Modified $app)) 'EM_GETMODIFY is set'

    $text2 = 'second version ' + (Chars 0x65E5)
    Ed-Set $app $text2
    Cmd $app 'IDM_FILE_SAVEAS'
    $dlg = Wait-Win $app 'mp_file' 'save as'
    CkEq 'T6.6 the file name box is prefilled with the current name' 'ui_t6.txt' (Get-Field $dlg $IDO.ID_NAME)
    Set-Field $dlg $IDO.ID_NAME $f
    Press $dlg $IDOK
    $box = Wait-Box $app 'save as'
    Ck 'T6.7 saving over an existing file asks "already exists. do you want to replace it?"' ($box.Text -like '*already exists. do you want to replace it?*') ('box text [' + (Show $box.Text) + ']')
    Ck 'T6.8 answer no (2nd button): the box closes' (Box-Press $box $BOX_BTN2) 'box still visible'
    Start-Sleep -Milliseconds 250
    Ck 'T6.9 ... the save as dialog stays open' ([U]::Visible($dlg)) 'the dialog went away'
    CkBytes 'T6.10 ... and the file on disk is unchanged' (U8 $text1) $f 300
    Press $dlg $IDOK
    $box = Wait-Box $app 'save as'
    Ck 'T6.11 ok again asks again; answer yes (1st button): the box closes' (Box-Press $box $IDOK) 'box still visible'
    Ck 'T6.12 ... the dialog closes' (Gone $dlg) 'the dialog is still visible'
    CkBytes 'T6.13 ... and the new text is written' (U8 $text2) $f

    # encoding + line ending dropdowns: an mp_btn that runs the MenuPopup loop; the pick is driven with posted key messages
    $text3 = 'h' + (Chars 0xE9) + "llo`r`nline two"
    Ed-Set $app $text3
    Cmd $app 'IDM_FILE_SAVEAS'
    $dlg = Wait-Win $app 'mp_file' 'save as'
    $encBtn = Ctl $dlg $IDO.ID_ENC
    $eolBtn = Ctl $dlg $IDO.ID_EOL
    CkEq 'T6.14 the encoding dropdown shows the document encoding' 'utf-8' ([U]::Text($encBtn))
    CkEq 'T6.15 the line ending dropdown shows the document line ending' 'windows (crlf)' ([U]::Text($eolBtn))
    $drive = $true
    Press $dlg $IDO.ID_ENC
    $pop = WaitFor { [U]::FindTop($app.Pid, 'mp_popup', '') } 2500
    if (-not $pop) {
        Skip 'T6.16 encoding dropdown: down x3 + enter picks "utf-16 le"' 'the dropdown popup (class mp_popup) did not appear after BM_CLICK on the encoding button'
        Skip 'T6.17 line ending dropdown: down x2 + enter picks "unix (lf)"' 'popup not drivable (see T6.16)'
        $drive = $false
    } else {
        for ($i = 0; $i -lt 3; $i++) { Pst $pop $WM_KEYDOWN $VK_DOWN 0 }
        Pst $pop $WM_KEYDOWN $VK_RETURN 0
        Ck 'T6.16 encoding dropdown: down x3 + enter picks "utf-16 le" (the button shows it)' ([bool](WaitFor { [U]::Text($encBtn) -ceq 'utf-16 le' } 2500)) ('button text [' + [U]::Text($encBtn) + ']')
        [void](WaitFor { -not [U]::FindTop($app.Pid, 'mp_popup', '') } 2000)
        Press $dlg $IDO.ID_EOL
        $pop = WaitFor { [U]::FindTop($app.Pid, 'mp_popup', '') } 2500
        if (-not $pop) { Fail 'T6.17 line ending dropdown: down x2 + enter picks "unix (lf)"' 'the popup did not appear'; $drive = $false }
        else {
            for ($i = 0; $i -lt 2; $i++) { Pst $pop $WM_KEYDOWN $VK_DOWN 0 }
            Pst $pop $WM_KEYDOWN $VK_RETURN 0
            Ck 'T6.17 line ending dropdown: down x2 + enter picks "unix (lf)" (the button shows it)' ([bool](WaitFor { [U]::Text($eolBtn) -ceq 'unix (lf)' } 2500)) ('button text [' + [U]::Text($eolBtn) + ']')
        }
    }
    if ($drive) {
        $base2 = Join-Path $work 'ui_t6b'
        Set-Field $dlg $IDO.ID_NAME $base2
        Press $dlg $IDOK
        Ck 'T6.18 save: title becomes "ui_t6b.txt - notepad mint"' (Wait-Title $app 'ui_t6b.txt - notepad mint') ('title [' + (Title $app) + ']')
        $exp16 = [byte[]]([byte[]]@(0xFF, 0xFE) + [Text.Encoding]::Unicode.GetBytes(($text3 -replace "`r`n", "`n")))
        CkBytes 'T6.19 the file is utf-16 le: FF FE bom, LF only' $exp16 ($base2 + '.txt')
    } else { Press $dlg $IDCANCEL }
}

# ============================================================================================================ T7
function Test-T7 {                                                               # reopen with encoding
    $f = Join-Path $work 'ui_t7.txt'
    $bytes = [byte[]]@(0x63, 0x61, 0x66, 0xE9, 0x20, 0x61, 0x75, 0x20, 0x6C, 0x61, 0x69, 0x74, 0x0D, 0x0A, 0x6E, 0x61, 0xEF, 0x76, 0x65, 0x0D, 0x0A)   # "caf<e acute> au lait", "na<i diaeresis>ve" in windows-1252
    [IO.File]::WriteAllBytes($f, $bytes)
    $exp1252 = [regex]::Replace([Text.Encoding]::GetEncoding(1252).GetString($bytes), "\r\n|\r|\n", "`r`n")
    $fffd = [string][char]0xFFFD
    $expU8 = 'caf' + $fffd + " au lait`r`nna" + $fffd + "ve`r`n"
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
    CkEdText 'T7.10 reopened as utf-8: the invalid bytes show as U+FFFD' $app $expU8 3000

    Cmd $app 'IDM_ENC_REOPEN'
    $dlg = Wait-Win $app 'mp_enc' 'reopen with encoding'
    Press $dlg $IDCANCEL
    Ck 'T7.11 cancel closes the dialog' (Gone $dlg) 'the dialog is still visible'
    Start-Sleep -Milliseconds 250
    CkText 'T7.12 ... and the text is unchanged' $expU8 (Ed-Text $app)

    Ed-Dirty $app 'zz'                                                           # a modified document: reopening asks first
    $dirtyText = Ed-Text $app
    Cmd $app 'IDM_ENC_REOPEN'
    $dlg = Wait-Win $app 'mp_enc' 'reopen with encoding'
    [void](Snd (Ctl $dlg $IDO.ID_ENCLIST) $LB_SETCURSEL ([U]::ListFind((Ctl $dlg $IDO.ID_ENCLIST), 1252)) 0)
    Press $dlg $IDOK
    $box = Wait-Box $app $AppName
    Ck 'T7.13 reopening a modified document warns: "reopening the file will discard your unsaved changes."' ($box.Text -like 'reopening the file will discard your unsaved changes.*') ('box text [' + (Show $box.Text) + ']')
    Ck 'T7.14 ... answer no (2nd button): the box closes' (Box-Press $box $BOX_BTN2) 'box still visible'
    Start-Sleep -Milliseconds 250
    CkText 'T7.15 ... the edited text is kept' $dirtyText (Ed-Text $app)
    Cmd $app 'IDM_ENC_REOPEN'
    $dlg = Wait-Win $app 'mp_enc' 'reopen with encoding'
    [void](Snd (Ctl $dlg $IDO.ID_ENCLIST) $LB_SETCURSEL ([U]::ListFind((Ctl $dlg $IDO.ID_ENCLIST), 1252)) 0)
    Press $dlg $IDOK
    $box = Wait-Box $app $AppName
    Ck 'T7.16 ... asked again; answer yes (1st button): the box closes' (Box-Press $box $IDOK) 'box still visible'
    CkEdText 'T7.17 ... the file is reloaded as windows-1252' $app $exp1252 3000
    Ck 'T7.18 ... and the document is clean again' (-not (Ed-Modified $app)) 'EM_GETMODIFY is set'

    $app2 = Start-App                                                            # untitled: nothing to reopen
    Cmd $app2 'IDM_ENC_REOPEN'
    $box = Wait-Box $app2 $AppName
    CkEq 'T7.19 an untitled document says there is nothing to reopen' 'this document hasn''t been saved yet, so there is nothing to reopen.' $box.Text
    [void](Box-Press $box $IDOK)
}

function Read-Palette() {                                                        # the two palettes from the g_themes table in ui.c: face + editor background
    $t = [IO.File]::ReadAllText((Join-Path $Src 'ui.c'))
    $m = [regex]::Match($t, 'g_themes\[2\]\s*=\s*\{(.*?)\n\};', 'Singleline')
    if (-not $m.Success) { return $null }
    $c = @([regex]::Matches($m.Groups[1].Value, 'RGB\(\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*\)') | ForEach-Object {
        , @([Convert]::ToInt32($_.Groups[1].Value.Substring(2), 16), [Convert]::ToInt32($_.Groups[2].Value.Substring(2), 16), [Convert]::ToInt32($_.Groups[3].Value.Substring(2), 16)) })
    if ($c.Count -ne 28) { return $null }                                        # 2 themes x 14 colours (struct Palette in mp.h)
    return @{ dark = @{ face = $c[3]; edit = $c[13] }; light = @{ face = $c[17]; edit = $c[27] } }
}
function Get-Pixel($app, [string]$where) {                                       # a colour from PrintWindow of the main window (works when covered)
    $w = [U]::WRect([long]$app.Main)
    if ($where -eq 'edit') { $e = [U]::WRect((Get-Edit $app)); $fx = 0.85; $fy = 0.85 }
    else {
        $bar = $null
        foreach ($k in [U]::Kids([long]$app.Main)) { if ([U]::Cls($k) -eq 'mp_menubar') { $bar = $k } }
        if (-not $bar) { return $null }
        $e = [U]::WRect([long]$bar); $fx = 0.96; $fy = 0.4                       # the right end of the menu bar: plain chrome face
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
    Ck 'T8.0 no settings.ini yet in the fresh %APPDATA% (isolation)' (-not (Test-Path -LiteralPath $app.Ini)) ('unexpected ' + $app.Ini)
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
    $sizes = @(([regex]::Match([IO.File]::ReadAllText((Join-Path $Src 'fontdlg.c')), 'g_sizes\[\d+\]\s*=\s*\{([^}]*)\}').Groups[1].Value -split '\s*,\s*') | ForEach-Object { [int]$_.Trim() })
    $app = Start-App
    Ed-Set $app ("a`r`nb")
    $lh13 = Line-Height $app
    $dlg = Open-Font $app
    Pass 'T9.1 font dialog opens (class mp_font, title "font")'
    $defPt = [regex]::Match([IO.File]::ReadAllText((Join-Path $Src 'main.c')), 'g_pf\.pt\s*=\s*(\d+)\s*;').Groups[1].Value     # PrefsDefaults
    CkEq 'T9.2 the size box starts with the default size (PrefsDefaults)' $defPt (Get-Field $dlg $IDT.ID_SIZE)
    CkChk 'T9.3 "monospaced fonts only" is on by default' $dlg $IDT.ID_MONO 1
    Set-Field $dlg $IDT.ID_SIZE '18'
    Press $dlg $IDOK
    Ck 'T9.4 ok closes the dialog' (Gone $dlg) 'the dialog is still visible'
    CkEq 'T9.5 settings.ini has size=18 under [editor]' '18' (Ini-Val $app 'editor' 'size' '18')
    $lh18 = 0; [void](WaitFor { $script:lh = Line-Height $app; $script:lh -gt $lh13 } 2000); $lh18 = $script:lh
    Ck 'T9.6 the editor font really changed: the line height grew (13 pt -> 18 pt)' ($lh18 -gt $lh13) ('line height ' + $lh13 + ' px at 13 pt, ' + $lh18 + ' px at 18 pt')

    $dlg = Open-Font $app
    CkEq 'T9.7 reopened: the size box shows 18' '18' (Get-Field $dlg $IDT.ID_SIZE)
    Set-Field $dlg $IDT.ID_SIZE '5'
    Press $dlg $IDOK
    CkEq 'T9.8 size 5 clamps up to 10 (FONT_MIN) on ok' ([string]$IDM.FONT_MIN) (Ini-Val $app 'editor' 'size' ([string]$IDM.FONT_MIN))
    $dlg = Open-Font $app
    Set-Field $dlg $IDT.ID_SIZE '500'
    Press $dlg $IDOK
    CkEq 'T9.9 size 500 clamps down to 96 (FONT_MAX) on ok' ([string]$IDM.FONT_MAX) (Ini-Val $app 'editor' 'size' ([string]$IDM.FONT_MAX))
    $lh96 = 0; [void](WaitFor { $script:lh = Line-Height $app; $script:lh -gt $lh18 } 2000); $lh96 = $script:lh
    Ck 'T9.10 ... and the editor line height grew again at 96 pt' ($lh96 -gt $lh18) ('line height ' + $lh18 + ' px at 18 pt, ' + $lh96 + ' px at 96 pt')

    $dlg = Open-Font $app
    Set-Field $dlg $IDT.ID_SIZE '30'
    Press $dlg $IDCANCEL
    Ck 'T9.11 cancel closes the dialog' (Gone $dlg) 'the dialog is still visible'
    CkEq 'T9.12 ... and leaves the saved size unchanged (96)' ([string]$IDM.FONT_MAX) (Ini-Val $app 'editor' 'size' '30' 700)

    $dlg = Open-Font $app                                                        # a preset button, then face / bold / italic
    $p = 6
    Press $dlg ($IDT.ID_PRESET + $p)
    Ck ('T9.13 preset button #' + $p + ' puts ' + $sizes[$p] + ' in the size box') ([bool](WaitFor { (Get-Field $dlg $IDT.ID_SIZE) -eq [string]$sizes[$p] } 1500)) ('size box [' + (Get-Field $dlg $IDT.ID_SIZE) + ']')
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
    CkEq ('T9.16 ok writes the preset size ' + $sizes[$p]) ([string]$sizes[$p]) (Ini-Val $app 'editor' 'size' ([string]$sizes[$p]))
    if ($idx -ge 0) {
        CkEq 'T9.17 ... face Courier New' 'Courier New' (Ini-Val $app 'editor' 'font' 'Courier New')
        CkEq 'T9.18 ... bold=1' '1' (Ini-Val $app 'editor' 'bold' '1')
        CkEq 'T9.19 ... italic=1' '1' (Ini-Val $app 'editor' 'italic' '1')
    }

    $dlg = Open-Font $app
    Press $dlg $IDT.ID_RESET
    Ck 'T9.20 "reset": the size box shows 13' ([bool](WaitFor { (Get-Field $dlg $IDT.ID_SIZE) -eq '13' } 1500)) ('size box [' + (Get-Field $dlg $IDT.ID_SIZE) + ']')
    CkChk 'T9.21 ... bold is off again' $dlg $IDT.ID_BOLD 0
    Press $dlg $IDOK
    CkEq 'T9.22 ok after reset: size=13' '13' (Ini-Val $app 'editor' 'size' '13')
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
        }
        Pst $h $WM_CLOSE 0 0
        Ck ($s[0] + 'c it closes on WM_CLOSE (esc / cancel)') (Gone $h) 'the window is still visible'
        Ck ($s[0] + 'd the app is still running and answers') ((-not $app.Proc.HasExited) -and [U]::Responds($app.Main, 2000, $false)) 'the app died or hangs'
    }
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
    if ($state -eq 'exited') { Fail 'T11.1 the title shows the file name within 60 s' ('the app process exited during the load after ' + $ms + ' ms, exit code 0x' + ('{0:X}' -f $app.Proc.ExitCode) + ': a crash, or killed from outside (another harness killing "notepad mint" by name?)'); return }
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
function Expected-Name([DateTime]$t) {                                           # DefaultDocName (util.c): "mint" + base 36 of year + month*100 + day + seconds since midnight
    $v = ($t.Year + $t.Month * 100 + $t.Day + $t.Hour * 3600 + $t.Minute * 60 + $t.Second) % 1679616
    $d = '0123456789abcdefghijklmnopqrstuvwxyz'
    $s = ''
    for ($i = 0; $i -lt 4; $i++) { $s = [string]$d[$v % 36] + $s; $v = [int][Math]::Floor($v / 36) }
    return 'mint' + $s
}
function Test-T13 {                                                              # the default document name "mintXXXX"
    $t0 = Get-Date
    $app = Start-App
    $t1 = Get-Date
    $name = Default-Name $app
    Ck 'T13.1 an unsaved document is called "mint" + 4 characters from 0-9 a-z (title "mintXXXX - notepad mint")' ($name -ne $null) ('title [' + (Title $app) + ']')
    $cands = @()                                                                 # the app computed the name some time between just before the launch and the first title
    for ($t = $t0.AddSeconds(-1); $t -le $t1.AddSeconds(1); $t = $t.AddSeconds(1)) { $cands += (Expected-Name $t) }
    Ck 'T13.2 ... it is the base-36 form of year + month*100 + day + seconds since midnight (local time, +-1 s)' ($cands -contains $name) ('name ' + $name + ', the formula gives ' + (($cands | Select-Object -Unique) -join ' '))

    Start-Sleep -Milliseconds 2200                                               # a later second: a different name
    Cmd $app 'IDM_FILE_NEW'
    $new = $null
    [void](WaitFor { $script:n2 = Default-Name $app; $script:n2 -and ($script:n2 -cne $name) } 3000)
    $new = $script:n2
    Ck 'T13.3 file > new gives the new document its own default name (a later second, so a different one)' (($new -ne $null) -and ($new -cne $name)) ('before [' + $name + '] after [' + $new + ']')

    Cmd $app 'IDM_FILE_SAVEAS'
    $dlg = Wait-Win $app 'mp_file' 'save as'
    CkEq 'T13.4 the save as dialog proposes the default name' $new (Get-Field $dlg $IDO.ID_NAME)
    Press $dlg $IDOK                                                             # just accept it: ".txt" is added and the dialog's folder (the app's cwd = $work) is used
    $f = Join-Path $work ($new + '.txt')
    Ck 'T13.5 accepting it saves "mintXXXX.txt" and the title shows the file name' (Wait-Title $app ($new + '.txt - notepad mint')) ('title [' + (Title $app) + ']')
    Ck 'T13.6 ... the file exists in the dialog''s folder' (Test-Path -LiteralPath $f) ('missing: ' + $f)
}

# ====================================================================================================== run them all
if ($NoRun) { return }
if ($deskName) { Info ('the app runs on a private desktop (' + $deskName + '): nothing shows on your screen and no keystroke can reach it (-Visible: real desktop)') }
else { Info 'the app runs on the real desktop: its windows pop up and TAKE THE FOREGROUND (it activates itself at startup): do not type until the run is over' }
try {
    foreach ($c in @('T1', 'T2', 'T3', 'T4', 'T5', 'T6', 'T7', 'T8', 'T9', 'T10', 'T11', 'T12', 'T13')) { Run-Case $c }
} finally {
    try { Stop-All } catch {}
    Kill-Mine
    [U]::DropDesktop()
    $env:APPDATA = $origAppData
    for ($try = 0; $try -lt 4; $try++) {
        Start-Sleep -Milliseconds 300
        Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
        if (-not (Test-Path -LiteralPath $work)) { break }
    }
}
Write-Output ('SUMMARY: ' + $script:cnt.PASS + ' passed, ' + $script:cnt.FAIL + ' failed, ' + $script:cnt.SKIP + ' skipped')
if ($script:cnt.FAIL -gt 0) { exit 1 }
exit 0
