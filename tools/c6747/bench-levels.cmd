@echo off
rem bench-levels.cmd - the kernels on the Windows box against both CCS toolchains at both levels,
rem every run at once on the C6747 cycle-accurate simulator, each stopped at 30 minutes.
rem For each <prog>.c or .cpp here: cl6x 7.4.4 (CCS 5.5) and cl6x 8.2.2 (CCS 7.4) at -O1, -O2 and
rem -O2 -ms3, each linked by its own linker against its own rts6740_elf_eh.lib; and cpp11's
rem <prog>.cpp11-O1.obj and <prog>.cpp11-O2.obj, linked by 7.4.4. The objects stay in obj\ for
rem the size table. Writes <prog>.<build>.result and .stdout; MAXRUNS caps the sessions at once.
setlocal enabledelayedexpansion
set W=%~dp0
set W=%W:~0,-1%
if "%CCS55%"=="" set CCS55=C:\ti\ccsv5
if "%CG74%"=="" set CG74=C:\ti\ccsv7\tools\compiler\ti-cgt-c6000_8.2.2
if "%C6747_EHLIB%"=="" set C6747_EHLIB=C:\cxx1\c6747-lib
if "%C6747_EHLIB74%"=="" set C6747_EHLIB74=C:\Users\GRA\Documents\VM6747\tilib
if "%MAXRUNS%"=="" set MAXRUNS=12
set CG55=%CCS55%\tools\compiler\c6000_7.4.4
set LINK55=-z --heap_size=0x800 --stack_size=0x800 -i"%C6747_EHLIB%" --rom_model "%W%\C6747.cmd" -lrts6740_elf_eh.lib
set LINK74=-z --heap_size=0x800 --stack_size=0x800 -i"%C6747_EHLIB74%" --rom_model "%W%\C6747.cmd" -lrts6740_elf_eh.lib
cd /d "%W%"
del /q *.result *.stdout *.out *.log *.map 2>nul
if not exist obj mkdir obj
set WANT=0
for %%f in (*.c *.cpp) do (
  set N=%%~nf
  call :ti 55 744-O1 -O1 "%%f"
  call :ti 55 744-O2 -O2 "%%f"
  call :ti 55 744-ms "-O2 -ms3" "%%f"
  call :ti 74 822-O1 -O1 "%%f"
  call :ti 74 822-O2 -O2 "%%f"
  call :ti 74 822-ms "-O2 -ms3" "%%f"
  for %%l in (O1 O2) do if exist !N!.cpp11-%%l.obj (
    "%CG55%\bin\cl6x" -mv6740 --abi=eabi !N!.cpp11-%%l.obj %LINK55% -m !N!.cpp11-%%l.map -o !N!.cpp11-%%l.out > !N!.cpp11-%%l.build.log 2>&1
    if exist !N!.cpp11-%%l.out set /a WANT+=1
  )
)
rem Three seconds apart and no more than MAXRUNS at once: every simulator session starts its own Eclipse.
for %%o in (*.out) do call :start %%~no
:gather
set N=0
for %%r in (*.result) do set /a N+=1
if !N! lss !WANT! (ping -n 11 127.0.0.1 >nul & goto gather)
type *.result 2>nul
exit /b 0

rem :ti <55|74> <build> <opt flags> <source>: one TI build by that toolchain, its object kept in obj\<build>\
:ti
set B=%~2
if "%~1"=="55" (set CG=%CG55%& set LINK=%LINK55%) else (set CG=%CG74%& set LINK=%LINK74%)
if not exist obj\%B% mkdir obj\%B%
"%CG%\bin\cl6x" -mv6740 --abi=eabi %~3 --symdebug:none -I"%CG%\include" --obj_directory=obj\%B% "%~4" %LINK% -m %N%.%B%.map -o %N%.%B%.out > %N%.%B%.build.log 2>&1
if exist %N%.%B%.out set /a WANT+=1
exit /b 0

:start
for /f %%n in ('tasklist /fi "imagename eq eclipsec.exe" ^| find /c "eclipsec"') do set RUNNING=%%n
if %RUNNING% geq %MAXRUNS% (ping -n 11 127.0.0.1 >nul & goto start)
start "%1" /b cmd /c ""%W%\par-one.cmd" %1"
ping -n 4 127.0.0.1 >nul
exit /b 0
