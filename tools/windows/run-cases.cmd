@echo off
rem  Build cxx1 with cl and put every case through it - the ones with recorded
rem  output and the ones that must be refused - in both x86_64-windows spellings.
rem
rem  Usage:  run-cases.cmd <root>
rem  Writes  <root>\winout\<case>.out for a case that runs and
rem          <root>\winout\<case>.err for one that must not compile, through
rem          the GNU spelling (assembled by clang), and the same names under
rem          <root>\winout\masm\ through -masm=masm, assembled by the project's
rem          own masm and linked by the project's own LINK - and nothing else.
rem  The comparison is deliberately NOT done here: a .expected
rem  file arrives with Unix line endings and a program's own output leaves
rem  through the CRT with Windows ones, so `fc` reports every case as
rem  different. tools/verify-three pulls these back and diffs them on the Mac,
rem  where one `tr -d` settles it.
rem
rem  **The refusals matter most on this target and were untested longest.**
rem  The parser is shared, so a diagnostic here usually repeats what the other
rem  two boxes proved - except where it does not: `throw`, `try` and a class
rem  with a destructor are refused *only* for x86_64-windows, and until this
rem  loop existed no box checked those at all.
rem
rem  **Both spellings, by default** (review D15, 2026-10-08): the MASM one had been
rem  run once by hand, and the project's masm refused 10 of 379 cases under a green
rem  suite. The project's tools are found, in this order:
rem    CXX1_MASM, CXX1_LINK      the two programs, named outright
rem    CXX1_MASM_SRC, CXX1_LINK_SRC   their trees, built here by cl into <root>\winout-tools
rem                              (default <root>\..\MASM and <root>\..\LINK)
rem  and where neither is found the MASM pass is not run, the reason is printed and
rem  the script exits 1 once the GNU pass is done. CXX1_CASES_SPELLINGS=gnu (or masm)
rem  runs one spelling alone.
rem
rem  CXX1_CASES_FLAGS, if set, goes on every compile of both passes - -O2, say.
setlocal enabledelayedexpansion
if "%~1"==":shard" goto :shard
if "%~1"=="" (echo run-cases.cmd: needs the tree root & exit /b 2)
set ROOT=%~1
set SPELLINGS=%CXX1_CASES_SPELLINGS%
if "%SPELLINGS%"=="" set SPELLINGS=gnu masm
set STATUS=0

call %ROOT%\msvc\build.cmd
if errorlevel 1 exit /b 1

if not exist %ROOT%\winout mkdir %ROOT%\winout
del /q %ROOT%\winout\* 2>nul
if exist %ROOT%\winout\masm rmdir /s /q %ROOT%\winout\masm

rem  Six shards at once, each taking every sixth case - see par.cmd.
set CXX1_PASS_FLAGS=-masm=gnu
if not "!SPELLINGS:gnu=!"=="!SPELLINGS!" call "%~dp0par.cmd" 6 "%~f0" %ROOT% %ROOT%\winout

if "!SPELLINGS:masm=!"=="!SPELLINGS!" goto :done
call :tools
if errorlevel 1 (set STATUS=1& goto :done)
mkdir %ROOT%\winout\masm
set CPP11_AS=%MASMEXE%
set CPP11_LD=%LINKEXE%
set CXX1_PASS_FLAGS=-masm=masm
echo run-cases.cmd: the MASM spelling, %MASMEXE% and %LINKEXE%
call "%~dp0par.cmd" 6 "%~f0" %ROOT% %ROOT%\winout\masm

:done
echo run-cases.cmd: done
endlocal & exit /b %STATUS%

rem  The project's masm and LINK: named, or built from their trees, or the pass refused by name.
:tools
set MASMEXE=%CXX1_MASM%
set LINKEXE=%CXX1_LINK%
set TOOLS=%ROOT%\winout-tools
if "%MASMEXE%"=="" call :build MASM masm "%CXX1_MASM_SRC%"
if "%LINKEXE%"=="" call :build LINK link "%CXX1_LINK_SRC%"
if "%MASMEXE%"=="" (echo run-cases.cmd: the MASM spelling was NOT run - no masm: set CXX1_MASM, or put the MASM tree at %ROOT%\..\MASM or CXX1_MASM_SRC& exit /b 1)
if "%LINKEXE%"=="" (echo run-cases.cmd: the MASM spelling was NOT run - no LINK: set CXX1_LINK, or put the LINK tree at %ROOT%\..\LINK or CXX1_LINK_SRC& exit /b 1)
exit /b 0

rem  %1 the tree's name, %2 the program, %3 its source tree if named; sets MASMEXE or LINKEXE.
:build
set SRCTREE=%~3
if "%SRCTREE%"=="" set SRCTREE=%ROOT%\..\%1
if not exist "%SRCTREE%\src\main.cpp" exit /b 0
if not exist %TOOLS%\%2 mkdir %TOOLS%\%2
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /MP /std:c++14 /W4 /permissive- /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS /Fo:%TOOLS%\%2\ /Fe:%TOOLS%\%2.exe "%SRCTREE%\src\*.cpp" > %TOOLS%\%2-cl.log 2>&1
endlocal
if not exist %TOOLS%\%2.exe (echo run-cases.cmd: %1 did not build from %SRCTREE% - see %TOOLS%\%2-cl.log& exit /b 0)
if "%2"=="masm" (set MASMEXE=%TOOLS%\masm.exe) else (set LINKEXE=%TOOLS%\link.exe)
exit /b 0

rem  One shard: every Nth case, the ones with recorded output and the refusals alike.
:shard
set K=%~2
set N=%~3
set ROOT=%~4
set OUT=%~5
set /a I=0
for %%f in (%ROOT%\tests\cases\*.expected %ROOT%\tests\cases\*.error) do (
    set /a I+=1
    set /a M=I %% N + 1
    if !M!==!K! call :one %%~nf %%~xf
)
exit /b 0

rem  A case may say it cannot be compiled for this target, one reason per line in <case>.notarget -
rem  the file tests/emit.sh reads. The reason is printed: an exclusion nobody sees is one nobody removes.
:one
set NAME=%~1
if exist %ROOT%\tests\cases\%NAME%.notarget (
    findstr /C:"x86_64-windows" %ROOT%\tests\cases\%NAME%.notarget >nul 2>&1 && (
        echo   skip %NAME% for x86_64-windows:
        findstr /C:"x86_64-windows" %ROOT%\tests\cases\%NAME%.notarget
        exit /b 0
    )
)
rem  A case's second translation unit, <case>.part.cpp, goes into its program - as run.sh does.
set PART=
if exist %ROOT%\tests\cases\%NAME%.part.cpp set PART=%ROOT%\tests\cases\%NAME%.part.cpp
if "%~2"==".expected" (
    %ROOT%\cxx1-msvc.exe %CXX1_PASS_FLAGS% %CXX1_CASES_FLAGS% %ROOT%\tests\cases\%NAME%.cpp %PART% -o %OUT%\%NAME%.exe >%OUT%\%NAME%.build 2>&1
    if errorlevel 1 (echo COMPILE-FAILED %CXX1_PASS_FLAGS% %NAME%& exit /b 0)
    %OUT%\%NAME%.exe > %OUT%\%NAME%.out 2>&1 < nul
    exit /b 0
)
rem  A refusal: only the compile is asked for, -S stopping before the assembler.
%ROOT%\cxx1-msvc.exe %CXX1_PASS_FLAGS% -S %ROOT%\tests\cases\%NAME%.cpp -o nul >%OUT%\%NAME%.err 2>&1
if not errorlevel 1 echo COMPILED-AND-SHOULD-NOT-HAVE %CXX1_PASS_FLAGS% %NAME%
exit /b 0
