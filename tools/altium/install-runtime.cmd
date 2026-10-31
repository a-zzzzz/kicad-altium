@echo off
rem Installs the build into %BUILD_DIR%\install and adds the runtime files that "cmake --install"
rem leaves out: vcpkg's DLLs and the embedded Python's standard library. Expects BUILD_DIR and
rem VCPKG_INSTALLED_DIR to be set (build-altium.cmd and build-installer.cmd do this).
rem
rem The install folder is fixed at configure time: KiCad's data files install to absolute paths,
rem so "cmake --install --prefix" elsewhere would leave share\kicad behind.
setlocal
set "DEST=%BUILD_DIR%\install"
cmake --install "%BUILD_DIR%" >nul || exit /b 1

set "VCPKG_BIN=%VCPKG_INSTALLED_DIR%\x64-windows\bin"
copy /Y "%VCPKG_BIN%\*.dll" "%DEST%\bin\" >nul || exit /b 1

set "PY=%VCPKG_INSTALLED_DIR%\x64-windows\tools\python3"
robocopy "%PY%\Lib" "%DEST%\bin\Lib" /E /XC /XN /XO /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 exit /b 1
robocopy "%PY%\DLLs" "%DEST%\bin\DLLs" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 exit /b 1
copy /Y "%PY%\python.exe" "%DEST%\bin\" >nul || exit /b 1
copy /Y "%PY%\pythonw.exe" "%DEST%\bin\" >nul || exit /b 1
exit /b 0
