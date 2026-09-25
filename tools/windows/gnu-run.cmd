@echo off
rem  **Assemble, link and run one GNU-spelling .s on the Windows box.**
rem
rem  asm-run.cmd is the MASM twin. The x86_64-windows default is the GNU
rem  spelling, which clang assembles into the same COFF - so a question about
rem  this target that needs a run, not a rebuild, goes through here:
rem    ./cpp11.exe -S -arch x86_64-windows p.cpp -o p.s
rem    scp p.s windows:C:/cxx1/fast/
rem    ssh windows "C:\cxx1\fast\gnu-run.cmd p"
rem  The extra libraries beyond asm-run's: libcpmt.lib, which holds
rem  std::set_new_handler and std::get_new_handler - measured with
rem  dumpbin /linkermember, they are in no other static library.
setlocal
if "%~1"=="" (echo gnu-run.cmd: needs the base name of a .s & exit /b 2)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo gnu-run: no vcvars64 & exit /b 1)
cd /d C:\cxx1\fast
"%VCINSTALLDIR%Tools\Llvm\x64\bin\clang.exe" -target x86_64-pc-windows-msvc -c %1.s -o %1.obj
if errorlevel 1 (echo gnu-run: the assembler failed & exit /b 1)
link /nologo /subsystem:console /out:%1.exe %1.obj libcmt.lib libcpmt.lib libucrt.lib libvcruntime.lib kernel32.lib legacy_stdio_definitions.lib
if errorlevel 1 (echo gnu-run: the link failed & exit /b 1)
%1.exe
echo exit %errorlevel%
