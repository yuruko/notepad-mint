# check_version.ps1 - version resources, about box and installer defaults must agree (APP_VERSION is in mp.h).
# the reference is FILEVERSION in src\notepad_mint.rc. exit 1 and a line per mismatch otherwise.
#   check_version.ps1 [-Expect 1.2.3]     with -Expect (the release workflow passes the tag's version) the version has to be that one too
param([string]$Expect = '')
$root = Split-Path -Parent $PSScriptRoot
function Slurp([string]$rel) { [IO.File]::ReadAllText((Join-Path $root $rel)) }
function First([string]$text, [string]$re) { $m = [regex]::Match($text, $re); if ($m.Success) { $m.Groups[1].Value } else { '(not found)' } }

$rc = Slurp 'src\notepad_mint.rc'
$fv = [regex]::Match($rc, 'FILEVERSION (\d+),(\d+),(\d+),(\d+)')
if (-not $fv.Success) { Write-Host 'check_version: no FILEVERSION in src\notepad_mint.rc'; exit 1 }
$want = $fv.Groups[1].Value + '.' + $fv.Groups[2].Value + '.' + $fv.Groups[3].Value
$want4 = $want + '.' + $fv.Groups[4].Value

$found = @(
    @('src\notepad_mint.rc PRODUCTVERSION', ((First $rc 'PRODUCTVERSION (\d+,\d+,\d+,\d+)') -replace ',', '.'), $want4),
    @('src\notepad_mint.rc FileVersion',    (First $rc 'VALUE "FileVersion", "([^"]+)"'),    $want4),
    @('src\notepad_mint.rc ProductVersion', (First $rc 'VALUE "ProductVersion", "([^"]+)"'), $want4),
    @('src\notepad_mint.manifest',          (First (Slurp 'src\notepad_mint.manifest') 'name="notepad\.mint" version="([^"]+)"'), $want4),
    @('src\mp.h APP_VERSION',               (First (Slurp 'src\mp.h') '#define APP_VERSION\s+L"([^"]+)"'), $want),
    @('tools\installer.bat default',        (First (Slurp 'tools\installer.bat') '==""\s+set "VER=([^"]+)"'), $want),
    @('installer\notepad-mint.nsi default', (First (Slurp 'installer\notepad-mint.nsi') '!define VERSION "([^"]+)"'), $want))

$bad = 0
foreach ($row in $found) {
    if ($row[1] -ne $row[2]) { Write-Host ('check_version: ' + $row[0] + ' is ' + $row[1] + ', wanted ' + $row[2]); $bad++ }
}
if ($Expect -and $Expect -ne $want) { Write-Host ('check_version: the files say ' + $want + ', wanted ' + $Expect); $bad++ }
if ($bad) { exit 1 }
Write-Host ('check_version: all version strings say ' + $want)
exit 0
