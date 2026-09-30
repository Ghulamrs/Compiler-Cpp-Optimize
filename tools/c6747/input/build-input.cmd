@echo off
rem build-in.cmd - the input-reading harness (hin) and the embedded one (hemb), each by cl6x 7.4.4 -O2,
rem cl6x 8.2.2 -O2, and cpp11's shipped object linked by lnk6x 7.4.4; the input ones with --args=1024.
rem LNK6x's images (lnk6x.out) are shipped linked. Also the argv/fopen probe built by cpp11 (iop).
setlocal
set W=%~dp0
set W=%W:~0,-1%
set CG55=C:\ti\ccsv5\tools\compiler\c6000_7.4.4
set CG74=C:\ti\ccsv7\tools\compiler\ti-cgt-c6000_8.2.2
set LIB55=C:\cxx1\c6747-lib
set LIB74=C:\Users\GRA\Documents\VM6747\tilib
cd /d "%W%"
"%CG55%\bin\cl6x" -mv6740 --abi=eabi iop.cpp11.obj -z --args=1024 --heap_size=0x800 --stack_size=0x800 -i"%LIB55%" --rom_model C6747.cmd -lrts6740_elf_eh.lib -m iop-cpp11.map -o iop-cpp11.out > iop-cpp11.build.log 2>&1
for %%v in (hin hemb) do (
  if not exist %%v\o55 mkdir %%v\o55
  if not exist %%v\o74 mkdir %%v\o74
)
set A=--args=1024
start "ccs74-in" /b cmd /c ""%CG74%\bin\cl6x" -mv6740 --abi=eabi -O2 --rtti --symdebug:none -I"%CG74%\include" -Ihin --obj_directory=hin\o74 hin\harness.cpp -z %A% --rom_model -i"%LIB74%" C6747-ddr.cmd -lrts6740_elf_eh.lib -m hin\ccs74.map -o hin\ccs74.out > hin\ccs74.build.log 2>&1 & echo done> hin\ccs74.built"
start "ccs74-emb" /b cmd /c ""%CG74%\bin\cl6x" -mv6740 --abi=eabi -O2 --rtti --symdebug:none -I"%CG74%\include" -Ihemb --obj_directory=hemb\o74 hemb\harness.cpp -z --rom_model -i"%LIB74%" C6747-ddr.cmd -lrts6740_elf_eh.lib -m hemb\ccs74.map -o hemb\ccs74.out > hemb\ccs74.build.log 2>&1 & echo done> hemb\ccs74.built"
"%CG55%\bin\cl6x" -mv6740 --abi=eabi hin\h.cpp11.obj -z %A% --rom_model -i"%LIB55%" C6747-ddr.cmd -lrts6740_elf_eh.lib -m hin\cpp11.map -o hin\cpp11.out > hin\cpp11.build.log 2>&1
"%CG55%\bin\cl6x" -mv6740 --abi=eabi hemb\h.cpp11.obj -z --rom_model -i"%LIB55%" C6747-ddr.cmd -lrts6740_elf_eh.lib -m hemb\cpp11.map -o hemb\cpp11.out > hemb\cpp11.build.log 2>&1
"%CG55%\bin\cl6x" -mv6740 --abi=eabi -O2 --rtti --symdebug:none -I"%CG55%\include" -Ihin --obj_directory=hin\o55 hin\harness.cpp -z %A% --rom_model -i"%LIB55%" C6747-ddr.cmd -lrts6740_elf_eh.lib -m hin\ccs55.map -o hin\ccs55.out > hin\ccs55.build.log 2>&1
"%CG55%\bin\cl6x" -mv6740 --abi=eabi -O2 --rtti --symdebug:none -I"%CG55%\include" -Ihemb --obj_directory=hemb\o55 hemb\harness.cpp -z --rom_model -i"%LIB55%" C6747-ddr.cmd -lrts6740_elf_eh.lib -m hemb\ccs55.map -o hemb\ccs55.out > hemb\ccs55.build.log 2>&1
:wait
if not exist hin\ccs74.built (ping -n 6 127.0.0.1 >nul & goto wait)
if not exist hemb\ccs74.built (ping -n 6 127.0.0.1 >nul & goto wait)
dir hin\*.out hemb\*.out iop-cpp11.out
type hin\*.build.log hemb\*.build.log iop-cpp11.build.log
