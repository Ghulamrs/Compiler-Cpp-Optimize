@echo off
REM Build cxx1.exe with MSVC.
REM
REM The third toolchain, and it is here for what it refuses rather than for
REM what it produces: a shadowed local or a narrowing MSVC rejects at its own
REM warning level is one that -Wall -Wextra never mentions on the other two.
REM
REM Run it by its full path and with NO `cmd /c` in front - the ssh shell on
REM that box is already cmd, and the prefix nests one inside the other, strips
REM the outer quotes, and leaks a quote into %1.
REM
REM     ssh windows "C:\Users\GRA\source\Compiler-Cpp\msvc\build.cmd"
REM
REM compat\ goes on the include path ahead of everything so that <unistd.h>
REM resolves to the three-line shim beside this file and nothing in src\ has
REM to know which platform it is on.
setlocal
set HERE=%~dp0
set ROOT=%HERE%..

REM **Forward slashes, and this is not cosmetic.** The include directory is
REM compiled into the binary as a C string literal, so a Windows path takes
REM every backslash as the start of an escape: \Users \GRA \source each
REM become an "unrecognized character escape sequence" at Driver.cpp's
REM definition, and the file that reports it is not the file that is wrong.
REM cc1.vcxproj spells this $(ProjectDir.Replace('\','/')) for the same reason.
set "ROOTFWD=%ROOT:\=/%"

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 ( echo build.cmd: no vcvars64 & exit /b 1 )

if not exist "%ROOT%\obj-msvc" mkdir "%ROOT%\obj-msvc"

REM /std:c++14 is the pin the other two toolchains carry as -std=c++14, and
REM /permissive- is what makes MSVC judge the same language they do.
REM
REM **The disabled warnings and _CRT_SECURE_NO_WARNINGS are inherited, not
REM chosen here.** They are exactly ..\..\Compiler-C\msvc\cc1.vcxproj's list,
REM and every one of them fires on code this tree forked rather than wrote:
REM 4996 is fopen and localtime, 4244/4267 are the int64-to-int and
REM long-double-to-double narrowings in the preprocessor and two backends, and
REM 4456 is one shadowed local in X86_64Linux.cpp. Building without the list
REM says nothing new about this project - it re-reports Compiler-C's decisions
REM as though they were faults found here. Anything cxx1 adds of its own must
REM still compile clean at /W4 /WX with these five off.
cl /nologo /std:c++14 /permissive- /EHsc /W4 /WX /O2 ^
   /wd4996 /wd4267 /wd4244 /wd4456 /wd4146 ^
   /I"%HERE%compat" /I"%ROOT%\src" ^
   /D_CRT_SECURE_NO_WARNINGS ^
   /DCXX1_INCLUDE_DIR="\"%ROOTFWD%/lib\"" ^
   /DCXX1_CXX_INCLUDE_DIR="\"%ROOTFWD%/include\"" ^
   /Fo"%ROOT%\obj-msvc\\" /Fe"%ROOT%\cxx1-msvc.exe" ^
   "%ROOT%\src\*.cpp" "%ROOT%\src\parser\*.cpp" "%ROOT%\src\backend\*.cpp" "%ROOT%\src\optimizer\*.cpp"
if errorlevel 1 ( echo build.cmd: FAILED & exit /b 1 )
echo build.cmd: cxx1-msvc.exe built
endlocal
