@echo off
rem run-one-input.cmd <image.out> <tag> <NAME=VALUE or -> [program args...] - one run on the C6747
rem cycle-accurate simulator, its own Eclipse workspace, 30-minute cap; the third word is a global
rem to write before the run (harnessFile=0) or -. Writes <tag>.result, <tag>.cio, <tag>.log.
setlocal
set W=%~dp0
set W=%W:~0,-1%
set WF=%W:\=/%
cd /d "%W%"
set IMG=%~1
set TAG=%~2
set MEMW=%~3
if "%MEMW%"=="-" set MEMW=
shift
shift
shift
set ARGS=
:more
if "%~1"=="" goto go
set ARGS=%ARGS% %1
shift
goto more
:go
set WAITED=0
:waitimg
if exist "%IMG%" goto have
if %WAITED% geq 60 (echo RUN %TAG% image=missing> %TAG%.result & exit /b 0)
set /a WAITED+=1
ping -n 31 127.0.0.1 >nul
goto waitimg
:have
if not exist ws mkdir ws 2>nul
set IMGF=%IMG:\=/%
set DD=--
if "%ARGS%"=="" set DD=
"C:\ti\ccsv5\eclipse\eclipsec.exe" -nosplash -data "%W%\ws\%TAG%" -application com.ti.ccstudio.apps.runScript -dss.rhinoArgs ""%WF%/runargs.js" "%WF%/c6747ca-windows.ccxml" "%WF%/%IMGF%" "%WF%/%TAG%.cio" 0 1800000 nocache %MEMW% %DD%%ARGS%" > %TAG%.log 2>&1
findstr /b RESULT %TAG%.log > %TAG%.r 2>nul
set R=
set /p R=< %TAG%.r
if "%R%"=="" (echo RUN %TAG% timeout-or-failed> %TAG%.result) else (echo RUN %TAG% %R:RESULT =%> %TAG%.result)
del %TAG%.r 2>nul
