@echo off
rem  Visual Studio's environment for cl, then the PowerShell half does the rest - see inv-bench.ps1.
cd /d %~dp0
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0inv-bench.ps1"
