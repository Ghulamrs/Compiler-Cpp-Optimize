# cpp11's conformance suite: every case of cases.tsv through one cpp11 on x86_64-windows, judged against the
# output cl's build gave (pos) or against a refusal (neg), and compared with baseline.tsv.
#   pos: -S, GRIDE's masm and link with Microsoft's CRT, run; "pass" when the output and exit code are the
#        recorded ones (check "run": the exit code only, the output being unspecified). Otherwise refused,
#        asm-fail, link-fail, differs, crash or timeout.
#   neg: "refused" passes; "accepted" means cpp11 compiled an ill-formed program that needs a diagnostic.
# A case whose baseline passed and that now fails is a regression, and the script exits 1. -Freeze writes this
# run's verdicts into baseline.tsv (the cases run, the others kept), once a release's gate is green.
param(
    [string]$Cpp11 = (Join-Path $PSScriptRoot "..\..\..\..\..\GRIDEd\x64\Release\cpp11.exe"),
    [string]$Levels = "-O0,-O2",
    [string]$Filter = "",
    [string]$Out = (Join-Path $env:TEMP "cpp11-conformance"),
    [int]$Shards = 6,
    [int]$CompileSecs = 60,
    [int]$RunSecs = 20,
    [switch]$Freeze
)
$Cpp11 = (Resolve-Path $Cpp11).Path
$tools = Split-Path -Parent $Cpp11
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
New-Item -ItemType Directory -Force $Out | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = (& $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
$msvc = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$kitLib = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Lib'
$sdk = (Get-ChildItem $kitLib -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'um\x64') } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
$LIB = (Join-Path $msvc.FullName 'lib\x64') + ";$kitLib\$sdk\ucrt\x64;$kitLib\$sdk\um\x64"

$all = @(Import-Csv (Join-Path $PSScriptRoot "cases.tsv") -Delimiter "`t")
$cases = @($all | Where-Object { -not $Filter -or ("$($_.set)/$($_.group)/$($_.case)" -match $Filter) })
$baseFile = Join-Path $PSScriptRoot "baseline.tsv"
$baseline = @{}
if (Test-Path $baseFile) { foreach ($b in (Import-Csv $baseFile -Delimiter "`t")) { $baseline["$($b.set)/$($b.group)/$($b.case)"] = $b } }
"$($cases.Count) of $($all.Count) cases, $Cpp11"

# One shard's cases, in a job of its own.
$work = {
        param($mine, $root, $Out, $Cpp11, $tools, $LIB, $LevelsIn, $CompileSecs, $RunSecs)
        $env:LIB = $LIB
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
                    $r = run $Cpp11 @("-nologo", "-S", "`"$tu`"", "-o", "`"$s`"", "-arch", "x86_64-windows", $lv) "$base.$l.$t.build" $CompileSecs $Out
                    if ($t -eq 1) { $said = (($r.text -split "`r?`n") | Where-Object { $_ -match 'error' } | Select-Object -First 1) }
                    if ($r.rc -eq -999) { $bad = "timeout"; break }
                    if ($r.rc -lt 0 -or $r.rc -gt 1) { $bad = "crash"; break }
                    if ($r.rc -ne 0 -or -not (Test-Path $s)) { $bad = "refused"; break }
                    if ($c.kind -eq "neg") { Remove-Item $s -ErrorAction SilentlyContinue; continue }
                    $r = run "$tools\masm.exe" @("-t", "x64", "`"$s`"", "-o", "`"$o`"") "$base.$l.$t.asm" $CompileSecs $Out
                    Remove-Item $s -ErrorAction SilentlyContinue
                    if (-not (Test-Path $o)) { $bad = "asm-fail"; $said = (($r.text -split "`r?`n") | Where-Object { $_ -match 'error' } | Select-Object -First 1); break }
                    $objs += "`"$o`""
                }
                if ($c.kind -eq "neg") {
                    $row.$l = $(if ($bad -eq "refused") { "refused" } elseif ($null -eq $bad) { "accepted" } else { $bad })
                } elseif ($bad) {
                    $row.$l = $bad
                } else {
                    $exe = "$base.$l.exe"
                    $r = run "$tools\link.exe" (@("/nologo", "/subsystem:console", "`"/out:$exe`"") + $objs + @("libcmt.lib", "libcpmt.lib", "libucrt.lib", "libvcruntime.lib", "kernel32.lib", "legacy_stdio_definitions.lib")) "$base.$l.link" $CompileSecs $Out
                    Remove-Item ($objs -replace '"', '') -ErrorAction SilentlyContinue
                    if (-not (Test-Path $exe)) { $row.$l = "link-fail"; $said = (($r.text -split "`r?`n") | Where-Object { $_ -match 'error' } | Select-Object -First 1) }
                    else {
                        $rr = run $exe @() "$base.$l.run" $RunSecs $Out
                        Remove-Item $exe -ErrorAction SilentlyContinue
                        $want = [IO.File]::ReadAllText((Join-Path $dir "$($c.case).expected"))
                        $okRc = ("$($rr.rc)" -eq "$($c.expect_rc)")
                        if ($rr.rc -eq -999) { $row.$l = "timeout" }
                        elseif ($c.check -eq "run") { $row.$l = $(if ($okRc) { "pass" } else { "differs" }) }
                        elseif ($rr.text -ceq $want -and $okRc) { $row.$l = "pass" }
                        else { $row.$l = "differs"; [IO.File]::WriteAllText("$base.$l.out", $rr.text) }
                    }
                }
                $row."$l`_said" = "$said".Trim()
            }
            [IO.File]::WriteAllText("$base.row.json", ($row | ConvertTo-Json -Compress))
        }
}
function runShards($list, $count) {
    $jobs = @()
    for ($k = 0; $k -lt $count; $k++) {
        $mine = @(); for ($i = $k; $i -lt $list.Count; $i += $count) { $mine += $list[$i] }
        if ($mine.Count -eq 0) { continue }
        $jobs += Start-Job -ScriptBlock $work -ArgumentList $mine, $PSScriptRoot, $Out, $Cpp11, $tools, $LIB, $Levels, $CompileSecs, $RunSecs
    }
    $jobs | Wait-Job | Out-Null
    $jobs | Receive-Job -ErrorAction SilentlyContinue | Out-Null
    $jobs | Remove-Job
}
runShards $cases $Shards
# **A timeout is asked again, alone**: a program that ends by std::terminate can stall in Windows
# Error Reporting while the machine is busy, which is not the compiler's verdict.
$again = @($cases | Where-Object { $f = Join-Path $Out "$($_.set).$($_.group).$($_.case).row.json"; (Test-Path $f) -and ((Get-Content $f -Raw) -match '"timeout"') })
if ($again.Count -gt 0) { "$($again.Count) timed out, run again alone"; runShards $again 1 }

$lvls = @(@($Levels -split ',') | ForEach-Object { $_.TrimStart('-') })
$good = @{ pos = "pass"; neg = "refused" }
$rows = @(foreach ($c in $cases) {
    $f = Join-Path $Out "$($c.set).$($c.group).$($c.case).row.json"
    if (Test-Path $f) { Get-Content $f -Raw | ConvertFrom-Json } else { [pscustomobject]@{ set = $c.set; group = $c.group; case = $c.case; kind = $c.kind } }
})
$lines = @("set`tgroup`tcase`tkind`t" + ($lvls -join "`t") + "`tsaid")
$fixed = @(); $regressed = @()
for ($i = 0; $i -lt $cases.Count; $i++) {
    $c = $cases[$i]; $r = $rows[$i]
    $lines += "$($c.set)`t$($c.group)`t$($c.case)`t$($c.kind)`t" + (($lvls | ForEach-Object { $r.$_ }) -join "`t") + "`t$($r."$($lvls[0])_said")"
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
