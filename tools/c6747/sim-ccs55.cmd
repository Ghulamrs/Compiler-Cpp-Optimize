@echo off
rem sim-ccs55.cmd <prog.c> ... - the Windows CCS 5.5 box: each program built by CCS 5.5's
rem C6000 compiler 7.4.4 and run on the C6747 cycle-accurate simulator through DSS.
rem Writes out\<prog>.windows-ccs55.stdout and .result beside this script.
setlocal enabledelayedexpansion
set W=%~dp0
set W=%W:~0,-1%
if "%CCS55%"=="" set CCS55=C:\ti\ccsv5
set CG=%CCS55%\tools\compiler\c6000_7.4.4
set BOX=windows-ccs55
set WF=%W:\=/%
if not exist "%CG%\bin\cl6x.exe" (echo RESULT %BOX% - no cl6x at %CG% & exit /b 1)
if not exist "%W%\out" mkdir "%W%\out" 2>nul
set STATUS=0
:next
if "%~1"=="" exit /b %STATUS%
set N=%~n1
set O=%W%\out\%~n1.%BOX%
del /q "!O!.*" 2>nul
"%CG%\bin\cl6x" -mv6740 --abi=eabi -g -I"%CG%\include" --obj_directory="%W%\out" "%~f1" -z -m "!O!.map" --heap_size=0x800 --stack_size=0x800 -i"%CG%\lib" --rom_model -o "!O!.out" "%W%\C6747.cmd" -llibc.a > "!O!.build.log" 2>&1
if errorlevel 1 (
  echo BOX %BOX% !N! build=FAILED> "!O!.result"
  type "!O!.result"
  set STATUS=1
  shift
  goto next
)
set R=
for /f "tokens=1,*" %%a in ('call "%CCS55%\ccs_base\scripting\bin\dss.bat" "%WF%/runca.js" "%WF%/c6747ca-windows.ccxml" "!O:\=/!.out" "!O:\=/!.stdout" 0 2^>^&1 ^| findstr /b RESULT') do set R=%%b
if "!R!"=="" set STATUS=1
echo BOX %BOX% !N! build=ok !R!> "!O!.result"
type "!O!.result"
shift
goto next
