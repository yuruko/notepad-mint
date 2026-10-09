# Loaded by ui_test.ps1. Navigation after scrolling must start at the logical
# caret, including when EDIT has parked its OS caret outside the viewport.
function Test-T43 {
    $app = Start-App
    try {
        [CV]::OnDesktop($deskName)
        $ed = Get-Edit $app
        [CV]::Focus($app.Tid, $app.Main, $ed)
        [void][Nd]::Size($app.Main, 430, 240)
        $idx = 0
        $mixed = (Chars @(0x65E5, 0x672C, 0x65, 0x0301, 0xD83D, 0xDE00)) + ' abcdef '
        foreach ($wrap in @($false, $true)) {
            if ($wrap) {
                [void](Snd $app.Main $WM_COMMAND $IDM.IDM_FMT_WRAP 0)
                $ed = Get-Edit $app
                [CV]::Focus($app.Tid, $app.Main, $ed)
            }
            $line = if ($wrap) { $mixed * 12 } else { 'abcdefghijklmnopqrstuvwxyz' }
            $text = ((0..99 | ForEach-Object { $line }) -join "`r`n")
            $starts = Line-Starts $text
            Reset-Doc $app $text
            Caret-Place $app $text.Length
            Ed-Dirty $app '!'
            foreach ($direction in @(-1, 1)) {
                foreach ($key in @(
                    @{ Name = 'up'; Code = 0x26 },
                    @{ Name = 'down'; Code = 0x28 },
                    @{ Name = 'left'; Code = 0x25 },
                    @{ Name = 'right'; Code = 0x27 },
                    @{ Name = 'home'; Code = 0x24 },
                    @{ Name = 'end'; Code = 0x23 },
                    @{ Name = 'page up'; Code = 0x21 },
                    @{ Name = 'page down'; Code = 0x22 }
                )) {
                    $origin = $starts[45] + 12
                    Caret-Place $app $origin
                    [void](Caret-StableSnapshot $app)
                    [void](Snd $ed $WM_KEYDOWN $key.Code 0)
                    $expected = [U]::Sel($ed)
                    if (-not $wrap -and $key.Code -in @(0x26, 0x28)) {
                        $want = $starts[45 + $(if ($key.Code -eq 0x26) { -1 } else { 1 })] + 12
                        $idx++; CkSel ('T43.' + $idx + ' visible ' + $key.Name + ' moves exactly one line') $app $want $want
                    }
                    Caret-Place $app $origin
                    [void](Caret-StableSnapshot $app)
                    # Wheel up/down far enough to hide the caret above/below.
                    $wheel = -120 * $direction
                    for ($j = 0; $j -lt 25; $j++) { [void](Snd $ed 0x20A ($wheel -shl 16) 0) }
                    $first = Snd $ed 0xCE 0 0
                    Start-Sleep -Milliseconds 150
                    $idx++; Ck ('T43.' + $idx + ' wrap ' + $wrap + ', wheel ' + $direction + ': caret stays put and scrolling settles') ((Wait-Sel $ed $origin $origin) -and (Snd $ed 0xCE 0 0) -eq $first -and -not [CV]::Visible((Caret-Snapshot $app))) ('selection ' + $script:lastSel + ', first row ' + (Snd $ed 0xCE 0 0))
                    [void](Snd $ed $WM_KEYDOWN $key.Code 0)
                    $idx++; CkSel ('T43.' + $idx + ' wrap ' + $wrap + ', wheel ' + $direction + ': ' + $key.Name + ' resumes from the original caret') $app $expected[0] $expected[1]
                    $idx++; Ck-CaretVisible ('T43.' + $idx + ' navigation restores the caret view') $app
                }
            }
            foreach ($backward in @($false, $true)) {
                $a = $starts[43] + 8; $b = $starts[47] + 12
                $anchor = if ($backward) { $b } else { $a }
                $active = if ($backward) { $a } else { $b }
                foreach ($key in @(0x26, 0x28)) {
                    [void](Snd $ed $EM_SETSEL $anchor $active)
                    [void](Snd $ed 0xB7 0 0)
                    [void](Caret-StableSnapshot $app)
                    [CV]::Key($app.Tid, $ed, $key, $true, $false)
                    $expected = [U]::Sel($ed)
                    $idx++; Ck ('T43.' + $idx + ' Shift+arrow extends the native selection') ($expected[0] -ne $a -or $expected[1] -ne $b) ('selection ' + ($expected -join ','))
                    [void](Snd $ed $EM_SETSEL $anchor $active)
                    [void](Snd $ed 0xB7 0 0)
                    [void](Snd $ed 0x115 6 0)                              # SB_TOP: caret below the view
                    $idx++; CkSel ('T43.' + $idx + ' scrollbar preserves selection bounds') $app $a $b
                    [CV]::Key($app.Tid, $ed, $key, $true, $false)
                    $idx++; CkSel ('T43.' + $idx + ' wrap ' + $wrap + ', backward ' + $backward + ': Shift+arrow keeps the original anchor') $app $expected[0] $expected[1]
                    $idx++; Ck-CaretVisible ('T43.' + $idx + ' selected navigation reveals its active endpoint') $app
                }
            }
            # Ctrl+up/down must keep their native behavior in the manually
            # chosen view rather than first revealing the logical caret.
            Caret-Place $app ($starts[70] + 12)
            [void](Snd $ed 0x115 6 0)
            [void](Snd $ed 0xB6 0 10)
            $first = Snd $ed 0xCE 0 0
            foreach ($key in @(0x28, 0x26)) {
                [CV]::Key($app.Tid, $ed, $key, $false, $true)
                $now = Snd $ed 0xCE 0 0
                $idx++; Ck ('T43.' + $idx + ' Ctrl+arrow preserves the chosen view') ([Math]::Abs($now - $first) -le 1 -and -not [CV]::Visible((Caret-Snapshot $app))) ('first row before ' + $first + ', after ' + $now)
                $first = $now
                $idx++; CkSel ('T43.' + $idx + ' Ctrl+arrow leaves the logical caret unchanged') $app ($starts[70] + 12) ($starts[70] + 12)
            }
            $idx++; CkEdText ('T43.' + $idx + ' scrolling/navigation preserve text and the existing edit') $app ($text + '!')
            $idx++; Ck ('T43.' + $idx + ' scrolling/navigation preserve modified and undo state') ((Ed-Modified $app) -and (Snd $ed $EM_CANUNDO) -ne 0) 'modified or undo state lost'
            [void](Snd $ed $EM_UNDO 0 0)
            $idx++; CkEdText ('T43.' + $idx + ' the pre-scroll undo step still restores the original text') $app $text
        }
        [void](Snd $app.Main $WM_COMMAND $IDM.IDM_FMT_WRAP 0)
        $ed = Get-Edit $app
        [CV]::Focus($app.Tid, $app.Main, $ed)
        $text = ((('0123456789' * 50) + "`r`n") * 80)
        $starts = Line-Starts $text
        Reset-Doc $app $text
        foreach ($key in @(0x26, 0x28)) {
            $origin = $starts[45] + 250
            Caret-Place $app $origin
            [void](Snd $ed $WM_KEYDOWN $key 0)
            $expected = [U]::Sel($ed)
            Caret-Place $app $origin
            [void](Snd $ed 0x114 6 0)                                      # SB_LEFT: caret to the right of the view
            [void](Snd $ed 0x115 6 0)                                      # SB_TOP: caret below the view too
            $idx++; CkSel ('T43.' + $idx + ' horizontal and vertical scroll leave the cursor in place') $app $origin $origin
            [void](Snd $ed $WM_KEYDOWN $key 0)
            $idx++; CkSel ('T43.' + $idx + ' navigation restores both scroll axes before moving') $app $expected[0] $expected[1]
            $idx++; Ck-CaretVisible ('T43.' + $idx + ' cursor returns into the horizontal and vertical view') $app
        }

        # An oversized native caret uses a display adjustment in 1.0.7. It still
        # needs the original logical row when navigating after a manual scroll.
        $maxPx = [int][Math]::Round(480 * [FP]::Dpi($ed) / 720.0, [MidpointRounding]::AwayFromZero)      # the ctrl+plus ladder tops out at 48 pt (480 tenths)
        for ($j = 0; $j -lt 32 -and ([FP]::Font($ed)).height -gt -$maxPx; $j++) { [void](Snd $app.Main $WM_COMMAND $IDM.IDM_ZOOM_IN 0) }
        $idx++; CkEq ('T43.' + $idx + ' oversized caret case reaches the maximum font size') (-$maxPx) (([FP]::Font($ed)).height)
        [void][Nd]::Size($app.Main, 450, 140)
        $text = ("abcdef`r`n" * 80)
        $starts = Line-Starts $text
        Reset-Doc $app $text
        foreach ($key in @(0x26, 0x28)) {
            $origin = $starts[45] + 2
            Caret-Place $app $origin
            [void](Caret-StableSnapshot $app)
            [void](Snd $ed 0x115 6 0)
            [void](Snd $ed $WM_KEYDOWN $key 0)
            $want = $starts[45 + $(if ($key -eq 0x26) { -1 } else { 1 })] + 2
            $idx++; CkSel ('T43.' + $idx + ' oversized caret navigation resumes on the original logical row') $app $want $want
            $idx++; Ck-CaretVisible ('T43.' + $idx + ' oversized caret returns to view after navigation') $app
        }
    } finally {
        Stop-App $app
        [CV]::Shutdown()
    }
}
