@echo off
rem run-split.cmd - the Compiler++ workload on the Windows box, one simulator run per file and build,
rem all at once, each capped at 30 minutes: cl6x 7.4.4 and 8.2.2 -O2 build the harness, 7.4.4 links
rem cpp11's object, and runca.js sets harnessFile so each run compiles one file.
rem Run in the directory holding harness.cpp, compilerpp_amalgamated.cpp, h.cpp11.obj, C6747-ddr.cmd,
rem runca.js and c6747ca-windows.ccxml. Writes <build>.<file>.result and .cio.
setlocal
set W=%~dp0
set W=%W:~0,-1%
set WF=%W:\=/%
set CG55=C:\ti\ccsv5\tools\compiler\c6000_7.4.4
set CG74=C:\ti\ccsv7\tools\compiler\ti-cgt-c6000_8.2.2
if "%C6747_EHLIB%"=="" set C6747_EHLIB=C:\cxx1\c6747-lib
if "%C6747_EHLIB74%"=="" set C6747_EHLIB74=C:\Users\GRA\Documents\VM6747\tilib
if "%FILES%"=="" set FILES=10
cd /d "%W%"
del /q *.result *.cio *.out *.built 2>nul
if not exist o55 mkdir o55
if not exist o74 mkdir o74
start "ccs74" /b cmd /c ""%CG74%\bin\cl6x" -mv6740 --abi=eabi -O2 --rtti --symdebug:none -I"%CG74%\include" -I. --obj_directory=o74 harness.cpp -z --rom_model -i"%C6747_EHLIB74%" C6747-ddr.cmd -lrts6740_elf_eh.lib -m ccs74.map -o ccs74.out > ccs74.build.log 2>&1 & echo done> ccs74.built"
"%CG55%\bin\cl6x" -mv6740 --abi=eabi -O2 --rtti --symdebug:none -I"%CG55%\include" -I. --obj_directory=o55 harness.cpp -z --rom_model -i"%C6747_EHLIB%" C6747-ddr.cmd -lrts6740_elf_eh.lib -m ccs55.map -o ccs55.out > ccs55.build.log 2>&1
"%CG55%\bin\cl6x" -mv6740 --abi=eabi h.cpp11.obj -z --rom_model -i"%C6747_EHLIB%" C6747-ddr.cmd -lrts6740_elf_eh.lib -m cpp11.map -o cpp11.out > cpp11.build.log 2>&1
:wait
if not exist ccs74.built (ping -n 6 127.0.0.1 >nul & goto wait)
set /a LAST=%FILES%-1
for %%b in (ccs55 ccs74 cpp11) do for /l %%k in (0,1,%LAST%) do start "%%b.%%k" /b cmd /c ""%W%\run-one.cmd" %%b %%k"
rem Held open until every run has answered, or the session's children could go with it.
set /a WANT=3*%FILES%
:gather
set N=0
for %%r in (*.result) do set /a N+=1
if %N% lss %WANT% (ping -n 16 127.0.0.1 >nul & goto gather)
type *.result 2>nul
exit /b 0
