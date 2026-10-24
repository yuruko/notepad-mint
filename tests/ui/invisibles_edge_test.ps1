function IE-Toggle($app) { [void](Snd $app.Main $WM_COMMAND $IDM.IDM_VIEW_INVISIBLES 0) }
function IE-Shot($app) {
    [void](Snd (Get-Edit $app) 0x8 0 0)
    return (Ed-Shot $app)
}
function IE-Diff($a, $b, [int]$w = 320, [int]$h = 100) {
    return ([Bx]::Diff($a.Bmp, $a.Ox, $a.Oy, $b.Bmp, $b.Ox, $b.Oy, $w, $h))[0]
}
function IE-Font($app, [string]$size, [string]$face = '') {
    $dlg = Open-Font $app
    if ($face) {
        if ((Chk $dlg $IDT.ID_MONO) -eq 1) {
            Press $dlg $IDT.ID_MONO
            [void](WaitFor { (Chk $dlg $IDT.ID_MONO) -eq 0 })
        }
        $list = Ctl $dlg $IDT.ID_LIST
        $item = [U]::SndStr($list, $LB_FINDSTRINGEXACT, -1, $face)
        if ($item -lt 0) { Press $dlg $IDCANCEL; [void](Gone $dlg); return $false }
        [void](Snd $list $LB_SETCURSEL $item 0)
    }
    Set-Field $dlg $IDT.ID_SIZE $size
    Press $dlg $IDOK
    [void](Gone $dlg)
    return $true
}
function IE-DirectCheck($app, [string]$label) {
    $width = 96
    $height = [Math]::Min(200, ([U]::CRect((Get-Edit $app)))[3] - (Trim-Px $app))
    $direct = Ed-Direct $app $width $height
    if (-not $direct) { Skip $label 'requires the SHOTDC probe build'; return }
    $shot = IE-Shot $app
    try {
        $d = [Bx]::DiffPpm($direct.B, $direct.Off, $width, 0, $shot.Bmp, $shot.Ox, $shot.Oy, 0, 0, $width, $height)
        if ($d[0] -ne 0) { $shot.Bmp.Save((Join-Path $Root 'build\v140-invisibles-edge-failure.png')) }
        Ck $label ($d[0] -eq 0) ('direct/composed differing pixels ' + $d[0])
    } finally { $shot.Bmp.Dispose() }
}
function Test-T52 {
    $app = Start-App
    $ed = Get-Edit $app
    $idx = 0
    $pal = Read-Palette
    $bg = $pal.dark.edit; $fg = $pal.dark.text
    $rgb = @(0,1,2 | ForEach-Object { [int][Math]::Floor((3*$bg[$_]+$fg[$_])/4) })
    foreach ($sample in @(
        @{ Name='vertical tab'; Code=0x000B }, @{ Name='soft hyphen'; Code=0x00AD },
        @{ Name='combining grapheme joiner'; Code=0x034F }, @{ Name='arabic letter mark'; Code=0x061C },
        @{ Name='thin space'; Code=0x2009 }, @{ Name='directional isolate'; Code=0x2066 },
        @{ Name='variation selector'; Code=0xFE0F }, @{ Name='annotation anchor'; Code=0xFFF9 },
        @{ Name='supplementary shorthand control'; Code=0x1BCA0 },
        @{ Name='supplementary language tag'; Code=0xE0001 }, @{ Name='supplementary variation selector'; Code=0xE0100 }
    )) {
        $text = 'X' + [char]::ConvertFromUtf32($sample.Code) + 'Y'
        Reset-Doc $app $text
        $before = IE-Shot $app
        IE-Toggle $app
        $after = IE-Shot $app
        try {
            $d = IE-Diff $before $after
            $idx++; Ck ('T52.'+$idx+' '+$sample.Name+' has a display marker') ($d -gt 0) 'overlay did not alter any viewport pixel'
            $idx++; CkEdText ('T52.'+$idx+' '+$sample.Name+' remains unchanged in the document') $app $text
        } finally { $before.Bmp.Dispose(); $after.Bmp.Dispose() }
        IE-Toggle $app
    }

    Reset-Doc $app 'ab cd'
    $plainSpace = Ed-Pos $app 2; $plainNext = Ed-Pos $app 3
    $spaceWidth = $plainNext[0] - $plainSpace[0]
    $mixed = 'ab ' + (Chars @(0x05D0,0x05D1,0x05D2)) + ' cd'
    Reset-Doc $app $mixed
    $prefix = Ed-Pos $app 2; $following = Ed-Pos $app 7
    $rtlLeft = $prefix[0] + $spaceWidth
    $rtlRight = $following[0] - $spaceWidth
    $before = IE-Shot $app
    IE-Toggle $app
    $after = IE-Shot $app
    try {
        $idx++; Ck ('T52.'+$idx+' a space before an ordinary bidi run is marked inside its actual blank cell') ((View-Ink $after $rgb $prefix[0] $prefix[1] $spaceWidth 40) -gt 0)
        $idx++; Ck ('T52.'+$idx+' a space after an ordinary bidi run is marked in the blank before the following Latin letter') ((View-Ink $after $rgb $rtlRight $following[1] $spaceWidth 40) -gt 0)
        $glyphDiff = [Bx]::Diff($before.Bmp, $before.Ox+$rtlLeft, $before.Oy, $after.Bmp, $after.Ox+$rtlLeft, $after.Oy, [Math]::Max(1,$rtlRight-$rtlLeft), 50)
        if ($glyphDiff[0] -ne 0) { $after.Bmp.Save((Join-Path $Root 'build\v140-invisibles-bidi-failure.png')) }
        $idx++; Ck ('T52.'+$idx+' toggling invisibles preserves every existing Hebrew glyph pixel at an ordinary bidi boundary') ($rtlRight -gt $rtlLeft -and $glyphDiff[0] -eq 0) ('glyph bounds '+$rtlLeft+'..'+$rtlRight+', changed pixels '+$glyphDiff[0])
    } finally { $before.Bmp.Dispose(); $after.Bmp.Dispose() }
    IE-Toggle $app

    $hebrew = Chars @(0x05E9,0x05DC,0x05D5,0x05DD)
    Reset-Doc $app ($hebrew + " `t" + $hebrew + "`r`n" + $hebrew)
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_DIR_RTL 0)
    IE-Toggle $app
    $shot = IE-Shot $app
    try {
        $p = Ed-Pos $app 4; $q = Ed-Pos $app 5
        $left = [Math]::Min($p[0],$q[0]); $width = [Math]::Abs($p[0]-$q[0])
        $idx++; Ck ('T52.'+$idx+' RTL space marker follows the native character cell') ((View-Ink $shot $rgb $left $p[1] ([Math]::Max(1,$width)) 40) -gt 0)
    } finally { $shot.Bmp.Dispose() }
    IE-Toggle $app
    [void](Snd $app.Main $WM_COMMAND $IDM.IDM_DIR_LTR 0)

    if (IE-Font $app '18' 'Segoe UI') {
        Reset-Doc $app "Wi ii`tW"
        IE-Toggle $app
        $shot = IE-Shot $app
        try {
            $a = Ed-Pos $app 0; $b = Ed-Pos $app 1; $p = Ed-Pos $app 2; $q = Ed-Pos $app 3
            $idx++; Ck ('T52.'+$idx+' the proportional-font case has unequal native advances') (($b[0]-$a[0]) -ne ($p[0]-$b[0]))
            $idx++; Ck ('T52.'+$idx+' the proportional space dot stays inside its native cell') ((View-Ink $shot $rgb $p[0] $p[1] ([Math]::Max(1,$q[0]-$p[0])) 50) -gt 0)
        } finally { $shot.Bmp.Dispose() }
        IE-Toggle $app
    } else { Skip 'T52 proportional font' 'Segoe UI is unavailable' }

    [void](IE-Font $app '200' 'Consolas')
    Reset-Doc $app " `t`r`n "
    IE-Toggle $app
    $shot = IE-Shot $app
    try { $idx++; Ck ('T52.'+$idx+' 200 pt retains visible subtle markers') ((View-Ink $shot $rgb 0 0 700 400) -gt 3) } finally { $shot.Bmp.Dispose() }
    [CV]::OnDesktop($deskName)
    [CV]::Focus($app.Tid,$app.Main,$ed)
    [void](Snd $ed 0x7 0 0)
    Caret-Place $app 0
    [void][Nd]::Size($app.Main,640,240)
    $idx++; Ck-CaretVisible ('T52.'+$idx+' 200 pt caret stays visible in a window shorter than the font') $app
    IE-Toggle $app
    [void][Nd]::Size($app.Main,900,600)

    Reset-Doc $app ("X`t"+('X'*20))
    [void](Snd $ed 0xB6 2 0)
    $tabStart = Ed-Pos $app 1; $tabEnd = Ed-Pos $app 2
    $before = IE-Shot $app
    IE-Toggle $app
    $after = IE-Shot $app
    try {
        $idx++; Ck ('T52.'+$idx+' the clipped-tab case starts left of the viewport with part of its cell visible') ($tabStart[0] -lt 0 -and $tabEnd[0] -gt 0) ('native tab positions '+($tabStart -join ',')+' -> '+($tabEnd -join ','))
        $idx++; Ck ('T52.'+$idx+' a partly clipped 200 pt tab still shows its visible marker') ((IE-Diff $before $after 500 400) -gt 0) 'no visible marker in the remaining tab cell'
    } finally { $before.Bmp.Dispose(); $after.Bmp.Dispose() }
    IE-Toggle $app

    $plain = 'x' * 1000
    Reset-Doc $app $plain
    $alias = -1
    for ($i=32; $i -lt 1000; $i++) {
        $p = Ed-Pos $app $i
        if ($p[0] -ge 12 -and $p[0] -lt 280 -and $p[1] -ge 0) { $alias=$i; break }
    }
    if ($alias -ge 0) {
        Reset-Doc $app ($plain.Substring(0,$alias)+' '+$plain.Substring($alias+1))
        $before = IE-Shot $app
        IE-Toggle $app
        $after = IE-Shot $app
        try { $idx++; Ck ('T52.'+$idx+' offscreen 200 pt whitespace cannot create a phantom marker through 16-bit coordinate wrapping') ((IE-Diff $before $after 300 100) -eq 0) ('aliased character index '+$alias) }
        finally { $before.Bmp.Dispose(); $after.Bmp.Dispose() }
        IE-Toggle $app
    } else { Skip 'T52 native x coordinate alias' 'this native edit control does not expose an aliased coordinate' }

    [void](IE-Font $app '11' 'Consolas')
    $long = ('x' * 120000) + ' '
    Reset-Doc $app $long
    $before = IE-Shot $app
    $watch = [Diagnostics.Stopwatch]::StartNew()
    IE-Toggle $app
    $after = IE-Shot $app
    $elapsed = $watch.ElapsedMilliseconds
    try {
        $idx++; Ck ('T52.'+$idx+' a 120,001-character row paints the viewport without offscreen marks') ((IE-Diff $before $after) -eq 0)
        $idx++; Ck ('T52.'+$idx+' a very long row keeps visibility toggling responsive') ($elapsed -lt 4000) ('toggle and screenshot '+$elapsed+' ms')
    } finally { $before.Bmp.Dispose(); $after.Bmp.Dispose() }

    $rows = ((0..39 | ForEach-Object { "a b`t c" }) -join "`r`n")
    Reset-Doc $app $rows
    [void][Nd]::Size($app.Main,550,255)
    [void](Snd $ed 0xB6 0 3)
    IE-DirectCheck $app 'T52 scrolling and a partial bottom row leave the native and composed overlay identical'
    Reset-Doc $app ('x'*200)
    IE-DirectCheck $app 'T52 replacing marked rows removes stale marks from the native window'
    $shot = IE-Shot $app
    try { $idx++; Ck ('T52.'+$idx+' replacing all invisible characters leaves no overlay pixels') ((View-Ink $shot $rgb 0 0 320 100) -eq 0) } finally { $shot.Bmp.Dispose() }
}
