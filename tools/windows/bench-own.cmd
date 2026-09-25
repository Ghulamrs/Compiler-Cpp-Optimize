@echo off
rem  **cxx1 through the project's own MASM and LINK, against ml64 + link.exe and cl.**
rem
rem  Times tools\windows\bench-kernels.cpp at -O1 and -O2 built three ways: cxx1 ->
rem  MASM -> LINK (own), the same assembly -> ml64 -> link.exe (ms), and cl /O1, /O2
rem  -> link.exe (cl). Medians of interleaved rounds, checksums compared, .text and
rem  .exe sizes. See bench-own.ps1 for the options.
rem
rem    C:\cxxopt\C++\tools\windows\bench-own.cmd -Masm C:\path\masm.exe -Link C:\path\link.exe
rem         [-Cxx1 C:\cxxopt\C++\cxx1-msvc.exe] [-Rounds 9]
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo bench-own.cmd: no vcvars64 & exit /b 1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bench-own.ps1" %*
exit /b %errorlevel%
