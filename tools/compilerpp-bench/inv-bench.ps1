# Compiler++ built by cl /O2 and by RIDE's installed cpp11 -O2, the two builds at once; both checked on
# inverse.cpp; then 1000 compiles with each, 3 rounds, the order alternating each round.
$ErrorActionPreference = 'Continue'
$N = 2000; $Rounds = 3; $fails = 0
$ride = 'C:\Program Files\RIDE 4.5\bin\cpp11.exe'
New-Item -ItemType Directory -Force objref | Out-Null
Remove-Item -ErrorAction SilentlyContinue ref.cxb, ride.cxb, x.cxb, cpp_ref.exe, cpp_ride.exe
Write-Output ("host: Windows, {0} cores; reference: {1}" -f $env:NUMBER_OF_PROCESSORS, ((cl 2>&1 | Select-Object -First 1) -replace '\s+$',''))
$pr = Start-Process cmd -ArgumentList '/c cl /nologo /O2 /EHsc /w /D_CRT_SECURE_NO_WARNINGS src\*.cpp /Fe:cpp_ref.exe /Fo:objref\ > ref.build 2>&1' -PassThru -NoNewWindow
$pc = Start-Process cmd -ArgumentList ('/c "' + $ride + '" -O2 src\*.cpp -o cpp_ride.exe > ride.build 2>&1') -PassThru -NoNewWindow
# the handles read at once: without it Start-Process leaves ExitCode and ExitTime empty
$null = $pr.Handle; $null = $pc.Handle
$pr.WaitForExit(); $pc.WaitForExit()
$tr = [int]($pr.ExitTime - $pr.StartTime).TotalMilliseconds; $tc = [int]($pc.ExitTime - $pc.StartTime).TotalMilliseconds
Write-Output ("build (rc ms): reference {0} {1}, cpp11 {2} {3}" -f $pr.ExitCode, $tr, $pc.ExitCode, $tc)
Write-Output ("size: reference {0} bytes, cpp11 {1} bytes" -f (Get-Item cpp_ref.exe).Length, (Get-Item cpp_ride.exe).Length)
& .\cpp_ref.exe -q -o ref.cxb inverse.cpp; $r1 = $LASTEXITCODE
& .\cpp_ride.exe -q -o ride.cxb inverse.cpp; $r2 = $LASTEXITCODE
$same = if ((Get-FileHash ref.cxb).Hash -eq (Get-FileHash ride.cxb).Hash) { 'identical' } else { 'DIFFERENT' }
Write-Output ("compile: rc {0} / {1}, bytecode {2} ({3} bytes)" -f $r1, $r2, $same, (Get-Item ref.cxb).Length)
$o1 = (& .\cpp_ref.exe -run inverse.cpp 2>&1) -join "`n"; $o2 = (& .\cpp_ride.exe -run inverse.cpp 2>&1) -join "`n"
$lines = ($o1 -split "`n")[1..2] -join ' '
if ($o1 -eq $o2) { Write-Output "run: identical - $lines" } else { Write-Output "run: DIFFERENT" }
function Batch($exe) {
    $s = [Diagnostics.Stopwatch]::StartNew()
    for ($i = 0; $i -lt $N; $i++) { & $exe -q -o x.cxb inverse.cpp; if ($LASTEXITCODE -ne 0) { $script:fails++ } }
    $s.Stop(); return [int]$s.ElapsedMilliseconds
}
$ra = @(); $rb = @()
for ($r = 1; $r -le $Rounds; $r++) {
    if ($r % 2 -eq 1) { $a = Batch '.\cpp_ride.exe'; $b = Batch '.\cpp_ref.exe' } else { $b = Batch '.\cpp_ref.exe'; $a = Batch '.\cpp_ride.exe' }
    Write-Output ("round {0}: {1} compiles - cpp11 {2} ms, reference {3} ms" -f $r, $N, $a, $b)
    $ra += $a; $rb += $b
}
$mid = [int][Math]::Floor(($Rounds - 1) / 2)
Write-Output ("median: cpp11 {0} ms, reference {1} ms" -f (($ra | Sort-Object)[$mid]), (($rb | Sort-Object)[$mid]))
Write-Output ("failed compiles: {0} of {1}" -f $fails, ($N * $Rounds * 2))
