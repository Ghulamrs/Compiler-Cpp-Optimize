@echo off
rem vm-ccs74.cmd <prog.c> ... - the Windows CCS 7.4 box: each program compiled by CCS 7.4's
rem C6000 compiler 8.2.2 to assembly and run on VM6747, the emulator. CCS 7.4 ships no
rem simulator, so vm6747 is this box's target; it reports output and exit status, no cycles.
rem Writes out\<prog>.windows-ccs74-vm.stdout and .result beside this script.
setlocal enabledelayedexpansion
set W=%~dp0
set W=%W:~0,-1%
if "%CGT74%"=="" set CGT74=C:\ti\ccsv7\tools\compiler\ti-cgt-c6000_8.2.2
if "%VM6747%"=="" set VM6747=C:\Users\GRA\source\RIDE-4.7\bin\vm6747.exe
set BOX=windows-ccs74-vm
if not exist "%CGT74%\bin\cl6x.exe" (echo RESULT %BOX% - no cl6x at %CGT74% & exit /b 1)
if not exist "%VM6747%" (echo RESULT %BOX% - no vm6747 at %VM6747% & exit /b 1)
if not exist "%W%\out" mkdir "%W%\out" 2>nul
set STATUS=0
:next
if "%~1"=="" exit /b %STATUS%
set N=%~n1
set O=%W%\out\%~n1.%BOX%
del /q "!O!.*" 2>nul
if exist "!O!-asm" rmdir /s /q "!O!-asm"
mkdir "!O!-asm"
rem -n stops at assembly; --symdebug:none keeps the debug directives vm6747 does not read out of it.
rem -O2 since 2026-10-05, when vm6747 learned the SPLOOP buffer (VM6747 ff64492); set CCS74_OPT to choose another, e.g. -O1.
if not defined CCS74_OPT set CCS74_OPT=-O2
"%CGT74%\bin\cl6x" -mv6740 --abi=eabi -n %CCS74_OPT% %TI_COMPRESS% --symdebug:none -I"%CGT74%\include" --asm_directory="!O!-asm" "%~f1" > "!O!.build.log" 2>&1
if errorlevel 1 (
  echo BOX %BOX% !N! build=FAILED> "!O!.result"
  type "!O!.result"
  set STATUS=1
  shift
  goto next
)
"%VM6747%" "!O!-asm" > "!O!.stdout" 2>&1 < nul
set RC=!errorlevel!
(echo BOX %BOX% !N! build=ok event=none count=- rc=!RC!)> "!O!.result"
type "!O!.result"
shift
goto next
