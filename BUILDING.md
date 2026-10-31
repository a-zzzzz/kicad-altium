# Building from source (Windows)

The build uses KiCad's normal CMake + vcpkg setup. The first build compiles all dependencies
(wxWidgets, OpenCascade, Python, ngspice, …) and takes several hours; rebuilds after that only take
minutes.

## Prerequisites

Install these once (from an ordinary command prompt):

```bat
winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install Git.Git
winget install SWIG.SWIG
winget install JRSoftware.InnoSetup
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
```

CMake and Ninja come with the Visual Studio Build Tools. Inno Setup is only needed for the
installer. Keep the source in a short path such as `C:\src\kicad-altium`: deep paths can hit
Windows' 260-character limit during the dependency build.

## Build

```bat
git clone <this repository> C:\src\kicad-altium
cd C:\src\kicad-altium
build-altium.cmd
```

The result is in `build\install\bin`. Start it with:

```bat
run-altium.cmd                    rem project manager
run-altium.cmd pcbnew board.kicad_pcb
```

`run-altium.cmd` borrows the symbol, footprint and 3D libraries of an installed KiCad 10 if there is
one.

After changing code, `build-altium.cmd build` skips the configure step.

### Options

Set these environment variables before running `build-altium.cmd` to change the defaults:

| Variable | Default | Meaning |
|---|---|---|
| `BUILD_DIR` | `.\build` | Build tree (and `install` folder inside it) |
| `VCPKG_ROOT` | `C:\vcpkg` | vcpkg checkout |
| `VCPKG_INSTALLED_DIR` | `%BUILD_DIR%\vcpkg_installed` | Built dependencies; point several build trees at one copy to save hours |
| `SWIG_EXECUTABLE` | found automatically | Full path to `swig.exe` |

## Installer

```bat
installer\build-installer.cmd
```

This stages the build without debug symbols, adds KiCad's standard symbol and footprint libraries,
and writes `build\AltiumSkin-<version>-win64-setup.exe`. The version number is in
`installer\VERSION`.

The libraries are copied from an installed KiCad 10 (`C:\Program Files\KiCad\10.0\share\kicad`).
Set `KICAD_LIBS` to use another `share\kicad` folder, or `NO_LIBS=1` to build an installer without
them.

## Layout

| Path | Contents |
|---|---|
| `altium-profile/` | Altium hotkey set and colour theme, installed on first run |
| `installer/` | Inno Setup script and installer build script |
| `tools/altium/` | Helper scripts for the build |
| everything else | KiCad 10 source with the Altium Skin changes |

Linux and macOS builds follow KiCad's normal
[build instructions](https://dev-docs.kicad.org/en/build/); only the Windows scripts are provided
here.
