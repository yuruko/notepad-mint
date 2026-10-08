# Loaded by ui_test.ps1. SHOTDC-only hooks run DPI changes and font/hold checks
# inside the app, avoiding cross-process pointers and assumptions about GDI handles.
# These synthetic transitions test ownership; they do not simulate dialog relayout
# on a physical second monitor.

function Resources-Dpi($app, [int]$dpi) {
    $actual = Snd $app.Main (0x8000 + 60) $dpi 0
    if ($actual -ne $dpi) { throw ('DPI hook returned ' + $actual + ', requested ' + $dpi) }
    $rect = [U]::CRect($app.Main)
    [void][U]::Repaint($app.Main, 0, 0, $rect[2], $rect[3])
    $ed = Get-Edit $app
    $rect = [U]::CRect($ed)
    [void][U]::Repaint($ed, 0, 0, $rect[2], $rect[3])
    [void](Snd $app.Main 0 0 0)
}

function Resources-FontHeight($app, [long]$font) {
    return [int](Snd $app.Main (0x8000 + 61) 4 $font)
}

function Test-T42 {
    $app = Start-App
    if ((Snd $app.Main (0x8000 + 60) 96 0) -ne 96) {
        Skip 'T42 UI resources' 'requires the /DSHOTDC probe build'
        Stop-App $app
        return
    }
    $text = "first line`r`nsecond line`r`nthird line"
    Reset-Doc $app $text
    $ed = Get-Edit $app
    [void](Snd $ed $EM_SETSEL 5 5)
    Cmd $app 'IDM_EDIT_FIND'
    $find = Wait-Win $app 'mp_find' 'find'
    Set-Field $find $IDF.ID_WHAT 'first'
    $field = Ctl $find $IDF.ID_WHAT
    $check = Ctl $find $IDF.ID_CASE
    $borrowed = Snd $field 0x31 0 0                                             # WM_GETFONT

    CkEq 'T42.1 nested native-dialog holds disable once and release only their own hold' 63 (Snd $app.Main (0x8000 + 61) 2 0)
    CkEq 'T42.2 a pre-disabled find window remains disabled when an inner hold is released' 7 (Snd $app.Main (0x8000 + 61) 3 0)
    Ck 'T42.3 native-dialog hold probes leave the find dialog enabled with its text intact' ([U]::Enabled($find) -and (Get-Field $find $IDF.ID_WHAT) -ceq 'first') 'find state changed after helper probes'

    $fonts = @{}
    $idx = 3
    foreach ($dpi in @(96, 144, 192)) {
        Resources-Dpi $app $dpi
        $normal = Snd $app.Main (0x8000 + 61) 0 0
        $bold = Snd $app.Main (0x8000 + 61) 1 0
        $fonts[$dpi] = @($normal, $bold)
        $idx++; Ck ('T42.' + $idx + ' ' + $dpi + ' DPI: both cached dialog fonts remain valid') ($normal -ne 0 -and $bold -ne 0 -and (Resources-FontHeight $app $normal) -gt 0 -and (Resources-FontHeight $app $bold) -gt 0) ('normal ' + $normal + ', bold ' + $bold)
        $idx++; Ck ('T42.' + $idx + ' ' + $dpi + ' DPI: existing native/custom controls retain a valid borrowed font') ((Snd $field 0x31 0 0) -eq $borrowed -and (Snd $check 0x31 0 0) -eq $borrowed -and (Resources-FontHeight $app $borrowed) -gt 0) 'find controls lost their original font or its handle became invalid'
    }

    # Native controls create and retire some GDI objects asynchronously. Warm
    # each scale and let those updates settle before comparing the same ending
    # state. The old behavior allocated two unreleased fonts on every change.
    for ($cycle = 0; $cycle -lt 8; $cycle++) {
        foreach ($dpi in @(144, 192, 96)) { Resources-Dpi $app $dpi }
        Start-Sleep -Milliseconds 120
    }
    Start-Sleep -Milliseconds 120
    $before = [U]::GdiCount($app.Proc)
    $reused = $true
    for ($cycle = 0; $cycle -lt 18; $cycle++) {
        foreach ($dpi in @(144, 192, 96)) {
            Resources-Dpi $app $dpi
            if ((Snd $app.Main (0x8000 + 61) 0 0) -ne $fonts[$dpi][0] -or
                (Snd $app.Main (0x8000 + 61) 1 0) -ne $fonts[$dpi][1]) { $reused = $false }
        }
    }
    Start-Sleep -Milliseconds 120
    $after = [U]::GdiCount($app.Proc)
    Info ('T42 GDI objects after warm-up ' + $before + ', after 54 further transitions ' + $after)
    $idx++; Ck ('T42.' + $idx + ' repeated visits reuse the same normal/bold handles at each DPI') $reused 'a previously cached DPI produced another font pair'
    $idx++; Ck ('T42.' + $idx + ' 54 warmed DPI transitions do not increase GDI object count') ($before -gt 0 -and $after -le $before) ('before ' + $before + ', after ' + $after)

    # A modal about dialog supplies a real bold-font borrower while the find
    # dialog stays alive. Both must retain usable handles through a DPI event.
    Cmd $app 'IDM_HELP_ABOUT'
    $about = Wait-Win $app 'mp_about' 'about notepad mint'
    $title = @([U]::Kids($about) | Where-Object { [U]::Cls($_) -eq 'Static' -and [U]::Text($_) -ceq 'notepad mint' }) | Select-Object -First 1
    if (-not $title) { throw 'the about dialog title label was not found' }
    $boldBorrowed = Snd $title 0x31 0 0
    Resources-Dpi $app 144
    $idx++; Ck ('T42.' + $idx + ' an open about label keeps its valid bold font across DPI changes') ($boldBorrowed -eq $fonts[96][1] -and (Snd $title 0x31 0 0) -eq $boldBorrowed -and (Resources-FontHeight $app $boldBorrowed) -gt 0) 'bold font borrower lost its handle'
    Press $about $IDOK
    $idx++; Ck ('T42.' + $idx + ' closing the modal dialog restores the existing find window') ((Gone $about) -and [U]::Enabled($find)) 'about did not close or find remained disabled'

    # The help editor borrows g_fontMenu. Chrome replacement must reassign it
    # before releasing the previous font, including when the main is disabled.
    Cmd $app 'IDM_HELP_TOPICS'
    $help = Wait-Win $app 'mp_help' 'help topics'
    $helpEdit = Ctl $help 1001
    $oldChrome = Snd $helpEdit 0x31 0 0
    $oldHeight = Resources-FontHeight $app $oldChrome
    $helpText = [U]::GetText($helpEdit)
    Resources-Dpi $app 192
    $newChrome = Snd $helpEdit 0x31 0 0
    $newHeight = Resources-FontHeight $app $newChrome
    $idx++; Ck ('T42.' + $idx + ' an open help editor receives a valid replacement chrome font') ($oldChrome -ne 0 -and $newChrome -ne 0 -and $newChrome -ne $oldChrome -and $newHeight -gt $oldHeight -and $oldHeight -gt 0) ('old font ' + $oldChrome + '/' + $oldHeight + ', new font ' + $newChrome + '/' + $newHeight)
    $idx++; CkEq ('T42.' + $idx + ' help font replacement preserves all help text') $helpText ([U]::GetText($helpEdit))
    Pst $help $WM_CLOSE 0 0
    $idx++; Ck ('T42.' + $idx + ' closing help restores the modeless find dialog') ((Gone $help) -and [U]::Enabled($find)) 'help did not close or find remained disabled'

    $idx++; CkText ('T42.' + $idx + ' DPI/font changes preserve document contents') $text (Ed-Text $app)
    $idx++; CkSel ('T42.' + $idx + ' DPI/font changes preserve the insertion point') $app 5 5
    Stop-App $app
}
