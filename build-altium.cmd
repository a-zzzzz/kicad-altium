@echo off
rem Build Altium Skin for KiCad on Windows.
rem
rem   build-altium.cmd            configure (the first run also builds the vcpkg dependencies),
rem                               build, and install into %BUILD_DIR%\install
rem   build-altium.cmd build      build and install only (after the first configure)
rem
rem Optional environment variables:
rem   BUILD_DIR            build tree            (default: .\build next to this script)
rem   VCPKG_ROOT           vcpkg checkout        (default: C:\vcpkg)
rem   VCPKG_INSTALLED_DIR  vcpkg packages        (default: %BUILD_DIR%\vcpkg_installed)
rem   SWIG_EXECUTABLE      swig.exe              (default: found on PATH or in the winget folder)
setlocal EnableExtensions

set "SRC=%~dp0"
set "SRC=%SRC:~0,-1%"
if not defined BUILD_DIR set "BUILD_DIR=%SRC%\build"
if not defined VCPKG_ROOT set "VCPKG_ROOT=C:\vcpkg"
if not defined VCPKG_INSTALLED_DIR set "VCPKG_INSTALLED_DIR=%BUILD_DIR%\vcpkg_installed"

call "%SRC%\tools\altium\find-tools.cmd" || exit /b 1

if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo vcpkg not found at %VCPKG_ROOT%. Install it with:
    echo     git clone https://github.com/microsoft/vcpkg C:\vcpkg
    echo     C:\vcpkg\bootstrap-vcpkg.bat
    exit /b 1
)

if /I not "%~1"=="build" (
    cmake -S "%SRC%" -B "%BUILD_DIR%" -G Ninja ^
        -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
        "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
        "-DVCPKG_INSTALLED_DIR=%VCPKG_INSTALLED_DIR%" ^
        "-DSWIG_EXECUTABLE=%SWIG_EXECUTABLE%" ^
        "-DCMAKE_INSTALL_PREFIX=%BUILD_DIR%/install" ^
        "-DDEFAULT_INSTALL_PATH=%BUILD_DIR%/install" ^
        -DKICAD_BUILD_QA_TESTS=OFF ^
        -DKICAD_INSTALL_DEMOS=OFF ^
        -DKICAD_WIN32_DPI_AWARE=ON ^
        -DKICAD_SCRIPTING_WXPYTHON=ON || exit /b 1
)

cmake --build "%BUILD_DIR%" || exit /b 1
call "%SRC%\tools\altium\install-runtime.cmd" || exit /b 1
echo Built: %BUILD_DIR%\install\bin\kicad.exe
