param(
    [string]$QtRoot = 'D:\QtIDEV4111',
    [string]$Configuration = 'Release',
    [switch]$Test,
    [switch]$Deploy,
    [switch]$Package,
    [switch]$Installer,
    [string]$InnoCompiler = 'D:\Inno Setup 6\ISCC.exe'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$qtSdk = Join-Path $QtRoot '6.11.0\mingw_64'
$compilerBin = Join-Path $QtRoot 'Tools\mingw1310_64\bin'
$cmake = Join-Path $QtRoot 'Tools\CMake_64\bin\cmake.exe'
$ninja = Join-Path $QtRoot 'Tools\Ninja\ninja.exe'
$env:PATH = "$qtSdk\bin;$compilerBin;$(Split-Path $ninja);$env:PATH"
$buildDir = Join-Path $projectRoot "build\$Configuration"
& $cmake -S $projectRoot -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_PREFIX_PATH=$qtSdk" "-DCMAKE_CXX_COMPILER=$compilerBin\g++.exe" "-DCMAKE_MAKE_PROGRAM=$ninja"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& $cmake --build $buildDir --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
if ($Test) {
    & (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir $buildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
}
if ($Deploy) {
    $deployRoot = Join-Path $projectRoot 'out\DeepMotorTool'
    $deployExe = [IO.Path]::GetFullPath((Join-Path $deployRoot 'bin\deep-motor-tool.exe'))
    Get-Process -Name 'deep-motor-tool' -ErrorAction SilentlyContinue | Where-Object {
        try { [IO.Path]::GetFullPath($_.Path) -eq $deployExe } catch { $false }
    } | Stop-Process -Force
    & $cmake --install $buildDir --prefix $deployRoot
    if ($LASTEXITCODE -ne 0) { throw 'Deployment failed' }
}
if ($Package -or $Installer) {
    New-Item -ItemType Directory -Path (Join-Path $projectRoot 'out') -Force | Out-Null
    $version = (Get-Content -LiteralPath (Join-Path $buildDir 'VERSION.txt') -Raw).Trim()
    if ($version -notmatch '^V\d+\.\d+\.\d+$') { throw "Invalid package version: $version" }
    $archive = Join-Path $projectRoot "out\DeepMotorTool-$version-win64.zip"
    # 发布前固定 V1.0.1；新包完成后替换同版本文件，不再递增预览版本号。
    if ($Installer -and -not (Test-Path -LiteralPath $InnoCompiler)) {
        throw "Inno Setup compiler not found: $InnoCompiler"
    }
    $stageParent = Join-Path $buildDir 'package-stage'
    $stageRoot = Join-Path $stageParent 'DeepMotorTool'
    $stageArchive = Join-Path $buildDir 'package-stage.zip'
    $resolvedBuild = [IO.Path]::GetFullPath($buildDir)
    $resolvedStage = [IO.Path]::GetFullPath($stageParent)
    if ($resolvedStage -ne (Join-Path $resolvedBuild 'package-stage') -or
        -not $resolvedStage.StartsWith($resolvedBuild + [IO.Path]::DirectorySeparatorChar,
                                      [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Package staging path is outside the build directory'
    }
    try {
        if (Test-Path -LiteralPath $stageParent) {
            if ((Get-Item -LiteralPath $stageParent).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw 'Package staging path is a link'
            }
            Remove-Item -LiteralPath $stageParent -Recurse -Force
        }
        if (Test-Path -LiteralPath $stageArchive) { Remove-Item -LiteralPath $stageArchive -Force }
        & $cmake --install $buildDir --prefix $stageRoot
        if ($LASTEXITCODE -ne 0) { throw 'Clean package installation failed' }
        if (Get-ChildItem -LiteralPath $stageRoot -Recurse -File | Where-Object {
            $_.Extension -eq '.csv' -or $_.Name -eq 'config.ini' -or $_.Extension -eq '.log'
        }) { throw 'Clean package contains local configuration, records, or logs' }
        if ($Package) {
            Compress-Archive -LiteralPath $stageRoot -DestinationPath $stageArchive -CompressionLevel Optimal
            Move-Item -LiteralPath $stageArchive -Destination $archive -Force
            Write-Host "Portable package: $archive"
        }
        if ($Installer) {
            & $InnoCompiler "/DAppVersion=$($version.Substring(1))" "/DSourceDir=$stageRoot" "/DPackageDir=$stageParent" (Join-Path $projectRoot 'installer\DeepMotorTool.iss')
            if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed' }
            $setupName = "DeepMotorTool-$version-Setup.exe"
            $setupSource = Join-Path $stageParent $setupName
            $setupTarget = Join-Path $projectRoot "out\$setupName"
            $setupVersion = (Get-Item -LiteralPath $setupSource).VersionInfo.ProductVersion.Trim()
            if ($setupVersion -ne $version.Substring(1)) { throw "Installer version mismatch: $setupVersion" }
            Move-Item -LiteralPath $setupSource -Destination $setupTarget -Force
            Write-Host "Inno Setup installer: $setupTarget"
        }
    }
    finally {
        if (Test-Path -LiteralPath $stageParent) { Remove-Item -LiteralPath $stageParent -Recurse -Force }
        if (Test-Path -LiteralPath $stageArchive) { Remove-Item -LiteralPath $stageArchive -Force }
    }
}
