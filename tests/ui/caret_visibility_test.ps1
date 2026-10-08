# Loaded by ui_test.ps1. Observe the operating system's real caret, rather than
# inferring visibility from a character position (which fails at EOF and with bidi).
# All input stays on the harness's private desktop and uses window messages.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Threading;
public static class CV {
    [StructLayout(LayoutKind.Sequential)] struct R { public int l, t, r, b; }
    [StructLayout(LayoutKind.Sequential)] struct G {
        public int cb, flags;
        public IntPtr active, focus, capture, menu, move, caret;
        public R rect;
    }
    [DllImport("user32.dll")] static extern bool GetGUIThreadInfo(uint tid, ref G g);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out R r);
    [DllImport("user32.dll")] static extern int GetWindowLongW(IntPtr h, int index);
    [DllImport("user32.dll")] static extern IntPtr SendMessageTimeoutW(IntPtr h, uint m, IntPtr w, ref R r, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr OpenDesktopW(string name, uint flags, bool inherit, uint access);
    [DllImport("user32.dll", SetLastError=true)] static extern bool SetThreadDesktop(IntPtr desk);
    [DllImport("user32.dll")] static extern IntPtr GetThreadDesktop(uint tid);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desk);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] static extern bool AttachThreadInput(uint own, uint target, bool attach);
    [DllImport("user32.dll")] static extern IntPtr SetActiveWindow(IntPtr h);
    [DllImport("user32.dll")] static extern IntPtr SetFocus(IntPtr h);
    static AutoResetEvent request = new AutoResetEvent(false), finished = new AutoResetEvent(false);
    static Thread worker;
    static Action action;
    static Exception error;
    public static void OnDesktop(string name) {
        if (worker != null) return;
        error = null;
        request.Reset(); finished.Reset(); action = null;
        worker = new Thread(delegate() {
            IntPtr desk = IntPtr.Zero, original = GetThreadDesktop(GetCurrentThreadId());
            try {
                if (!String.IsNullOrEmpty(name)) {
                    desk = OpenDesktopW(name, 0, false, 0x10000000);
                    if (desk == IntPtr.Zero || !SetThreadDesktop(desk)) throw new Exception("cannot enter the isolated desktop: " + Marshal.GetLastWin32Error());
                }
                finished.Set();
                while (true) {
                    request.WaitOne();
                    if (action == null) break;
                    try { action(); } catch (Exception ex) { error = ex; }
                    finished.Set();
                }
            } catch (Exception ex) { error = ex; finished.Set(); }
            finally {
                if (desk != IntPtr.Zero) { SetThreadDesktop(original); CloseDesktop(desk); }
            }
        });
        worker.IsBackground = true; worker.Start();
        if (!finished.WaitOne(8000)) throw new Exception("isolated desktop worker did not start within 8 seconds");
        if (error != null) throw error;
    }
    public static void Shutdown() {
        if (worker == null) return;
        action = null; request.Set();
        if (!worker.Join(8000)) throw new Exception("isolated desktop worker did not stop within 8 seconds");
        worker = null;
    }
    static void OnWorker(Action work) {
        if (worker == null || !worker.IsAlive) throw new Exception("isolated desktop worker is not running");
        error = null; action = work; request.Set();
        if (!finished.WaitOne(8000)) throw new Exception("isolated desktop worker did not answer within 8 seconds");
        if (error != null) throw error;
    }
    public static void Focus(uint tid, long main, long edit) {
        OnWorker(delegate() {
            uint own = GetCurrentThreadId();
            if (!AttachThreadInput(own, tid, true)) throw new Exception("cannot attach to the isolated app's input queue");
            try { SetActiveWindow(new IntPtr(main)); SetFocus(new IntPtr(edit)); }
            finally { AttachThreadInput(own, tid, false); }
        });
    }
    public static int[] Snapshot(uint tid, long edit, int trim) {
        int[] result = null;
        OnWorker(delegate() { result = ReadSnapshot(tid, edit, trim); });
        return result;
    }
    public static int[] Format(long edit) {
        R r = new R(); IntPtr result;
        if (SendMessageTimeoutW(new IntPtr(edit), 0xB2, IntPtr.Zero, ref r, 2, 8000, out result) == IntPtr.Zero) throw new Exception("format rectangle query timed out");
        return new int[] { r.l, r.t, r.r, r.b };
    }
    static int[] ReadSnapshot(uint tid, long edit, int trim) {
        G g = new G(); g.cb = Marshal.SizeOf(typeof(G));
        R r;
        IntPtr h = new IntPtr(edit);
        if (!GetGUIThreadInfo(tid, ref g) || !GetClientRect(h, out r)) return new int[0];
        int style = GetWindowLongW(h, -16);
        if ((style & 0x200000) == 0) r.r -= trim;
        if ((style & 0x100000) == 0) r.b -= trim;
        return new int[] { g.caret == h ? 1 : 0, g.focus == h ? 1 : 0,
            g.rect.l, g.rect.t, g.rect.r, g.rect.b, r.l, r.t, r.r, r.b };
    }
    public static bool Visible(int[] q) {
        if (q.Length != 10 || q[0] != 1 || q[1] != 1 || q[4] <= q[2] || q[5] <= q[3]) return false;
        // A font can be taller than the smallest supported window. In that case
        // require an intersection on that axis; otherwise require the whole caret.
        bool x = q[4] - q[2] > q[8] - q[6] ? q[4] > q[6] && q[2] < q[8] : q[2] >= q[6] && q[4] <= q[8];
        bool y = q[5] - q[3] > q[9] - q[7] ? q[5] > q[7] && q[3] < q[9] : q[3] >= q[7] && q[5] <= q[9];
        return x && y;
    }
}
'@

function Caret-Snapshot($app) { return ,([CV]::Snapshot($app.Tid, (Get-Edit $app), (Trim-Px $app))) }
function Caret-StableSnapshot($app) {
    # Cross-thread OS queries can see UpdateBars' temporary native layout, even
    # while our UI thread is still completing one resize. Observe convergence
    # without sending an editor message that could itself repair the caret.
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $sameSince = -1L; $last = ''
    do {
        $q = Caret-Snapshot $app
        $key = $q -join ','
        if (-not [CV]::Visible($q) -or $key -cne $last) { $sameSince = $watch.ElapsedMilliseconds }
        elseif ($watch.ElapsedMilliseconds - $sameSince -ge 100) { return ,$q }
        $last = $key
        Start-Sleep -Milliseconds 10
    } while ($watch.ElapsedMilliseconds -lt 3000)
    return ,$q
}
function Ck-CaretVisible([string]$name, $app) {
    $q = Caret-StableSnapshot $app
    Ck $name ([CV]::Visible($q)) ('owner/focus, caret, viewport: ' + ($q -join ',') + '; format ' + ([CV]::Format((Get-Edit $app)) -join ','))
}
function Caret-Place($app, [int]$index) {
    $ed = Get-Edit $app
    [void](Snd $ed $EM_SETSEL $index $index)
    [void](Snd $ed 0xB7 0 0)                                                  # EM_SCROLLCARET: establish a visible starting position
}

function Test-T41 {
    $app = Start-App
    $ed = Get-Edit $app
    try {
    [CV]::OnDesktop($deskName)
    [CV]::Focus($app.Tid, $app.Main, $ed)
    $line = ('abcdef0123456789 ' * 14)
    $text = ((0..79 | ForEach-Object { $line }) -join "`r`n")
    $starts = Line-Starts $text
    [void][Nd]::Size($app.Main, 960, 700)
    Reset-Doc $app $text
    Caret-Place $app ($starts[24] + 90)
    Ck-CaretVisible 'T41.1 caret starts visible in a wide, tall editor' $app
    [void][Nd]::Size($app.Main, 430, 700)
    Ck-CaretVisible 'T41.2 width shrink reveals a caret to the right' $app
    [void][Nd]::Size($app.Main, 430, 240)
    Ck-CaretVisible 'T41.3 height shrink reveals a caret below the viewport' $app
    CkEdText 'T41.4 resize preserves every character' $app $text
    Ck 'T41.5 resize preserves a clean document and empty undo history' (-not (Ed-Modified $app) -and (Snd $ed $EM_CANUNDO) -eq 0) 'modified or undo state changed'

    # Layout work must preserve native backward-selection orientation and undo.
    [void](Snd $ed $EM_SETSEL $text.Length $text.Length)
    Ed-Dirty $app '!'
    $a = $starts[50] + 12; $b = $starts[52] + 25
    [void](Snd $ed $EM_SETSEL $b $a)
    [void](Snd $ed 0xB7 0 0)
    foreach ($size in @(@(850, 600), @(400, 200), @(960, 650), @(430, 240))) {
        [void][Nd]::Size($app.Main, $size[0], $size[1])
    }
    Ck-CaretVisible 'T41.6 resize keeps the active end of a backward selection visible' $app
    CkSel 'T41.7 resize preserves backward-selection bounds' $app $a $b
    $q = Caret-Snapshot $app; $pos = Ed-Pos $app $a
    Ck 'T41.8 backward selection still has its caret at the lower endpoint' ([Math]::Abs($q[2] - $pos[0]) -le 1 -and $q[3] -eq $pos[1]) ('caret ' + ($q[2..5] -join ',') + ', lower endpoint ' + ($pos -join ','))
    $idx = 8
    foreach ($cmd in @('IDM_ZOOM_IN', 'IDM_ZOOM_IN', 'IDM_ZOOM_OUT', 'IDM_ZOOM_RESET')) {
        [void](Snd $app.Main $WM_COMMAND $IDM[$cmd] 0)
        $idx++; Ck-CaretVisible ('T41.' + $idx + ' ' + $cmd + ' reveals the selected caret') $app
    }
    CkSel 'T41.13 zoom preserves selection bounds' $app $a $b
    $q = Caret-Snapshot $app; $pos = Ed-Pos $app $a
    Ck 'T41.14 zoom preserves the backward selection active end' ([Math]::Abs($q[2] - $pos[0]) -le 1 -and $q[3] -eq $pos[1]) ('caret ' + ($q[2..5] -join ',') + ', lower endpoint ' + ($pos -join ','))
    Ck 'T41.15 layout preserves text, modified state, and an available undo step' ((Ed-Text $app) -ceq ($text + '!') -and (Ed-Modified $app) -and (Snd $ed $EM_CANUNDO) -ne 0) 'text, modified state or undo changed'
    [void](Snd $ed $EM_UNDO 0 0)
    CkEdText 'T41.16 the pre-layout undo step still restores the document' $app $text

    # Mouse-wheel modifier bits travel with the message, avoiding global keyboard state.
    Caret-Place $app ($starts[45] + 150)
    $font = [FP]::Font($ed)
    [void](Snd $ed 0x20A ((120 -shl 16) -bor 8) 0)
    Ck-CaretVisible 'T41.17 Ctrl+wheel zoom in reveals the caret' $app
    Ck 'T41.18 Ctrl+wheel actually increases the native font size' (([FP]::Font($ed)).height -lt $font.height) 'font did not grow'
    [void](Snd $ed 0x20A ((-120 -shl 16) -bor 8) 0)
    Ck-CaretVisible 'T41.19 Ctrl+wheel zoom out reveals the caret' $app
    CkEq 'T41.20 opposite wheel steps restore the font size' $font.height (([FP]::Font($ed)).height)

    $mixed = (Chars @(0x65E5, 0x672C, 0x8A9E, 0x4E2D, 0x6587, 0xD83D, 0xDE00, 0x65, 0x0301)) + " mixed text `t "
    $wrapped = (($mixed * 40 + "`r`n") * 65) + 'last line'
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_FMT_WRAP 0)
    $ed = Get-Edit $app
    Reset-Doc $app $wrapped
    [void][Nd]::Size($app.Main, 960, 650)
    Caret-Place $app ([int]($wrapped.Length * 0.72))
    [void][Nd]::Size($app.Main, 390, 220)
    Ck-CaretVisible 'T41.21 wrapping multilingual text remains at the caret after shrinking' $app
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_ZOOM_IN 0)
    Ck-CaretVisible 'T41.22 wrapped multilingual text reveals the caret after zooming' $app
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_TAB_8 0)
    Ck-CaretVisible 'T41.23 changing tab layout also reveals the caret' $app
    CkEdText 'T41.24 wrap, zoom and tab layout preserve Unicode text' $app $wrapped

    $rtl = ((Chars @(0x05E9, 0x05DC, 0x05D5, 0x05DD, 0x20, 0x0627, 0x0644, 0x0639, 0x0631, 0x0628, 0x064A, 0x0629)) + ' 01234 ') * 40
    $rtl = ($rtl + "`r`n") * 35
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_RTL 0)
    Reset-Doc $app $rtl
    [void][Nd]::Size($app.Main, 920, 620)
    Caret-Place $app ([int]($rtl.Length * 0.6))
    [void][Nd]::Size($app.Main, 380, 205)
    Ck-CaretVisible 'T41.25 right-to-left wrapped text reveals the native caret after shrinking' $app
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_ZOOM_IN 0)
    Ck-CaretVisible 'T41.26 right-to-left text reveals the caret after zooming' $app
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_RTL 0)
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_FMT_WRAP 0)
    $ed = Get-Edit $app

    $idx = 26
    foreach ($doc in @('last nonempty row', "first`r`nlast row`r`n", '')) {
        Reset-Doc $app $doc
        Caret-Place $app $doc.Length
        [void][Nd]::Size($app.Main, 850, 600)
        [void][Nd]::Size($app.Main, 350, 160)
        $idx++; Ck-CaretVisible ('T41.' + $idx + ' EOF / trailing empty row / empty document survives resize') $app
        [void](Snd $app.Main $WM_COMMAND $IDM.IDM_ZOOM_IN 0)
        $idx++; Ck-CaretVisible ('T41.' + $idx + ' EOF / trailing empty row / empty document survives zoom') $app
    }

    # Keep zooming to the maximum in the minimum-height viewport: a whole caret
    # cannot fit there, but its editing position must still intersect the screen.
    for ($i = 0; $i -lt 35; $i++) { [void](Snd $app.Main $WM_COMMAND $IDM.IDM_ZOOM_IN 0) }
    [void][Nd]::Size($app.Main, 320, 140)
    Ck-CaretVisible 'T41.33 a maximum-size caret intersects a viewport shorter than one text row' $app
    [void](Snd $ed 0x102 ([int][char]'x') 0)                                  # WM_CHAR: native typing can park an oversized caret off screen again
    Ck-CaretVisible 'T41.44 typing keeps the oversized native caret visible' $app
    CkEdText 'T41.45 typing at the oversized caret edits the expected position' $app 'x'
    [void](Snd $ed $WM_KEYDOWN 0x25 0)                                       # VK_LEFT
    Ck-CaretVisible 'T41.46 navigation keeps the oversized caret visible' $app
    CkSel 'T41.47 left-arrow moves to the correct logical position' $app 0 0
    [void](Snd $ed $EM_UNDO 0 0)
    Ck-CaretVisible 'T41.48 undo keeps the oversized caret visible' $app
    CkEdText 'T41.49 native undo still restores text in a tiny viewport' $app ''

    # Compare with the control's own X at the same font and width. This reference
    # catches a visually plausible but wrong EOF position in bidi or a cluster.
    $tinyIdx = 49
    $eofDocs = @(
        @{ Name = 'Latin'; Text = 'abc' },
        @{ Name = 'CJK'; Text = (Chars @(0x65E5, 0x672C)) },
        @{ Name = 'emoji'; Text = (Chars @(0xD83D, 0xDE00)) },
        @{ Name = 'combining mark'; Text = (Chars @(0x65, 0x0301)) },
        @{ Name = 'Hebrew'; Text = (Chars @(0x05E9, 0x05DC, 0x05D5, 0x05DD)) },
        @{ Name = 'Arabic'; Text = (Chars @(0x0627, 0x0644, 0x0639, 0x0631, 0x0628, 0x064A, 0x0629)) },
        @{ Name = 'trailing tab'; Text = "ab`t" },
        @{ Name = 'trailing empty row'; Text = "abc`r`n" }
    )
    foreach ($rtlMode in @($false, $true)) {
        if ($rtlMode) { [void](Snd $app.Main $WM_COMMAND $IDM.IDM_RTL 0) }
        foreach ($doc in $eofDocs) {
            Reset-Doc $app $doc.Text
            [void][Nd]::Size($app.Main, 360, 550)
            Caret-Place $app $doc.Text.Length
            $native = Caret-StableSnapshot $app
            [void][Nd]::Size($app.Main, 360, 140)
            $clipped = Caret-StableSnapshot $app
            $expectedX = $native[2]
            if ($rtlMode) { $expectedX += $clipped[8] - $native[8] }            # a new left scrollbar moves the RTL client origin; retain the native offset from the right edge
            $tinyIdx++; Ck ('T41.' + $tinyIdx + ' ' + $doc.Name + ', RTL ' + $rtlMode + ': tiny EOF matches the native tall-window caret X') ([CV]::Visible($clipped) -and $expectedX -eq $clipped[2]) ('native ' + ($native -join ',') + ', expected X ' + $expectedX + ', clipped ' + ($clipped -join ','))
        }
        if ($rtlMode) {
            foreach ($suffix in @(' ', '.')) {
                $doc = (Chars @(0x05E9, 0x05DC, 0x05D5, 0x05DD)) + $suffix
                Reset-Doc $app $doc
                [void][Nd]::Size($app.Main, 450, 550)
                Caret-Place $app $doc.Length
                $native = Caret-StableSnapshot $app
                [void][Nd]::Size($app.Main, 450, 140)
                $clipped = Caret-StableSnapshot $app
                $tinyIdx++; Ck ('T41.' + $tinyIdx + ' RTL trailing neutral: tiny EOF matches the native caret X') ([CV]::Visible($clipped) -and $native[2] -eq $clipped[2]) ('native ' + ($native -join ',') + ', clipped ' + ($clipped -join ','))
            }
            [void](Snd $app.Main $WM_COMMAND $IDM.IDM_RTL 0)
        }
    }
    $selectionText = "first`r`nsecond`r`nlast"
    Reset-Doc $app $selectionText
    [void][Nd]::Size($app.Main, 450, 550)
    [void](Snd $ed $EM_SETSEL $selectionText.Length 1)
    [void](Snd $ed 0xB7 0 0)
    $backwardNative = Caret-StableSnapshot $app
    [void][Nd]::Size($app.Main, 450, 140)
    [void](WaitFor { [CV]::Visible((Caret-Snapshot $app)) } 2000)
    Ck-CaretVisible 'T41.68 backward selection from EOF stays visible in the tiny viewport' $app
    $backwardTiny = Caret-StableSnapshot $app; $format = [CV]::Format($ed)
    Ck 'T41.78 oversized backward selection displays the native active endpoint' ($backwardTiny[2] -eq $backwardNative[2] -and $backwardTiny[3] -eq $format[1]) ('native ' + ($backwardNative -join ',') + ', tiny ' + ($backwardTiny -join ',') + ', format ' + ($format -join ','))
    $client = [U]::CRect($ed)
    [void][U]::Repaint($ed, 0, 0, $client[2], $client[3])
    $backwardTiny = Caret-StableSnapshot $app
    Ck 'T41.79 repaint preserves the backward selection editing position' ([CV]::Visible($backwardTiny) -and $backwardTiny[2] -eq $backwardNative[2] -and $backwardTiny[3] -eq $format[1]) ('native ' + ($backwardNative -join ',') + ', tiny ' + ($backwardTiny -join ','))
    Ck 'T41.69 status retains the logical active endpoint of the oversized backward selection' ((Status-Text $app) -like '1:2 *') (Status-Text $app)
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_EDIT_CLEARLINE 0)
    CkEdText 'T41.70 Ctrl+K clears the active logical line of the oversized backward selection' $app "`r`nsecond`r`nlast"
    [void](Snd $ed $EM_UNDO 0 0)
    CkEdText 'T41.71 undo restores that complete logical line' $app $selectionText

    # Zoom while already tiny must compute the new EOF position, rather than
    # reuse an advance measured at the previous font size.
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_ZOOM_OUT 0)
    Reset-Doc $app (Chars @(0x0627, 0x0644, 0x0639, 0x0631, 0x0628, 0x064A, 0x0629))
    Caret-Place $app ((Ed-Text $app).Length)
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_ZOOM_IN 0)
    $zoomed = Caret-StableSnapshot $app
    [void][Nd]::Size($app.Main, 450, 550)
    Caret-Place $app ((Ed-Text $app).Length)
    $native = Caret-StableSnapshot $app
    Ck 'T41.72 zooming Arabic EOF while already tiny matches native X at the new font' ([CV]::Visible($zoomed) -and $zoomed[2] -eq $native[2]) ('tiny zoom ' + ($zoomed -join ',') + ', native ' + ($native -join ','))
    [void][Nd]::Size($app.Main, 360, 140)
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_TAB_4 0)
    Reset-Doc $app "ab`t"
    Caret-Place $app 3
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_TAB_8 0)
    $changedTab = Caret-StableSnapshot $app
    [void][Nd]::Size($app.Main, 360, 550)
    Caret-Place $app 3
    $native = Caret-StableSnapshot $app
    Ck 'T41.73 changing tab size while tiny matches native EOF X with horizontal scrolling' ([CV]::Visible($changedTab) -and $changedTab[2] -eq $native[2]) ('tiny tab ' + ($changedTab -join ',') + ', native ' + ($native -join ','))
    $tinyScrollText = ("scroll marker`r`n" * 30)
    Reset-Doc $app $tinyScrollText
    [void][Nd]::Size($app.Main, 360, 140)
    Caret-Place $app $tinyScrollText.Length
    Ck-CaretVisible 'T41.74 oversized caret is visible before the deliberate scroll' $app
    [void](Snd $ed 0x115 6 0)                                                 # scroll to the top, leaving the logical caret near EOF
    $client = [U]::CRect($ed)
    [void][U]::Repaint($ed, 0, 0, $client[2], $client[3])
    Start-Sleep -Milliseconds 150
    Ck 'T41.75 repaint and native maintenance preserve manual scrolling with an oversized caret' ((Snd $ed 0xCE 0 0) -eq 0 -and -not [CV]::Visible((Caret-Snapshot $app))) ('first row ' + (Snd $ed 0xCE 0 0) + ', caret ' + ((Caret-Snapshot $app) -join ','))
    [void](Snd $ed $WM_KEYDOWN 0x25 0)
    Ck-CaretVisible 'T41.76 real navigation restores oversized caret visibility after the manual scroll' $app
    CkEdText 'T41.77 manual scrolling, painting and navigation preserve the text' $app $tinyScrollText
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_ZOOM_RESET 0)

    # Bar visibility changes cause extra internal resizes; they must settle with
    # the caret visible and must never turn later manual scrolling into a reveal.
    Reset-Doc $app (('bar convergence text ' * 4 + "`r`n") * 12)
    Caret-Place $app ((Ed-Text $app).Length)
    [void][Nd]::Size($app.Main, 380, 220)
    Ck-CaretVisible 'T41.34 appearing scrollbars leave the caret visible' $app
    [void][Nd]::Size($app.Main, 1200, 750)
    Ck-CaretVisible 'T41.35 disappearing scrollbars leave the caret visible' $app
    Ck 'T41.36 both scrollbars disappear after growing past the document' (([U]::Style($ed) -band 0x300000) -eq 0) ('style ' + [U]::Style($ed))
    Reset-Doc $app $text
    [void][Nd]::Size($app.Main, 430, 240)
    Caret-Place $app ($starts[65] + 100)
    Ck-CaretVisible 'T41.37 caret is revealed before the deliberate manual scroll' $app
    [void](Snd $ed 0x115 6 0)                                                 # WM_VSCROLL / SB_TOP
    $first = Snd $ed 0xCE 0 0                                                # EM_GETFIRSTVISIBLELINE
    Start-Sleep -Milliseconds 250
    Ck 'T41.38 manual vertical scroll does not snap back to the caret' ($first -eq 0 -and (Snd $ed 0xCE 0 0) -eq $first -and -not [CV]::Visible((Caret-Snapshot $app))) ('first row ' + (Snd $ed 0xCE 0 0))
    [void](Snd $ed 0x114 6 0)                                                 # WM_HSCROLL / SB_LEFT
    $x = [U]::Scroll($ed, 0)[3]
    Start-Sleep -Milliseconds 250
    Ck 'T41.39 manual horizontal scroll does not snap back to the caret' ($x -eq 0 -and [U]::Scroll($ed, 0)[3] -eq $x) ('horizontal offset ' + [U]::Scroll($ed, 0)[3])

    # A 1,600-line wrapped document makes the resize throttle relevant. Check
    # each synchronous resize before its 40-ms timer can conceal a clipped caret.
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_FMT_WRAP 0)
    $ed = Get-Edit $app
    $large = ((('rapid resize words ' * 14) + "`r`n") * 1600)
    Reset-Doc $app $large
    [void][Nd]::Size($app.Main, 1050, 720)
    Caret-Place $app ([int]($large.Length * 0.8))
    $miss = New-Object Collections.Generic.List[string]
    for ($i = 0; $i -lt 32; $i++) {
        $w = 1050 - ($i % 16) * 42; $h = 720 - ($i % 16) * 31
        [void][Nd]::Size($app.Main, $w, $h)
        $q = Caret-Snapshot $app
        if (-not [CV]::Visible($q)) { $miss.Add(('size ' + $w + 'x' + $h + ': ' + ($q -join ','))) }
    }
    Ck 'T41.40 each rapid large-document resize keeps the caret on screen immediately' ($miss.Count -eq 0) ($miss -join '; ')
    Ck-CaretVisible 'T41.41 deferred wrapping and final scrollbar layout converge at the caret' $app
    CkEdText 'T41.42 repeated large-document wrapping preserves every character' $app $large
    Ck 'T41.43 repeated layout keeps the large document clean with no undo history' (-not (Ed-Modified $app) -and (Snd $ed $EM_CANUNDO) -eq 0) 'modified or undo state changed'
    } finally { [CV]::Shutdown() }
}
