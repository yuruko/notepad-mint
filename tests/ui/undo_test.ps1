# Loaded by ui_test.ps1. T48: the undo / redo history of 1.3.0 (src\hist.c): 128 steps, ctrl+z / ctrl+y / ctrl+shift+z, typing runs grouped into one step,
# the selection put back, the history surviving a word wrap toggle, and a randomised run checked against a text model.
# the keys themselves need real keyboard state (like ctrl+k in T26): the accelerator table / menu labels / help text are checked in the sources, the commands run here.
# keep this file pure ascii.
$EM_REDO = 0x454; $EM_CANREDO = 0x455; $WM_CHAR = 0x102; $WM_CLEAR = 0x303
$VK_LEFT = 0x25; $VK_RIGHT = 0x27; $VK_DELETE = 0x2E

$script:HistRec = $null
function Hist-Rec($ed) {                                                         # the fuzz test records every text state, also those between the messages of one operation
    if ($null -ne $script:HistRec) {
        $t = [U]::GetText($ed)
        if ($t -cne $script:HistRec[$script:HistRec.Count - 1]) { [void]$script:HistRec.Add($t) }
    }
}
function Hist-Type($ed, [string]$s) {                                            # WM_CHAR per character (13 = enter)
    foreach ($c in $s.ToCharArray()) { [void](Snd $ed $WM_CHAR ([int]$c) 0); Hist-Rec $ed }
}
function Hist-Back($ed, [int]$n = 1) { for ($i = 0; $i -lt $n; $i++) { [void](Snd $ed $WM_CHAR 8 0); Hist-Rec $ed } }
function Hist-Del($ed, [int]$n = 1) { for ($i = 0; $i -lt $n; $i++) { [void](Snd $ed 0x100 $VK_DELETE 0); Hist-Rec $ed } }
function Hist-Snap([string]$t, [int]$i) {                                       # a caret index never sits inside a crlf or a surrogate pair
    if ($i -gt 0 -and $i -lt $t.Length) {
        if ($t[$i - 1] -eq [char]13 -and $t[$i] -eq [char]10) { return $i - 1 }
        if ([char]::IsHighSurrogate($t[$i - 1]) -and [char]::IsLowSurrogate($t[$i])) { return $i - 1 }
    }
    return $i
}
function Hist-Undo($ed) { return (Snd $ed $EM_UNDO 0 0) -ne 0 }
function Hist-Redo($ed) { return (Snd $ed $EM_REDO 0 0) -ne 0 }
function Hist-CanUndo($ed) { return (Snd $ed $EM_CANUNDO 0 0) -ne 0 }
function Hist-CanRedo($ed) { return (Snd $ed $EM_CANREDO 0 0) -ne 0 }
function Hist-Append($ed, [string]$s) {                                          # a separate step: EM_REPLACESEL at the end
    $n = [U]::GetText($ed).Length
    [void](Snd $ed $EM_SETSEL $n $n)
    [void][U]::SndStr($ed, $EM_REPLACESEL, 1, $s)
}

function Test-T48 {
    $main = [IO.File]::ReadAllText((Join-Path $Src 'main.c'))
    $menu = [IO.File]::ReadAllText((Join-Path $Src 'menu_defs.c'))
    Ck 'T48.1 the accelerator table maps ctrl+z to undo' ($main -match 'FVIRTKEY\s*\|\s*FCONTROL\s*,\s*''Z''\s*,\s*IDM_EDIT_UNDO\s*\}') 'no { FVIRTKEY | FCONTROL, ''Z'', IDM_EDIT_UNDO } entry'
    Ck 'T48.2 ... ctrl+y to redo' ($main -match 'FVIRTKEY\s*\|\s*FCONTROL\s*,\s*''Y''\s*,\s*IDM_EDIT_REDO\s*\}') 'no { FVIRTKEY | FCONTROL, ''Y'', IDM_EDIT_REDO } entry'
    Ck 'T48.3 ... and ctrl+shift+z to redo' ($main -match 'FVIRTKEY\s*\|\s*FCONTROL\s*\|\s*FSHIFT\s*,\s*''Z''\s*,\s*IDM_EDIT_REDO\s*\}') 'no { FVIRTKEY | FCONTROL | FSHIFT, ''Z'', IDM_EDIT_REDO } entry'
    Ck 'T48.4 edit > redo shows ctrl+y, and the context menu has redo' (($menu -match 'IT\(L"red&o",\s*L"ctrl\+y",\s*IDM_EDIT_REDO\)') -and ($menu -match 'IT\(L"red&o",\s*NULL,\s*IDM_EDIT_REDO\)')) 'redo item missing in src\menu_defs.c'
    Ck 'T48.5 the redo item is grayed from the history (HistCanRedo), not from the stock edit' ($main -match 'case IDM_EDIT_REDO:\s*return HistCanRedo\(\)') 'MenuState has no HistCanRedo entry'
    $app = Start-App
    $ed = Get-Edit $app
    Cmd $app 'IDM_HELP_TOPICS'
    $h = Wait-Win $app 'mp_help' 'help topics'
    $t = ''
    foreach ($k in [U]::Kids($h)) { if ([U]::Cls($k) -eq 'Edit') { $t = [U]::GetText($k) } }
    Ck 'T48.6 the help topics list ctrl+y / ctrl+shift+z (redo) and the 128 steps' (($t -match 'ctrl\+y\s+redo') -and ($t -match 'ctrl\+shift\+z') -and ($t -match '128')) ('help text [' + (Show $t) + ']')
    Pst $h $WM_CLOSE 0 0
    [void](Gone $h)

    Reset-Doc $app ''
    Ck 'T48.7 a fresh document: nothing to undo or redo' (-not (Hist-CanUndo $ed) -and -not (Hist-CanRedo $ed)) 'CANUNDO / CANREDO set'
    Hist-Type $ed 'hello'
    CkEdText 'T48.8 (setup) typed text' $app 'hello'
    Ck 'T48.9 typing makes one undo available, no redo' ((Hist-CanUndo $ed) -and -not (Hist-CanRedo $ed)) 'wrong undo / redo state'
    Ck 'T48.10 undo works' (Hist-Undo $ed) 'EM_UNDO returned 0'
    CkEdText 'T48.11 one undo removes the whole typed word' $app ''
    Ck 'T48.12 ... and now redo is available, undo is not' ((Hist-CanRedo $ed) -and -not (Hist-CanUndo $ed)) 'wrong undo / redo state'
    Ck 'T48.13 a second undo with nothing left does nothing' (-not (Hist-Undo $ed)) 'EM_UNDO returned non-zero'
    Ck 'T48.14 redo works' (Hist-Redo $ed) 'EM_REDO returned 0'
    CkEdText 'T48.15 redo types it again' $app 'hello'
    CkSel 'T48.16 ... and the caret is after the redone text' $app 5 5
    Ck 'T48.17 a second redo with nothing left does nothing' (-not (Hist-Redo $ed)) 'EM_REDO returned non-zero'

    Reset-Doc $app ''
    Hist-Type $ed ('ab' + [char]13 + 'cd')
    CkEdText 'T48.20 (setup) two typed lines' $app "ab`r`ncd"
    [void](Hist-Undo $ed)
    CkEdText 'T48.21 typing is grouped per line: the first undo takes the second line' $app "ab`r`n"
    [void](Hist-Undo $ed)
    CkEdText 'T48.22 ... the second undo the first line' $app ''
    Ck 'T48.23 ... and that was all' (-not (Hist-CanUndo $ed)) 'CANUNDO still set'

    Reset-Doc $app ''
    Hist-Type $ed 'ab'
    [void](Snd $ed 0x100 $VK_LEFT 0)
    [void](Snd $ed 0x100 $VK_RIGHT 0)
    Hist-Type $ed 'c'
    CkEdText 'T48.24 (setup) typed, moved the caret with the arrow keys, typed again' $app 'abc'
    [void](Hist-Undo $ed)
    CkEdText 'T48.25 an arrow key ends a typing group' $app 'ab'
    [void](Hist-Undo $ed)
    CkEdText 'T48.26 ... the first group is its own step' $app ''
    Reset-Doc $app ''
    Hist-Type $ed 'ab'
    Pst $ed 0x0201 1 0
    Pst $ed 0x0202 0 0
    Start-Sleep -Milliseconds 300
    $n = ([U]::GetText($ed)).Length
    [void](Snd $ed $EM_SETSEL $n $n)
    Hist-Type $ed 'c'
    [void](Hist-Undo $ed)
    CkEdText 'T48.27 a mouse click ends a typing group' $app 'ab'

    Reset-Doc $app 'abcdef'
    [void](Snd $ed $EM_SETSEL 6 6)
    Hist-Back $ed 3
    CkEdText 'T48.30 (setup) three backspaces' $app 'abc'
    [void](Hist-Undo $ed)
    CkEdText 'T48.31 a run of backspaces is one step' $app 'abcdef'
    CkSel 'T48.32 ... and undo puts the caret where it was' $app 6 6
    [void](Hist-Redo $ed)
    CkEdText 'T48.33 redo deletes them again' $app 'abc'

    Reset-Doc $app 'abcdef'
    Hist-Del $ed 3
    CkEdText 'T48.34 (setup) three forward deletes' $app 'def'
    [void](Hist-Undo $ed)
    CkEdText 'T48.35 a run of forward deletes is one step' $app 'abcdef'
    CkSel 'T48.36 ... caret back at the start' $app 0 0
    [void](Hist-Redo $ed)
    CkEdText 'T48.37 redo' $app 'def'

    Reset-Doc $app 'hello world'
    [void](Snd $ed $EM_SETSEL 6 11)
    Hist-Type $ed 'x'
    CkEdText 'T48.40 (setup) typing over a selection' $app 'hello x'
    Hist-Type $ed 'yz'
    CkEdText 'T48.41 (setup) ... and on' $app 'hello xyz'
    [void](Hist-Undo $ed)
    CkEdText 'T48.42 one undo brings the replaced word back' $app 'hello world'
    CkSel 'T48.43 ... with it selected again' $app 6 11
    [void](Hist-Redo $ed)
    CkEdText 'T48.44 redo' $app 'hello xyz'
    CkSel 'T48.45 ... caret after the typed text' $app 9 9

    Reset-Doc $app 'abcd'
    [void](Snd $ed $EM_SETSEL 1 3)
    [void](Snd $ed $WM_CLEAR 0 0)
    CkEdText 'T48.46 (setup) delete the selection' $app 'ad'
    [void](Hist-Undo $ed)
    CkEdText 'T48.47 undo brings the selection back' $app 'abcd'
    CkSel 'T48.48 ... selected' $app 1 3

    Reset-Doc $app ''
    for ($i = 1; $i -le 130; $i++) { Hist-Append $ed ('<' + $i + '>') }
    $all = Ed-Text $app
    $done = 0
    for ($i = 0; $i -lt 128; $i++) { if (Hist-Undo $ed) { $done++ } }
    Ck 'T48.50 128 separate steps can be undone' ($done -eq 128) ('undid ' + $done)
    Ck 'T48.51 ... the 129th cannot (the history holds 128 steps)' (-not (Hist-Undo $ed) -and -not (Hist-CanUndo $ed)) 'a 129th undo worked'
    CkEdText 'T48.52 ... the two oldest steps stay in the text' $app '<1><2>'
    $done = 0
    for ($i = 0; $i -lt 130; $i++) { if (Hist-Redo $ed) { $done++ } }
    Ck 'T48.53 all 128 can be redone' ($done -eq 128) ('redid ' + $done)
    CkEdText 'T48.54 ... which gives the full text again' $app $all

    Reset-Doc $app ''
    Hist-Type $ed 'a'
    [void](Hist-Undo $ed)
    Ck 'T48.55 (setup) redo is available' (Hist-CanRedo $ed) 'no redo'
    Hist-Type $ed 'b'
    Ck 'T48.56 a new edit after an undo throws the redo steps away' (-not (Hist-CanRedo $ed)) 'CANREDO still set'
    CkEdText 'T48.57 ... and the new text is there' $app 'b'

    Reset-Doc $app ''
    Hist-Type $ed 'x'
    Ck 'T48.60 typing makes the document modified (title starts with *)' ([bool](WaitFor { (Title $app) -match '^\*' } 2000)) ('title [' + (Title $app) + ']')
    [void](Hist-Undo $ed)
    Ck 'T48.61 undoing back to the saved text is not modified' ([bool](WaitFor { (Title $app) -notmatch '^\*' } 2000)) ('title [' + (Title $app) + ']')
    [void](Hist-Redo $ed)
    Ck 'T48.62 redo makes it modified again' ([bool](WaitFor { (Title $app) -match '^\*' } 2000)) ('title [' + (Title $app) + ']')

    Reset-Doc $app ''
    Hist-Type $ed 'abc'
    Cmd $app 'IDM_EDIT_UNDO'
    CkEdText 'T48.63 edit > undo (the command) undoes' $app ''
    Cmd $app 'IDM_EDIT_REDO'
    CkEdText 'T48.64 edit > redo (the command) redoes' $app 'abc'

    Reset-Doc $app ''
    Hist-Type $ed 'abc'
    $oldEd = $ed
    Cmd $app 'IDM_FMT_WRAP'
    [void](WaitFor { (Get-Edit $app) -ne $oldEd } 3000)
    $ed = Get-Edit $app
    Ck 'T48.65 toggling word wrap (a new edit control) keeps the history' ((Hist-CanUndo $ed) -and -not (Hist-CanRedo $ed)) 'undo state lost'
    [void](Hist-Undo $ed)
    CkEdText 'T48.66 ... and the step still undoes' $app ''
    $oldEd = $ed
    Cmd $app 'IDM_FMT_WRAP'
    [void](WaitFor { (Get-Edit $app) -ne $oldEd } 3000)
    $ed = Get-Edit $app
    Ck 'T48.67 ... the redo step survives the second toggle too' (Hist-CanRedo $ed) 'redo lost'
    [void](Hist-Redo $ed)
    CkEdText 'T48.68 ... and redoes' $app 'abc'

    $emoji = [char]::ConvertFromUtf32(0x1F600)
    Reset-Doc $app ('a' + $emoji + 'b')
    [void](Snd $ed $EM_SETSEL 3 3)
    Hist-Back $ed 1
    $left = Ed-Text $app
    [void](Hist-Undo $ed)
    CkEdText 'T48.70 backspace over a surrogate pair: undo restores both halves' $app ('a' + $emoji + 'b')
    Ck 'T48.71 (sanity) the backspace removed the pair' ($left -ceq 'ab' -or $left.Length -lt 4) ('after the backspace [' + (Show $left) + ']')
    Reset-Doc $app "x`r`ny"
    [void](Snd $ed $EM_SETSEL 3 3)
    Hist-Back $ed 1
    [void](Hist-Undo $ed)
    CkEdText 'T48.72 backspace over a line break (crlf): undo restores it whole' $app "x`r`ny"

    Reset-Doc $app ''
    $jp = (Chars 0x65E5, 0x672C, 0x8A9E)
    Hist-Type $ed $jp
    CkEdText 'T48.73 (setup) typed japanese' $app $jp
    [void](Hist-Undo $ed)
    CkEdText 'T48.74 ... undone as one step' $app ''

    Reset-Doc $app (('word word word word word' + "`r`n") * 8000)
    Start-Sleep -Milliseconds 300
    $big = ([U]::GetText($ed)).Length
    [void](Snd $ed $EM_SETSEL $big $big)
    $sw = [Diagnostics.Stopwatch]::StartNew()
    Hist-Type $ed ('x' * 400)
    $sw.Stop()
    Ck 'T48.75 typing 400 characters into a 230 KB document works (the stock edit alone needs ~12 ms a key here)' ($sw.ElapsedMilliseconds -lt 30000) ('took ' + $sw.ElapsedMilliseconds + ' ms')
    [void](Hist-Undo $ed)
    Ck 'T48.76 ... and they are one undo step' (([U]::GetText($ed)).Length -eq $big) 'the typed run is not one step'

    Reset-Doc $app 'one two three'
    [void](Snd $ed $EM_SETSEL 0 13)
    [void][U]::SndStr($ed, $EM_REPLACESEL, 1, 'x')
    [void](Snd $ed $EM_SETSEL 0 1)
    [void][U]::SndStr($ed, $EM_REPLACESEL, 1, 'one two three four')
    [void](Hist-Undo $ed)
    CkEdText 'T48.77 two whole-document replacements undo one by one (1/2)' $app 'x'
    [void](Hist-Undo $ed)
    CkEdText 'T48.78 (2/2)' $app 'one two three'

    Reset-Doc $app 'ab'
    [void](Snd $ed $EM_SETSEL 2 2)
    Hist-Type $ed 'cd'
    [CV]::OnDesktop($deskName)
    [CV]::KeyPost($app.Tid, $ed, 0x100, 0x5A, 1, $false, $true, 400)
    CkEdText 'T48.80 ctrl+z (a posted key with the ctrl state, translated by the app message loop) undoes' $app 'ab'
    [CV]::KeyPost($app.Tid, $ed, 0x100, 0x59, 1, $false, $true, 400)
    CkEdText 'T48.81 ctrl+y redoes' $app 'abcd'
    [CV]::KeyPost($app.Tid, $ed, 0x100, 0x5A, 1, $false, $true, 400)
    CkEdText 'T48.82 (ctrl+z again)' $app 'ab'
    [CV]::KeyPost($app.Tid, $ed, 0x100, 0x5A, 1, $true, $true, 400)
    CkEdText 'T48.83 ctrl+shift+z redoes' $app 'abcd'
    [CV]::KeyPost($app.Tid, $ed, 0x100, 0x5A, 1, $false, $false, 400)
    CkEdText 'T48.84 a plain z key (no ctrl) is a typed letter, not an undo' $app 'abcdz'

    Reset-Doc $app 'keep'
    Hist-Type $ed '!'
    [void][U]::SetText($ed, 'fresh')
    Ck 'T48.79 loading text (WM_SETTEXT) empties the history' (-not (Hist-CanUndo $ed) -and -not (Hist-CanRedo $ed)) 'history survived WM_SETTEXT'
    Stop-App $app
}

function Test-T49 {                                                              # random edits against a model: undo all the way down and redo all the way up
    $app = Start-App
    $ed = Get-Edit $app
    $words = @('alpha', 'be', "`r`n", ' ', 'gamma delta', 'x', "`r`n`r`n", 'tab' + [char]9 + 'ed', 'zz', (Chars 0x65E5, 0x672C), [char]::ConvertFromUtf32(0x1F600) + 'q')
    foreach ($seed in 7, 99, 2024, 31337, 5, 12345, 777, 4242) {
        $rng = New-Object System.Random $seed
        Reset-Doc $app ''
        $start = 'start of the document' + "`r`n" + 'second line' + "`r`n"
        [void][U]::SndStr($ed, $EM_REPLACESEL, 0, $start)
        [void](Snd $ed $EM_EMPTYUNDOBUFFER 0 0)
        $snaps = New-Object System.Collections.ArrayList
        [void]$snaps.Add((Ed-Text $app))
        $script:HistRec = $snaps
        $ops = 90
        for ($i = 0; $i -lt $ops; $i++) {
            $cur = [U]::GetText($ed)
            $len = $cur.Length
            $a = Hist-Snap $cur ($rng.Next(0, $len + 1))
            $b = if ($rng.Next(0, 3) -eq 0) { Hist-Snap $cur ([Math]::Min($len, $a + $rng.Next(0, 9))) } else { $a }
            [void](Snd $ed $EM_SETSEL $a $b)
            switch ($rng.Next(0, 6)) {
                0 { Hist-Type $ed ($words[$rng.Next(0, $words.Count)]) }
                1 { [void][U]::SndStr($ed, $EM_REPLACESEL, 1, ($words[$rng.Next(0, $words.Count)] + $words[$rng.Next(0, $words.Count)])); Hist-Rec $ed }
                2 { [void](Snd $ed $WM_CLEAR 0 0); Hist-Rec $ed }
                3 { Hist-Back $ed ($rng.Next(1, 4)) }
                4 { Hist-Del $ed ($rng.Next(1, 4)) }
                5 { [void][U]::SndStr($ed, $EM_REPLACESEL, 1, ''); Hist-Rec $ed }
            }
        }
        $script:HistRec = $null
        $final = Ed-Text $app
        $steps = 0; $ok = $true; $why = ''
        $at = $snaps.Count - 1
        while (Hist-Undo $ed) {
            $steps++
            $t = Ed-Text $app
            $found = -1
            for ($j = $at - 1; $j -ge 0; $j--) { if ($snaps[$j] -ceq $t) { $found = $j; break } }
            if ($found -lt 0) { $ok = $false; $why = 'after undo step ' + $steps + ' the text [' + (Show $t) + '] is not an earlier state'; break }
            $at = $found
            if ($steps -gt 200) { break }
        }
        if ($ok -and (Ed-Text $app) -cne $snaps[0]) { $ok = $false; $why = 'all undone, but the text is [' + (Show (Ed-Text $app)) + '] not the start text' }
        Ck ('T49.' + $seed + 'a random edits (' + $ops + ' ops, ' + $snaps.Count + ' text states between the messages): undoing step by step only visits earlier states and ends at the start (' + $steps + ' steps)') $ok $why
        $redone = 0
        while (Hist-Redo $ed) { $redone++; if ($redone -gt 200) { break } }
        Ck ('T49.' + $seed + 'b ... redoing all of them gives the final text back (' + $redone + ' steps)') (($redone -eq $steps) -and ((Ed-Text $app) -ceq $final)) ('redid ' + $redone + ' of ' + $steps + '; text [' + (Show (Ed-Text $app)) + ']')
    }
    Stop-App $app
}
