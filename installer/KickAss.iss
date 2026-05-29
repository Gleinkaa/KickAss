; KickAss — Inno Setup installer script (v1.1)
; Builds a Windows installer that deploys:
;   * KickAss.vst3  -> C:\Program Files\Common Files\VST3\        (component: vst3)
;   * KickAss.exe    -> C:\Program Files\KickAss\                  (component: standalone)
;
; Build:
;   1. Build the plugin Release first:
;        cmake --build build --config Release --target KickAss_All
;   2. Compile this script with Inno Setup 6:
;        scripts\build_installer.bat
;      or directly:
;        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\KickAss.iss
;
; Output: installer\Output\KickAss-<version>-Setup.exe
;
; Override the build artefacts dir (e.g. for a different config) with:
;   ISCC.exe /DBuildDir="..\build\KickAss_artefacts\Release" installer\KickAss.iss

#define MyAppName        "KickAss"
#define MyAppPublisher   "Gleinkaa"
#define MyAppURL         "https://github.com/Gleinkaa/KickAss"
#define MyAppExeName     "KickAss.exe"

; Marketing version (the CMake project version trails at 0.1.x — bump there when tagging).
#ifndef MyAppVersion
  #define MyAppVersion "1.1.0"
#endif

; Where the Release artefacts live, relative to this .iss (installer\ -> project root).
#ifndef BuildDir
  #define BuildDir "..\build\KickAss_artefacts\Release"
#endif

[Setup]
AppId={{9D2A6C41-3F1B-4C2E-9C7E-KICKASS11ABCD}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; VST3 lands in Common Files and the standalone in Program Files: both need admin.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=Output
OutputBaseFilename=KickAss-{#MyAppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName} {#MyAppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Components]
Name: "vst3";       Description: "VST3 plugin (for your DAW)";   Types: full custom; Flags: checkablealone
Name: "standalone"; Description: "Standalone application";        Types: full custom

[Types]
Name: "full";   Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Files]
; VST3 is a *bundle* (a folder) — recurse it into Common Files\VST3.
Source: "{#BuildDir}\VST3\KickAss.vst3\*"; DestDir: "{commoncf}\VST3\KickAss.vst3"; \
    Flags: recursesubdirs createallsubdirs ignoreversion; Components: vst3

; Standalone executable.
Source: "{#BuildDir}\Standalone\{#MyAppExeName}"; DestDir: "{app}"; \
    Flags: ignoreversion; Components: standalone

[Icons]
Name: "{group}\{#MyAppName}";              Filename: "{app}\{#MyAppExeName}"; Components: standalone
Name: "{group}\Uninstall {#MyAppName}";    Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}";        Filename: "{app}\{#MyAppExeName}"; Components: standalone; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"; Components: standalone; Flags: unchecked

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; \
    Flags: nowait postinstall skipifsilent; Components: standalone

[UninstallDelete]
; Remove the VST3 bundle dir if anything is left behind after the per-file uninstall.
Type: filesandordirs; Name: "{commoncf}\VST3\KickAss.vst3"
