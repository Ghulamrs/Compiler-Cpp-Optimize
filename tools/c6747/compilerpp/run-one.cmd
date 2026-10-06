@echo off
rem run-one.cmd <build> <file> - one run of run-split.cmd: <build>.out on the C6747 cycle-accurate
rem simulator with harnessFile = <file>, stopped at 30 minutes. Writes <build>.<file>.result.
rem eclipsec is called with a workspace of its own - dss.bat gives every run the one default - and
rem its output goes to a file: the RESULT line read back through for /f lost runs started together.
setlocal
set W=%~dp0
set W=%W:~0,-1%
set WF=%W:\=/%
if "%CCS55%"=="" set CCS55=C:\ti\ccsv5
cd /d "%W%"
if not exist %1.out (echo BUILD windows-%1 file=%2 build=FAILED> %1.%2.result & exit /b 0)
if not exist ws mkdir ws 2>nul
"%CCS55%\eclipse\eclipsec.exe" -nosplash -data "%W%\ws\%1.%2" -application com.ti.ccstudio.apps.runScript -dss.rhinoArgs ""%WF%/runca.js" "%WF%/c6747ca-windows.ccxml" "%WF%/%1.out" "%WF%/%1.%2.cio" 0 1800000 nocache harnessFile=%2" > %1.%2.log 2>&1
findstr /b RESULT %1.%2.log > %1.%2.r 2>nul
set R=
set /p R=< %1.%2.r
if "%R%"=="" ((echo BUILD windows-%1 file=%2 build=ok timeout-or-failed)> %1.%2.result) else ((echo BUILD windows-%1 file=%2 build=ok %R:RESULT =%)> %1.%2.result)
del %1.%2.r 2>nul
