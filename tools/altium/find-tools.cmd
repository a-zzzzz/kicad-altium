@echo off
rem Puts the Visual Studio 2022 x64 compiler, CMake and Ninja on PATH and sets SWIG_EXECUTABLE.
rem Called by build-altium.cmd and installer\build-installer.cmd; the caller keeps the environment.

where cl.exe >nul 2>&1 && goto :swig

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :novs
set "VSDIR="
for /f "usebackq delims=" %%I in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%I"
if not defined VSDIR goto :novs
rem vcvars64 runs Visual Studio's own vcpkg hook when VCPKG_ROOT is set, and that hook fails;
rem hide our VCPKG_ROOT from it and restore it afterwards.
set "_VCPKG_ROOT=%VCPKG_ROOT%"
set "VCPKG_ROOT="
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
set "_VCVARS_ERR=%ERRORLEVEL%"
set "VCPKG_ROOT=%_VCPKG_ROOT%"
set "_VCPKG_ROOT="
if not "%_VCVARS_ERR%"=="0" exit /b 1

:swig
if defined SWIG_EXECUTABLE goto :done
for /f "delims=" %%I in ('where swig.exe 2^>nul') do if not defined SWIG_EXECUTABLE set "SWIG_EXECUTABLE=%%I"
if defined SWIG_EXECUTABLE goto :done
for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\SWIG.SWIG_*") do (
    for /d %%E in ("%%D\swigwin-*") do if exist "%%E\swig.exe" set "SWIG_EXECUTABLE=%%E\swig.exe"
)
if defined SWIG_EXECUTABLE goto :done
echo SWIG not found. Install it with:  winget install SWIG.SWIG
echo or set SWIG_EXECUTABLE to the full path of swig.exe.
exit /b 1

:novs
echo Visual Studio 2022 with the "Desktop development with C++" workload was not found.
echo Install the Build Tools with:
echo     winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
exit /b 1

:done
exit /b 0
