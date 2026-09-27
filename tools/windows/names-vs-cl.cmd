@echo off
rem  Every case's symbols, from cxx1 and from cl, side by side.
rem
rem  Usage:  names-vs-cl.cmd <root>
rem  Writes  <root>\winnames\<case>.mine.txt  dumpbin /symbols on cxx1's object
rem          <root>\winnames\<case>.cl.txt    dumpbin /symbols on cl's object
rem          <root>\winnames\<case>.skip      when cl would not compile it
rem
rem  **Why this exists when names.sh already checks the names.** That one asks
rem  clang with -target x86_64-pc-windows-msvc, which is a second
rem  implementation of the Microsoft ABI. This asks the ABI itself. CLAUDE.md
rem  says a Microsoft question goes to cl first and clang second, and until now
rem  every mangled name this compiler emits was checked only against the
rem  second - on a Mac, by a compiler that is not the one anybody links with.
rem  The two have already been seen to disagree: the secondary vtable and the
rem  biased `this` in thunk.cpp were settled by cl and not by clang.
rem
rem  **No comparison happens here**, for the reason run-cases.cmd gives about
rem  its own output: cmd is a poor place to cut up text and this box has no
rem  awk. The raw listings go back to the Mac and tools/verify-three does the
rem  reading, where one grep settles what a screenful of `for /f` would not.
rem
rem  /std:c++14 because cl has no C++11 mode - its floor is c++14 - so cl
rem  answers about the ABI and not about which language version a construct
rem  belongs to. /GR- and /EHsc- keep RTTI and exception tables out, the same
rem  flags measure.cmd uses and the same reason tools/mangled-names asks clang
rem  for -fno-rtti -fno-exceptions: this is a question about names.
setlocal enabledelayedexpansion
if "%~1"==":shard" goto :shard
if "%~1"=="" (echo names-vs-cl.cmd: needs the tree root & exit /b 2)
set ROOT=%~1

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo names-vs-cl.cmd: no vcvars & exit /b 1)

if not exist %ROOT%\winnames mkdir %ROOT%\winnames
del /q %ROOT%\winnames\* 2>nul

rem  cxx1 and cl side by side - three shards each, the compiler under test and the reference at
rem  once - and then dumpbin over six. See par.cmd.
call "%~dp0par.cmd" 6 "%~f0" %ROOT% compile
call "%~dp0par.cmd" 6 "%~f0" %ROOT% dump

echo names-vs-cl.cmd: done
endlocal
exit /b 0

rem  One shard. compile: shards 1-3 run cxx1 and 4-6 cl, each on every third case. dump: every sixth.
:shard
set K=%~2
set N=%~3
set ROOT=%~4
set MODE=%~5
set W=%N%
set PART=%K%
set TOOL=
if "%MODE%"=="compile" (
    set /a W=N / 2
    set TOOL=cxx1
    if %K% GTR !W! (set TOOL=cl& set /a PART=K - W)
)
set /a I=0
for %%f in (%ROOT%\tests\cases\*.expected) do (
    set /a I+=1
    set /a M=I %% W + 1
    if !M!==!PART! call :%MODE% %%~nf
)
exit /b 0

rem  The two exclusion files the other runners read: .notarget, not compiled for this target at
rem  all; .nocl, cl and cxx1 differ here for a reason somebody wrote down, as .nonames does for clang.
:compile
set NAME=%~1
set SKIP=
if exist %ROOT%\tests\cases\%NAME%.notarget (
    findstr /C:"x86_64-windows" %ROOT%\tests\cases\%NAME%.notarget >nul 2>&1 && set SKIP=1
)
if exist %ROOT%\tests\cases\%NAME%.nocl set SKIP=1
if defined SKIP (
    if "%TOOL%"=="cxx1" echo skipped > %ROOT%\winnames\%NAME%.skip
    exit /b 0
)
if "%TOOL%"=="cxx1" (
    %ROOT%\cxx1-msvc.exe -c %ROOT%\tests\cases\%NAME%.cpp -o %ROOT%\winnames\%NAME%.obj >nul 2>&1
    if errorlevel 1 echo cxx1-refused > %ROOT%\winnames\%NAME%.skip
    exit /b 0
)
rem  cl refusing is not a cxx1 failure: this corpus is C++11 and cl has no C++11 mode. Recorded so
rem  the count says how much was really compared.
cl /nologo /c /std:c++14 /GR- /EHsc- /Fo:%ROOT%\winnames\%NAME%.cl.obj %ROOT%\tests\cases\%NAME%.cpp >nul 2>&1
if errorlevel 1 echo cl-refused > %ROOT%\winnames\%NAME%.clskip
exit /b 0

rem  cxx1's refusal is the one reported when both said no, as when cl was only asked after cxx1.
:dump
set NAME=%~1
if exist %ROOT%\winnames\%NAME%.skip (del /q %ROOT%\winnames\%NAME%.clskip 2>nul& exit /b 0)
if exist %ROOT%\winnames\%NAME%.clskip (move /y %ROOT%\winnames\%NAME%.clskip %ROOT%\winnames\%NAME%.skip >nul& exit /b 0)
dumpbin /nologo /symbols %ROOT%\winnames\%NAME%.obj > %ROOT%\winnames\%NAME%.mine.txt 2>&1
dumpbin /nologo /symbols %ROOT%\winnames\%NAME%.cl.obj > %ROOT%\winnames\%NAME%.cl.txt 2>&1
exit /b 0
