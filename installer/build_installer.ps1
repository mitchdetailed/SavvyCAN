<#
.SYNOPSIS
    Builds SavvyCAN for Windows and packages it into an Inno Setup installer.

.DESCRIPTION
    Runs the whole chain: qmake/make release build, windeployqt staging into
    deploy\, translation compile, then ISCC to produce
    installer\SavvyCAN_Setup_V<version>_x64.exe.

    The staged deploy\ tree is what the installer ships, so anything the app
    loads at runtime has to land there - that includes libusb-1.0.dll, the MinGW
    runtime DLLs and the help\ folder, none of which windeployqt knows about.

.PARAMETER QtDir
    Qt kit directory containing bin\qmake.exe. Defaults to the 6.7.2 MinGW kit.

.PARAMETER MakeExe
    mingw32-make.exe to build with.

.PARAMETER SkipBuild
    Reuse an existing build directory instead of recompiling.

.EXAMPLE
    .\installer\build_installer.ps1
#>
[CmdletBinding()]
param(
    [string]$QtDir   = 'C:\Qt\6.7.2\mingw_64',
    [string]$MakeExe = 'C:\Program Files\mingw64\bin\mingw32-make.exe',
    [string]$IsccExe = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$RepoRoot    = Split-Path -Parent $PSScriptRoot
$DeployDir   = Join-Path $RepoRoot 'deploy'
$BuildDir    = Join-Path $RepoRoot 'build-installer'
$QtBin       = Join-Path $QtDir 'bin'
$QMake       = Join-Path $QtBin 'qmake.exe'
$WinDeployQt = Join-Path $QtBin 'windeployqt.exe'
$LRelease    = Join-Path $QtBin 'lrelease.exe'

function Assert-Tool([string]$Path, [string]$What) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$What not found at '$Path'. Pass the right path via -QtDir/-MakeExe/-IsccExe."
    }
}

Assert-Tool $QMake       'qmake'
Assert-Tool $WinDeployQt 'windeployqt'
Assert-Tool $MakeExe     'mingw32-make'
Assert-Tool $IsccExe     'Inno Setup compiler (ISCC.exe)'

$env:PATH = "$QtBin;$(Split-Path -Parent $MakeExe);$env:PATH"

# ---------------------------------------------------------------- build
if (-not $SkipBuild) {
    Write-Host '==> Building SavvyCAN (release)' -ForegroundColor Cyan
    if (Test-Path -LiteralPath $BuildDir) { Remove-Item -Recurse -Force -LiteralPath $BuildDir }
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

    Push-Location $BuildDir
    try {
        & $QMake (Join-Path $RepoRoot 'SavvyCAN.pro') -spec win32-g++ 'CONFIG+=release'
        if ($LASTEXITCODE -ne 0) { throw "qmake failed ($LASTEXITCODE)" }

        & $MakeExe -j$env:NUMBER_OF_PROCESSORS release
        if ($LASTEXITCODE -ne 0) { throw "make failed ($LASTEXITCODE)" }
    }
    finally { Pop-Location }
}

$BuiltExe = Join-Path $BuildDir 'release\SavvyCAN.exe'
if (-not (Test-Path -LiteralPath $BuiltExe)) { throw "Build produced no binary at '$BuiltExe'." }

# --------------------------------------------------------------- stage
Write-Host '==> Staging deploy\' -ForegroundColor Cyan
if (Test-Path -LiteralPath $DeployDir) { Remove-Item -Recurse -Force -LiteralPath $DeployDir }
New-Item -ItemType Directory -Force -Path $DeployDir | Out-Null

Copy-Item -LiteralPath $BuiltExe -Destination $DeployDir

# SavvyCAN links libusb for the direct-USB backends (GVRET, CANalyst-II, gs_usb).
# windeployqt only knows about Qt, so this has to be copied by hand - without it
# the installed app dies at startup with 0xC0000135 (DLL not found).
Copy-Item -LiteralPath (Join-Path $RepoRoot 'third_party\libusb\MinGW64\dll\libusb-1.0.dll') -Destination $DeployDir

# MinGW runtime. windeployqt --compiler-runtime does not reliably place these.
foreach ($dll in @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
    $src = Join-Path $QtBin $dll
    if (-not (Test-Path -LiteralPath $src)) { throw "MinGW runtime '$dll' not found in '$QtBin'." }
    Copy-Item -LiteralPath $src -Destination $DeployDir
}

Write-Host '==> Running windeployqt' -ForegroundColor Cyan
& $WinDeployQt --release --no-translations --no-virtualkeyboard --no-quick-import `
               --no-system-d3d-compiler (Join-Path $DeployDir 'SavvyCAN.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed ($LASTEXITCODE)" }

# Debug-only QML tooling and the PDF image plugin drag in Qt6Quick*/Qt6Pdf,
# roughly 27 MB that a release build never loads.
foreach ($dir in @('qmltooling', 'generic', 'qml')) {
    $p = Join-Path $DeployDir $dir
    if (Test-Path -LiteralPath $p) { Remove-Item -Recurse -Force -LiteralPath $p }
}
foreach ($f in @('imageformats\qpdf.dll', 'Qt6Pdf.dll', 'Qt6Quick.dll', 'Qt6QmlModels.dll', 'Qt6Quick3DUtils.dll')) {
    $p = Join-Path $DeployDir $f
    if (Test-Path -LiteralPath $p) { Remove-Item -Force -LiteralPath $p }
}

# SavvyCAN's own translations, loaded from applicationDirPath()/translations
Write-Host '==> Compiling translations' -ForegroundColor Cyan
$TransOut = Join-Path $DeployDir 'translations'
New-Item -ItemType Directory -Force -Path $TransOut | Out-Null
foreach ($ts in Get-ChildItem (Join-Path $RepoRoot 'translations') -Filter *.ts) {
    & $LRelease $ts.FullName -qm (Join-Path $TransOut "$($ts.BaseName).qm") | Out-Null
}

# HelpWindow::showHelp reads applicationDirPath()/help/<file>, so these markdown
# files have to ship or every Help button opens a blank page.
Copy-Item -Recurse -Force -LiteralPath (Join-Path $RepoRoot 'help') -Destination (Join-Path $DeployDir 'help')

# Sanity check: everything the exe imports must resolve inside deploy\
$required = @('SavvyCAN.exe', 'libusb-1.0.dll', 'libgcc_s_seh-1.dll', 'libstdc++-6.dll',
              'libwinpthread-1.dll', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
              'Qt6SerialBus.dll', 'Qt6SerialPort.dll', 'Qt6Qml.dll', 'platforms\qwindows.dll')
foreach ($f in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $DeployDir $f))) { throw "deploy\ is missing '$f'." }
}

$size = (Get-ChildItem $DeployDir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
Write-Host ('    staged {0:N1} MB' -f $size)

# ------------------------------------------------------------- package
Write-Host '==> Compiling installer' -ForegroundColor Cyan
& $IsccExe (Join-Path $PSScriptRoot 'SavvyCAN_setup.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)" }

$out = Get-ChildItem $PSScriptRoot -Filter 'SavvyCAN_Setup_*.exe' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Write-Host ''
Write-Host ('Installer: {0} ({1:N1} MB)' -f $out.FullName, ($out.Length / 1MB)) -ForegroundColor Green
