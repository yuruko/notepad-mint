# check_imports.ps1 - the exe may only link kernel32 / user32 / gdi32 statically (everything else is LoadLibrary'd at run time).
#   powershell -NoProfile -File tools\check_imports.ps1 [-Exe <path>]
# reads the pe import table itself (no dumpbin / vs environment needed), so it runs the same locally and in ci.
# also checks the exe is a 32-bit (x86) pe and has no delay-load imports. exit code 1 = violation.
param([string]$Exe = (Join-Path $PSScriptRoot "..\build\notepad mint.exe"))

$allowed = @('kernel32.dll', 'user32.dll', 'gdi32.dll')
$b = [System.IO.File]::ReadAllBytes((Resolve-Path $Exe))
function U16([int]$o) { return [int][BitConverter]::ToUInt16($b, $o) }
function U32([int]$o) { return [int64][BitConverter]::ToUInt32($b, $o) }

if ($b.Length -lt 0x40 -or $b[0] -ne 0x4D -or $b[1] -ne 0x5A) { Write-Output "FAIL not an exe (no MZ header)"; exit 1 }
$pe = [int](U32 0x3C)
if ((U32 $pe) -ne 0x4550) { Write-Output "FAIL no PE signature"; exit 1 }
$machine = U16 ($pe + 4)
$nsec = U16 ($pe + 6)
$optSize = U16 ($pe + 20)
$opt = $pe + 24
$magic = U16 $opt
if ($machine -ne 0x14C -or $magic -ne 0x10B) { Write-Output ("FAIL not a 32-bit x86 pe (machine 0x{0:X}, magic 0x{1:X})" -f $machine, $magic); exit 1 }

$ddir = $opt + 96                                  # pe32 data directories: [1] = imports, [13] = delay imports
$impRva = U32 ($ddir + 1 * 8)
$delayRva = U32 ($ddir + 13 * 8)
$sec = $opt + $optSize
function Rva2Off([int64]$rva) {
    for ($i = 0; $i -lt $nsec; $i++) {
        $s = $sec + $i * 40
        $va = U32 ($s + 12); $vsz = U32 ($s + 8); $raw = U32 ($s + 20); $rsz = U32 ($s + 16)
        $span = [Math]::Max($vsz, $rsz)
        if ($rva -ge $va -and $rva -lt $va + $span) { return [int]($rva - $va + $raw) }
    }
    return -1
}
function CStr([int]$o) {
    $e = $o; while ($b[$e] -ne 0) { $e++ }
    return [System.Text.Encoding]::ASCII.GetString($b, $o, $e - $o)
}

if ($delayRva -ne 0) { Write-Output "FAIL the exe has delay-load imports (use LoadLibraryW + GetProcAddress instead)"; exit 1 }
if ($impRva -eq 0) { Write-Output "FAIL no import table"; exit 1 }
$off = Rva2Off $impRva
$dlls = @()
$funcs = 0
while ($true) {
    $nameRva = U32 ($off + 12)
    $first = U32 ($off + 16)
    if ($nameRva -eq 0 -and $first -eq 0) { break }
    $dll = (CStr (Rva2Off $nameRva)).ToLower()
    $dlls += $dll
    $thunk = Rva2Off ([int64](U32 $off))
    if ($thunk -lt 0) { $thunk = Rva2Off $first }
    while ((U32 $thunk) -ne 0) { $funcs++; $thunk += 4 }
    $off += 20
}
Write-Output ("imports: " + ($dlls -join ', ') + "  ($funcs functions)")
$bad = $dlls | Where-Object { $allowed -notcontains $_ }
if ($bad) { Write-Output ("FAIL imports outside kernel32 / user32 / gdi32: " + ($bad -join ', ')); exit 1 }
Write-Output "imports ok: only kernel32 / user32 / gdi32"
