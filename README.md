# Altium Skin for KiCad

An Altium Designer–style version of [KiCad](https://www.kicad.org/) 10: the familiar Altium layout,
menus, panels, shortcuts and editing behaviour, on top of KiCad's free and open-source engine.

> **Not affiliated with Altium.** "Altium" and "Altium Designer" are trademarks of their owners and
> are used here only to describe the workflow this project imitates. It contains no Altium code,
> icons, images or other assets; every feature is a clean-room reimplementation written from
> observed behaviour and public documentation. Icons are original drawings.

## What's different from stock KiCad

- One main window with document tabs; Projects, SCH Library, PCB Library, Components, Messages
  and Properties panels in Altium-style tab strips.
- Altium menu layout, single-key menu chords (`P`,`T`, `J`,`C`, …), popup chord menus and hotkeys.
- Context-sensitive Properties panel with per-object layouts, Active Bar, layer tabs,
  Board Insight heads-up display, Find Similar Objects, List panels and selection memory.
- Altium-style schematic and PCB library editing, Update PCB (ECO) dialog, interactive routing
  options, power ports, project-local library creation, and more.

Files are ordinary KiCad 10 files, so projects open in stock KiCad too, and KiCad's importers for
Altium `.SchDoc` / `.PcbDoc` / `.IntLib` files are available under **File ▸ Import**.

## Install (Windows 10/11, 64-bit)

1. Download `AltiumSkin-<version>-win64-setup.exe` from the [Releases](../../releases) page.
2. Run it and follow the wizard.

Full walkthrough, options and troubleshooting: **[SETUP.md](SETUP.md)**.

## Build from source

See **[BUILDING.md](BUILDING.md)**. In short, with Visual Studio 2022 Build Tools, vcpkg and SWIG
installed:

```bat
build-altium.cmd                   rem configure + build (first run also builds dependencies)
run-altium.cmd                     rem start the build
installer\build-installer.cmd      rem make the setup .exe
```

## License

Same as KiCad: GNU GPL v3 or later (see [LICENSE](LICENSE) and [LICENSE.README](LICENSE.README)).
The installer's bundled symbol and footprint libraries are KiCad's, under CC-BY-SA 4.0 with the
KiCad library exception. The original KiCad README is in [README-KiCad.md](README-KiCad.md).
