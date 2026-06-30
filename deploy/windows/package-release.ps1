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
$qtPlugins = Join-Path $QtDir "plugins"

if (-not (Test-Path -LiteralPath $CMake)) {
    throw "CMake was not found at $CMake"
}
if (-not (Test-Path -LiteralPath $qtBin)) {
    throw "Qt bin directory was not found at $qtBin"
}
if (-not (Test-Path -LiteralPath $mingwBin)) {
    throw "MinGW bin directory was not found at $mingwBin"
}
if (-not (Test-Path -LiteralPath $qtPlugins)) {
    throw "Qt plugins directory was not found at $qtPlugins"
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

function Copy-ReleaseFile {
    param(
        [string]$Source,
        [string]$Destination
    )
    if (-not (Test-Path -LiteralPath $Source)) {
        throw "Required release file was not found: $Source"
    }
    $destinationDir = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $destinationDir)) {
        New-Item -ItemType Directory -Path $destinationDir -Force | Out-Null
    }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

$qtRootDlls = @(
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Multimedia.dll",
    "Qt6Network.dll",
    "Qt6Svg.dll",
    "Qt6Widgets.dll",
    "opengl32sw.dll",
    "D3Dcompiler_47.dll",
    "avcodec-61.dll",
    "avformat-61.dll",
    "avutil-59.dll",
    "swresample-5.dll",
    "swscale-8.dll"
)
foreach ($dll in $qtRootDlls) {
    Copy-ReleaseFile -Source (Join-Path $qtBin $dll) -Destination (Join-Path $distDir $dll)
}

$mingwDlls = @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")
foreach ($dll in $mingwDlls) {
    Copy-ReleaseFile -Source (Join-Path $mingwBin $dll) -Destination (Join-Path $distDir $dll)
}

$pluginFiles = @(
    "generic\qtuiotouchplugin.dll",
    "iconengines\qsvgicon.dll",
    "imageformats\qgif.dll",
    "imageformats\qico.dll",
    "imageformats\qjpeg.dll",
    "imageformats\qsvg.dll",
    "multimedia\ffmpegmediaplugin.dll",
    "multimedia\windowsmediaplugin.dll",
    "networkinformation\qnetworklistmanager.dll",
    "platforms\qwindows.dll",
    "styles\qmodernwindowsstyle.dll",
    "tls\qcertonlybackend.dll",
    "tls\qschannelbackend.dll"
)
foreach ($plugin in $pluginFiles) {
    Copy-ReleaseFile -Source (Join-Path $qtPlugins $plugin) -Destination (Join-Path $distDir $plugin)
}

$requiredDlls = @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Network.dll", "Qt6Multimedia.dll")
foreach ($dll in $requiredDlls) {
    if (-not (Test-Path -LiteralPath (Join-Path $distDir $dll))) {
        throw "Release package is missing $dll"
    }
}

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
