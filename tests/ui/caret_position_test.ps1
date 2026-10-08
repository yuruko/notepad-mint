# logical caret/status positions, against an independent scalar UTF-16 reference after local movement, far jumps and text changes.
Add-Type -TypeDefinition @'
using System;
public static class CP {
    public static string Position(string text, int idx) {
        int line = 1, start = 0;
        for (int i = 0; i < idx; i++) if (text[i] == '\n') { line++; start = i + 1; }
        return line + ":" + (idx - start + 1);
    }
}
'@

function Caret-ReferenceCheck($app, [string]$text, [int[]]$positions) {
    $ed = Get-Edit $app
    foreach ($idx in $positions) {
        [void](Snd $ed $EM_SETSEL $idx $idx)
        $actualIndex = [int][U]::Sel($ed)[1]                                     # the native edit can snap a requested index to a valid caret boundary
        $expected = [CP]::Position($text, $actualIndex)
        $actual = (Status-Text $app) -split ' \| ', 2
        if ($actual[0] -cne $expected) { return ('caret index ' + $actualIndex + ': expected ' + $expected + ', status ' + $actual[0]) }
    }
    return ''
}

function Test-T40 {
    $app = Start-App
    $unicode = (Chars @(0x65, 0x0301, 0x65E5, 0x672C, 0x05E9, 0x05DC, 0x0627, 0xD83D, 0xDE00))
    $docs = @(
        @{ Name = 'CRLF'; Text = ("alpha`r`n`r`nbeta`r`n" + $unicode + "`r`nlast`r`n") },
        @{ Name = 'LF'; Text = ("alpha`n`nbeta`n" + $unicode + "`nlast`n") },
        @{ Name = 'mixed breaks and Unicode'; Text = ("a`r`nb`nc`rd" + $unicode + "`n`r`n" + $unicode + "`tab`n") },
        @{ Name = 'long line'; Text = (('abcdef' * 4000) + $unicode) }
    )
    $idx = 0; $total = 0
    foreach ($wrap in @($false, $true)) {
        if ($wrap) { [void](Snd $app.Main $WM_COMMAND $IDM.IDM_FMT_WRAP 0) }
        foreach ($rtl in @($false, $true)) {
            if ($rtl) { [void](Snd $app.Main $WM_COMMAND $IDM.IDM_RTL 0) }
            foreach ($doc in $docs) {
                Reset-Doc $app $doc.Text
                $text = Ed-Text $app
                $n = $text.Length
                $positions = New-Object Collections.Generic.List[int]
                if ($n -lt 1000) {
                    for ($p = 0; $p -le $n; $p++) { $positions.Add($p) }
                    for ($p = $n; $p -ge 0; $p--) { $positions.Add($p) }         # both sides of every LF, CR, combining mark and surrogate unit
                }
                $positions.Add($n); $positions.Add(0); $positions.Add([int]($n / 2)); $positions.Add($n)
                $random = New-Object Random(41040)
                $p = $n
                for ($j = 0; $j -lt 360; $j++) {
                    if ($j % 7 -eq 0) { $p = $random.Next($n + 1) }
                    else { $p = [Math]::Max(0, [Math]::Min($n, $p + $random.Next(-3, 4))) }
                    $positions.Add($p)
                }
                $why = Caret-ReferenceCheck $app $text $positions.ToArray()
                $total += $positions.Count
                $idx++; Ck ('T40.' + $idx + ' ' + $doc.Name + ', wrap ' + $wrap + ', RTL ' + $rtl + ': local moves and far jumps match the scalar reference') ($why -eq '') $why
            }
            if ($rtl) { [void](Snd $app.Main $WM_COMMAND $IDM.IDM_RTL 0) }
        }
    }

    foreach ($edit in @('WM_SETTEXT', 'EM_REPLACESEL', 'undo')) {
        Reset-Doc $app "abc`ndef`nlast"
        $ed = Get-Edit $app
        [void](Snd $ed $EM_SETSEL 12 12)                                         # cache the last logical line first
        if ($edit -eq 'WM_SETTEXT') { Ed-Set $app "abcXdefXlast" }
        else {
            [void](Snd $ed $EM_SETSEL 3 4)
            [void][U]::SndStr($ed, $EM_REPLACESEL, 1, 'X')                       # same length, one less LF: revision alone must invalidate the cache
            if ($edit -eq 'undo') { [void](Snd $ed $EM_UNDO 0 0) }
        }
        $text = Ed-Text $app
        $why = Caret-ReferenceCheck $app $text @(($text.Length), ($text.Length - 1), 4, 0, ($text.Length))
        $total += 5
        $idx++; Ck ('T40.' + $idx + ' ' + $edit + ': text revision invalidates cached logical lines at the same document length') ($text.Length -eq 12 -and $why -eq '') ('length ' + $text.Length + ', ' + $why)
    }
    Info ('T40 scalar caret/status comparisons: ' + $total)
}
