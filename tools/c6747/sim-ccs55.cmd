@echo off
rem sim-ccs55.cmd <prog.c> ... - the Windows CCS 5.5 box: each program built by CCS 5.5's
rem C6000 compiler 7.4.4 at -O2, and cpp11's object for it when programs\<prog>.cpp11.obj
rem is there, each linked by 7.4.4's linker against the same library and run on the C6747
rem cycle-accurate simulator through DSS. Writes out\<prog>.<box>.stdout and .result.
setlocal enabledelayedexpansion
set W=%~dp0
set W=%W:~0,-1%
if "%CCS55%"=="" set CCS55=C:\ti\ccsv5
if "%C6747_EHLIB%"=="" set C6747_EHLIB=C:\cxx1\c6747-lib
set CG=%CCS55%\tools\compiler\c6000_7.4.4
set BOX=windows-ccs55
set WF=%W:\=/%
if not exist "%CG%\bin\cl6x.exe" (echo RESULT %BOX% - no cl6x at %CG% & exit /b 1)
if not exist "%C6747_EHLIB%\rts6740_elf_eh.lib" (echo RESULT %BOX% - no rts6740_elf_eh.lib in %C6747_EHLIB% & exit /b 1)
if not exist "%W%\out" mkdir "%W%\out" 2>nul
set LINK=-z --heap_size=0x800 --stack_size=0x800 -i"%C6747_EHLIB%" --rom_model "%W%\C6747.cmd" -lrts6740_elf_eh.lib
set STATUS=0
:next
if "%~1"=="" exit /b %STATUS%
set N=%~n1
set O=%W%\out\%~n1.%BOX%
del /q "!O!.*" 2>nul
"%CG%\bin\cl6x" -mv6740 --abi=eabi -O2 --symdebug:none -I"%CG%\include" --obj_directory="%W%\out" "%~f1" %LINK% -m "!O!.map" -o "!O!.out" > "!O!.build.log" 2>&1
if errorlevel 1 (
  echo BOX %BOX% !N! build=FAILED> "!O!.result"
  type "!O!.result"
  set STATUS=1
) else (
  call :run !N! %BOX% "!O!.out"
)
set OBJ=%~dpn1.cpp11.obj
set C=%W%\out\%~n1.%BOX%-cpp11
if exist "!OBJ!" (
  del /q "!C!.*" 2>nul
  "%CG%\bin\cl6x" -mv6740 --abi=eabi "!OBJ!" %LINK% -m "!C!.map" -o "!C!.out" > "!C!.build.log" 2>&1
  if errorlevel 1 (
    echo BOX %BOX%-cpp11 !N! build=FAILED> "!C!.result"
    type "!C!.result"
    set STATUS=1
  ) else (
    call :run !N! %BOX%-cpp11 "!C!.out"
  )
)
shift
goto next

rem :run <prog> <box> <.out> - the simulator, and the result line
:run
set R=
set OUTF=%~3
for /f "tokens=1,*" %%a in ('call "%CCS55%\ccs_base\scripting\bin\dss.bat" "%WF%/runca.js" "%WF%/c6747ca-windows.ccxml" "!OUTF:\=/!" "%WF%/out/%1.%2.stdout" 0 2^>^&1 ^| findstr /b RESULT') do set R=%%b
if "!R!"=="" set STATUS=1
echo BOX %2 %1 build=ok !R!> "%W%\out\%1.%2.result"
type "%W%\out\%1.%2.result"
exit /b 0
