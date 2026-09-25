@echo off
REM Build cxx1.sln with MSBuild, from a plain shell.
REM
REM **Run it by its full path and with no `cmd /c` in front.** The ssh shell on
REM the Windows box is already cmd; a prefix nests one inside the other, strips
REM the outer quotes and leaks one into %1. The same note is on msvc\build.cmd,
REM and it is here because a chain of quoted paths typed at ssh fails the same
REM way - which is what this file exists to stop anybody rediscovering.
REM
REM     ssh windows "C:\cxx1\idetest\C++-IDE\build-vs.cmd"
REM     build-vs.cmd Debug          (Release is the default)
setlocal
set HERE=%~dp0
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 ( echo build-vs.cmd: no vcvars64 & exit /b 1 )

msbuild "%HERE%cxx1.sln" /nologo /v:minimal /p:Configuration=%CONFIG% /p:Platform=x64
if errorlevel 1 ( echo build-vs.cmd: FAILED & exit /b 1 )
echo build-vs.cmd: %HERE%build\%CONFIG%\cpp11.exe
endlocal
