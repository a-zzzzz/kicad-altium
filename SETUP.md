# Setup guide

This guide covers installing Altium Skin for KiCad on Windows, the first start, libraries, and
what to do if something goes wrong.

## Requirements

- Windows 10 or 11, 64-bit
- About 1.5 GB of disk space with the standard libraries (about 500 MB without them)
- A graphics card with OpenGL 3.0 or newer (any GPU from the last ten years)

Stock KiCad does **not** need to be installed. If it is, both run side by side without sharing
settings.

## 1. Download

Get `AltiumSkin-<version>-win64-setup.exe` from the repository's **Releases** page.

The installer isn't code-signed, so Windows SmartScreen may show *"Windows protected your PC"*.
Click **More info ▸ Run anyway**.

## 2. Run the installer

| Page | What to choose |
|---|---|
| **Install mode** | *Install for all users* (needs administrator rights, goes to `C:\Program Files\Altium Skin`) or *Install for me only* (no admin rights, goes to `%LOCALAPPDATA%\Programs\Altium Skin`). |
| **License** | KiCad's GNU GPL v3. Accept to continue. |
| **Destination** | Keep the default unless you have a reason to change it. |
| **Components** | **Program and KiCad standard libraries** (recommended) gives you the full set of standard symbols and footprints. **Program only** is for people who use their own libraries or an installed KiCad's. |
| **Additional tasks** | *Create a desktop shortcut*, and *Open .kicad_pro, .kicad_sch and .kicad_pcb files with Altium Skin*. Leave the second unticked if you want stock KiCad to stay the default for those files; Altium Skin still appears in Explorer's **Open with** menu either way. |

Click **Install**, then **Finish** to start the program.

### Silent install

For scripted installs:

```bat
AltiumSkin-0.2.0-win64-setup.exe /VERYSILENT /ALLUSERS
AltiumSkin-0.2.0-win64-setup.exe /VERYSILENT /CURRENTUSER /COMPONENTS="program" /TASKS="desktopicon"
```

## 3. First start

Start **Altium Skin** from the Start menu. On the first run it creates its settings folder,
`%APPDATA%\kicad-altium`, installs the Altium hotkey set and the **Altium** colour theme, and opens
KiCad's **Welcome to KiCad 10.0** setup wizard:

| Wizard page | What to choose |
|---|---|
| **Configuration** | **Start with default settings.** Importing settings from a stock KiCad install would bring its hotkeys and layout along. |
| **Libraries** | **Start with the built-in KiCad libraries** if you installed the standard libraries. With *Program only*, choose **Start with no libraries** and add your own afterwards (section 4). |
| **Updates & Privacy** | Your choice. It only checks for updates to add-ons from the Plugin and Content Manager. |

Click **Finish**.

The main window opens with the Projects panel on the left. Use **File ▸ New ▸ Project**, or
**File ▸ Open Project** for an existing `.kicad_pro`. Schematic and PCB documents open as tabs in the
same window, like in Altium.

## 4. Libraries

**With the standard libraries installed:** everything is ready. They live in
`<install folder>\share\kicad\symbols` and `...\footprints`.

**Program only:** add libraries under **Preferences ▸ Manage Symbol Libraries** and
**Preferences ▸ Manage Footprint Libraries**. To use an installed KiCad 10's libraries, set these
under **Preferences ▸ Configure Paths**:

| Variable | Value |
|---|---|
| `KICAD10_SYMBOL_DIR` | `C:\Program Files\KiCad\10.0\share\kicad\symbols` |
| `KICAD10_FOOTPRINT_DIR` | `C:\Program Files\KiCad\10.0\share\kicad\footprints` |

**3D models** are not bundled (they are over 3 GB). Install KiCad 10 (or download
[kicad-packages3D](https://gitlab.com/kicad/libraries/kicad-packages3D)), then set
`KICAD10_3DMODEL_DIR` under **Preferences ▸ Configure Paths** to its `share\kicad\3dmodels` folder.

**Altium libraries:** KiCad can import `.SchLib`, `.PcbLib` and `.IntLib` files under
**Preferences ▸ Manage Symbol/Footprint Libraries ▸ Add existing library**, with the file type
set to Altium.

## 5. Opening Altium projects

Use **File ▸ Import ▸ Non-KiCad Project** in the project manager and pick the `.PrjPcb` file. KiCad's
importer converts the schematics and board into a new KiCad project; check the result, especially
polygons, rules and custom pad shapes.

## Updating

Download the new installer and run it. It upgrades the existing install in place and keeps your
settings. You don't need to uninstall first.

## Uninstalling

**Settings ▸ Apps ▸ Installed apps ▸ Altium Skin for KiCad ▸ Uninstall**. This removes the program
and its file associations. Your projects and the `%APPDATA%\kicad-altium` settings folder are left
alone; delete that folder by hand for a completely clean removal.

## Troubleshooting

| Problem | Fix |
|---|---|
| Layout, panels or hotkeys are messed up | Close the program and delete `%APPDATA%\kicad-altium`. The next start rebuilds the defaults. |
| No symbols or footprints in the choosers | Check **Preferences ▸ Manage Symbol Libraries**. If the table is empty, re-run the installer with the libraries component, then delete `sym-lib-table` and `fp-lib-table` from `%APPDATA%\kicad-altium\10.0` and restart to get the wizard's **Libraries** page again. Or add libraries as in section 4. |
| 3D viewer shows boards without parts | 3D models aren't bundled; see section 4. |
| Black or flickering canvas | Update your graphics driver. As a workaround, **Preferences ▸ Common ▸ Accelerated graphics** can be set to fallback mode. |
| Double-clicking a `.kicad_pcb` opens stock KiCad | Right-click the file ▸ **Open with ▸ Choose another app ▸ KiCad PCB (Altium Skin)** ▸ *Always*. Windows 11 keeps its own choice per file type. |
| `.kicad_pro` files still open in Altium Skin after uninstalling | Pick another default with **Open with ▸ Choose another app**. |
