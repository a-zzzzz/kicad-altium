@echo off
rem Start the copy built by build-altium.cmd.
rem   run-altium.cmd [kicad|pcbnew|eeschema] [file]
rem
rem The build doesn't contain KiCad's symbol/footprint/3D libraries. If KiCad 10 is installed,
rem its libraries are used; otherwise add your own under Preferences > Manage Libraries.
setlocal

set "SRC=%~dp0"
set "SRC=%SRC:~0,-1%"
if not defined BUILD_DIR set "BUILD_DIR=%SRC%\build"
set "BIN=%BUILD_DIR%\install\bin"
if not exist "%BIN%\kicad.exe" (
    echo %BIN%\kicad.exe not found. Run build-altium.cmd first.
    exit /b 1
)

set "STOCKLIBS=%ProgramFiles%\KiCad\10.0\share\kicad"
if not exist "%BIN%\..\share\kicad\symbols" if exist "%STOCKLIBS%\symbols" (
    if not defined KICAD10_SYMBOL_DIR set "KICAD10_SYMBOL_DIR=%STOCKLIBS%\symbols"
    if not defined KICAD10_FOOTPRINT_DIR set "KICAD10_FOOTPRINT_DIR=%STOCKLIBS%\footprints"
    if not defined KICAD10_3DMODEL_DIR set "KICAD10_3DMODEL_DIR=%STOCKLIBS%\3dmodels"
)

set "APP=%~1"
if "%APP%"=="" set "APP=kicad"
start "" "%BIN%\%APP%.exe" %2
