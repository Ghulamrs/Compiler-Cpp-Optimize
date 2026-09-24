@echo off
rem  **cxx1 -O2 against cl /O2 and cl6x --opt_level=2, on the Windows box.**
rem
rem  Times tools\windows\bench-kernels.cpp built by each x86 compiler (the
rem  median of interleaved rounds, the checksums compared) and sums the .text
rem  of each object; for the C6000, compares code size only, cl6x against a
rem  Compiler-Cppi cxx1 that has the tms6747 target. See bench.ps1 for the options.
rem
rem    C:\cxxopt\C++\tools\windows\bench.cmd -New C:\cxxopt\C++\cxx1-msvc.exe
rem         -Base C:\cxxopt\w\base\cxx1-msvc.exe -Cppi C:\path\to\Compiler-Cppi\cxx1-msvc.exe
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo bench.cmd: no vcvars64 & exit /b 1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bench.ps1" %*
exit /b %errorlevel%
