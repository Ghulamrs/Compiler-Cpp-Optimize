# Run by bench.cmd, which sets up the MSVC environment first.
#   -New    the cxx1 under test (default: this tree's cxx1-msvc.exe)
#   -Base   a cxx1 to compare it with, e.g. a build of the commit before (optional)
#   -Cppi   a Compiler-Cppi cxx1 with the C6000 target, for the cl6x comparison (optional)
#   -CppiArch  that target's -arch name (default tms6747)
#   -Rounds interleaved rounds per x86 build (default 9); -Out the work directory
param(
    [string]$New = (Join-Path $PSScriptRoot '..\..\cxx1-msvc.exe'),
    [string]$Base = '',
    [string]$Cppi = '',
    [string]$CppiArch = 'tms6747',
    [int]$Rounds = 9,
    [string]$Out = 'C:\cxxopt\bench'
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$src = Join-Path $Out 'bench-kernels.cpp'
Copy-Item (Join-Path $PSScriptRoot 'bench-kernels.cpp') $src -Force
Set-Location $Out

function Median([double[]]$xs) {
    $s = $xs | Sort-Object
    return $s[[int][math]::Floor(($s.Count - 1) / 2)]
}

# The .text of an object, every section whose name begins .text, from dumpbin.
function TextBytes([string]$obj) {
    $total = 0; $name = ''
    foreach ($line in (dumpbin /nologo /headers $obj)) {
        if ($line -match '^SECTION HEADER #') { $name = '' }
        elseif ($name -eq '' -and $line -match '^\s+(\S+)\s+name$') { $name = $Matches[1] }
        elseif ($name -like '.text*' -and $line -match '^\s+([0-9A-Fa-f]+)\s+size of raw data$') {
            $total += [Convert]::ToInt64($Matches[1], 16)
        }
    }
    return $total
}

# ---- x86_64-windows: build each, then time them round by round
$builds = [ordered]@{}
function AddCxx1([string]$label, [string]$cxx) {
    if (-not (Test-Path $cxx)) { Write-Host "bench: no $cxx - '$label' skipped"; return }
    & $cxx -O2 $src -o "$label.exe"; if ($LASTEXITCODE) { throw "bench: $label failed to build" }
    & $cxx -O2 -c $src -o "$label.obj"; if ($LASTEXITCODE) { throw "bench: $label failed to compile" }
    $builds[$label] = "$label.obj"
}
AddCxx1 'cxx1-new' $New
if ($Base) { AddCxx1 'cxx1-base' $Base }
cl /nologo /O2 /EHsc /GR /std:c++14 $src /Fe:cl-O2.exe /Fo:cl-O2.obj | Out-Null
if ($LASTEXITCODE) { throw 'bench: cl /O2 failed' }
$builds['cl-O2'] = 'cl-O2.obj'

$times = @{}; $checks = @{}
foreach ($b in $builds.Keys) { $times[$b] = @{} }
for ($r = 1; $r -le $Rounds; ++$r) {
    foreach ($b in $builds.Keys) {
        foreach ($line in (& ".\$b.exe")) {
            if ($line -match '^check ') { $checks[$b] = $line; continue }
            $name, $v = $line -split ' '
            if (-not $times[$b].ContainsKey($name)) { $times[$b][$name] = @() }
            $times[$b][$name] += [double]$v
        }
    }
}

Write-Host ''
Write-Host ("x86_64-windows, median of {0} interleaved rounds, ms" -f $Rounds)
$kernels = 'fib', 'sieve', 'matmul', 'isort', 'hash', 'virtual', 'total'
Write-Host ('{0,-9}' -f 'kernel') -NoNewline
foreach ($b in $builds.Keys) { Write-Host ('{0,12}' -f $b) -NoNewline }
Write-Host ''
foreach ($k in $kernels) {
    Write-Host ('{0,-9}' -f $k) -NoNewline
    foreach ($b in $builds.Keys) { Write-Host ('{0,12}' -f (Median $times[$b][$k])) -NoNewline }
    Write-Host ''
}
Write-Host ('{0,-9}' -f '.text') -NoNewline
foreach ($b in $builds.Keys) { Write-Host ('{0,12}' -f (TextBytes $builds[$b])) -NoNewline }
Write-Host ''
$distinct = @($checks.Values | Sort-Object -Unique)
if ($distinct.Count -eq 1) { Write-Host "checksums agree: $($distinct[0])" }
else { Write-Host 'CHECKSUMS DIFFER:'; foreach ($b in $checks.Keys) { Write-Host "  $b  $($checks[$b])" } }

# ---- TMS320C6747: cl6x against Compiler-Cppi's cxx1, code size only
$cl6x = Get-ChildItem -Path 'C:\ti' -Recurse -Filter 'cl6x.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $cl6x) { Write-Host ''; Write-Host 'bench: no cl6x.exe under C:\ti - the C6000 comparison skipped'; exit 0 }
$cgt = Split-Path (Split-Path $cl6x.FullName)
$ofd = Join-Path (Split-Path $cl6x.FullName) 'ofd6x.exe'
Write-Host ''
Write-Host "C6000 reference: $($cl6x.FullName)"

# The .text of a C6000 object, from ofd6x's XML listing.
function TiTextBytes([string]$obj) {
    try { [xml]$x = (& $ofd -x $obj) -join "`n" } catch { Write-Host "bench: ofd6x -x gave no XML for $obj"; return 0 }
    $total = 0
    foreach ($s in $x.SelectNodes('//section')) {
        if ($s.name -like '.text*' -and $s.raw_data_size) { $total += [Convert]::ToInt64(($s.raw_data_size -replace '^0x', ''), 16) }
    }
    return $total
}

$tiFlags = @('-mv6740', '--abi=eabi', "-I$cgt\include")
# cl6x names an object after its source, in --obj_directory.
& $cl6x.FullName @tiFlags --opt_level=2 --opt_for_speed=5 -c $src --obj_directory=$Out | Out-Null
if ($LASTEXITCODE) { throw 'bench: cl6x --opt_level=2 failed' }
Move-Item -Force 'bench-kernels.obj' 'cl6x-O2.obj'
Write-Host ('{0,-24}{1,10} bytes .text' -f 'cl6x --opt_level=2', (TiTextBytes 'cl6x-O2.obj'))
if ($Cppi -and (Test-Path $Cppi)) {
    foreach ($lvl in 'O0', 'O2') {
        & $Cppi "-$lvl" -arch $CppiArch -S $src -o "cxx1-c6x-$lvl.asm"
        if ($LASTEXITCODE) { Write-Host "bench: Compiler-Cppi cxx1 -$lvl failed"; continue }
        & $cl6x.FullName @tiFlags -c "cxx1-c6x-$lvl.asm" --obj_directory=$Out | Out-Null
        if ($LASTEXITCODE) { Write-Host "bench: cl6x did not assemble cxx1-c6x-$lvl.asm"; continue }
        Write-Host ('{0,-24}{1,10} bytes .text' -f "Compiler-Cppi cxx1 -$lvl", (TiTextBytes "cxx1-c6x-$lvl.obj"))
    }
} else {
    Write-Host 'bench: no -Cppi given - only cl6x measured (this tree has no C6000 target)'
}
