@echo off
rem run-one.cmd <build> <file> - one run of run-split.cmd: <build>.out on the C6747 cycle-accurate
rem simulator with harnessFile = <file>, stopped at 30 minutes. Writes <build>.<file>.result.
setlocal
set W=%~dp0
set W=%W:~0,-1%
set WF=%W:\=/%
cd /d "%W%"
if not exist %1.out (echo BUILD windows-%1 file=%2 build=FAILED> %1.%2.result & exit /b 0)
set R=
for /f "tokens=1,*" %%a in ('call C:\ti\ccsv5\ccs_base\scripting\bin\dss.bat "%WF%/runca.js" "%WF%/c6747ca-windows.ccxml" "%WF%/%1.out" "%WF%/%1.%2.cio" 0 1800000 nocache harnessFile^=%2 2^>^&1 ^| findstr /b RESULT') do set R=%%b
if "%R%"=="" (echo BUILD windows-%1 file=%2 build=ok timeout> %1.%2.result) else (echo BUILD windows-%1 file=%2 build=ok %R%> %1.%2.result)
