; Inno Setup script for "Auto Exposure (Adaptive Brightness)" OBS Studio plugin
; Build with: ISCC.exe installer\setup.iss   (run from the project root)
; Requires the plugin to already be built in RelWithDebInfo (see README.md).

#define MyAppName "Auto Exposure (Adaptive Brightness) for OBS Studio"
#define MyAppVersion "1.0.1"
#define MyAppPublisher "nice okiraku"
#define MyPluginId "auto-exposure-filter"
#define MyBuildDir "..\build_x64\rundir\RelWithDebInfo"

[Setup]
AppId={{8F2C1E3A-6B4D-4E7F-9A2B-5C8D3E1F4A6B}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL=https://example.com
DefaultDirName={commonappdata}\obs-studio\plugins\{#MyPluginId}
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\release
OutputBaseFilename={#MyPluginId}-{#MyAppVersion}-windows-installer
Compression=lzma2
SolidCompression=yes
LicenseFile=..\LICENSE
UninstallDisplayIcon={app}\bin\64bit\{#MyPluginId}.dll
WizardStyle=modern

; Installer UI (buttons, labels, etc.) is Japanese-only. The [Setup] LicenseFile
; below stays in English on purpose: the FSF considers only the original English
; text of the GPL legally authoritative, so it is not translated here.
[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[Files]
Source: "{#MyBuildDir}\{#MyPluginId}.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "..\data\auto_exposure.effect"; DestDir: "{app}\data"; Flags: ignoreversion
Source: "..\data\locale\en-US.ini"; DestDir: "{app}\data\locale"; Flags: ignoreversion
Source: "..\data\locale\ja-JP.ini"; DestDir: "{app}\data\locale"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
