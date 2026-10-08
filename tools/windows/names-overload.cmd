@echo off
rem  tests/names.sh and tests/overload.sh on the Windows box.
rem
rem  Usage:  names-overload.cmd <root> [compiler]
rem          <root>      the tree, e.g. C:\cxx1\rtsdiv\ws-a
rem          [compiler]  the cxx1 to test; default <root>\cxx1-msvc.exe,
rem                      which msvc\build.cmd makes
rem
rem  Both suites ask clang, and the two scripts are POSIX sh - so they run under
rem  Git's bash with Visual Studio's LLVM first on PATH. That directory is the
rem  one Visual Studio 2022 installs its optional clang into; tests/names.sh
rem  asks `command -v clang++` and tools/mangled-names calls `clang++` by name,
rem  and overload.sh takes the same binary through CLANG. Measured 2026-10-08
rem  (plan REVIEW-PLAN-2026-10-08 WS-A, first hour): clang 19.1.5 there,
rem  overload.sh 31 agreed / 0 differed, names.sh 398 passed / 6 failed from a
rem  tree built by msvc\build.cmd - so the control-room rule holds for these
rem  two suites too. Two of the six are this box's and not the compiler's:
rem  a case that includes <cstdio> cannot be compiled by this clang for
rem  arm64-darwin, which has no Darwin headers here (ifdef-comment,
rem  using-declaration-chain); the Mac compares those. The other four are real
rem  and host-independent (docs/review-2026-10-08/ws-a.md names them).
rem
rem  Two things about this clang worth knowing before trusting it as an
rem  oracle: its default target is x86_64-pc-windows-msvc, so a program
rem  compiled without -target is compiled as MSVC would see it - with
rem  -fdelayed-template-parsing on, which turns two-phase lookup off; pass
rem  -fno-delayed-template-parsing for any template-lookup question. And
rem  -target x86_64-linux-gnu has no headers on this box, so a case that
rem  includes one is compared on the Mac or not at all.
rem
rem  The output directories (tests\out-names, tests\out-overload) are the
rem  scripts' own; read a FAIL line's report there.
setlocal
if "%~1"=="" (echo names-overload.cmd: needs the tree root & exit /b 2)
set ROOT=%~1
set CXX1=%~2
if "%CXX1%"=="" set CXX1=%ROOT%\cxx1-msvc.exe
if not exist "%CXX1%" (echo names-overload.cmd: no compiler at %CXX1% - run msvc\build.cmd first & exit /b 2)

rem  C:\a\b -> /c/a/b, the spelling bash wants.
set UROOT=%ROOT::=%
set UROOT=/%UROOT:\=/%
set UCXX1=%CXX1::=%
set UCXX1=/%UCXX1:\=/%

set LLVM=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin
if not exist "%LLVM%\clang++.exe" (echo names-overload.cmd: no clang++.exe in "%LLVM%" - install Visual Studio's "C++ Clang tools" component & exit /b 2)

set STATUS=0
"C:\Program Files\Git\bin\bash.exe" -c "export PATH='/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin':$PATH; export CXX1='%UCXX1%' CLANG=clang++; cd '%UROOT%' && tests/names.sh" < NUL
if errorlevel 1 set STATUS=1
"C:\Program Files\Git\bin\bash.exe" -c "export PATH='/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin':$PATH; export CXX1='%UCXX1%' CLANG=clang++; cd '%UROOT%' && tests/overload.sh" < NUL
if errorlevel 1 set STATUS=1
endlocal & exit /b %STATUS%
