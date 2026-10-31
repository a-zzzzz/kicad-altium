@echo off
rem Build the Windows installer from a finished build-altium.cmd build.
rem
rem   installer\build-installer.cmd
rem
rem Output: %BUILD_DIR%\AltiumSkin-<version>-win64-setup.exe
rem
rem Needs Inno Setup 6 (winget install JRSoftware.InnoSetup). The standard symbol and footprint
rem libraries are taken from an installed KiCad 10 (or set KICAD_LIBS to another share\kicad
rem folder); set NO_LIBS=1 to build an installer without them.
setlocal EnableExtensions

set "HERE=%~dp0"
set "HERE=%HERE:~0,-1%"
for %%I in ("%HERE%\..") do set "SRC=%%~fI"
if not defined BUILD_DIR set "BUILD_DIR=%SRC%\build"
if not defined VCPKG_INSTALLED_DIR set "VCPKG_INSTALLED_DIR=%BUILD_DIR%\vcpkg_installed"
if not defined KICAD_LIBS set "KICAD_LIBS=%ProgramFiles%\KiCad\10.0\share\kicad"
set /p VER=<"%HERE%\VERSION"
set "STAGE=%BUILD_DIR%\stage"
set "LIBS=%BUILD_DIR%\stage-libs"
set "RC=/E /NFL /NDL /NJH /NJS /NP"

if not exist "%BUILD_DIR%\build.ninja" (
    echo No build in %BUILD_DIR%. Run build-altium.cmd first.
    exit /b 1
)

set "ISCC="
for %%P in ("%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" "%ProgramFiles%\Inno Setup 6\ISCC.exe") do (
    if not defined ISCC if exist "%%~P" set "ISCC=%%~P"
)
if not defined ISCC for /f "delims=" %%I in ('where ISCC.exe 2^>nul') do if not defined ISCC set "ISCC=%%I"
if not defined ISCC (
    echo Inno Setup 6 not found. Install it with:  winget install JRSoftware.InnoSetup
    exit /b 1
)

call "%SRC%\tools\altium\find-tools.cmd" || exit /b 1

echo Staging the program...
if exist "%STAGE%" rmdir /s /q "%STAGE%"
if exist "%LIBS%" rmdir /s /q "%LIBS%"
call "%SRC%\tools\altium\install-runtime.cmd" || exit /b 1
rem Leave out Python's compiled caches (they record the build machine's paths and are rebuilt on
rem demand) and pip's script launchers (they point at the build machine's python.exe).
robocopy "%BUILD_DIR%\install" "%STAGE%" %RC% /XD __pycache__ "%BUILD_DIR%\install\bin\Lib\site-packages\bin" /XF *.pyc >nul
if errorlevel 8 exit /b 1
rem Debug symbols are for developers, not users.
del /s /q "%STAGE%\*.pdb" >nul 2>&1
for %%F in (LICENSE LICENSE.README LICENSE.GPLv3 LICENSE.CC-BY-SA-4.0 README.md SETUP.md) do (
    copy /Y "%SRC%\%%F" "%STAGE%\" >nul || exit /b 1
)

mkdir "%LIBS%\share\kicad\template"
if defined NO_LIBS goto :nolibs
if not exist "%KICAD_LIBS%\symbols" (
    echo KiCad 10 libraries not found in "%KICAD_LIBS%".
    echo Install KiCad 10, set KICAD_LIBS to a share\kicad folder that has symbols and footprints,
    echo or set NO_LIBS=1 to build without them.
    exit /b 1
)
echo Staging the libraries from %KICAD_LIBS%...
robocopy "%KICAD_LIBS%\symbols" "%LIBS%\share\kicad\symbols" %RC% >nul
if errorlevel 8 exit /b 1
robocopy "%KICAD_LIBS%\footprints" "%LIBS%\share\kicad\footprints" %RC% >nul
if errorlevel 8 exit /b 1
rem The default library tables belong with the libraries they list.
copy /Y "%KICAD_LIBS%\template\sym-lib-table" "%LIBS%\share\kicad\template\" >nul || exit /b 1
copy /Y "%KICAD_LIBS%\template\fp-lib-table" "%LIBS%\share\kicad\template\" >nul || exit /b 1
rem Translations and project templates the build doesn't produce (never replacing ours).
robocopy "%KICAD_LIBS%\internat" "%STAGE%\share\kicad\internat" %RC% /XC /XN /XO >nul
if errorlevel 8 exit /b 1
robocopy "%KICAD_LIBS%\template" "%STAGE%\share\kicad\template" %RC% /XC /XN /XO /XF sym-lib-table fp-lib-table >nul
if errorlevel 8 exit /b 1
:nolibs
rem Inno Setup needs at least one file in each source folder.
echo Libraries for Altium Skin for KiCad> "%LIBS%\share\kicad\template\libraries.txt"

echo Building the installer...
"%ISCC%" /Q "/DAppVer=%VER%" "/DStageDir=%STAGE%" "/DLibsDir=%LIBS%" "/DOutDir=%BUILD_DIR%" "%HERE%\kicad-altium.iss" || exit /b 1
echo Installer: %BUILD_DIR%\AltiumSkin-%VER%-win64-setup.exe
