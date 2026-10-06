@echo off
rem run-windows.cmd - the Windows box's half of tools/c6747-compilerpp, run in the directory
rem holding harness.cpp, compilerpp_amalgamated.cpp, h.cpp11.obj, C6747-ddr.cmd and runca.js.
rem Compiler++ built by CCS 5.5's cl6x 7.4.4 and CCS 7.4's cl6x 8.2.2, both -O2, each linked by its
rem own linker against its own rts6740_elf_eh.lib; cpp11's object linked by 7.4.4's as the Linux box
rem does. All three timed on CCS 5.5's C6747 cycle-accurate simulator - CCS 7.4 has none.
rem Writes <build>.result, .cio and .map for each of ccs55, ccs74 and cpp11, and .prof for two of them.
setlocal
set W=%~dp0
set W=%W:~0,-1%
set WF=%W:\=/%
set CG55=C:\ti\ccsv5\tools\compiler\c6000_7.4.4
set CG74=C:\ti\ccsv7\tools\compiler\ti-cgt-c6000_8.2.2
if "%C6747_EHLIB%"=="" set C6747_EHLIB=C:\cxx1\c6747-lib
if "%C6747_EHLIB74%"=="" set C6747_EHLIB74=C:\Users\GRA\Documents\VM6747\tilib
cd /d "%W%"
del /q *.result *.cio *.out ccs74.built 2>nul
if not exist o55 mkdir o55
if not exist o74 mkdir o74
rem The two TI builds at once: each is one translation unit of 14,000 lines, minutes at -O2.
start "ccs74" /b cmd /c ""%CG74%\bin\cl6x" -mv6740 --abi=eabi -O2 %TI_COMPRESS% --rtti --symdebug:none -I"%CG74%\include" -I. --obj_directory=o74 harness.cpp -z --rom_model -i"%C6747_EHLIB74%" C6747-ddr.cmd -lrts6740_elf_eh.lib -m ccs74.map -o ccs74.out > ccs74.build.log 2>&1 & echo done> ccs74.built"
"%CG55%\bin\cl6x" -mv6740 --abi=eabi -O2 %TI_COMPRESS% --rtti --symdebug:none -I"%CG55%\include" -I. --obj_directory=o55 harness.cpp -z --rom_model -i"%C6747_EHLIB%" C6747-ddr.cmd -lrts6740_elf_eh.lib -m ccs55.map -o ccs55.out > ccs55.build.log 2>&1
"%CG55%\bin\cl6x" -mv6740 --abi=eabi h.cpp11.obj -z --rom_model -i"%C6747_EHLIB%" C6747-ddr.cmd -lrts6740_elf_eh.lib -m cpp11.map -o cpp11.out > cpp11.build.log 2>&1
:wait
if not exist ccs74.built (ping -n 6 127.0.0.1 >nul & goto wait)
for %%b in (ccs55 ccs74 cpp11) do call :run %%b
exit /b 0

:run
if not exist %1.out (echo BUILD %1 build=FAILED> %1.result & type %1.result & exit /b 0)
rem Timed unsampled - a halt costs cycles, so a profiled run is not the count - then, for cl6x 7.4.4
rem and cpp11, run again under prof.js for where the cycles went.
set R=
for /f "tokens=1,*" %%a in ('call C:\ti\ccsv5\ccs_base\scripting\bin\dss.bat "%WF%/runca.js" "%WF%/c6747ca-windows.ccxml" "%WF%/%1.out" "%WF%/%1.cio" 0 86400000 2^>^&1 ^| findstr /b RESULT') do set R=%%b
(echo BUILD windows-%1 build=ok %R%)> %1.result
type %1.result
if "%1"=="ccs74" exit /b 0
call C:\ti\ccsv5\ccs_base\scripting\bin\dss.bat "%WF%/prof.js" "%WF%/c6747ca-windows.ccxml" "%WF%/%1.out" "%WF%/%1.prof.cio" 500 > %1.prof 2>&1
exit /b 0
