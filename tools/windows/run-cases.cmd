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
if "%~1"=="" (echo run-cases.cmd: needs the tree root & exit /b 2)
set ROOT=%~1

call %ROOT%\msvc\build.cmd
if errorlevel 1 exit /b 1

if not exist %ROOT%\winout mkdir %ROOT%\winout
del /q %ROOT%\winout\* 2>nul

for %%f in (%ROOT%\tests\cases\*.expected) do (
    set NAME=%%~nf
    set SKIP=
    rem  A case may say it cannot be compiled for this target, one reason per
    rem  line in <case>.notarget - the same file tests/emit.sh reads. The
    rem  reason is printed rather than swallowed: an exclusion nobody sees is
    rem  an exclusion nobody removes.
    if exist %ROOT%\tests\cases\!NAME!.notarget (
        findstr /C:"x86_64-windows" %ROOT%\tests\cases\!NAME!.notarget >nul 2>&1 && set SKIP=1
    )
    if defined SKIP (
        echo   skip !NAME! for x86_64-windows:
        findstr /C:"x86_64-windows" %ROOT%\tests\cases\!NAME!.notarget
    ) else (
        %ROOT%\cxx1-msvc.exe %CXX1_CASES_FLAGS% %ROOT%\tests\cases\!NAME!.cpp -o %ROOT%\winout\!NAME!.exe >%ROOT%\winout\!NAME!.build 2>&1
        if errorlevel 1 (
            echo COMPILE-FAILED !NAME!
        ) else (
            %ROOT%\winout\!NAME!.exe > %ROOT%\winout\!NAME!.out 2>&1
        )
    )
)
rem  The refusal cases. Only the compile is asked for - -S stops before the
rem  assembler, which this box has for MASM but which is beside the point when
rem  what is being checked is that the compiler said no.
for %%f in (%ROOT%\tests\cases\*.error) do (
    set NAME=%%~nf
    set SKIP=
    if exist %ROOT%\tests\cases\!NAME!.notarget (
        findstr /C:"x86_64-windows" %ROOT%\tests\cases\!NAME!.notarget >nul 2>&1 && set SKIP=1
    )
    if defined SKIP (
        echo   skip !NAME! for x86_64-windows:
        findstr /C:"x86_64-windows" %ROOT%\tests\cases\!NAME!.notarget
    ) else (
        %ROOT%\cxx1-msvc.exe -S %ROOT%\tests\cases\!NAME!.cpp -o nul >%ROOT%\winout\!NAME!.err 2>&1
        if not errorlevel 1 echo COMPILED-AND-SHOULD-NOT-HAVE !NAME!
    )
)

echo run-cases.cmd: done
endlocal
