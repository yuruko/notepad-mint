# isolated EditCaretPos benchmark: native caret movement and the actual status-position calculation, without status painting or document loading in the timing.
param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\..\build\probe\notepad-mint.exe'),
    [int]$SizeMB = 8,
    [int]$Steps = 12000,
    [int]$Span = 384,
    [int]$Runs = 5
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ui_test.ps1') -NoRun -Exe $Exe -Timeout 30000
try {
    $app = Start-App
    $ed = Get-Edit $app
    if ((Snd $ed (0x8000 + 96) 0 0) -ne 0x4D494E54) { throw 'build the SHOTDC probe first: tools\probe.bat /DSHOTDC' }
    $piece = '0123456789 abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ' + "`r`n"
    $builder = New-Object Text.StringBuilder
    while ($builder.Length -lt ($SizeMB * 1024 * 1024)) { [void]$builder.Append($piece) }
    Ed-Set $app $builder.ToString()
    [void](Snd $ed $EM_SETSEL 0 0)
    $times = @()
    for ($j = 0; $j -lt $Runs; $j++) {
        $ms = Snd $ed (0x8000 + 96) $Steps $Span
        $times += [int]$ms
        Write-Output ('caret run {0}: {1} ms' -f ($j + 1), $ms)
    }
    $sorted = @($times | Sort-Object)
    Write-Output ('CARET {0} UTF-16 units ({1:N1} MiB backing buffer), {2} caret calculations alternating {3} units near EOF: median {4} ms; runs {5}' -f $builder.Length, ($builder.Length * 2 / 1MB), $Steps, $Span, $sorted[[int]($sorted.Count / 2)], ($times -join ','))
} finally {
    try { Stop-All } catch {}
    try { Kill-Mine } catch {}
    [U]::DropDesktop()
    $env:APPDATA = $origAppData
    $resolvedWork = [IO.Path]::GetFullPath($work)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolvedWork.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) { Remove-Item -LiteralPath $resolvedWork -Recurse -Force -ErrorAction SilentlyContinue }
}
