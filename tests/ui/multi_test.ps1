# Loaded by ui_test.ps1. T54: 1.8.0 multiple cursors (src\multi.c) and alt+up / alt+down line moves.
# alt+click / alt+drag are posted mouse messages with the alt key held in the app's keyboard state ([CV]::KeyPost), typing is WM_CHAR.
# keep this file pure ascii.
$WM_CHAR = 0x102; $WM_KEYDOWN = 0x100; $WM_SYSKEYDOWN = 0x104; $WM_SYSKEYUP = 0x105
$WM_LBUTTONDOWN = 0x201; $WM_LBUTTONUP = 0x202; $WM_MOUSEMOVE = 0x200
$EM_POSFROMCHAR = 0xD6; $EM_REDO = 0x454

function MC-Pos($ed, [int]$i) {
    $p = [long](Snd $ed $EM_POSFROMCHAR $i 0)
    $x = [int]($p -band 0xFFFF); if ($x -ge 0x8000) { $x -= 0x10000 }
    $y = [int](($p -shr 16) -band 0xFFFF); if ($y -ge 0x8000) { $y -= 0x10000 }
    return @($x, $y)
}
function MC-LParam($ed, [int]$i) {                                               # EM_POSFROMCHAR gives -1 at the very end of the text: never click there
 $p = MC-Pos $ed $i; return [int](((($p[1] + 4) -band 0xFFFF) -shl 16) -bor (($p[0] + 1) -band 0xFFFF)) }
function MC-AltClick($app, $ed, [int]$i) {
    $l = MC-LParam $ed $i
    [CV]::KeyPost($app.Tid, $ed, $WM_LBUTTONDOWN, 1, $l, $false, $false, 120, $true)
    [CV]::KeyPost($app.Tid, $ed, $WM_LBUTTONUP, 0, $l, $false, $false, 120, $true)
}
function MC-AltDrag($app, $ed, [int]$i, [int]$j) {
    $a = MC-LParam $ed $i; $b = MC-LParam $ed $j
    [CV]::KeyPost($app.Tid, $ed, $WM_LBUTTONDOWN, 1, $a, $false, $false, 120, $true)
    [CV]::KeyPost($app.Tid, $ed, $WM_MOUSEMOVE, 1, $b, $false, $false, 120, $true)
    [CV]::KeyPost($app.Tid, $ed, $WM_LBUTTONUP, 0, $b, $false, $false, 120, $true)
}
function MC-Type($ed, [string]$s) { foreach ($c in $s.ToCharArray()) { [void](Snd $ed $WM_CHAR ([int]$c) 0) } }
function MC-Sel($ed) { $r = [long](Snd $ed $EM_GETSEL 0 0); return ('' + ($r -band 0xFFFF) + ',' + (($r -shr 16) -band 0xFFFF)) }
function MC-Key($app, $ed, [int]$vk, [bool]$shift = $false, [bool]$ctrl = $false) { [CV]::KeyMsg($app.Tid, $ed, $WM_KEYDOWN, $vk, 1, $shift, $ctrl) }
function MC-Start($app, [string]$t, [int]$caret) {
    $ed = Get-Edit $app
    Reset-Doc $app $t
    [void](Snd $ed $EM_SETSEL $caret $caret)
    [CV]::Focus($app.Tid, $app.Main, $ed)
    [void](Snd $ed $WM_KEYDOWN 0x1B 0)
    return $ed
}

function Test-T54 {
    $main = [IO.File]::ReadAllText((Join-Path $Src 'main.c'))
    $menu = [IO.File]::ReadAllText((Join-Path $Src 'menu_defs.c'))
    Ck 'T54.1 the accelerator table maps alt+up / alt+down to move line up / down' (($main -match 'FVIRTKEY\s*\|\s*FALT\s*,\s*VK_UP\s*,\s*IDM_EDIT_LINEUP\s*\}') -and ($main -match 'FVIRTKEY\s*\|\s*FALT\s*,\s*VK_DOWN\s*,\s*IDM_EDIT_LINEDOWN\s*\}')) 'accelerator entries missing in src\main.c'
    Ck 'T54.2 edit > move line up / down show alt+up / alt+down' (($menu -match 'IT\(L"&move line up",\s*L"alt\+up",\s*IDM_EDIT_LINEUP\)') -and ($menu -match 'IT\(L"move line do&wn",\s*L"alt\+down",\s*IDM_EDIT_LINEDOWN\)')) 'menu items missing in src\menu_defs.c'
    $app = Start-App
    [CV]::OnDesktop($deskName)
    Cmd $app 'IDM_HELP_TOPICS'
    $h = Wait-Win $app 'mp_help' 'help topics'
    $t = ''
    foreach ($k in [U]::Kids($h)) { if ([U]::Cls($k) -eq 'Edit') { $t = [U]::GetText($k) } }
    Ck 'T54.3 the help topics list alt+up / down, alt+click, alt+drag and esc' (($t -match 'alt\+up / down\s+move the current line') -and ($t -match 'alt\+click\s+add a cursor') -and ($t -match 'alt\+drag\s+add a selection') -and ($t -match 'esc\s+back to a single cursor')) ('help text [' + (Show $t) + ']')
    Pst $h $WM_CLOSE 0 0
    [void](Gone $h)

    $ed = MC-Start $app "aaa`r`nbbb`r`nccc" 0
    MC-AltClick $app $ed 5
    MC-AltClick $app $ed 10
    CkEq 'T54.4 alt+click moves the main caret to the clicked place' '10,10' (MC-Sel $ed)
    MC-Type $ed 'x'
    CkEdText 'T54.5 typing goes to every cursor' $app "xaaa`r`nxbbb`r`nxccc"
    MC-Type $ed 'yz'
    CkEdText 'T54.6 ... a whole typing run' $app "xyzaaa`r`nxyzbbb`r`nxyzccc"
    CkEq 'T54.7 the main caret follows its own insertions' '19,19' (MC-Sel $ed)
    [void](Snd $ed $WM_CHAR 8 0)
    CkEdText 'T54.8 backspace deletes before every cursor' $app "xyaaa`r`nxybbb`r`nxyccc"
    [void](Hist-Undo $ed)
    CkEdText 'T54.9 undo puts the backspaced characters back at every cursor in one step' $app "xyzaaa`r`nxyzbbb`r`nxyzccc"
    [void](Hist-Undo $ed)
    CkEdText 'T54.10 undo removes the typing run from every cursor in one step' $app "aaa`r`nbbb`r`nccc"
    MC-Type $ed 'k'
    CkEdText 'T54.11 undo brought every cursor back (typing reaches all of them)' $app "kaaa`r`nkbbb`r`nkccc"
    [void](Hist-Undo $ed)
    [void](Hist-Redo $ed)
    CkEdText 'T54.12 redo applies the step at every cursor again' $app "kaaa`r`nkbbb`r`nkccc"
    MC-Type $ed 'm'
    CkEdText 'T54.13 redo brought every cursor back' $app "kmaaa`r`nkmbbb`r`nkmccc"

    $ed = MC-Start $app "aaa`r`nbbb`r`nccc" 0
    MC-AltClick $app $ed 5
    MC-AltClick $app $ed 10
    [void](Snd $ed $WM_KEYDOWN 0x2E 0)
    CkEdText 'T54.14 delete removes the character after every cursor' $app "aa`r`nbb`r`ncc"
    MC-Type $ed "`r"
    CkEdText 'T54.15 enter splits the line at every cursor' $app "`r`naa`r`n`r`nbb`r`n`r`ncc"
    [void](Hist-Undo $ed)
    MC-Key $app $ed 0x27
    MC-Type $ed '-'
    CkEdText 'T54.16 right arrow moves every cursor' $app "a-a`r`nb-b`r`nc-c"
    MC-Key $app $ed 0x23
    MC-Type $ed '!'
    CkEdText 'T54.17 end moves every cursor to its line end' $app "a-a!`r`nb-b!`r`nc-c!"
    MC-Key $app $ed 0x24 $true
    MC-Type $ed 'Q'
    CkEdText 'T54.18 shift+home selects back to each line start and typing replaces every selection' $app "Q`r`nQ`r`nQ"
    [void](Snd $ed $WM_KEYDOWN 0x1B 0)
    MC-Type $ed 'Z'
    CkEdText 'T54.19 esc leaves only the main cursor' $app "Q`r`nQ`r`nQZ"

    $ed = MC-Start $app "one two`r`nthree four`r`n" 7
    MC-AltClick $app $ed 19
    [void](Snd $ed $WM_CHAR 0x7F 0)
    CkEdText 'T54.20 ctrl+backspace deletes the word before every cursor' $app "one `r`nthree `r`n"
    $ed = MC-Start $app "x`r`nab`r`n" 3
    MC-AltClick $app $ed 5
    [void](Snd $ed $WM_CHAR 0x7F 0)
    CkEdText 'T54.21 ctrl+backspace never deletes past the cursor before it' $app "x`r`n"

    $ed = MC-Start $app "abc" 0
    MC-AltClick $app $ed 2
    MC-AltClick $app $ed 2
    MC-Type $ed 'x'
    CkEdText 'T54.22 alt+click on the main caret removes it (an added cursor takes over)' $app "xabc"
    $ed = MC-Start $app "abc" 0
    MC-AltClick $app $ed 2
    MC-AltClick $app $ed 0
    MC-Type $ed 'x'
    CkEdText 'T54.23 alt+click on an added cursor removes it' $app "abxc"
    $ed = MC-Start $app "abc" 0
    MC-AltClick $app $ed 2
    [CV]::KeyPost($app.Tid, $ed, $WM_LBUTTONDOWN, 1, (MC-LParam $ed 1), $false, $false, 120, $false)
    [CV]::KeyPost($app.Tid, $ed, $WM_LBUTTONUP, 0, (MC-LParam $ed 1), $false, $false, 120, $false)
    MC-Type $ed 'x'
    CkEdText 'T54.24 a plain click goes back to one cursor' $app "axbc"

    $ed = MC-Start $app "foo bar`r`nfoo baz" 0
    [void](Snd $ed $EM_SETSEL 0 3)
    MC-AltDrag $app $ed 9 12
    CkEq 'T54.25 alt+drag selects with a new main cursor' '9,12' (MC-Sel $ed)
    MC-Type $ed 'Q'
    CkEdText 'T54.26 typing replaces both selections' $app "Q bar`r`nQ baz"
    $ed = MC-Start $app "abcdef" 0
    [void](Snd $ed $EM_SETSEL 0 3)
    MC-AltDrag $app $ed 2 5
    MC-Type $ed '-'
    CkEdText 'T54.27 overlapping selections merge into one' $app "-f"

    $saved = $null
    try { $saved = Get-Clipboard -Raw -ErrorAction Stop } catch { $saved = $null }
    try {
        $ed = MC-Start $app "a1 x`r`nb2 x`r`nc3 x" 0
        [void](Snd $ed $EM_SETSEL 0 2)
        MC-AltDrag $app $ed 6 8
        MC-AltDrag $app $ed 12 14
        [void](Snd $ed 0x301 0 0)
        [void](WaitFor { $script:mcClip = Get-Clipboard -Raw; $script:mcClip -ceq "a1`r`nb2`r`nc3" } 2000)
        CkEq 'T54.28 copy joins every selection, one per line' "a1`r`nb2`r`nc3" $script:mcClip
        MC-Key $app $ed 0x23
        [void](Snd $ed 0x302 0 0)
        CkEdText 'T54.29 paste with as many lines as cursors gives each cursor its own line' $app "a1 xa1`r`nb2 xb2`r`nc3 xc3"
        [void](Snd $ed 0x300 0 0)
        CkEdText 'T54.30 cut deletes every selection (none left: nothing happens)' $app "a1 xa1`r`nb2 xb2`r`nc3 xc3"
    } finally {
        if ($null -ne $saved) { try { Set-Clipboard -Value $saved } catch {} }
    }

    $ed = MC-Start $app "one`r`ntwo`r`nthree" 6
    Cmd $app 'IDM_EDIT_LINEDOWN'
    CkEdText 'T54.31 move line down swaps the line with the next one' $app "one`r`nthree`r`ntwo"
    Ck 'T54.32 the caret moves with the line' ([bool](WaitFor { (MC-Sel $ed) -eq '13,13' })) ('selection ' + (MC-Sel $ed))
    Cmd $app 'IDM_EDIT_LINEDOWN'
    Start-Sleep -Milliseconds 200
    CkEdText 'T54.33 the last line does not move down' $app "one`r`nthree`r`ntwo"
    Cmd $app 'IDM_EDIT_LINEUP'
    Cmd $app 'IDM_EDIT_LINEUP'
    CkEdText 'T54.34 move line up twice brings it to the top' $app "two`r`none`r`nthree"
    Ck 'T54.35 the caret is still in the moved line' ([bool](WaitFor { (MC-Sel $ed) -eq '1,1' })) ('selection ' + (MC-Sel $ed))
    Cmd $app 'IDM_EDIT_LINEUP'
    Start-Sleep -Milliseconds 200
    CkEdText 'T54.36 the first line does not move up' $app "two`r`none`r`nthree"
    [void](Hist-Undo $ed)
    CkEdText 'T54.37 undo takes back one move' $app "one`r`ntwo`r`nthree"
    CkEq 'T54.38 undo puts the caret back' '6,6' (MC-Sel $ed)

    $ed = MC-Start $app "a`r`nb`r`nc`r`nd" 0
    [void](Snd $ed $EM_SETSEL 3 7)
    Cmd $app 'IDM_EDIT_LINEDOWN'
    CkEdText 'T54.39 a selection over two lines moves both' $app "a`r`nd`r`nb`r`nc"
    Ck 'T54.40 the selection stays on the moved lines' ([bool](WaitFor { (MC-Sel $ed) -eq '6,10' })) ('selection ' + (MC-Sel $ed))
    $ed = MC-Start $app "a`r`nb`r`nc`r`nd" 3
    [void](Snd $ed $EM_SETSEL 3 6)
    Cmd $app 'IDM_EDIT_LINEUP'
    CkEdText 'T54.41 a selection that ends at a line start leaves that line alone' $app "b`r`na`r`nc`r`nd"

    $ed = MC-Start $app "1`r`n2`r`n3`r`n4`r`n5" 3
    MC-AltClick $app $ed 9
    Cmd $app 'IDM_EDIT_LINEUP'
    CkEdText 'T54.42 every cursor line moves' $app "2`r`n1`r`n4`r`n3`r`n5"
    MC-Type $ed '*'
    CkEdText 'T54.43 the cursors moved with their lines' $app "*2`r`n1`r`n*4`r`n3`r`n5"

    $ed = MC-Start $app "top`r`nbottom" 0
    [CV]::KeyPost($app.Tid, $ed, $WM_SYSKEYDOWN, 0x28, 0x20000001, $false, $false, 300, $true)
    CkEdText 'T54.44 alt+down through the real accelerator loop' $app "bottom`r`ntop"
    Stop-App $app
}
