# What cpp11 says of a file with more than one thing to say. The conformance suite asks only whether a program is
# refused; here each <name>.cpp is compiled and <name>.diag holds the exit code, then line:column and kind of each
# error, warning and note in order - the words are free to change, the places are not.
#   run-diagnostics.ps1 -Cpp11 <cpp11.exe> [-Write]     -Write records what is said now as the expectation
param(
    [Parameter(Mandatory = $true)][string]$Cpp11,
    [switch]$Write
)
$out = Join-Path $env:TEMP "cpp11-diagnostics"
New-Item -ItemType Directory -Force $out | Out-Null
$failed = 0
$cases = @(Get-ChildItem $PSScriptRoot -Filter *.cpp | Sort-Object Name)
foreach ($c in $cases) {
    $err = Join-Path $out "$($c.BaseName).err"
    $p = Start-Process $Cpp11 -ArgumentList @("-nologo", "-S", "`"$($c.FullName)`"", "-o", "`"$out\$($c.BaseName).s`"", "-arch", "x86_64-windows", "-O0") -NoNewWindow -PassThru -Wait -RedirectStandardError $err
    $said = @("exit $($p.ExitCode)")
    foreach ($l in (Get-Content $err)) {
        if ($l -match ':(\d+):(\d+): (error|warning|note): ') { $said += "$($Matches[1]):$($Matches[2]) $($Matches[3])" }
    }
    $expected = [IO.Path]::ChangeExtension($c.FullName, ".diag")
    if ($Write) { [IO.File]::WriteAllText($expected, ($said -join "`n") + "`n"); continue }
    $want = @(Get-Content $expected)
    if (($said -join "|") -cne ($want -join "|")) {
        "DIFFERS $($c.BaseName)"
        "  said:  $($said -join ', ')"
        "  wants: $($want -join ', ')"
        $failed++
    }
}
"$($cases.Count - $failed) of $($cases.Count) diagnostics cases as expected"
exit $(if ($failed -gt 0) { 1 } else { 0 })
