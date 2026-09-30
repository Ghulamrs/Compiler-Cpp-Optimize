@echo off
rem hold.cmd - start the two lanes and stay until both are done, so the ssh session lives that long.
cd /d C:\cxx1\input\cpp
del /q laneA.done laneB.done 2>nul
start "laneA" /b cmd /c lane.cmd laneA.txt
start "laneB" /b cmd /c lane.cmd laneB.txt
:w
ping -n 31 127.0.0.1 >nul
if not exist laneA.done goto w
if not exist laneB.done goto w
type *.result
