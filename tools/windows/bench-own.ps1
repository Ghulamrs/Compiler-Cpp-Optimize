# Run by bench-own.cmd, which sets up the MSVC environment first. Six builds of
# bench-kernels.cpp, each at -O1 and -O2 (cl: /O1 and /O2):
#   own    cxx1 -masm=masm -> the project's MASM -> the project's LINK
#   ms     cxx1 -masm=masm -> ml64 -> link.exe     (same code, Microsoft's tools)
#   cl     cl /c -> link.exe
# `own` against `ms` isolates the assembler and linker; `ms` against `cl` the compiler.
param(
    [string]$Cxx1 = (Join-Path $PSScriptRoot '..\..\cxx1-msvc.exe'),
    [Parameter(Mandatory = $true)][string]$Masm,
    [Parameter(Mandatory = $true)][string]$Link,
    [int]$Rounds = 9,
    [string]$Out = 'C:\cxxopt\bench-own'
)
$ErrorActionPreference = 'Stop'
foreach ($p in $Cxx1, $Masm, $Link) { if (-not (Test-Path $p)) { throw "bench-own: no $p" } }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$src = Join-Path $Out 'bench-kernels.cpp'
Copy-Item (Join-Path $PSScriptRoot 'bench-kernels.cpp') $src -Force
Set-Location $Out
$libs = @('libcmt.lib', 'libucrt.lib', 'libvcruntime.lib', 'kernel32.lib')
$lflags = @('/nologo', '/subsystem:console', '/stack:8388608')

function Median([double[]]$xs) { $s = $xs | Sort-Object; return $s[[int][math]::Floor(($s.Count - 1) / 2)] }
function Check([string]$what) { if ($LASTEXITCODE) { throw "bench-own: $what failed ($LASTEXITCODE)" } }
function TextBytes([string]$obj) {
    $total = 0; $name = ''
    foreach ($line in (dumpbin /nologo /headers $obj)) {
        if ($line -match '^SECTION HEADER #') { $name = '' }
        elseif ($name -eq '' -and $line -match '^\s+(\S+)\s+name$') { $name = $Matches[1] }
        elseif ($name -like '.text*' -and $line -match '^\s+([0-9A-Fa-f]+)\s+size of raw data$') {
            $total += [Convert]::ToInt64($Matches[1], 16) }
    }
    return $total
}

$builds = [ordered]@{}
foreach ($lvl in 'O1', 'O2') {
    & $Cxx1 "-$lvl" -arch x86_64-windows -masm=masm -S $src -o "cxx1-$lvl.asm"; Check "cxx1 -$lvl"
    # own: the project's assembler and linker
    & $Masm /c /nologo /Fo "own-$lvl.obj" "cxx1-$lvl.asm"; Check "MASM -$lvl"
    & $Link @lflags "/out:own-$lvl.exe" "own-$lvl.obj" @libs; Check "LINK -$lvl"
    $builds["own-$lvl"] = "own-$lvl.obj"
    # ms: the same assembly through ml64 and link.exe
    ml64 /c /nologo /Fo "ms-$lvl.obj" "cxx1-$lvl.asm" | Out-Null; Check "ml64 -$lvl"
    link.exe @lflags "/out:ms-$lvl.exe" "ms-$lvl.obj" @libs | Out-Null; Check "link.exe -$lvl"
    $builds["ms-$lvl"] = "ms-$lvl.obj"
    # cl: Microsoft's compiler
    cl /nologo "/$lvl" /EHsc /GR /std:c++14 /c $src "/Fo:cl-$lvl.obj" | Out-Null; Check "cl /$lvl"
    link.exe @lflags "/out:cl-$lvl.exe" "cl-$lvl.obj" @libs | Out-Null; Check "link.exe cl /$lvl"
    $builds["cl-$lvl"] = "cl-$lvl.obj"
}

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
Write-Host ('{0,-9}' -f 'kernel') -NoNewline
foreach ($b in $builds.Keys) { Write-Host ('{0,9}' -f $b) -NoNewline }
Write-Host ''
foreach ($k in 'fib', 'sieve', 'matmul', 'isort', 'hash', 'virtual', 'total') {
    Write-Host ('{0,-9}' -f $k) -NoNewline
    foreach ($b in $builds.Keys) {
        $v = if ($times[$b].ContainsKey($k)) { Median $times[$b][$k] } else { '-' }
        Write-Host ('{0,9}' -f $v) -NoNewline
    }
    Write-Host ''
}
Write-Host ('{0,-9}' -f '.text') -NoNewline
foreach ($b in $builds.Keys) { Write-Host ('{0,9}' -f (TextBytes $builds[$b])) -NoNewline }
Write-Host ''
Write-Host ('{0,-9}' -f 'exe') -NoNewline
foreach ($b in $builds.Keys) { Write-Host ('{0,9}' -f (Get-Item "$b.exe").Length) -NoNewline }
Write-Host ''
$distinct = @($checks.Values | Sort-Object -Unique)
if ($distinct.Count -eq 1) { Write-Host "checksums agree: $($distinct[0])" }
else { Write-Host 'CHECKSUMS DIFFER:'; foreach ($b in $checks.Keys) { Write-Host "  $b  $($checks[$b])" } }
