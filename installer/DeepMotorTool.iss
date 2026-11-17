; 版本号与输入目录由打包脚本传入，避免安装包与程序版本不一致。
#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef SourceDir
  #error SourceDir is required
#endif
#ifndef PackageDir
  #error PackageDir is required
#endif

[Setup]
AppId={{EA266D5D-340C-4EB0-83C5-959FCF5F8B7A}
AppName=DEEP Robotics 关节调试系统
AppVersion={#AppVersion}
AppVerName=DEEP Robotics 关节调试系统 V{#AppVersion}
AppPublisher=DEEP Robotics
VersionInfoVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\DeepMotorTool
DefaultGroupName=DEEP Robotics 关节调试系统
DisableProgramGroupPage=yes
DisableDirPage=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
WizardStyle=modern
SetupIconFile=..\resources\branding\deep-app-icon.ico
UninstallDisplayIcon={app}\bin\deep-motor-tool.exe
OutputDir={#PackageDir}
OutputBaseFilename=DeepMotorTool-V{#AppVersion}-Setup
Compression=lzma2
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "zh"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; 只安装干净暂存目录中的程序文件；不携带客户配置、记录或调试工具。
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "bin\records\*,*.csv,*.log,config.ini,deep-device-simulator.exe"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\DEEP Robotics 关节调试系统"; Filename: "{app}\bin\deep-motor-tool.exe"; WorkingDir: "{app}\bin"
Name: "{autodesktop}\DEEP Robotics 关节调试系统"; Filename: "{app}\bin\deep-motor-tool.exe"; WorkingDir: "{app}\bin"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\deep-motor-tool.exe"; Description: "{cm:LaunchProgram,DEEP Robotics}"; Flags: nowait postinstall skipifsilent unchecked

; 不设置递归卸载删除，也不删除用户注册表配置，保留测试记录与操作习惯。
