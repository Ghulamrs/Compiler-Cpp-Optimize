@echo off
rem release-check.cmd - the Windows half of tools/c6747/release-check, run in a
rem directory the Mac driver shipped: leveled cpp11 assembly <name>.<level>.s and
rem ASM6x's object <name>.<level>.asm6x.obj, plus runca.js, par-one.cmd, the ccxml
rem and C6747.cmd. It answers the two release questions on the box the emulator
rem cannot:
rem   encoding - TI's assembler (cl6x 8.2.2) assembles each .s --no_compress, and
rem              dis6x disassembles both its object and ASM6x's, to out\.
rem   run      - each -O2 ASM6x object is linked by cl6x 7.4.4 against the EH
rem              runtime and C6747.cmd and run on the C6747 cycle-accurate
rem              simulator through DSS (par-one.cmd), in parallel.
rem The Mac compares; this box only builds, disassembles and runs.
setlocal enabledelayedexpansion
set W=%~dp0
set W=%W:~0,-1%
if "%CCS55%"=="" set CCS55=C:\ti\ccsv5
if "%CGT74%"=="" set CGT74=C:\ti\ccsv7\tools\compiler\ti-cgt-c6000_8.2.2
if "%C6747_EHLIB%"=="" set C6747_EHLIB=C:\cxx1\c6747-lib
if "%MAXRUNS%"=="" set MAXRUNS=6
set CG55=%CCS55%\tools\compiler\c6000_7.4.4
if not exist "%CGT74%\bin\cl6x.exe" (echo RC-ERROR no cl6x 8.2.2 at %CGT74% & exit /b 2)
if not exist "%CGT74%\bin\dis6x.exe" (echo RC-ERROR no dis6x at %CGT74% & exit /b 2)
if not exist "%CG55%\bin\cl6x.exe" (echo RC-ERROR no cl6x 7.4.4 at %CG55% & exit /b 2)
if not exist "%C6747_EHLIB%\rts6740_elf_eh.lib" (echo RC-ERROR no rts6740_elf_eh.lib in %C6747_EHLIB% & exit /b 2)
cd /d "%W%"
if exist out rmdir /s /q out
mkdir out
del /q *.result *.stdout *.out 2>nul

rem --- encoding: TI assembles every .s, dis6x on both objects ---
for %%f in (*.s) do (
  set B=%%~nf
  "%CGT74%\bin\cl6x" -mv6740 --abi=eabi --no_compress -c "%%f" --output_file=out\!B!.ti.obj > out\!B!.ti.log 2>&1
  if exist out\!B!.ti.obj (
    "%CGT74%\bin\dis6x" out\!B!.ti.obj > out\!B!.ti.dis 2>&1
  ) else (
    echo TI-ASM-FAILED !B! > out\!B!.ti.dis
    type out\!B!.ti.log >> out\!B!.ti.dis
  )
  if exist "!B!.asm6x.obj" (
    "%CGT74%\bin\dis6x" "!B!.asm6x.obj" > out\!B!.asm6x.dis 2>&1
  ) else (
    echo NO-ASM6X-OBJ !B! > out\!B!.asm6x.dis
  )
)

rem --- run: link each -O2 ASM6x object, then the simulator in parallel ---
set LINK=-z --heap_size=0x800 --stack_size=0x800 -i"%C6747_EHLIB%" --rom_model "%W%\C6747.cmd" -lrts6740_elf_eh.lib
set WANT=0
for %%o in (*.O2.asm6x.obj) do (
  set N=%%~no
  set N=!N:.O2.asm6x=!
  "%CG55%\bin\cl6x" -mv6740 --abi=eabi "%%o" %LINK% -m "!N!.map" -o "!N!.out" > "!N!.link.log" 2>&1
  if exist "!N!.out" (set /a WANT+=1) else (echo BOX !N! link=FAILED> "!N!.result" & type "!N!.link.log")
)
if exist ws rmdir /s /q ws 2>nul
mkdir ws 2>nul
for %%p in (*.out) do if not exist %%~np.result call :start %%~np
:gather
set DONE=0
for %%r in (*.result) do set /a DONE+=1
if !DONE! lss !WANT! (ping -n 11 127.0.0.1 >nul & goto gather)
rem A DSS session that never started - some started together fail (T8) - is run again, alone.
for %%r in (*.result) do findstr /c:"count=" %%r >nul || (del %%r & call "%W%\par-one.cmd" %%~nr)
copy /y *.result out\ >nul 2>&1
copy /y *.stdout out\ >nul 2>&1
echo done> out\release-check.done
exit /b 0

:start
for /f %%n in ('tasklist /fi "imagename eq eclipsec.exe" ^| find /c "eclipsec"') do set RUNNING=%%n
if %RUNNING% geq %MAXRUNS% (ping -n 11 127.0.0.1 >nul & goto start)
start "%1" /b cmd /c ""%W%\par-one.cmd" %1"
ping -n 4 127.0.0.1 >nul
exit /b 0
