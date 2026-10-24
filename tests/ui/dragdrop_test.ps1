# Loaded by ui_test.ps1. drag and drop (dnd.c, main.c AppOpenFiles, edit.c drag start).
# T24: WM_DROPFILES, what the shell posts to a window that has no ole drop target (and what this harness can send from another process):
#      a plain drop opens the first file in this window after the save prompt, the others in new windows; folders are skipped.
#      shift held while dropping inserts the paths instead: that reads the real key state, which messages cannot set (T46 covers it).
# T46: the ole drop target and the text drag source, driven through the probe build's WM_COPYDATA hook (DndProbe in dnd.c): a real ole drag
#      needs the real mouse. the hook calls the same DragEnter / DragOver / Drop methods ole would call, with a data object built in the app.
# keep this file pure ascii: non-ascii text is built from char codes.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class Dp {
    [DllImport("kernel32.dll")] static extern IntPtr GlobalAlloc(uint f, UIntPtr n);
    [DllImport("kernel32.dll")] static extern IntPtr GlobalLock(IntPtr h);
    [DllImport("kernel32.dll")] static extern bool GlobalUnlock(IntPtr h);
    public static long Make(string[] paths) {
        StringBuilder sb = new StringBuilder();
        foreach (string q in paths) { sb.Append(q); sb.Append('\0'); }
        sb.Append('\0');
        byte[] data = Encoding.Unicode.GetBytes(sb.ToString());
        IntPtr h = GlobalAlloc(0x2042, new UIntPtr((uint)(20 + data.Length)));
        IntPtr p = GlobalLock(h);
        Marshal.WriteInt32(p, 0, 20);
        Marshal.WriteInt32(p, 16, 1);
        Marshal.Copy(data, 0, new IntPtr(p.ToInt64() + 20), data.Length);
        GlobalUnlock(h);
        return h.ToInt64();
    }
}
public static class Dnd {
    [StructLayout(LayoutKind.Sequential)] struct COPYDATA { public IntPtr dwData; public int cbData; public IntPtr lpData; }
    [DllImport("user32.dll")] static extern IntPtr SendMessageTimeoutW(IntPtr h, uint m, IntPtr w, ref COPYDATA l, uint fl, uint to, out IntPtr res);
    public static int Probe(long main, int op, int keys, int allowed, int x, int y, string payload) {
        byte[] text = Encoding.Unicode.GetBytes(payload == null ? "" : payload);
        int size = 20 + text.Length;
        IntPtr buf = Marshal.AllocHGlobal(size);
        try {
            Marshal.WriteInt32(buf, 0, op); Marshal.WriteInt32(buf, 4, keys); Marshal.WriteInt32(buf, 8, allowed);
            Marshal.WriteInt32(buf, 12, x); Marshal.WriteInt32(buf, 16, y);
            if (text.Length > 0) Marshal.Copy(text, 0, new IntPtr(buf.ToInt64() + 20), text.Length);
            COPYDATA cd; cd.dwData = new IntPtr(0x444E4450); cd.cbData = size; cd.lpData = buf;
            IntPtr res;
            if (SendMessageTimeoutW(new IntPtr(main), 0x004A, IntPtr.Zero, ref cd, 2, 8000, out res) == IntPtr.Zero) throw new Exception("WM_COPYDATA did not return");
            return unchecked((int)res.ToInt64());
        } finally { Marshal.FreeHGlobal(buf); }
    }
}
'@
function Drop-Paths($app, [string[]]$paths) { [void][U]::Post([long]$app.Main, 0x233, [long][Dp]::Make($paths), 0) }     # WM_DROPFILES
function Dnd-Probe($app, [int]$op, [int]$keys, [int]$allowed, [int[]]$pt, [string]$payload = '') { [Dnd]::Probe([long]$app.Main, $op, $keys, $allowed, $pt[0], $pt[1], $payload) }
function Char-Pt($app, [int]$index) {                                             # a point inside the row of char $index, on its left edge (edit client coords)
    $p = [long](Snd (Get-Edit $app) 0xD6 $index 0) -band 0xFFFFFFFF                  # EM_POSFROMCHAR
    if ($p -eq 0xFFFFFFFF) { throw ('no position for char ' + $index) }
    $x = [int]($p -band 0xFFFF); if ($x -ge 0x8000) { $x -= 0x10000 }
    $y = [int](($p -shr 16) -band 0xFFFF); if ($y -ge 0x8000) { $y -= 0x10000 }
    return @($x, ($y + 3))
}
function Drop-File([string]$name, [string]$text) {
    $p = Join-Path $work $name
    [IO.File]::WriteAllText($p, $text, (New-Object Text.UTF8Encoding($false)))
    return $p
}
function Other-Mains($app) {                                                      # main windows of the other copies of the exe (new windows)
    $found = @()
    foreach ($proc in @(Get-Process -Name 'mint_ui_test' -ErrorAction SilentlyContinue)) {
        if ($proc.Id -eq $app.Pid) { continue }
        $h = [U]::FindTop([uint32]$proc.Id, $MainClass, '')
        if ($h) { $found += [pscustomobject]@{ Pid = $proc.Id; Main = [long]$h; Proc = $proc } }
    }
    return $found
}
function Wait-Probe($app) { [bool](WaitFor { (Dnd-Probe $app 0 0 0 @(0, 0)) -eq 0x4D494E54 } 5000) }

function Test-T24 {
    $f1 = Drop-File 't24_first.txt' "first file`r`n"
    $f2 = Drop-File 't24_second.txt' "second file`r`n"
    $jp = Chars @(0x30C6, 0x30B9, 0x30C8)
    $f3 = Drop-File ('t24_' + $jp + '.txt') "katakana name`r`n"
    $app = Start-App
    Drop-Paths $app @($f1)
    Ck 'T24.1 a file dropped on a clean window opens in it (like notepad)' (WaitFor { (Title $app) -like 't24_first.txt*' } 3000) ('title [' + (Title $app) + ']')
    CkEdText 'T24.2 ... with its text' $app "first file`r`n"

    Reset-Doc $app ''
    [void][U]::SndStr((Get-Edit $app), $EM_REPLACESEL, 1, 'unsaved words')
    [void](WaitFor { (Title $app) -like '`**' } 2000)
    Drop-Paths $app @($f2)
    $box = Wait-Box $app $AppName 3000
    Ck 'T24.3 with unsaved changes the save prompt comes first' ($box.Text -like 'do you want to save changes to*') ('box text [' + $box.Text + ']')
    Ck 'T24.4 cancel closes the prompt' (Box-Press $box 103) 'the prompt stayed'
    CkEdText 'T24.5 ... and the drop is abandoned: the unsaved text stays' $app 'unsaved words'
    Drop-Paths $app @($f2)
    $box = Wait-Box $app $AppName 3000
    Ck "T24.6 don't save closes the prompt" (Box-Press $box 102) 'the prompt stayed'
    Ck 'T24.6b ... and the dropped file opens' (WaitFor { (Title $app) -like 't24_second.txt*' } 3000) ('title [' + (Title $app) + ']')
    CkEdText 'T24.7 ... with its text' $app "second file`r`n"

    Drop-Paths $app @($env:windir)
    Start-Sleep -Milliseconds 400
    Ck 'T24.8 a folder alone opens nothing' ((Title $app) -like 't24_second.txt*') ('title [' + (Title $app) + ']')

    Drop-Paths $app @()
    Start-Sleep -Milliseconds 300
    CkEdText 'T24.9 an empty drop changes nothing' $app "second file`r`n"

    Drop-Paths $app @($f3)
    Ck 'T24.10 a file with a non-ascii name opens' (WaitFor { (Title $app) -like ('t24_' + $jp + '.txt*') } 3000) ('title [' + (Show (Title $app)) + ']')

    $before = @(Other-Mains $app).Count
    Drop-Paths $app @($env:windir, $f1, $f2)
    Ck 'T24.11 several files: the first file (folders skipped) opens here' (WaitFor { (Title $app) -like 't24_first.txt*' } 3000) ('title [' + (Title $app) + ']')
    $new = WaitFor { @(Other-Mains $app | Where-Object { [U]::Text($_.Main) -like 't24_second.txt*' }) } 8000
    Ck 'T24.12 ... and the next one opens in a new window' ($new -and @($new).Count -ge 1) ('other windows ' + @(Other-Mains $app).Count + ' (was ' + $before + ')')
    foreach ($o in @(Other-Mains $app)) { try { $o.Proc.Kill() } catch {} }

    $many = @()
    for ($i = 0; $i -lt 11; $i++) { $many += (Drop-File ('t24_many' + $i + '.txt') ('n' + $i)) }
    Drop-Paths $app $many
    $box = Wait-Box $app $AppName 3000
    Ck 'T24.13 more than 10 files: asked first (they would open as many windows)' ($box.Text -like 'open these 11 files*') ('box text [' + $box.Text + ']')
    Ck 'T24.14 cancel opens none of them' (Box-Press $box 102) 'the prompt stayed'
    Start-Sleep -Milliseconds 400
    Ck 'T24.14b ... this window kept its file' ((Title $app) -like 't24_first.txt*') ('title [' + (Title $app) + ']')
    CkEq 'T24.14c ... and no window was started' 0 @(Other-Mains $app).Count
}

function Test-T46 {
    $app = Start-App
    if (-not (Wait-Probe $app)) { Skip 'T46 ole drag and drop' 'this exe has no drop probe (build the probe: tools\probe.bat /DSHOTDC, run with -Exe build\probe\notepad-mint.exe)'; return }
    $ed = Get-Edit $app

    Reset-Doc $app 'hello world'
    CkEq 'T46.1 text dropped from another app that allows move and copy: a move (like wordpad / win11 notepad)' 2 (Dnd-Probe $app 1 1 3 (Char-Pt $app 6) 'big ')
    CkEdText 'T46.2 ... it lands at the drop point' $app 'hello big world'
    CkSel 'T46.3 ... and is selected' $app 6 10
    [void](Snd $ed $EM_UNDO 0 0)
    CkEdText 'T46.4 one undo takes the drop back' $app 'hello world'
    Reset-Doc $app 'hello world'
    CkEq 'T46.5 ctrl held: a copy' 1 (Dnd-Probe $app 1 9 3 (Char-Pt $app 0) 'x')
    Reset-Doc $app 'hello world'
    CkEq 'T46.6 a source that only allows copy gets a copy without ctrl' 1 (Dnd-Probe $app 1 1 1 (Char-Pt $app 0) 'x')
    Reset-Doc $app 'ab'
    [void](Dnd-Probe $app 1 1 3 (Char-Pt $app 1) ("1`n2`r3`r`n4"))
    CkEdText 'T46.7 lf and cr line breaks in dropped text become crlf' $app ("a1`r`n2`r`n3`r`n4b")
    Reset-Doc $app ("line one`r`nline two")
    $p = Char-Pt $app 10; $p[1] += 80
    [void](Dnd-Probe $app 1 1 3 $p '!')
    CkEdText 'T46.8 dropped below the last line at its left: lands at the start of that line (like a click there)' $app ("line one`r`n!line two")
    Reset-Doc $app ("line one`r`nline two")
    $p = Char-Pt $app 10; $p[0] += 400; $p[1] += 80
    [void](Dnd-Probe $app 1 1 3 $p '!')
    CkEdText 'T46.8b ... and below and right of it: appended at the end' $app ("line one`r`nline two!")

    Reset-Doc $app 'one two three'
    [void](Snd $ed $EM_SETSEL 0 4)
    CkEq 'T46.9 the selection dragged forward inside the editor: a move' 2 (Dnd-Probe $app 3 1 3 (Char-Pt $app 8))
    CkEdText 'T46.10 ... "one " now sits before "three"' $app 'two one three'
    CkSel 'T46.11 ... and stays selected at its new place' $app 4 8
    [void](Snd $ed $EM_UNDO 0 0)
    CkEdText 'T46.12 one undo puts it back' $app 'one two three'
    Reset-Doc $app 'one two three'
    [void](Snd $ed $EM_SETSEL 8 13)
    [void](Dnd-Probe $app 3 1 3 (Char-Pt $app 0))
    CkEdText 'T46.13 dragged backward: "three" moves to the start' $app 'threeone two '
    CkSel 'T46.14 ... and is selected there' $app 0 5
    Reset-Doc $app 'one two three'
    [void](Snd $ed $EM_SETSEL 4 7)
    $p = Char-Pt $app 12; $p[0] += 40
    CkEq 'T46.15 ctrl held while dragging the selection: a copy' 1 (Dnd-Probe $app 3 9 3 $p)
    CkEdText 'T46.16 ... "two" is copied to the end, the original stays' $app 'one two threetwo'
    Reset-Doc $app 'one two three'
    [void](Snd $ed $EM_SETSEL 0 7)
    CkEq 'T46.17 dropping into the dragged selection itself is refused' 0 (Dnd-Probe $app 3 1 3 (Char-Pt $app 3))
    CkEdText 'T46.18 ... nothing changed' $app 'one two three'
    CkEq 'T46.19 dropping at the edge of the selection without ctrl is refused too (it would move nothing)' 0 (Dnd-Probe $app 3 1 3 (Char-Pt $app 7))
    Reset-Doc $app 'cut me out'
    [void](Snd $ed $EM_SETSEL 4 7)
    [void](Dnd-Probe $app 6 0 2 @(0, 0))
    CkEdText 'T46.20 dragged out to another app that took it as a move: the text leaves this document' $app 'cut out'
    Reset-Doc $app 'copy me'
    [void](Snd $ed $EM_SETSEL 0 4)
    [void](Dnd-Probe $app 6 0 1 @(0, 0))
    CkEdText 'T46.21 ... taken as a copy: the text stays' $app 'copy me'

    Reset-Doc $app 'hello world'
    $p = Char-Pt $app 6
    [void](Dnd-Probe $app 4 1 3 $p 'x')
    $caret = Dnd-Probe $app 7 0 0 @(0, 0)
    Ck 'T46.22 while text is dragged over the editor an insertion mark shows' ($caret -ne 0) 'no drop caret window'
    if ($caret) {
        $r = [U]::WRect([long]$caret); $e = [U]::WRect($ed)
        CkEq 'T46.23 ... at the drop point (left edge of the char the text would go before)' ([string]($e[0] + $p[0]) + ',' + ($e[1] + $p[1] - 3)) ([string]$r[0] + ',' + $r[1])
    }
    [void](Dnd-Probe $app 5 0 0 @(0, 0))
    CkEq 'T46.24 the drag leaves: the mark goes away' 0 (Dnd-Probe $app 7 0 0 @(0, 0))
    CkEdText 'T46.25 ... and nothing was inserted' $app 'hello world'
    CkEq 'T46.26 text dragged over the menu bar (not the editor) is refused' 0 (Dnd-Probe $app 4 1 3 @(20, -12) 'x')
    [void](Dnd-Probe $app 5 0 0 @(0, 0))

    $sb = New-Object Text.StringBuilder
    for ($i = 1; $i -le 200; $i++) { [void]$sb.Append('line ' + $i + "`r`n") }
    Reset-Doc $app $sb.ToString()
    $f0 = [int](Snd $ed 0xCE 0 0)
    $fr = [CV]::Format($ed)
    [void](Dnd-Probe $app 4 1 3 @(40, ($fr[3] - 2)) 'x')
    for ($i = 0; $i -lt 15; $i++) { Start-Sleep -Milliseconds 80; [void](Dnd-Probe $app 8 1 3 @(40, ($fr[3] - 2)) '') }
    $f1 = [int](Snd $ed 0xCE 0 0)
    Ck 'T46.27 held near the bottom edge while dragging, the editor scrolls down' ($f1 -gt $f0) ('first visible line ' + $f0 + ' -> ' + $f1)
    [void](Dnd-Probe $app 5 0 0 @(0, 0))
    CkEdText 'T46.28 ... and leaving drops nothing' $app $sb.ToString()

    $file = Drop-File 't46_dropped.txt' "dropped file`r`n"
    Reset-Doc $app ''
    CkEq 'T46.29 a file dragged over the menu bar can be dropped there (it opens)' 1 (Dnd-Probe $app 9 1 7 @(20, -12) ($file + "`0"))
    [void](Dnd-Probe $app 5 0 0 @(0, 0))
    CkEq 'T46.30 a file dropped on the editor: a copy effect (the file stays where it is)' 1 (Dnd-Probe $app 2 1 7 @(30, 10) ($file + "`0"))
    Ck 'T46.31 ... it opens in this window' (WaitFor { (Title $app) -like 't46_dropped.txt*' } 3000) ('title [' + (Title $app) + ']')
    CkEdText 'T46.32 ... with its text' $app "dropped file`r`n"
    Reset-Doc $app 'ab'
    CkEq 'T46.33 shift held: the paths are inserted instead (copy effect)' 1 (Dnd-Probe $app 2 5 7 (Char-Pt $app 1) ("C:\x\one.txt`0C:\two words`0"))
    CkEdText 'T46.34 ... at the drop point, one per line, no quotes' $app ("aC:\x\one.txt`r`nC:\two wordsb")
    CkSel 'T46.35 ... the caret ends right after them' $app 27 27
    CkEq 'T46.36 only folders dropped without shift: refused (a folder cannot be opened)' 0 (Dnd-Probe $app 9 1 7 @(30, 10) ($env:windir + "`0"))
    [void](Dnd-Probe $app 5 0 0 @(0, 0))
    CkEq 'T46.37 ... with shift its path can be inserted' 1 (Dnd-Probe $app 9 5 7 (Char-Pt $app 0) ($env:windir + "`0"))
    [void](Dnd-Probe $app 5 0 0 @(0, 0))

    $limit = Snd $ed 0xD5 0 0
    try {
        Reset-Doc $app 'hello world'
        [void](Snd $ed $EM_SETSEL 2 5)
        [void](Snd $ed 0xC5 13 0)
        CkEq 'T46.38 an external move that exceeds the editor limit reports no accepted drop' 0 (Dnd-Probe $app 1 1 3 (Char-Pt $app 6) 'big ')
        CkEdText 'T46.39 ... every original character stays, with no partial insertion' $app 'hello world'
        CkSel 'T46.40 ... the original selection stays' $app 2 5
        Ck 'T46.41 ... no undo or redo step was created' ((Snd $ed $EM_CANUNDO 0 0) -eq 0 -and (Snd $ed 0x455 0 0) -eq 0) 'failed drop changed history'
        [void](Snd $ed 0xC5 15 0)
        CkEq 'T46.42 the same external move fits exactly at the limit and is accepted' 2 (Dnd-Probe $app 1 1 3 (Char-Pt $app 6) 'big ')
        CkEdText 'T46.43 ... the complete dropped text was accepted' $app 'hello big world'
        [void](Snd $ed $EM_UNDO 0 0)
        CkEdText 'T46.44 ... one undo restores the document' $app 'hello world'

        Reset-Doc $app 'one two three'
        [void](Snd $ed 0xC5 13 0)
        [void](Snd $ed $EM_SETSEL 0 4)
        CkEq 'T46.45 an internal copy at the editor limit is refused' 0 (Dnd-Probe $app 3 9 3 (Char-Pt $app 8))
        CkEdText 'T46.46 ... the dragged original was not deleted' $app 'one two three'
        CkSel 'T46.47 ... its selection stays in place' $app 0 4
        CkEq 'T46.48 an internal move at the limit succeeds without adding characters' 2 (Dnd-Probe $app 3 1 3 (Char-Pt $app 8))
        CkEdText 'T46.49 ... the single replacement moves the complete text' $app 'two one three'
        [void](Snd $ed $EM_UNDO 0 0)
        CkEdText 'T46.50 ... one undo restores the original move' $app 'one two three'

        Reset-Doc $app 'one two three'
        [void](Snd $ed 0xC5 12 0)
        [void](Snd $ed $EM_SETSEL 0 4)
        CkEq 'T46.51 an internal move rejected by a lower limit reports no accepted drop' 0 (Dnd-Probe $app 3 1 3 (Char-Pt $app 8))
        CkEdText 'T46.52 ... it preserves the whole original document' $app 'one two three'
        CkSel 'T46.53 ... and preserves the original selection' $app 0 4
        Ck 'T46.54 ... it leaves undo history empty' ((Snd $ed $EM_CANUNDO 0 0) -eq 0) 'failed internal move added history'

        Reset-Doc $app 'ab'
        [void](Snd $ed 0xC5 3 0)
        CkEq 'T46.55 a shift file drop that cannot insert the path reports no accepted drop' 0 (Dnd-Probe $app 2 5 7 (Char-Pt $app 1) ("C:\x\one.txt`0"))
        CkEdText 'T46.56 ... the original text stays unchanged' $app 'ab'
        [void](Snd $ed $EM_SETSEL 0 1)
        [void](Snd $ed 0xCF 1 0)
        CkEq 'T46.57 a read-only editor refuses an external move' 0 (Dnd-Probe $app 1 1 3 (Char-Pt $app 1) 'x')
        CkEdText 'T46.58 ... the read-only text stays intact' $app 'ab'
        CkSel 'T46.59 ... its original selection is preserved' $app 0 1
        Ck 'T46.60 ... undo history stays empty' ((Snd $ed $EM_CANUNDO 0 0) -eq 0) 'read-only drop changed history'
    } finally { [void](Snd $ed 0xCF 0 0); [void](Snd $ed 0xC5 $limit 0) }
}
