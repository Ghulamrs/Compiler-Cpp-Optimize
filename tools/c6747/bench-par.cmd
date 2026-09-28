@echo off
rem bench-par.cmd - the kernels on the Windows box, every build at once: each <prog>.c or .cpp here
rem built by CCS 5.5's cl6x 7.4.4 -O2, and <prog>.cpp11.obj when it is here, both linked by 7.4.4
rem against rts6740_elf_eh.lib and timed on the C6747 cycle-accurate simulator, each run stopped at
rem 30 minutes. Writes <prog>.cl6x.result / .cpp11.result and .stdout.
setlocal enabledelayedexpansion
set W=%~dp0
set W=%W:~0,-1%
if "%CCS55%"=="" set CCS55=C:\ti\ccsv5
if "%C6747_EHLIB%"=="" set C6747_EHLIB=C:\cxx1\c6747-lib
set CG=%CCS55%\tools\compiler\c6000_7.4.4
set LINK=-z --heap_size=0x800 --stack_size=0x800 -i"%C6747_EHLIB%" --rom_model "%W%\C6747.cmd" -lrts6740_elf_eh.lib
cd /d "%W%"
del /q *.result *.stdout *.out 2>nul
if not exist obj mkdir obj
set WANT=0
for %%f in (*.c *.cpp) do (
  "%CG%\bin\cl6x" -mv6740 --abi=eabi -O2 --symdebug:none -I"%CG%\include" --obj_directory=obj "%%f" %LINK% -m %%~nf.cl6x.map -o %%~nf.cl6x.out > %%~nf.cl6x.build.log 2>&1
  set /a WANT+=1
  if exist %%~nf.cpp11.obj (
    "%CG%\bin\cl6x" -mv6740 --abi=eabi %%~nf.cpp11.obj %LINK% -m %%~nf.cpp11.map -o %%~nf.cpp11.out > %%~nf.cpp11.build.log 2>&1
    set /a WANT+=1
  )
)
rem Three seconds apart: every simulator session starts its own Eclipse.
for %%o in (*.out) do (start "%%~no" /b cmd /c ""%W%\par-one.cmd" %%~no" & ping -n 4 127.0.0.1 >nul)
rem Held open until every run has answered, or the session's children could go with it.
:gather
set N=0
for %%r in (*.result) do set /a N+=1
if !N! lss !WANT! (ping -n 11 127.0.0.1 >nul & goto gather)
type *.result 2>nul
exit /b 0
