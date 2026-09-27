@echo off
rem  The tms6747 target taken all the way to a TI program by the driver: every
rem  case run-cases.cmd compiles is compiled with `cxx1 -arch tms6747`, which
rem  assembles what it wrote with asm6x and links the object with TI's lnk6x
rem  against rts6740_elf_eh.lib into <case>.out. The linker is the judge: it
rem  takes every object asm6x wrote, or names the one it does not.
rem    ti-link.cmd <tree root>
rem  CXX1_TI_FLAGS, if set, goes on every compile - -O1 or -O2 links the optimizer's code.
rem  asm6x.exe is ASM6x's own cl build (tests/windows.sh leaves it at
rem  C:\asm6x-tests\build) or the one in RIDE's bin; the TI tools are CCS 7.4's.
setlocal enabledelayedexpansion
if "%~1"==":shard" goto :shard
if "%~1"=="" (echo ti-link.cmd: needs the tree root & exit /b 2)
set ROOT=%~1
set CPP11_TI=C:\ti\ccsv7\tools\compiler\ti-cgt-c6000_8.2.2
set CPP11_TILIB=C:\Users\GRA\Documents\VM6747\tilib
set CPP11_AS=C:\asm6x-tests\build\asm6x.exe
if not exist %CPP11_AS% set CPP11_AS=C:\Users\GRA\source\RStudio\bin\asm6x.exe
if not exist %CPP11_AS% (echo ti-link.cmd: no asm6x.exe & exit /b 1)
if not exist %CPP11_TI%\bin\lnk6x.exe (echo ti-link.cmd: no lnk6x under %CPP11_TI% & exit /b 1)
if not exist %CPP11_TILIB%\rts6740_elf_eh.lib (echo ti-link.cmd: no rts6740_elf_eh.lib - see Emulator/tests/ti.sh & exit /b 1)
if not exist %ROOT%\cxx1-msvc.exe (echo ti-link.cmd: no cxx1-msvc.exe - run-cases.cmd builds it & exit /b 1)
if not exist %ROOT%\winout\ti mkdir %ROOT%\winout\ti
del /q %ROOT%\winout\ti\* 2>nul
rem  Six shards at once with par.cmd; each prints a marker line per case, and the counts are
rem  read off the markers once all six are done.
rem  Visual Studio's environment once, here: a compiler that finds it set runs its tools
rem  directly, where without it every asm6x and lnk6x call went through vcvars64.bat again.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
call "%~dp0par.cmd" 6 "%~f0" > %ROOT%\winout\ti\shards.log
findstr /v /b /c:"=" %ROOT%\winout\ti\shards.log
for /f %%n in ('findstr /b /c:"=LINKED " %ROOT%\winout\ti\shards.log ^| find /c /v ""') do set linked=%%n
for /f %%n in ('findstr /b /c:"=FAILED " %ROOT%\winout\ti\shards.log ^| find /c /v ""') do set failed=%%n
for /f %%n in ('findstr /b /c:"=SKIPPED " %ROOT%\winout\ti\shards.log ^| find /c /v ""') do set skipped=%%n
echo ti-link.cmd: %linked% programs linked by lnk6x, %failed% failed, %skipped% not for this target
if not %failed%==0 exit /b 1
endlocal
exit /b 0

:shard
set /a I=0
for %%f in (%ROOT%\tests\cases\*.expected) do (
    set /a I+=1, M=I %% %~3 + 1
    if !M!==%~2 call :one %%~nf
)
exit /b 0

:one
if exist %ROOT%\tests\cases\%1.notarget findstr /C:"tms6747" %ROOT%\tests\cases\%1.notarget >nul 2>&1 && (echo =SKIPPED %1& exit /b 0)
%ROOT%\cxx1-msvc.exe -arch tms6747 -nologo %CXX1_TI_FLAGS% %ROOT%\tests\cases\%1.cpp -o %ROOT%\winout\ti\%1.out > %ROOT%\winout\ti\%1.log 2>&1
if errorlevel 1 (echo =FAILED %1& echo TI-FAILED %1& type %ROOT%\winout\ti\%1.log | findstr /v "^$"& exit /b 0)
if exist %ROOT%\winout\ti\%1.out (echo =LINKED %1) else (echo =FAILED %1& echo TI-NO-OUT %1)
exit /b 0
