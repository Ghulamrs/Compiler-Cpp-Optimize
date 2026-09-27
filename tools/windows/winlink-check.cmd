@echo off
rem  The two-half programs of tests\winlink, each linked across the boundary
rem  both ways: the a-half by cxx1 and the b-half by cl, then the reverse.
rem  Usage:  winlink-check.cmd <root>
rem  Writes  <root>\winout\winlink-<name>-ab.out and -ba.out for a program
rem  that ran, and a line naming the step for one that did not; the
rem  comparison with <name>.expected is tools/verify-three's, on the Mac,
rem  for the line-ending reason run-cases.cmd gives.
setlocal enabledelayedexpansion
if "%~1"==":shard" goto :shard
if "%~1"=="" (echo winlink-check.cmd: needs the tree root & exit /b 2)
set ROOT=%~1
set WORK=%ROOT%\winlink
if not exist %WORK% mkdir %WORK%
del /q %WORK%\* 2>nul
if not exist %ROOT%\winout mkdir %ROOT%\winout

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo winlink-check: no vcvars64 & exit /b 1)

rem  Every program both ways round, six at once with par.cmd - see :pair.
call "%~dp0par.cmd" 6 "%~f0"
echo winlink-check.cmd: done
endlocal
exit /b 0

rem  The items are each program's two directions, ab (a by cxx1, b by cl) and ba (the reverse).
:shard
set /a I=0
for %%f in (%ROOT%\tests\winlink\*.expected) do for %%d in (ab ba) do (
    set /a I+=1, M=I %% %~3 + 1
    if !M!==%~2 call :pair %%~nf %%d
)
exit /b 0

rem  cxx1's half and cl's half compiled side by side - the compiler under test and the reference at
rem  once - then linked and run. %1 the program, %2 the direction; ab puts a through cxx1.
:pair
set SRC=%ROOT%\tests\winlink
if "%2"=="ab" (set MINE=a& set THEIRS=b) else (set MINE=b& set THEIRS=a)
set LOG=%WORK%\%1-%2.log
start "" /b cmd /c "cl /nologo /EHsc /c /I%SRC% /Fo%WORK%\%1-%2-%THEIRS%.obj %SRC%\%1-%THEIRS%.cpp > %LOG%.cl 2>&1 & echo.> %LOG%.cldone"
%ROOT%\cxx1-msvc.exe -c %SRC%\%1-%MINE%.cpp -o %WORK%\%1-%2-%MINE%.obj > %LOG% 2>&1
set MINERC=%errorlevel%
:wait
if not exist %LOG%.cldone (ping -n 2 127.0.0.1 >nul & goto wait)
type %LOG%.cl >> %LOG%
if not %MINERC%==0 (echo WINLINK-FAILED %1-%2: cxx1 refused the %MINE%-half& exit /b 0)
if not exist %WORK%\%1-%2-%THEIRS%.obj (echo WINLINK-FAILED %1-%2: cl refused the %THEIRS%-half& exit /b 0)
link /nologo /OUT:%WORK%\%1-%2.exe %WORK%\%1-%2-a.obj %WORK%\%1-%2-b.obj >> %LOG% 2>&1
if errorlevel 1 (echo WINLINK-FAILED %1-%2: the link failed& exit /b 0)
%WORK%\%1-%2.exe > %ROOT%\winout\winlink-%1-%2.out 2>&1 < nul
exit /b 0
