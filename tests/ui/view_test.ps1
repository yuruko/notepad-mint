Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
public static class IV {
    public static int Count(Bitmap b, int ox, int oy, int x, int y, int w, int h, int r, int g, int blue) {
        int n = 0;
        for (int yy=Math.Max(0,oy+y); yy<Math.Min(b.Height,oy+y+h); yy++)
            for (int xx=Math.Max(0,ox+x); xx<Math.Min(b.Width,ox+x+w); xx++) {
                Color c=b.GetPixel(xx,yy);
                if (c.R==r && c.G==g && c.B==blue) n++;
            }
        return n;
    }
}
'@
function View-Alt($app, [int]$key) {
    [CV]::OnDesktop($deskName)
    [CV]::Focus($app.Tid, $app.Main, (Get-Edit $app))
    [CV]::KeyPost($app.Tid, (Get-Edit $app), 0x104, $key, 0x20000001, $false, $false, 150, $true)
}
function View-Ink($shot, $rgb, [int]$x = 0, [int]$y = 0, [int]$w = 700, [int]$h = 400) {
    [IV]::Count($shot.Bmp, $shot.Ox, $shot.Oy, $x, $y, $w, $h, $rgb[0], $rgb[1], $rgb[2])
}
function Test-T50 {
    $app = Start-App
    $other = Start-App
    $rect = [U]::WRect($app.Main) -join ','
    Ck 'T50.1 each new window starts without always on top' (([U]::ExStyle($app.Main) -band 8) -eq 0 -and ([U]::ExStyle($other.Main) -band 8) -eq 0)
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_VIEW_ONTOP 0)
    Ck 'T50.2 View > always on top sets the native topmost style' (([U]::ExStyle($app.Main) -band 8) -ne 0)
    Ck 'T50.3 another open window keeps its independent topmost state' (([U]::ExStyle($other.Main) -band 8) -eq 0)
    CkEq 'T50.4 pinning preserves window position and size' $rect ([U]::WRect($app.Main) -join ',')
    $third = Start-App
    Ck 'T50.5 a window opened while another is pinned starts unpinned' (([U]::ExStyle($third.Main) -band 8) -eq 0)
    View-Alt $app 0x41
    Ck 'T50.6 Alt+A unpins through the real accelerator loop' ([bool](WaitFor { ([U]::ExStyle($app.Main) -band 8) -eq 0 }))
    View-Alt $app 0x41
    Ck 'T50.7 Alt+A pins again' ([bool](WaitFor { ([U]::ExStyle($app.Main) -band 8) -ne 0 }))
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_VIEW_ONTOP 0)
    Ck 'T50.8 the menu command unpins again' (([U]::ExStyle($app.Main) -band 8) -eq 0)
}
function Test-T51 {
    $pal = Read-Palette
    $idx = 0
    foreach ($theme in @('dark', 'light')) {
        $app = Start-App
        if ($theme -eq 'light') { [void](Snd $app.Main $WM_COMMAND $IDM.IDM_THEME_LIGHT 0) }
        $ed = Get-Edit $app
        $text = "a b`tc`r`n`r`nx" + [char]0xA0 + "y`r`nx" + [char]0x200B + "y`r`nx" + [char]0x200E + "y`r`nx" + [char]0xFEFF + "y`r`nlast "
        Reset-Doc $app $text
        [void](Snd $ed 0x8 0 0)
        $bg = $pal[$theme].edit; $fg = $pal[$theme].text
        $rgb = @(0,1,2 | ForEach-Object { [int][Math]::Floor((3 * $bg[$_] + $fg[$_]) / 4) })
        $shot = Ed-Shot $app
        try { $off = View-Ink $shot $rgb } finally { $shot.Bmp.Dispose() }
        $idx++; Ck ('T51.' + $idx + ' ' + $theme + ': invisible markers start hidden') ($off -eq 0) ('marker pixels ' + $off)
        View-Alt $app 0x53
        [void](Snd $ed 0x8 0 0)
        $shot = Ed-Shot $app
        try {
            $count = View-Ink $shot $rgb
            $idx++; Ck ('T51.' + $idx + ' ' + $theme + ': Alt+S shows marks at exactly 25% foreground opacity') ($count -gt 15) ('marker pixels ' + $count + ', expected rgb ' + ($rgb -join ','))
            $pitch = (Ed-Pos $app 7)[1] - (Ed-Pos $app 0)[1]
            foreach ($mark in @(@(1, 'space'), @(3, 'tab'), @(5, 'newline'), @(7, 'empty line'), @(10, 'nonbreaking space'), @(15, 'zero-width space'), @(20, 'direction mark'), @(25, 'byte order mark'))) {
                $p = Ed-Pos $app $mark[0]
                $n = View-Ink $shot $rgb ($p[0] - 4) $p[1] 22 $pitch
                $idx++; Ck ('T51.' + $idx + ' ' + $theme + ': ' + $mark[1] + ' has a compact visible marker') ($n -gt 0) ('no marker near ' + ($p -join ','))
            }
        } finally { $shot.Bmp.Dispose() }
        $idx++; CkEdText ('T51.' + $idx + ' toggling preserves every original character') $app $text
        $idx++; Ck ('T51.' + $idx + ' toggling preserves clean state, selection and empty undo history') (-not (Ed-Modified $app) -and (Snd $ed $EM_CANUNDO 0 0) -eq 0 -and (([U]::Sel($ed)) -join ',') -eq '0,0')
        [void](Snd $ed $EM_SETSEL 0 -1)
        $shot = Ed-Shot $app
        try {
            $hbg = [System.Drawing.SystemColors]::Highlight; $hfg = [System.Drawing.SystemColors]::HighlightText
            $selected = @([int][Math]::Floor((3*$hbg.R+$hfg.R)/4), [int][Math]::Floor((3*$hbg.G+$hfg.G)/4), [int][Math]::Floor((3*$hbg.B+$hfg.B)/4))
            $idx++; Ck ('T51.' + $idx + ' selected markers use 25% over the highlight colour') ((View-Ink $shot $selected) -gt 15)
        } finally { $shot.Bmp.Dispose() }
        [void](Snd $ed $EM_SETSEL 0 0)
        [void](Snd $app.Main $WM_COMMAND $IDM.IDM_FMT_WRAP 0)
        $ed = Get-Edit $app
        [void](Snd $ed 0x8 0 0)
        $shot = Ed-Shot $app
        try { $idx++; Ck ('T51.' + $idx + ' word wrap preserves visible markers') ((View-Ink $shot $rgb) -gt 15) } finally { $shot.Bmp.Dispose() }
        [void](Snd $ed $EM_SETSEL $text.Length $text.Length)
        Hist-Type $ed ' '
        $changed = $text + ' '
        $idx++; CkEdText ('T51.' + $idx + ' editing inserts ordinary text with marks enabled') $app $changed
        $direct = Ed-Direct $app 300 120
        if ($direct) {
            $shot = Ed-Shot $app
            try {
                $diff = [Bx]::DiffPpm($direct.B, $direct.Off, 300, 0, $shot.Bmp, $shot.Ox, $shot.Oy, 0, 0, 300, 120)
                $idx++; Ck ('T51.' + $idx + ' native direct painting matches the composed frame after editing') ($diff[0] -eq 0) ('different pixels ' + $diff[0])
            } finally { $shot.Bmp.Dispose() }
        } else { Skip 'T51 direct overlay painting' 'requires the SHOTDC probe build' }
        View-Alt $app 0x53
        [void](Snd $ed 0x8 0 0)
        $shot = Ed-Shot $app
        try { $idx++; Ck ('T51.' + $idx + ' Alt+S clears every overlay marker') ((View-Ink $shot $rgb) -eq 0) } finally { $shot.Bmp.Dispose() }
        $idx++; Ck ('T51.' + $idx + ' the visibility toggle preserves undo') (Hist-Undo $ed)
        $idx++; CkEdText ('T51.' + $idx + ' undo restores the original Unicode text') $app $text
        Stop-App $app
    }
}
