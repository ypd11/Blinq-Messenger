param(
    [string]$QtDir = "C:\Qt\Qt\6.11.1\mingw_64",
    [string]$CMake = "C:\Qt\Qt\Tools\CMake_64\bin\cmake.exe",
    [string]$InnoSetupCompiler = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    [switch]$SkipInstaller
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Resolve-Path (Join-Path $scriptDir "..\..")
$buildDir = Join-Path $root "build\Desktop_Qt_6_11_1_MinGW_64_bit-Release"
$distDir = Join-Path $root "deploy\windows\dist\BlinqMessenger"
$installerDir = Join-Path $root "deploy\windows\installer"
$qtBin = Join-Path $QtDir "bin"
$mingwBin = "C:\Qt\Qt\Tools\mingw1310_64\bin"
$windeployqt = Join-Path $qtBin "windeployqt.exe"

if (-not (Test-Path -LiteralPath $CMake)) {
    throw "CMake was not found at $CMake"
}
if (-not (Test-Path -LiteralPath $windeployqt)) {
    throw "windeployqt was not found at $windeployqt"
}

$env:PATH = "$qtBin;$mingwBin;$env:PATH"

& $CMake -S $root -B $buildDir -G "MinGW Makefiles" "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_PREFIX_PATH=$QtDir"
& $CMake --build $buildDir --config Release

if (Test-Path -LiteralPath $distDir) {
    Remove-Item -LiteralPath $distDir -Recurse -Force
}
New-Item -ItemType Directory -Path $distDir | Out-Null
New-Item -ItemType Directory -Path $installerDir -Force | Out-Null

Copy-Item -LiteralPath (Join-Path $buildDir "messenger.exe") -Destination $distDir
Copy-Item -LiteralPath (Join-Path $root "help.html") -Destination $distDir
New-Item -ItemType Directory -Path (Join-Path $distDir "assets") -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root "assets\appicon.ico") -Destination (Join-Path $distDir "assets\appicon.ico")

& $windeployqt --release --no-translations --compiler-runtime (Join-Path $distDir "messenger.exe")

if (-not $SkipInstaller) {
    if (-not (Test-Path -LiteralPath $InnoSetupCompiler)) {
        throw "Inno Setup compiler was not found at $InnoSetupCompiler. Install Inno Setup 6 or rerun with -SkipInstaller."
    }
    & $InnoSetupCompiler (Join-Path $scriptDir "BlinqMessenger.iss") "/DSourceDir=$distDir" "/DOutputDir=$installerDir"
}

Write-Host "Release package ready: $distDir"
if (-not $SkipInstaller) {
    Write-Host "Installer output: $installerDir"
}
