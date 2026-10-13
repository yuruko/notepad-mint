# Loaded by ui_test.ps1 (after dragdrop_test.ps1: uses its Char-Pt). T47: windows xp notepad parity of 1.2.0 that window messages can drive:
# a click inside a selection without a drag, the tilt wheel, f3 in the direction of the find dialog (shift+f3 the other way), the search
# text remembered across sessions, and a missing command line name without an extension offered as name.txt.
# keep this file pure ascii.
function Test-T47 {
    $app = Start-App
    $ed = Get-Edit $app
    Start-Sleep -Milliseconds 600                                                 # (drag start needs the ole setup, done after the first paint)
    Reset-Doc $app 'hello world'
    [void](Snd $ed $EM_SETSEL 0 11)
    $p = Char-Pt $app 4
    $lp = ($p[0] -band 0xFFFF) -bor (($p[1] -band 0xFFFF) -shl 16)
    Pst $ed 0x0201 1 $lp
    Pst $ed 0x0202 0 $lp
    CkSel 'T47.1 a click inside the selection that does not drag puts the caret there, like any click' $app 4 4

    Reset-Doc $app ('word ' * 300)
    $h0 = ([U]::Scroll($ed, 0))[3]
    Pst $ed 0x020E (120 -shl 16) 0
    $moved = WaitFor { ([U]::Scroll($ed, 0))[3] -gt $h0 } 2000
    Ck 'T47.2 the tilt wheel scrolls the text sideways (right)' $moved ('horizontal position stayed ' + $h0)
    $h1 = ([U]::Scroll($ed, 0))[3]
    Pst $ed 0x020E (0xFF88 -shl 16) 0
    Ck 'T47.3 ... and back (left)' (WaitFor { ([U]::Scroll($ed, 0))[3] -lt $h1 } 2000) ('horizontal position stayed ' + $h1)

    Reset-Doc $app 'x1 x2 x3'
    [void](Snd $ed $EM_SETSEL 5 5)
    Cmd $app 'IDM_EDIT_FIND'
    $dlg = Wait-Win $app 'mp_find' 'find'
    Set-Field $dlg $IDF.ID_WHAT 'x'
    Press $dlg $IDF.ID_UP
    CkChk 'T47.4 (setup) direction up in the find dialog' $dlg $IDF.ID_UP 1
    Press $dlg $IDCANCEL
    [void](Gone $dlg 2000)
    [void](Snd $ed $EM_SETSEL 5 5)
    Cmd $app 'IDM_EDIT_FINDNEXT'
    CkSel 'T47.5 f3 searches in the direction picked in the find dialog (up), like xp' $app 3 4
    Cmd $app 'IDM_EDIT_FINDPREV'
    CkSel 'T47.6 shift+f3 searches the other way' $app 6 7
    Stop-App $app
    CkEq 'T47.7 the search text is remembered (settings.ini [find] what)' '"x"' (Ini-Val $app 'find' 'what' '"x"')
    CkEq 'T47.8 ... and the direction' '1' (Ini-Val $app 'find' 'up' '1')
    $app2 = Start-App '' $app.AppData
    Cmd $app2 'IDM_EDIT_FIND'
    $dlg2 = Wait-Win $app2 'mp_find' 'find'
    CkEq 'T47.9 the next start offers it again in the find box' 'x' (Get-Field $dlg2 $IDF.ID_WHAT)
    CkChk 'T47.10 ... with the direction' $dlg2 $IDF.ID_UP 1
    Stop-App $app2

    $name = Join-Path $work 't47_new'
    $app3 = Start-App $name
    $box = Wait-Box $app3 $AppName 3000
    Ck 'T47.11 a missing name without an extension: the prompt offers name.txt, like xp' ($box.Text -like '*t47_new.txt file.*') ('box text [' + $box.Text + ']')
    [void](Box-Press $box 1)
    Ck 'T47.12 yes creates name.txt' (WaitFor { Test-Path -LiteralPath ($name + '.txt') } 3000) 'no t47_new.txt'
    Ck 'T47.13 ... and it is the document of the window' (WaitFor { (Title $app3) -like 't47_new.txt*' } 3000) ('title [' + (Title $app3) + ']')
}
