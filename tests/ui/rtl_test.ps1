# Loaded by ui_test.ps1. T44: right to left reading order (edit.c "rtl", main.c "reading order"): automatic from the first letter that has a
# direction (on open, and while typing into a document whose order the user has not picked), the context menu toggle, ctrl+right shift /
# ctrl+left shift, and the editor's overhang (SBAR_TRIM) on the side of its vertical scrollbar, so right aligned text is never cut off.
# keep this file pure ascii: non-ascii text is built from char codes.
function Is-Rtl($app) { ([CV]::ExStyle((Get-Edit $app)) -band 0x2000) -ne 0 }                  # WS_EX_RTLREADING
function Wait-Rtl($app, [bool]$want, [int]$ms = 2000) { [bool](WaitFor { (Is-Rtl $app) -eq $want } $ms) }
function Shift-Dir($app, [bool]$right, [int]$between = 0) {                     # ctrl down, one shift key down and up, ctrl up (optionally another key in between)
    $ed = Get-Edit $app
    $sc = 0x2A; if ($right) { $sc = 0x36 }                                       # scan codes: left shift 0x2a, right shift 0x36
    $up = [int]0xC0000000
    [CV]::KeyMsg($app.Tid, $ed, 0x100, 0x11, (1 -bor (0x1D -shl 16)), $false, $true)
    [CV]::KeyMsg($app.Tid, $ed, 0x100, 0x10, (1 -bor ($sc -shl 16)), $true, $true)
    if ($between) {
        [CV]::KeyMsg($app.Tid, $ed, 0x100, $between, 1, $true, $true)
        [CV]::KeyMsg($app.Tid, $ed, 0x101, $between, (1 -bor $up), $true, $true)
    }
    [CV]::KeyMsg($app.Tid, $ed, 0x101, 0x10, (1 -bor ($sc -shl 16) -bor $up), $false, $true)
    [CV]::KeyMsg($app.Tid, $ed, 0x101, 0x11, (1 -bor (0x1D -shl 16) -bor $up), $false, $false)
}
function Rtl-File([string]$name, [string]$text) {
    $p = Join-Path $work $name
    [IO.File]::WriteAllText($p, $text, (New-Object Text.UTF8Encoding($true)))
    return $p
}
function Px([double]$v, $app) { return [int][Math]::Round($v * ([U]::Dpi([long]$app.Main)) / 96, [MidpointRounding]::AwayFromZero) }

function Test-T44 {
    $heb = Chars @(0x05E9, 0x05DC, 0x05D5, 0x05DD)                               # shalom
    $ara = Chars @(0x0645, 0x0631, 0x062D, 0x0628, 0x0627)                       # marhaba
    $fH = Rtl-File 't44_hebrew.txt' ($heb + " hello`r`nsecond line`r`n")
    $fL = Rtl-File 't44_latin.txt' ("hello " + $heb + "`r`n")
    $fN = Rtl-File 't44_neutral.txt' ("123 (!) -- " + $ara + " abc`r`n")

    [CV]::OnDesktop($deskName)                                                   # (the worker that sets ctrl / shift on the app's desktop, Shift-Dir)
    $app = Start-App $fL
    $ed = Get-Edit $app
    Ck 'T44.1 a file whose first letter is latin opens left to right' (Wait-Rtl $app $false) ('exstyle 0x' + ('{0:X}' -f [CV]::ExStyle($ed)))
    $w0 = [U]::WRect($ed)
    $trim = Trim-Px $app; $pad = Px $IDM.EDIT_PAD $app
    $fr = [CV]::Format($ed); $cr = [U]::CRect($ed)
    CkEq 'T44.2 left to right without a vertical bar: the text runs to the right edge of the visible part (client - overhang)' ($cr[2] - $trim) $fr[2]

    $app2 = Start-App $fH
    $ed2 = Get-Edit $app2
    Ck 'T44.3 a file whose first letter is hebrew opens right to left' (Wait-Rtl $app2 $true) ('exstyle 0x' + ('{0:X}' -f [CV]::ExStyle($ed2)))
    $w1 = [U]::WRect($ed2); $m0 = [U]::WRect([long]$app.Main); $m2 = [U]::WRect([long]$app2.Main)   # (the second window may be cascaded: compare with its own main window)
    CkEq 'T44.4 ... the editor window moves left by the overhang (it is on the side of the vertical bar now)' ([string]($w0[0] - $m0[0] - $trim) + ',' + ($w0[2] - $m0[0] - $trim)) ([string]($w1[0] - $m2[0]) + ',' + ($w1[2] - $m2[0]))
    $fr = [CV]::Format($ed2); $cr = [U]::CRect($ed2)
    CkEq 'T44.5 ... the right aligned text ends one padding short of the visible right edge (nothing cut off)' ($cr[2] - $pad) $fr[2]
    CkEq 'T44.6 ... and starts one padding after the hidden overhang on the left' ($trim + $pad) $fr[0]
    Stop-App $app2

    $app3 = Start-App $fN
    Ck 'T44.7 digits, brackets and dashes have no direction: the first letter (arabic) decides' (Wait-Rtl $app3 $true) ''
    Stop-App $app3

    Cmd $app 'IDM_FILE_NEW'
    [void](WaitFor { (Ed-Text $app) -eq '' } 2000)
    Ck 'T44.8 file > new starts left to right' (Wait-Rtl $app $false) ''
    Pst (Get-Edit $app) 0x0102 0x05E9 1                                          # WM_CHAR: a hebrew letter typed into the empty document
    Ck 'T44.9 typing a hebrew letter into an empty document switches it to right to left' (Wait-Rtl $app $true) ('text [' + (Show (Ed-Text $app)) + ']')
    [void](Snd (Get-Edit $app) $EM_SETSEL 0 -1)
    Pst (Get-Edit $app) 0x0102 0x61 1                                            # 'a' replaces it: the first letter is latin now
    Ck 'T44.10 ... and back to left to right when the first letter becomes latin (automatic until the user picks)' (Wait-Rtl $app $false) ('text [' + (Show (Ed-Text $app)) + ']')

    Pst (Get-Edit $app) 0x007B (Get-Edit $app) -1                                # WM_CONTEXTMENU from the keyboard: our edit menu
    Start-Sleep -Milliseconds 400
    Pst $app.Main 0x0102 ([int][char]'r') 0                                      # its "&right to left reading order"
    Ck 'T44.11 the edit menu item right to left reading order switches the order' (Wait-Rtl $app $true) ''
    $w2 = [U]::WRect((Get-Edit $app))
    CkEq 'T44.12 ... and moves the editor by the overhang' ([string]($w0[0] - $trim) + ',' + ($w0[2] - $trim)) ([string]$w2[0] + ',' + $w2[2])
    [void](Snd (Get-Edit $app) $EM_SETSEL 0 0)
    Pst (Get-Edit $app) 0x0102 0x62 1                                            # a latin letter at the start: the user picked, nothing automatic
    Start-Sleep -Milliseconds 300
    Ck 'T44.13 ... a picked order stays when the first letter changes' (Is-Rtl $app) ('text [' + (Show (Ed-Text $app)) + ']')

    Shift-Dir $app $false
    Ck 'T44.14 ctrl+left shift sets left to right' (Wait-Rtl $app $false) ''
    CkEq 'T44.15 ... and the editor is back in its place' ([string]$w0[0] + ',' + $w0[2]) ([string]([U]::WRect((Get-Edit $app)))[0] + ',' + ([U]::WRect((Get-Edit $app)))[2])
    Shift-Dir $app $true
    Ck 'T44.16 ctrl+right shift sets right to left' (Wait-Rtl $app $true) ''
    Shift-Dir $app $true
    Start-Sleep -Milliseconds 300
    Ck 'T44.17 ... it sets, it does not toggle (pressed again: still right to left)' (Is-Rtl $app) ''
    Shift-Dir $app $false 0x23                                                   # ctrl+shift+end in between: a selection, not a direction
    Start-Sleep -Milliseconds 300
    Ck 'T44.18 ctrl+shift with another key in between changes nothing' (Is-Rtl $app) ''

    Cmd $app 'IDM_FMT_WRAP'                                                      # word wrap re-creates the editor
    [void](WaitFor { (Get-Edit $app) -ne $ed } 3000)
    Start-Sleep -Milliseconds 300
    Ck 'T44.19 word wrap on keeps right to left (the editor is re-created)' (Is-Rtl $app) ''
    $w3 = [U]::WRect((Get-Edit $app))
    CkEq 'T44.20 ... and its place' ([string]($w0[0] - $trim) + ',' + ($w0[2] - $trim)) ([string]$w3[0] + ',' + $w3[2])
    $fr = [CV]::Format((Get-Edit $app)); $cr = [U]::CRect((Get-Edit $app))
    CkEq 'T44.21 ... the wrapped text ends one padding short of the visible right edge' ($cr[2] - $pad) $fr[2]
    $ed = Get-Edit $app
    Cmd $app 'IDM_FMT_WRAP'
    [void](WaitFor { (Get-Edit $app) -ne $ed } 3000)

    Cmd $app 'IDM_FILE_NEW'
    $box = $null
    try { $box = Wait-Box $app 'notepad mint' 3000 } catch { $box = $null }
    if ($box) { [void](Box-Press $box 102) }                                             # don't save
    Ck 'T44.22 file > new after a picked right to left starts left to right again' (Wait-Rtl $app $false 3000) ''
    Stop-App $app
}

# T45: notepad's command line switches /a (open as ansi) and /w (open as utf-16 le) (main.c CmdSwitch). /p (print) needs a printer: not here.
function Status-Enc($app) {
    foreach ($k in [U]::Kids([long]$app.Main)) { if ([U]::Cls($k) -eq 'mp_status') { return ([U]::GetText($k) -split ' \| ')[-1] } }
    return $null
}
function Test-T45 {
    $f = Join-Path $work 't45.txt'
    [IO.File]::WriteAllBytes($f, [byte[]](0x41, 0x42))                          # "AB": utf-8 by detection, U+4241 as utf-16 le
    $app = Start-App $f
    CkEdText 'T45.1 without a switch "AB" is detected as utf-8' $app 'AB' 3000
    $enc0 = Status-Enc $app
    Stop-App $app
    $app = Start-App ('/w ' + $f)
    CkEdText 'T45.2 /w opens it as utf-16 le: one character U+4241' $app (Chars @(0x4241)) 3000
    Ck 'T45.3 ... the title is the file name (the switch is not part of it)' ((Title $app) -like 't45.txt - *') (Title $app)
    Stop-App $app
    $app = Start-App ('/A ' + $f)
    CkEdText 'T45.4 /A (any case) opens it as ansi: "AB"' $app 'AB' 3000
    $enc = Status-Enc $app
    Ck 'T45.5 ... and the status bar shows another encoding than detection did' ($enc -and $enc -ne $enc0) ('detected [' + $enc0 + '] /a [' + $enc + ']')
    Stop-App $app
}
