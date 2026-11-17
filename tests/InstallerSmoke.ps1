param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $projectRoot 'build\Release\VERSION.txt') -Raw).Trim()
$installer = Join-Path $projectRoot "out\DeepMotorTool-$version-Setup.exe"
$qaRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build\Release\installer-qa'))
$installRoot = Join-Path $qaRoot 'app'
$uninstallKey = '{EA266D5D-340C-4EB0-83C5-959FCF5F8B7A}_is1'
$registryRoots = @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
                   'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
                   'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall')

# 仅验证尚未安装的产品；不覆盖用户安装，也不复用旧的验收目录。
foreach ($root in $registryRoots) {
    if (Test-Path -LiteralPath (Join-Path $root $uninstallKey)) {
        throw 'The application is already installed; installer smoke test cancelled'
    }
}
if (-not (Test-Path -LiteralPath $installer)) { throw 'Installer is missing' }
if (Test-Path -LiteralPath $qaRoot) { throw 'Installer QA directory already exists' }
New-Item -ItemType Directory -Path $qaRoot | Out-Null

function Invoke-TestInstall([string]$logName) {
    $installArgs = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOICONS', '/TASKS=""',
                     ('/DIR="' + $installRoot + '"'), ('/LOG="' + (Join-Path $qaRoot $logName) + '"'))
    $process = Start-Process -FilePath $installer -ArgumentList $installArgs -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Installer failed: $($process.ExitCode)" }
}

Invoke-TestInstall 'install.log'
$installedExe = Join-Path $installRoot 'bin\deep-motor-tool.exe'
if (-not (Test-Path -LiteralPath $installedExe)) { throw 'Installed executable is missing' }
$installedVersion = (Get-Item -LiteralPath $installedExe).VersionInfo.ProductVersion.Trim()
if ($installedVersion -ne $version.Substring(1)) { throw 'Installed application version mismatch' }
if ((Get-Content -LiteralPath (Join-Path $installRoot 'VERSION.txt') -Raw).Trim() -ne $version) {
    throw 'Installed version manifest mismatch'
}
if (Get-ChildItem -LiteralPath $installRoot -Recurse -File | Where-Object {
    $_.Extension -eq '.csv' -or $_.Name -eq 'config.ini' -or $_.Name -eq 'deep-device-simulator.exe'
}) { throw 'Installer contains local data or development tools' }

# 从启动环境中移除 Qt 和编译器路径，使用安装包自带运行库验证 Windows 界面。
$savedPath = $env:PATH
$savedQtPluginPath = $env:QT_PLUGIN_PATH
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_PLUGIN_PATH = $null
    $smoke = Start-Process -FilePath $installedExe -ArgumentList '--smoke-test -platform windows' -WorkingDirectory (Split-Path $installedExe) -WindowStyle Hidden -Wait -PassThru
    if ($smoke.ExitCode -ne 0) { throw "Installed UI smoke test failed: $($smoke.ExitCode)" }
}
finally {
    $env:PATH = $savedPath
    $env:QT_PLUGIN_PATH = $savedQtPluginPath
}

# 这些文件仅是本次验收样本，用于检查覆盖安装和卸载不会误删用户数据。
$records = Join-Path $installRoot 'bin\records'
New-Item -ItemType Directory -Path $records | Out-Null
$csv = Join-Path $records 'installer-test.csv'
$config = Join-Path $installRoot 'bin\config.ini'
Set-Content -LiteralPath $csv -Value "time_s,value`n0,1" -Encoding UTF8
Set-Content -LiteralPath $config -Value '[installer-test]' -Encoding UTF8
$expectedCsvHash = (Get-FileHash -LiteralPath $csv).Hash
$expectedConfigHash = (Get-FileHash -LiteralPath $config).Hash

function Assert-TestDataPreserved {
    if ((Get-FileHash -LiteralPath $csv).Hash -ne $expectedCsvHash -or
        (Get-FileHash -LiteralPath $config).Hash -ne $expectedConfigHash) {
        throw 'User data changed during installation or uninstallation'
    }
}

Invoke-TestInstall 'reinstall.log'
Assert-TestDataPreserved
$uninstall = Join-Path $installRoot 'unins000.exe'
$uninstallArgs = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
                   ('/LOG="' + (Join-Path $qaRoot 'uninstall.log') + '"'))
$process = Start-Process -FilePath $uninstall -ArgumentList $uninstallArgs -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Uninstaller failed: $($process.ExitCode)" }
Assert-TestDataPreserved
if (Test-Path -LiteralPath $installedExe) { throw 'Uninstaller left the application executable' }
foreach ($root in $registryRoots) {
    if (Test-Path -LiteralPath (Join-Path $root $uninstallKey)) {
        throw 'Uninstaller left the product registration'
    }
}
Write-Output 'PASS: installation, packaged runtime, Windows UI, reinstallation, uninstallation, user data preservation'
Write-Output "QA evidence: $qaRoot"
