@echo off
rem  Build cxx1 with cl and put every case through it - the ones with recorded
rem  output and the ones that must be refused.
rem
rem  Usage:  run-cases.cmd <root>
rem  Writes  <root>\winout\<case>.out for a case that runs and
rem          <root>\winout\<case>.err for one that must not compile, and
rem  nothing else. The comparison is deliberately NOT done here: a .expected
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
rem  CXX1_CASES_FLAGS, if set, goes on every compile - `-masm=masm` with CPP11_AS
rem  naming the project's own assembler puts the whole suite through it instead
rem  of clang, which is how that assembler's COMDAT was proven.
setlocal enabledelayedexpansion
if "%~1"==":shard" goto :shard
if "%~1"=="" (echo run-cases.cmd: needs the tree root & exit /b 2)
set ROOT=%~1

call %ROOT%\msvc\build.cmd
if errorlevel 1 exit /b 1

if not exist %ROOT%\winout mkdir %ROOT%\winout
del /q %ROOT%\winout\* 2>nul

rem  Six shards at once, each taking every sixth case - see par.cmd.
call "%~dp0par.cmd" 6 "%~f0" %ROOT%

echo run-cases.cmd: done
endlocal
exit /b 0

rem  One shard: every Nth case, the ones with recorded output and the refusals alike.
:shard
set K=%~2
set N=%~3
set ROOT=%~4
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
if "%~2"==".expected" (
    %ROOT%\cxx1-msvc.exe %CXX1_CASES_FLAGS% %ROOT%\tests\cases\%NAME%.cpp -o %ROOT%\winout\%NAME%.exe >%ROOT%\winout\%NAME%.build 2>&1
    if errorlevel 1 (echo COMPILE-FAILED %NAME%& exit /b 0)
    %ROOT%\winout\%NAME%.exe > %ROOT%\winout\%NAME%.out 2>&1 < nul
    exit /b 0
)
rem  A refusal: only the compile is asked for, -S stopping before the assembler.
%ROOT%\cxx1-msvc.exe -S %ROOT%\tests\cases\%NAME%.cpp -o nul >%ROOT%\winout\%NAME%.err 2>&1
if not errorlevel 1 echo COMPILED-AND-SHOULD-NOT-HAVE %NAME%
exit /b 0
