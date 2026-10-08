@echo off
rem  tools/expected-from-clang on the Windows box: every .expected against VS clang.
rem
rem  Usage:  expected-from-clang.cmd <root> [case ...]
rem  Runs under Git's bash with Visual Studio's LLVM first on PATH, as names-overload.cmd does.
setlocal
if "%~1"=="" (echo expected-from-clang.cmd: needs the tree root & exit /b 2)
set ROOT=%~1
shift
set UROOT=%ROOT::=%
set UROOT=/%UROOT:\=/%
set ARGS=
:args
if "%~1"=="" goto run
set ARGS=%ARGS% %~1
shift
goto args
:run
"C:\Program Files\Git\bin\bash.exe" -c "export PATH='/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin':$PATH; export CLANG=clang++ OWNLIBS=-llibcpmt; cd '%UROOT%' && tools/expected-from-clang %ARGS%" < NUL
endlocal
