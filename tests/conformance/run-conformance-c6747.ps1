# cpp11's conformance suite on the TMS320C6747: every case of cases.tsv through one cpp11 with -arch tms6747,
# as run-conformance.ps1 takes them through it for x86_64-windows, and compared with baseline-c6747.tsv.
#   pos: -S, GRIDE's asm6x and lnk6x against RTS6x (rts6x.lib) with the flat memory map GRIDE's builds write,
#        then sim6747 --run on the .out; "pass" when the output and exit code are the recorded ones - the output
#        cl's build gave on Windows, line ends aside (check "run": the exit code only). Otherwise refused,
#        asm-fail, link-fail, differs, crash or timeout.
#   neg: "refused" passes; "accepted" means cpp11 compiled an ill-formed program that needs a diagnostic.
# The expected output is Windows': a case that prints what the C6747 rightly has otherwise (a size, a type's
# range, what RTS6x documents it leaves out - docs\EXCLUSIONS.md) "differs" without being a defect. The verdicts
# are a measurement to read, case by case, before any of them is called one.
# A case whose baseline passed and that now fails is a regression, and the script exits 1. -Freeze writes this
# run's verdicts into baseline-c6747.tsv (the cases run, the others kept).
param(
    [string]$Cpp11 = (Join-Path $PSScriptRoot "..\..\..\..\..\GRIDEd\x64\Release\cpp11.exe"),
    [string]$Levels = "-O0,-O2",
    [string]$Filter = "",
    [string]$Out = (Join-Path $env:TEMP "cpp11-conformance-c6747"),
    [int]$Shards = 6,
    [int]$CompileSecs = 300,
    [int]$RunSecs = 600,
    [switch]$Freeze
)
Import-Module Microsoft.PowerShell.Utility
$Cpp11 = (Resolve-Path $Cpp11).Path
$tools = Split-Path -Parent $Cpp11
foreach ($need in "asm6x.exe", "lnk6x.exe", "sim6747.exe", "lib\tms6747\rts6x.lib") {
    if (-not (Test-Path (Join-Path $tools $need))) { throw "$need is not beside $Cpp11" }
}
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
New-Item -ItemType Directory -Force $Out | Out-Null

# the memory map GRIDE's own builds write for the C6747 (compile.dll's ti-link.cmd, as White wrote it)
$cmd = Join-Path $Out "ti-link.cmd"
[IO.File]::WriteAllText($cmd, @"
/* one flat memory for the C6747 and every section in it - the conformance suite's, as GRIDE's builds write it */
--rom_model
--stack_size=0x100000
--heap_size=0x1000000
MEMORY
{
    RAM : origin = 0xC0000000, length = 0x04000000
}
SECTIONS
{
    .text        > RAM
    .const       > RAM
    .data        > RAM
    .bss         > RAM
    .far         > RAM
    .fardata     > RAM
    .neardata    > RAM
    .rodata      > RAM
    .cinit       > RAM
    .init_array  > RAM
    .switch      > RAM
    .cio         > RAM
    .stack       > RAM
    .sysmem      > RAM
    .vm6747.eh   > RAM
}
"@)

$all = @(Import-Csv (Join-Path $PSScriptRoot "cases.tsv") -Delimiter "`t")
$cases = @($all | Where-Object { -not $Filter -or ("$($_.set)/$($_.group)/$($_.case)" -match $Filter) })
$baseFile = Join-Path $PSScriptRoot "baseline-c6747.tsv"
$baseline = @{}
if (Test-Path $baseFile) { foreach ($b in (Import-Csv $baseFile -Delimiter "`t")) { $baseline["$($b.set)/$($b.group)/$($b.case)"] = $b } }
"$($cases.Count) of $($all.Count) cases, $Cpp11, -arch tms6747"

# One shard's cases, in a job of its own.
$work = {
        param($mine, $root, $Out, $Cpp11, $tools, $cmd, $LevelsIn, $CompileSecs, $RunSecs)
        $empty = Join-Path $Out "empty.stdin"; if (-not (Test-Path $empty)) { try { [IO.File]::WriteAllText($empty, "") } catch {} }
        # One tool with a time limit: exit code (-999 = timed out) and its stdout and stderr together.
        function run($exe, $argList, $log, $secs, $cwd) {
            $more = @{}; if (@($argList).Count -gt 0) { $more.ArgumentList = $argList }
            try {
                $p = Start-Process $exe @more -NoNewWindow -PassThru -WorkingDirectory $cwd -RedirectStandardInput $empty -RedirectStandardOutput "$log.o" -RedirectStandardError "$log.e"
            } catch { return @{ rc = -998; text = "could not start: $_" } }
            $h = $p.Handle
            $timedOut = $false
            if (-not $p.WaitForExit($secs * 1000)) { $timedOut = $true; try { Stop-Process -Id $p.Id -Force } catch {}; Start-Sleep -Milliseconds 200 }
            else { $p.WaitForExit() }
            $text = ((Get-Content "$log.o", "$log.e" -Raw -ErrorAction SilentlyContinue) -join "")
            if ($null -eq $text) { $text = "" }
            Remove-Item "$log.o", "$log.e" -ErrorAction SilentlyContinue
            return @{ rc = $(if ($timedOut) { -999 } else { $p.ExitCode }); text = $text }
        }
        function firstError($text) { (($text -split "`r?`n") | Where-Object { $_ -match 'error|undefined|unresolved|cannot|refus' } | Select-Object -First 1) }
        foreach ($c in $mine) {
            $dir = Join-Path $root "$($c.kind)\$($c.set)\$($c.group)"
            $tus = @(Join-Path $dir "$($c.case).cpp"); $part = Join-Path $dir "$($c.case).part.cpp"; if (Test-Path $part) { $tus += $part }
            $tag = "$($c.set).$($c.group).$($c.case)"
            $base = Join-Path $Out $tag
            $row = [ordered]@{ set = $c.set; group = $c.group; case = $c.case; kind = $c.kind }
            foreach ($lv in @($LevelsIn -split ',')) {
                $l = $lv.TrimStart('-')
                $objs = @(); $bad = $null; $said = ""; $t = 0
                foreach ($tu in $tus) {
                    $t++; $s = "$base.$l.$t.s"; $o = "$base.$l.$t.obj"
                    $r = run $Cpp11 @("-nologo", "-S", "`"$tu`"", "-o", "`"$s`"", "-arch", "tms6747", $lv) "$base.$l.$t.build" $CompileSecs $Out
                    if ($t -eq 1) { $said = (($r.text -split "`r?`n") | Where-Object { $_ -match 'error' } | Select-Object -First 1) }
                    if ($r.rc -eq -999) { $bad = "timeout"; break }
                    if ($r.rc -lt 0 -or $r.rc -gt 1) { $bad = "crash"; break }
                    if ($r.rc -ne 0 -or -not (Test-Path $s)) { $bad = "refused"; break }
                    if ($c.kind -eq "neg") { Remove-Item $s -ErrorAction SilentlyContinue; continue }
                    $r = run "$tools\asm6x.exe" @("`"$s`"", "-o", "`"$o`"") "$base.$l.$t.asm" $CompileSecs $Out
                    Remove-Item $s -ErrorAction SilentlyContinue
                    if ($r.rc -ne 0 -or -not (Test-Path $o)) { $bad = "asm-fail"; $said = firstError $r.text; break }
                    $objs += "`"$o`""
                }
                if ($c.kind -eq "neg") {
                    $row.$l = $(if ($bad -eq "refused") { "refused" } elseif ($null -eq $bad) { "accepted" } else { $bad })
                } elseif ($bad) {
                    $row.$l = $bad
                } else {
                    $prog = "$base.$l.out"
                    $r = run "$tools\lnk6x.exe" (@("-mv6740", "--abi=eabi", "-i", "`"$tools\lib\tms6747`"", "`"$cmd`"") + $objs + @("-l", "rts6x.lib", "-o", "`"$prog`"")) "$base.$l.link" $CompileSecs $Out
                    Remove-Item ($objs -replace '"', '') -ErrorAction SilentlyContinue
                    if ($r.rc -ne 0 -or -not (Test-Path $prog)) { $row.$l = "link-fail"; $said = firstError $r.text; Remove-Item $prog -ErrorAction SilentlyContinue }
                    else {
                        $rr = run "$tools\sim6747.exe" @("--run", "`"$prog`"") "$base.$l.run" $RunSecs $Out
                        Remove-Item $prog -ErrorAction SilentlyContinue
                        # line ends aside: the expected text is what a Windows program wrote
                        $want = [IO.File]::ReadAllText((Join-Path $dir "$($c.case).expected")) -replace "`r`n", "`n"
                        $got = $rr.text -replace "`r`n", "`n"
                        $okRc = ("$($rr.rc)" -eq "$($c.expect_rc)")
                        if ($rr.rc -eq -999) { $row.$l = "timeout" }
                        elseif ($c.check -eq "run") { $row.$l = $(if ($okRc) { "pass" } else { "differs" }) }
                        elseif ($got -ceq $want -and $okRc) { $row.$l = "pass" }
                        else { $row.$l = "differs" }
                        if ($row.$l -eq "differs") {
                            [IO.File]::WriteAllText("$base.$l.got.txt", $rr.text)
                            # what to read first: the exit code when that is what differs, else the first line that does
                            if (-not $okRc) { $said = "exit $($rr.rc), expected $($c.expect_rc): " + (($got -split "`n") | Where-Object { $_.Trim() } | Select-Object -Last 1) }
                            else {
                                $a = $got -split "`n"; $b = $want -split "`n"; $k = 0
                                while ($k -lt $a.Count -and $k -lt $b.Count -and $a[$k] -ceq $b[$k]) { $k++ }
                                $said = "line $($k + 1): got '$(if ($k -lt $a.Count) { $a[$k] })' expected '$(if ($k -lt $b.Count) { $b[$k] })'"
                            }
                        }
                    }
                }
                $row."$l`_said" = "$said".Trim() -replace "`t", " "
            }
            [IO.File]::WriteAllText("$base.row.json", ($row | ConvertTo-Json -Compress))
        }
}
function runShards($list, $count) {
    $jobs = @()
    for ($k = 0; $k -lt $count; $k++) {
        $mine = @(); for ($i = $k; $i -lt $list.Count; $i += $count) { $mine += $list[$i] }
        if ($mine.Count -eq 0) { continue }
        $jobs += Start-Job -ScriptBlock $work -ArgumentList $mine, $PSScriptRoot, $Out, $Cpp11, $tools, $cmd, $Levels, $CompileSecs, $RunSecs
    }
    $jobs | Wait-Job | Out-Null
    $jobs | Receive-Job -ErrorAction SilentlyContinue | Out-Null
    $jobs | Remove-Job
}
runShards $cases $Shards
# a timeout is asked again, alone: the machine may have been busy
$again = @($cases | Where-Object { $f = Join-Path $Out "$($_.set).$($_.group).$($_.case).row.json"; (Test-Path $f) -and ((Get-Content $f -Raw) -match '"timeout"') })
if ($again.Count -gt 0) { "$($again.Count) timed out, run again alone"; runShards $again 1 }

$lvls = @(@($Levels -split ',') | ForEach-Object { $_.TrimStart('-') })
$good = @{ pos = "pass"; neg = "refused" }
$rows = @(foreach ($c in $cases) {
    $f = Join-Path $Out "$($c.set).$($c.group).$($c.case).row.json"
    if (Test-Path $f) { Get-Content $f -Raw | ConvertFrom-Json } else { [pscustomobject]@{ set = $c.set; group = $c.group; case = $c.case; kind = $c.kind } }
})
$lines = @("set`tgroup`tcase`tkind`t" + ($lvls -join "`t") + "`t" + (($lvls | ForEach-Object { "$_`_said" }) -join "`t"))
$fixed = @(); $regressed = @()
for ($i = 0; $i -lt $cases.Count; $i++) {
    $c = $cases[$i]; $r = $rows[$i]
    $lines += "$($c.set)`t$($c.group)`t$($c.case)`t$($c.kind)`t" + (($lvls | ForEach-Object { $r.$_ }) -join "`t") + "`t" + (($lvls | ForEach-Object { $r."$_`_said" }) -join "`t")
    foreach ($l in $lvls) {
        $b = $baseline["$($c.set)/$($c.group)/$($c.case)"]
        $was = $(if ($b) { $b.$l } else { $null }); $now = $r.$l; $want = $good[$c.kind]
        if ($null -eq $was) { continue }
        if ($was -ne $want -and $now -eq $want) { $fixed += "$l $($c.set)/$($c.group)/$($c.case): $was -> $now" }
        if ($was -eq $want -and $now -ne $want) { $regressed += "$l $($c.set)/$($c.group)/$($c.case): $was -> $now  $($r."$l`_said")" }
    }
}
$lines | Set-Content (Join-Path $Out "results.tsv")
foreach ($k in "pos", "neg") {
    foreach ($l in $lvls) {
        $g = $rows | Where-Object { $_.kind -eq $k } | Group-Object $l | Sort-Object Name
        "$k $l`: " + (($g | ForEach-Object { "$($_.Name)=$($_.Count)" }) -join ", ")
    }
}
"fixed against the baseline: $($fixed.Count)"; $fixed | ForEach-Object { "  $_" }
"regressed against the baseline: $($regressed.Count)"; $regressed | ForEach-Object { "  $_" }
"results: $(Join-Path $Out 'results.tsv')"
if ($Freeze) {
    for ($i = 0; $i -lt $cases.Count; $i++) {
        $c = $cases[$i]; $r = $rows[$i]
        $b = [ordered]@{ set = $c.set; group = $c.group; case = $c.case }
        foreach ($l in @("O0", "O2")) { $b.$l = $(if ($lvls -contains $l) { $r.$l } elseif ($baseline["$($c.set)/$($c.group)/$($c.case)"]) { $baseline["$($c.set)/$($c.group)/$($c.case)"].$l } else { "" }) }
        $baseline["$($c.set)/$($c.group)/$($c.case)"] = [pscustomobject]$b
    }
    $text = @("set`tgroup`tcase`tO0`tO2") + @(foreach ($c in $all) { $b = $baseline["$($c.set)/$($c.group)/$($c.case)"]; if ($b) { "$($b.set)`t$($b.group)`t$($b.case)`t$($b.O0)`t$($b.O2)" } })
    [IO.File]::WriteAllText($baseFile, (($text -join "`n") + "`n"))
    "baseline written: $baseFile"
    exit 0
}
if ($regressed.Count -gt 0) { exit 1 }
