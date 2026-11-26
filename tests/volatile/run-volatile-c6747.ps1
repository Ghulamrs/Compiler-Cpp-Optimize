<#
  run-volatile-c6747.ps1 - what cpp11's optimised code does to volatile objects on the C6747, held to what its
  -O0 code does. volatile-accesses.cpp is compiled with -arch tms6747 at -O0, -O1 and -O2, assembled and linked as
  the conformance run does it, and run by `sim6747 --run --watch <object>` for each of its objects: the loads and
  stores of every object - how many, and the first sixty-four in their order - and what the program prints must be
  the same at the three levels. -O0 is the reference because its code is the program statement by statement.

    .\run-volatile-c6747.ps1 [-Cpp11 <cpp11.exe>] [-Oracle <TI compiler folder>] [-Library <TI library>] [-Show]

  -Oracle: TI's cl6x is asked the same program (its -O2), and where its accesses differ from cpp11's -O0 they are
  listed - a difference to read, not a failure: the standard prescribes the accesses, and two compilers may still
  part where it leaves room (a structure's members, a bit-field's unit). -Show prints every WATCH line.
  Exit 1 when a level differs from -O0.
#>
param([string]$Cpp11 = (Join-Path $PSScriptRoot "..\..\..\..\..\GRIDEd\x64\Release\cpp11.exe"), [string]$Oracle = "", [string]$Library = "rts6740_elf.lib", [switch]$Show)
Import-Module Microsoft.PowerShell.Utility
$ErrorActionPreference = 'Continue'
$Cpp11 = (Resolve-Path $Cpp11).Path
$tools = Split-Path -Parent $Cpp11
$src = Join-Path $PSScriptRoot 'volatile-accesses.cpp'
$work = Join-Path $env:TEMP 'cpp11-volatile-c6747'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null
$map = @('--rom_model', '--stack_size=0x100000', '--heap_size=0x1000000', 'MEMORY', '{', '    RAM : origin = 0xC0000000, length = 0x04000000', '}',
  'SECTIONS', '{', '    .text        > RAM', '    .const       > RAM', '    .data        > RAM', '    .bss         > RAM',
  '    .far         > RAM', '    .fardata     > RAM', '    .neardata    > RAM', '    .rodata      > RAM', '    .cinit       > RAM',
  '    .init_array  > RAM', '    .switch      > RAM', '    .cio         > RAM', '    .stack       > RAM', '    .sysmem      > RAM')
($map + @('    .vm6747.eh   > RAM', '}')) | Set-Content -Encoding ASCII (Join-Path $work 'gride-link.cmd')
($map + @('}')) | Set-Content -Encoding ASCII (Join-Path $work 'ti-link.cmd')

# The objects, and how many bytes of each are watched.
$objects = @()
1..30 | Where-Object { $_ -notin 8, 10, 14, 16, 17, 26, 29 } | ForEach-Object { $objects += "r{0:d2}" -f $_ }
$objects += @('fifo:8', 'half:2', 'small:1', 'device:8', 'pair:8', 'whole:8')

function Watched([string]$out, [string[]]$watch = $objects) {
    $a = @('--run', '--max-cycles', '200000000') + ($watch | ForEach-Object { '--watch'; $_ }) + @($out)
    $said = @(& (Join-Path $tools 'sim6747.exe') @a 2>&1 | ForEach-Object { "$_" })
    return ,$said
}
$runs = [ordered]@{}
foreach ($level in '-O0', '-O1', '-O2') {
    $tag = Join-Path $work "v$level"
    $o = & $Cpp11 -S $src -o "$tag.s" -arch tms6747 $level -nologo 2>&1
    if ($LASTEXITCODE -eq 0) { $o = & (Join-Path $tools 'asm6x.exe') "$tag.s" -o "$tag.obj" 2>&1 }
    if ($LASTEXITCODE -eq 0) { $o = & (Join-Path $tools 'lnk6x.exe') -mv6740 --abi=eabi -i (Join-Path $tools 'lib\tms6747') "$work\gride-link.cmd" "$tag.obj" -l rts6x.lib -o "$tag.out" 2>&1 }
    if ($LASTEXITCODE -ne 0) { "cpp11 ${level}: not built: $(@($o | ForEach-Object { "$_" }) -join ' ')"; exit 1 }
    $runs[$level] = Watched "$tag.out"
}
if ($Oracle) {
    $cl = Join-Path $Oracle 'bin\cl6x.exe'
    $libDir = Join-Path $Oracle 'lib'; $libArg = $Library
    if (Test-Path $Library) { $libDir = Split-Path -Parent (Resolve-Path $Library).Path; $libArg = Split-Path -Leaf $Library }
    $tag = Join-Path $work 'ti'
    $o = & $cl -mv6740 --abi=eabi -O2 -q "-I`"$Oracle\include`"" "--obj_directory=`"$work`"" "`"$src`"" -z -c "-i`"$libDir`"" "`"$work\ti-link.cmd`"" -l $libArg -o "`"$tag.out`"" 2>&1
    if (Test-Path "$tag.out") {
        # An object TI's compiler left out of the program has no symbol to watch: said, and the others watched.
        $names = @(& (Join-Path $Oracle 'bin\nm6x.exe') "$tag.out" 2>$null | ForEach-Object { ($_ -split '\s+')[-1] })
        $there = @($objects | Where-Object { $names -contains ($_ -split ':')[0] })
        $absent = @($objects | Where-Object { $names -notcontains ($_ -split ':')[0] })
        if ($absent.Count) { "TI's cl6x -O2: no object in its program for $($absent -join ', ')" }
        $runs["TI's cl6x -O2"] = Watched "$tag.out" $there
    } else { "TI's cl6x: not built: $(@($o | ForEach-Object { "$_" } | Select-Object -First 3) -join ' ')" }
}
$ref = $runs['-O0']
if ($Show) { $ref | ForEach-Object { "    $_" } }
$failed = 0
foreach ($name in $runs.Keys) {
    if ($name -eq '-O0') { continue }
    $got = $runs[$name]
    $diff = @()
    # What the program printed, line by line; then each object's WATCH line by the object's name, so that an
    # object one program does not hold is one difference and not a shift of all that follow.
    $said = @($ref | Where-Object { $_ -notmatch '^WATCH ' }); $saidGot = @($got | Where-Object { $_ -notmatch '^WATCH ' })
    $n = [Math]::Max($said.Count, $saidGot.Count)
    for ($i = 0; $i -lt $n; $i++) {
        $w = if ($i -lt $said.Count) { $said[$i] } else { '<nothing>' }; $g = if ($i -lt $saidGot.Count) { $saidGot[$i] } else { '<nothing>' }
        if ($w -cne $g) { $diff += "    -O0: $w"; $diff += "    {0}: {1}" -f $name, $g }
    }
    $byName = @{}
    foreach ($l in $got) { if ($l -match '^WATCH (\S+) ') { $byName[$Matches[1]] = $l } }
    foreach ($l in $ref) {
        if ($l -notmatch '^WATCH (\S+) ') { continue }
        $g = if ($byName.ContainsKey($Matches[1])) { $byName[$Matches[1]] } else { "WATCH $($Matches[1]): not in this program" }
        if ($l -cne $g) { $diff += "    -O0: $l"; $diff += "    {0}: {1}" -f $name, $g }
    }
    if ($diff.Count -eq 0) { "{0}: the same accesses and the same output as -O0 ({1} objects watched)" -f $name, $objects.Count }
    else {
        "{0}: DIFFERS from -O0 in {1} lines" -f $name, ($diff.Count / 2)
        $diff | Select-Object -First 24
        if ($name -notlike 'TI*') { $failed++ }
    }
}
exit $(if ($failed -gt 0) { 1 } else { 0 })
