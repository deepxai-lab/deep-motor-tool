param([switch]$KeepSimulator)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $projectRoot 'out\DeepMotorTool\bin'
$simulatorExe = Join-Path $bin 'deep-device-simulator.exe'
$toolExe = Join-Path $bin 'deep-motor-tool.exe'
if (-not (Test-Path -LiteralPath $simulatorExe) -or -not (Test-Path -LiteralPath $toolExe)) {
    throw '请先运行 scripts\build.ps1 -Test -Deploy'
}
$stdout = Join-Path (Split-Path $bin) 'simulator.log'
$stderr = Join-Path (Split-Path $bin) 'simulator-error.log'
$simulator = Start-Process -FilePath $simulatorExe -ArgumentList '--bind', '127.0.0.1' `
    -WorkingDirectory $bin -WindowStyle Hidden -RedirectStandardOutput $stdout `
    -RedirectStandardError $stderr -PassThru
Start-Sleep -Milliseconds 400
if ($simulator.HasExited) {
    throw "模拟设备启动失败，请检查 $stderr（UDP 43893 可能已占用）"
}
try {
    $application = Start-Process -FilePath $toolExe -ArgumentList '--loopback-demo' `
        -WorkingDirectory $bin -PassThru
    $application.WaitForExit()
}
finally {
    if (-not $KeepSimulator -and -not $simulator.HasExited) {
        Stop-Process -Id $simulator.Id
    }
}
