@echo off
rem par-one.cmd <name> - <name>.out on the C6747 cycle-accurate simulator, stopped at 30 minutes;
rem writes <name>.result, .stdout and .log. One run of bench-par.cmd.
rem eclipsec is called with a workspace of its own - dss.bat gives every run the one default - and
rem its output goes to a file: the RESULT line read back through for /f lost runs started together.
setlocal
set W=%~dp0
set W=%W:~0,-1%
set WF=%W:\=/%
if "%CCS55%"=="" set CCS55=C:\ti\ccsv5
cd /d "%W%"
if not exist ws mkdir ws 2>nul
"%CCS55%\eclipse\eclipsec.exe" -nosplash -data "%W%\ws\%1" -application com.ti.ccstudio.apps.runScript -dss.rhinoArgs ""%WF%/runca.js" "%WF%/c6747ca-windows.ccxml" "%WF%/%1.out" "%WF%/%1.stdout" 0 1800000" > %1.log 2>&1
findstr /b RESULT %1.log > %1.r 2>nul
set R=
set /p R=< %1.r
rem In parentheses: a line ending in a digit, wall_ms=2, would make "2>" a redirection of stderr.
if "%R%"=="" ((echo BOX %1 timeout-or-failed)> %1.result) else ((echo BOX %1 %R:RESULT =%)> %1.result)
del %1.r 2>nul
