@echo OFF
@setlocal

set VERSION=1.25.0
set TFDIR=C:\TreeFrog\%VERSION%
set LZ4_VERSION=1.9.1

:parse_loop
if "%1" == "" goto :start
if /i "%1" == "--prefix" goto :prefix
if /i "%1" == "--enable-debug" goto :enable_debug
if /i "%1" == "--enable-gui-mod" goto :enable_gui_mod
if /i "%1" == "--help" goto :help
if /i "%1" == "-h" goto :help
goto :help
:continue
shift
goto :parse_loop


:help
  echo Usage: %0 [OPTION]... [VAR=VALUE]...
  echo;
  echo Configuration:
  echo   -h, --help          display this help and exit
  echo   --enable-debug      compile with debugging information
  echo   --enable-gui-mod    compile and link with QtGui module
  echo;
  echo Installation directories:
  echo   --prefix=PREFIX     install files in PREFIX [%TFDIR%]
  goto :exit

:prefix
  shift
  if "%1" == "" goto :help
  set TFDIR=%1
  goto :continue

:enable_debug
  set DEBUG=yes
  goto :continue

:enable_gui_mod
  set USE_GUI=use_gui=1
  goto :continue

:start
if "%DEBUG%" == "yes" (
  set OPT="CONFIG+=debug"
) else (
  set OPT="CONFIG+=release"
)

::
:: Generates tfenv.bat
::
for %%I in (qmake.exe) do if exist %%~$path:I set QMAKE=%%~$path:I
for %%I in (cl.exe) do if exist %%~$path:I set MSCOMPILER=%%~$path:I
for %%I in (g++.exe) do if exist %%~$path:I set GNUCOMPILER=%%~$path:I

if "%QMAKE%" == "" (
  echo Qt environment not found
  exit /b
)
if "%MSCOMPILER%" == "" if "%GNUCOMPILER%"  == "" (
  echo compiler not found
  exit /b
)

:: vcvarsall.bat setup
if not "%MSCOMPILER%" == "" (
  set MAKE=nmake
  if "%Platform%" == "X64" (
    set VCVARSOPT=amd64
    set ENVSTR=Environment to build for 64-bit executable  MSVC / Qt
  ) else if "%Platform%" == "x64" (
    set VCVARSOPT=amd64
    set ENVSTR=Environment to build for 64-bit executable  MSVC / Qt
  ) else (
    set VCVARSOPT=x86
    set ENVSTR=Environment to build for 32-bit executable  MSVC / Qt
  )
) else (
  set MAKE=mingw32-make -j%NUMBER_OF_PROCESSORS%
  set ENVSTR=Environment to build for executable MinGW / Qt
)
SET /P X="%ENVSTR%"<NUL
qtpaths.exe --qt-version


for %%I in (qtenv2.bat) do if exist %%~$path:I set QTENV=%%~$path:I
set TFENV=tfenv.bat
echo @echo OFF> %TFENV%
echo ::>> %TFENV%
echo :: This file is generated by configure.bat>> %TFENV%
echo ::>> %TFENV%
echo;>> %TFENV%
echo set TFDIR=%TFDIR%>> %TFENV%
echo set TreeFrog_DIR=%TFDIR%>> %TFENV%
echo set QTENV="%QTENV%">> %TFENV%
echo set QMAKESPEC=%QMAKESPEC%>> %TFENV%
echo if exist %%QTENV%% ( call %%QTENV%% )>> %TFENV%
if not "%VCVARSOPT%" == "" (
  echo if not "%%VS140COMNTOOLS%%" == "" ^(>> %TFENV%
  echo   set VCVARSBAT="%%VS140COMNTOOLS%%..\..\VC\vcvarsall.bat">> %TFENV%
  echo ^) else if not "%%VS120COMNTOOLS%%" == "" ^(>> %TFENV%
  echo   set VCVARSBAT="%%VS120COMNTOOLS%%..\..\VC\vcvarsall.bat">> %TFENV%
  echo ^) else ^(>> %TFENV%
  echo   set VCVARSBAT="">> %TFENV%
  echo ^)>> %TFENV%
  echo if exist %%VCVARSBAT%% ^(>> %TFENV%
  echo   echo Setting up environment for MSVC usage...>> %TFENV%
  echo   call %%VCVARSBAT%% %VCVARSOPT%>> %TFENV%
  echo ^)>> %TFENV%
)
echo set PATH=%%TFDIR^%%\bin;%%PATH%%>> %TFENV%
echo echo Setup a TreeFrog/Qt environment.>> %TFENV%
echo echo -- TFDIR set to %%TFDIR%%>> %TFENV%
echo cd /D %%HOMEDRIVE%%%%HOMEPATH%%>> %TFENV%


set TFDIR=%TFDIR:\=/%
del 3rdparty\mongo-c-driver\.qmake.stash src\.qmake.stash tools\.qmake.stash >nul 2>&1
:: Builds MongoDB driver
echo Compiling MongoDB driver library ...
cd 3rdparty\mongo-c-driver
if exist Makefile ( %MAKE% -k distclean >nul 2>&1 )

qmake -r %OPT%
%MAKE% >nul 2>&1
if ERRORLEVEL 1 (
  echo Compile failed.
  echo MongoDB driver not available.
  exit /b
)

:: Builds LZ4
cd ..
rd /s /q lz4 >nul 2>&1
del /f /q lz4 >nul 2>&1
mklink /d lz4 lz4-%LZ4_VERSION% >nul 2>&1
if not "%MSCOMPILER%" == "" (
  devenv lz4\visual\VS2017\lz4.sln /project liblz4 /rebuild "Release|x64"
) else (
  cd lz4\lib
  qmake -o makefile.liblz4
  %MAKE% -f makefile.liblz4 clean
  %MAKE% -f makefile.liblz4 liblz4.a
  cd ..\..
)
cd ..

cd src
if exist Makefile ( %MAKE% -k distclean >nul 2>&1 )
qmake %OPT% target.path='%TFDIR%/bin' header.path='%TFDIR%/include' %USE_GUI%
cd ..
cd tools
if exist Makefile ( %MAKE% -k distclean >nul 2>&1 )
qmake -recursive %OPT% target.path='%TFDIR%/bin' header.path='%TFDIR%/include' datadir='%TFDIR%'
%MAKE% qmake
cd ..

echo;
echo First, run "%MAKE% install" in src directory.
echo Next, run "%MAKE% install" in tools directory.

:exit
exit /b
