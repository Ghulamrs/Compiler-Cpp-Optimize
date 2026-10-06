# r50.ps1 - RIDE 5.0 as installed, against TI. Every program of corpus\ is built six ways:
#   c11-O0 c11-O1 c11-O2 c11-Os  the installed cpp11 -> asm6x -> lnk6x (RIDE 5.0\bin), TI's EH runtime
#   ti822-O2                     cl6x 8.2.2 (CCS 7.4), its own linker and runtime
#   ti744-O2                     cl6x 7.4.4 (CCS 5.5), its own linker and runtime
# and every image that links is run on the installed vm6747sim and on CCS 5.5's C6747 cycle-accurate
# simulator (DSS, cycle.CPU). One memory map for all: RIDE's own ti-link.cmd, the one the product writes.
#   powershell -File r50.ps1 [-Phase build|sim|ccs|report|all] [-Max 10]
param([string]$Phase = "all", [int]$Max = 10, [string]$Only = "")
$ErrorActionPreference = "Continue"
$W = Split-Path -Parent $MyInvocation.MyCommand.Path
$Corpus = "$W\corpus"; $Img = "$W\img"; $Src = "$W\src"
$Bin = "C:\Program Files\RIDE 5.0\bin"
$CG8 = "C:\ti\ccsv7\tools\compiler\ti-cgt-c6000_8.2.2"; $Lib8 = "C:\Users\GRA\Documents\VM6747\tilib"
$CG7 = "C:\ti\ccsv5\tools\compiler\c6000_7.4.4";      $Lib7 = "C:\cxx1\c6747-lib"
$Eclipse = "C:\ti\ccsv5\eclipse\eclipsec.exe"
New-Item -ItemType Directory -Force $Img, $Src, "$W\ws" | Out-Null
$Variants = "c11-O0", "c11-O1", "c11-O2", "c11-Os", "ti822-O2", "ti744-O2"
$Progs = Get-Content "$W\list.txt" | ForEach-Object { $p = $_ -split ' '; [pscustomobject]@{ Name = $p[0]; Ext = $p[1] } }
if ($Only) { $Progs = $Progs | Where-Object { $_.Name -like $Only } }

# Run cmd lines, $Max at a time, each with its own timeout; a job is @{ Cmd; Done (file made at the end) ; Secs }.
function Run-Throttled($jobs, $max, $label) {
  $queue = New-Object System.Collections.Queue; $jobs | ForEach-Object { $queue.Enqueue($_) }
  $running = @(); $n = $jobs.Count; $k = 0; $t0 = Get-Date
  while ($queue.Count -gt 0 -or $running.Count -gt 0) {
    while ($queue.Count -gt 0 -and $running.Count -lt $max) {
      $j = $queue.Dequeue()
      $psi = New-Object System.Diagnostics.ProcessStartInfo "cmd.exe", ('/s /c "' + $j.Cmd + '"')
      $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
      $p = [System.Diagnostics.Process]::Start($psi)
      $running += [pscustomobject]@{ P = $p; J = $j; T = Get-Date }
      if ($j.Stagger) { Start-Sleep -Milliseconds $j.Stagger }
    }
    Start-Sleep -Milliseconds 200
    $still = @()
    foreach ($r in $running) {
      if ($r.P.HasExited) { $k++ }
      elseif (((Get-Date) - $r.T).TotalSeconds -gt $r.J.Secs) {
        & taskkill /T /F /PID $r.P.Id 2>&1 | Out-Null; Set-Content $r.J.Timeout "TIMEOUT after $($r.J.Secs)s"; $k++
      } else { $still += $r }
    }
    $running = $still
  }
  "{0}: {1} jobs in {2:N0}s" -f $label, $n, ((Get-Date) - $t0).TotalSeconds
}

$q = '"'
function Q($s) { return $q + $s + $q }

if ($Phase -in "build", "all") {
  # RIDE's own map, word for word as compile.cpp tiLinkCmd writes it
  Copy-Item "$W\ti-link.cmd" "$Img\ti-link.cmd" -Force
  $jobs = @()
  foreach ($p in $Progs) {
    $n = $p.Name; $s = "$Corpus\$n.$($p.Ext)"
    $c11src = $s
    if ($p.Ext -eq "c") { Copy-Item $s "$Src\$n.cpp" -Force; $c11src = "$Src\$n.cpp" }   # cpp11 compiles C++; a .c kernel is built as .cpp, as the suite does
    foreach ($v in $Variants) {
      $o = "$Img\$n.$v"; Remove-Item "$o.out", "$o.build.log" -ErrorAction SilentlyContinue
      if ($v -like "c11-*") {
        $L = $v.Substring(4)
        $cmd = "($(Q "$Bin\cpp11.exe") -arch tms6747 -nologo -$L -S $(Q $c11src) -o $(Q "$o.s") && " +
               "$(Q "$Bin\asm6x.exe") $(Q "$o.s") -o $(Q "$o.obj") && " +
               "$(Q "$Bin\lnk6x.exe") -mv6740 --abi=eabi -i $(Q $Lib7) $(Q "$Img\ti-link.cmd") $(Q "$o.obj") -l rts6740_elf_eh.lib -o $(Q "$o.out")) > $(Q "$o.build.log") 2>&1"
      } else {
        if ($v -like "ti822*") { $cg = $CG8; $lib = $Lib8 } else { $cg = $CG7; $lib = $Lib7 }
        $lang = if ($p.Ext -eq "c") { "" } else { "--exceptions --rtti" }
        $cmd = "($(Q "$cg\bin\cl6x.exe") -mv6740 --abi=eabi -O2 $lang --symdebug:none -I$(Q "$cg\include") -I$(Q $Corpus) --obj_directory=$(Q "$Img\o-$v") -c $(Q $s) && " +
               "$(Q "$cg\bin\cl6x.exe") -mv6740 --abi=eabi -z -i $(Q $lib) -i $(Q "$cg\lib") $(Q "$Img\ti-link.cmd") $(Q "$Img\o-$v\$n.obj") -l rts6740_elf_eh.lib -m $(Q "$o.map") -o $(Q "$o.out")) > $(Q "$o.build.log") 2>&1"
        New-Item -ItemType Directory -Force "$Img\o-$v" | Out-Null
      }
      $jobs += [pscustomobject]@{ Cmd = $cmd; Secs = 300; Timeout = "$o.build.timeout"; Stagger = 0 }
    }
  }
  Run-Throttled $jobs 16 "build"
}

if ($Phase -in "sim", "all") {
  $jobs = @()
  foreach ($p in $Progs) { foreach ($v in $Variants) {
    $o = "$Img\$($p.Name).$v"; if (-not (Test-Path "$o.out")) { continue }
    Remove-Item "$o.sim.txt", "$o.sim.err", "$o.sim.timeout" -ErrorAction SilentlyContinue
    $jobs += [pscustomobject]@{ Cmd = "$(Q "$Bin\vm6747sim.exe") --run -c $(Q "$o.out") > $(Q "$o.sim.txt") 2> $(Q "$o.sim.err") < NUL"; Secs = 600; Timeout = "$o.sim.timeout"; Stagger = 0 }
  } }
  Run-Throttled $jobs 16 "vm6747sim"
}

if ($Phase -in "ccs", "all") {
  $WF = $W -replace '\\', '/'
  $jobs = @()
  foreach ($p in $Progs) { foreach ($v in $Variants) {
    $b = "$($p.Name).$v"; $o = "$Img\$b"; if (-not (Test-Path "$o.out")) { continue }
    if ((Test-Path "$o.ccs.log") -and (Select-String -Quiet -Path "$o.ccs.log" -Pattern "^RESULT")) { continue }   # resumable
    Remove-Item "$o.ccs.cio", "$o.ccs.timeout" -ErrorAction SilentlyContinue
    $args = "-nosplash -data $(Q "$W\ws\$b") -application com.ti.ccstudio.apps.runScript -dss.rhinoArgs " +
            "$q$(Q "$WF/runca.js") $(Q "$WF/c6747ca-windows.ccxml") $(Q "$WF/img/$b.out") $(Q "$WF/img/$b.ccs.cio") 1 1800000$q"
    $jobs += [pscustomobject]@{ Cmd = "$(Q $Eclipse) $args > $(Q "$o.ccs.log") 2>&1"; Secs = 1860; Timeout = "$o.ccs.timeout"; Stagger = 1500 }
  } }
  Run-Throttled $jobs $Max "ccs"
  # a session that never started (no RESULT, no timeout) is run once more, alone
  $again = $jobs | Where-Object { $l = ($_.Timeout -replace 'timeout$', 'log'); -not (Test-Path $_.Timeout) -and -not (Select-String -Quiet -Path $l -Pattern "^RESULT" -ErrorAction SilentlyContinue) }
  if ($again) { Run-Throttled @($again) 2 "ccs again" }
}

if ($Phase -in "report", "all") {
  $rows = @()
  function Norm($f) { if (Test-Path $f) { ([IO.File]::ReadAllText($f) -replace "`r`n", "`n") } else { $null } }
  foreach ($p in $Progs) {
    $exp = Norm "$Corpus\$($p.Name).expected"
    foreach ($v in $Variants) {
      $o = "$Img\$($p.Name).$v"
      $r = [ordered]@{ prog = $p.Name; variant = $v; build = "ok"; simExit = ""; simCycles = ""; ccsCycles = ""; ccsExit = ""; simVsCcs = ""; simVsExp = ""; ccsVsExp = ""; ratio = "" }
      if (-not (Test-Path "$o.out")) { $r.build = if (Test-Path "$o.build.timeout") { "timeout" } else { "failed" }; $rows += [pscustomobject]$r; continue }
      $sim = Norm "$o.sim.txt"; $err = Norm "$o.sim.err"
      if (Test-Path "$o.sim.timeout") { $r.simExit = "TIMEOUT" }
      elseif ($err -match 'count=(\d+)') { $r.simCycles = $Matches[1]; $r.simExit = if ($err -match 'exit=C\$\$EXIT') { "C`$`$EXIT" } elseif ($err -match 'exit=(\S+)') { $Matches[1] } else { "?" } }
      else { $r.simExit = "none" }
      $log = Norm "$o.ccs.log"
      if ($log -and $log -match 'RESULT event=\S+ count=(\d+) pc=(0x[0-9a-f]+) exit=(0x[0-9a-f-]+)') {
        $r.ccsCycles = $Matches[1]; $r.ccsExit = if ($Matches[2] -eq $Matches[3]) { "C`$`$EXIT" } else { "pc=$($Matches[2])" }
      } elseif (Test-Path "$o.ccs.timeout") { $r.ccsExit = "TIMEOUT" } else { $r.ccsExit = "none" }
      $cio = Norm "$o.ccs.cio"; if ($null -eq $cio -and $r.ccsCycles) { $cio = "" }
      if ($null -ne $sim -and $null -ne $cio -and $r.ccsCycles) { $r.simVsCcs = if ($sim -ceq $cio) { "same" } else { "DIFF" } }
      if ($null -ne $sim) { $r.simVsExp = if ($sim -ceq $exp) { "ok" } else { "differ" } }
      if ($null -ne $cio -and $r.ccsCycles) { $r.ccsVsExp = if ($cio -ceq $exp) { "ok" } else { "differ" } }
      if ($r.simCycles -and $r.ccsCycles -and [double]$r.ccsCycles -gt 0) { $r.ratio = "{0:N4}" -f ([double]$r.simCycles / [double]$r.ccsCycles) }
      $rows += [pscustomobject]$r
    }
  }
  $rows | Export-Csv -NoTypeInformation "$W\results.csv"
  "report: $($rows.Count) rows -> results.csv"
}
