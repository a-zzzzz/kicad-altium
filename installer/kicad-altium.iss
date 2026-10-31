; Inno Setup script for Altium Skin for KiCad.
; Build it with installer\build-installer.cmd, which stages the files and passes these defines.

#ifndef AppVer
  #define AppVer "0.0.0"
#endif
#ifndef StageDir
  #error StageDir must be defined (run installer\build-installer.cmd)
#endif
#ifndef LibsDir
  #error LibsDir must be defined (run installer\build-installer.cmd)
#endif
#ifndef OutDir
  #define OutDir "."
#endif
#define Icons "..\resources\bitmaps_png\icons"

[Setup]
; Keep this AppId: it lets new versions upgrade an existing install in place.
AppId={{1D17B77D-AA0E-478E-B75B-4D20B5E3807F}
AppName=Altium Skin for KiCad
AppVersion={#AppVer}
AppVerName=Altium Skin for KiCad {#AppVer}
AppPublisher=Altium Skin
VersionInfoVersion={#AppVer}
DefaultDirName={autopf}\Altium Skin
DefaultGroupName=Altium Skin
DisableProgramGroupPage=yes
LicenseFile={#StageDir}\LICENSE.GPLv3
InfoAfterFile=after-install.txt
OutputDir={#OutDir}
OutputBaseFilename=AltiumSkin-{#AppVer}-win64-setup
SetupIconFile={#Icons}\icon_kicad.ico
UninstallDisplayIcon={app}\bin\kicad.exe
UninstallDisplayName=Altium Skin for KiCad
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes
CloseApplications=yes
Compression=lzma2/max
SolidCompression=yes
LZMAUseSeparateProcess=yes
WizardStyle=modern

[Types]
Name: "full"; Description: "Program and KiCad standard libraries (recommended)"
Name: "program"; Description: "Program only (use your own or an installed KiCad's libraries)"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "program"; Description: "Altium Skin for KiCad"; Types: full program custom; Flags: fixed
Name: "libs"; Description: "KiCad standard symbol and footprint libraries"; Types: full

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
Name: "assoc"; Description: "Open .kicad_pro, .kicad_sch and .kicad_pcb files with Altium Skin"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Components: program; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#LibsDir}\*"; DestDir: "{app}"; Components: libs; Flags: recursesubdirs createallsubdirs ignoreversion

[InstallDelete]
; An upgrade without the libraries component removes the previously installed copy.
Type: filesandordirs; Name: "{app}\share\kicad\symbols"; Components: not libs
Type: filesandordirs; Name: "{app}\share\kicad\footprints"; Components: not libs

[UninstallDelete]
; Python writes __pycache__ folders here at run time; remove them so the install folder goes away.
Type: filesandordirs; Name: "{app}\bin\Lib"
Type: filesandordirs; Name: "{app}\share\kicad\scripting"

[Icons]
Name: "{autoprograms}\Altium Skin"; Filename: "{app}\bin\kicad.exe"; Comment: "Altium Skin for KiCad project manager"
Name: "{autoprograms}\Altium Skin Schematic Editor"; Filename: "{app}\bin\eeschema.exe"; IconFilename: "{app}\bin\eeschema.exe"
Name: "{autoprograms}\Altium Skin PCB Editor"; Filename: "{app}\bin\pcbnew.exe"; IconFilename: "{app}\bin\pcbnew.exe"
Name: "{autodesktop}\Altium Skin"; Filename: "{app}\bin\kicad.exe"; Tasks: desktopicon

[Registry]
; "Open with" entries are always added; the default handler only with the assoc task.
Root: HKA; Subkey: "Software\Classes\KiCadAltium.Project"; ValueType: string; ValueData: "KiCad Project (Altium Skin)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\KiCadAltium.Project\DefaultIcon"; ValueType: string; ValueData: """{app}\bin\kicad.exe"",0"
Root: HKA; Subkey: "Software\Classes\KiCadAltium.Project\shell\open\command"; ValueType: string; ValueData: """{app}\bin\kicad.exe"" ""%1"""
Root: HKA; Subkey: "Software\Classes\KiCadAltium.Schematic"; ValueType: string; ValueData: "KiCad Schematic (Altium Skin)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\KiCadAltium.Schematic\DefaultIcon"; ValueType: string; ValueData: """{app}\bin\eeschema.exe"",0"
Root: HKA; Subkey: "Software\Classes\KiCadAltium.Schematic\shell\open\command"; ValueType: string; ValueData: """{app}\bin\eeschema.exe"" ""%1"""
Root: HKA; Subkey: "Software\Classes\KiCadAltium.Board"; ValueType: string; ValueData: "KiCad PCB (Altium Skin)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\KiCadAltium.Board\DefaultIcon"; ValueType: string; ValueData: """{app}\bin\pcbnew.exe"",0"
Root: HKA; Subkey: "Software\Classes\KiCadAltium.Board\shell\open\command"; ValueType: string; ValueData: """{app}\bin\pcbnew.exe"" ""%1"""
Root: HKA; Subkey: "Software\Classes\.kicad_pro\OpenWithProgids"; ValueType: string; ValueName: "KiCadAltium.Project"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.kicad_sch\OpenWithProgids"; ValueType: string; ValueName: "KiCadAltium.Schematic"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.kicad_pcb\OpenWithProgids"; ValueType: string; ValueName: "KiCadAltium.Board"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.kicad_pro"; ValueType: string; ValueData: "KiCadAltium.Project"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.kicad_sch"; ValueType: string; ValueData: "KiCadAltium.Schematic"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.kicad_pcb"; ValueType: string; ValueData: "KiCadAltium.Board"; Tasks: assoc

[Run]
Filename: "{app}\bin\kicad.exe"; Description: "Start Altium Skin"; Flags: nowait postinstall skipifsilent
