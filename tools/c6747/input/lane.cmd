@echo off
rem lane.cmd <list file> - runs each line of the list, one after another: "<image> <tag> <args...>".
setlocal
set W=%~dp0
set W=%W:~0,-1%
cd /d "%W%"
for /f "usebackq tokens=1,2,* delims= " %%a in ("%~1") do call "%W%\run-one-input.cmd" %%a %%b %%c
echo lane-done> %~n1.done
