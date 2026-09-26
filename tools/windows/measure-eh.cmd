@echo off
rem  cl as the measurement venue for the Microsoft exception and RTTI records.
rem
rem  measure.cmd passes /GR- and no /EHsc on purpose - it exists for mangling
rem  questions. This one is the same script with /EHsc /GR /d2FH4- so that the
rem  ThrowInfo chain, the catchable-type records, the FH3 tables and the type
rem  descriptors a throw, a catch or a typeid needs are in the listing.
rem  Usage:  measure-eh.cmd <basename>   (one unquoted argument, as measure.cmd)
setlocal
if "%~1"=="" (echo measure-eh.cmd: needs a basename & exit /b 2)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\Users\GRA\source\measure
del /q %~1.asm %~1.obj 2>nul
cl /nologo /c /std:c++14 /EHsc /GR /d2FH4- /FAsc /Fa%~1.asm /Fo:%~1.obj %~1.cpp
if errorlevel 1 (echo *** cl refused it *** & exit /b 1)
echo === listing ===
type %~1.asm
echo === external symbols ===
dumpbin /nologo /symbols %~1.obj | findstr /C:"External"
