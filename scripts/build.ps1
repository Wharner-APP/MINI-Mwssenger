[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$QtPrefix = '',
    [string]$VcpkgRoot = '',
    [switch]$ClassicVcpkg,
    [switch]$Clean,
    [switch]$NoDeploy
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root 'build\client'
$distDir = Join-Path $root 'dist'

function Get-VsInstallPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'vswhere.exe not found. Install Visual Studio with the Desktop development with C++ workload.'
    }
    $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $path) {
        throw 'Visual Studio with the MSVC x64 toolset was not found.'
    }
    return $path.Trim()
}

function Import-VsEnvironment([string]$vsPath) {
    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) {
        throw "vcvars64.bat not found: $vcvars"
    }

    # cmd /c with an inline quoted batch path is parsed differently by
    # PowerShell and cmd, so use a tiny generated batch file instead.
    $envScript = Join-Path ([IO.Path]::GetTempPath()) ('mini-vs-env-' + [guid]::NewGuid().ToString('N') + '.cmd')
    try {
        @"
@echo off
call "$vcvars" >nul 2>&1
if errorlevel 1 exit /b 1
set
"@ | Set-Content -Path $envScript -Encoding ASCII
        $lines = & cmd.exe /c $envScript
        if ($LASTEXITCODE -ne 0) {
            throw 'Failed to import the Visual Studio environment (vcvars64.bat).'
        }
        foreach ($line in $lines) {
            if ($line -match '^([^=]+)=(.*)$') {
                Set-Item -Path ('env:' + $matches[1]) -Value $matches[2]
            }
        }
    } finally {
        Remove-Item $envScript -Force -ErrorAction SilentlyContinue
    }
}

function Find-QtPrefix([string]$explicitPrefix) {
    $candidates = @()
    if ($explicitPrefix) { $candidates += $explicitPrefix }
    if ($env:MINI_QT_PREFIX) { $candidates += $env:MINI_QT_PREFIX }

    foreach ($candidate in $candidates) {
        if (Test-Path (Join-Path $candidate 'bin\qmake.exe')) { return (Resolve-Path $candidate).Path }
    }

    $roots = @('D:\Qt', (Join-Path $env:USERPROFILE 'Qt'))
    foreach ($qtRoot in $roots) {
        if (-not (Test-Path $qtRoot)) { continue }
        $versions = Get-ChildItem $qtRoot -Directory |
            Where-Object { $_.Name -match '^6\.8\.' } |
            Sort-Object Name -Descending
        foreach ($version in $versions) {
            $prefix = Join-Path $version.FullName 'msvc2022_64'
            if (Test-Path (Join-Path $prefix 'bin\qmake.exe')) { return $prefix }
        }
    }

    throw 'Qt 6.8 msvc2022_64 was not found. Install it with aqtinstall into D:\Qt or pass -QtPrefix.'
}

function Test-VcpkgRoot([string]$path) {
    if (-not $path) { return $false }
    return (Test-Path (Join-Path $path 'vcpkg.exe')) -and
           (Test-Path (Join-Path $path 'ports')) -and
           (Test-Path (Join-Path $path 'scripts\buildsystems\vcpkg.cmake'))
}

# Qt's OpenSSL TLS backend is a plugin that loads libssl-3-x64.dll and
# libcrypto-3-x64.dll at runtime. windeployqt only copies the SChannel and
# cert-only backends when it cannot see OpenSSL next to Qt, and then every
# HTTPS request fails with "server unreachable". Locate both libraries in the
# usual ships-with-the-toolchain places (Qt, Visual Studio's Git, Git for
# Windows, vcpkg, OpenSSL) and deploy them explicitly.
function Find-OpenSslBinDir([string]$qtPrefix, [string]$vsPath, [string[]]$extraDirs) {
    $required = @('libssl-3-x64.dll', 'libcrypto-3-x64.dll')
    $candidates = @()
    if ($extraDirs) { $candidates += $extraDirs }
    $candidates += (Join-Path $qtPrefix 'bin')
    if ($vsPath) {
        $candidates += (Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\mingw64\bin')
    }
    if ($env:ProgramFiles) {
        $candidates += (Join-Path $env:ProgramFiles 'Git\mingw64\bin')
        $candidates += (Join-Path $env:ProgramFiles 'OpenSSL-Win64\bin')
    }
    if (${env:ProgramFiles(x86)}) {
        $candidates += (Join-Path ${env:ProgramFiles(x86)} 'Git\mingw64\bin')
    }
    $qtRoot = Split-Path (Split-Path $qtPrefix -Parent) -Parent
    if ($qtRoot) {
        $candidates += (Join-Path $qtRoot 'Tools\OpenSSL\Win_x64\bin')
    }
    if ($env:OPENSSL_ROOT_DIR) {
        $candidates += (Join-Path $env:OPENSSL_ROOT_DIR 'bin')
    }

    foreach ($dir in $candidates) {
        if (-not $dir -or -not (Test-Path $dir)) { continue }
        $missing = $required | Where-Object { -not (Test-Path (Join-Path $dir $_)) }
        if (-not $missing) { return (Resolve-Path $dir).Path }
    }
    return $null
}

function Deploy-OpenSsl([string]$qtPrefix, [string]$vsPath, [string[]]$binDirs,
                        [string[]]$targetDirs) {
    $pluginName = if ($Configuration -eq 'Debug') { 'qopensslbackendd.dll' } else { 'qopensslbackend.dll' }
    $plugin = Join-Path $qtPrefix "plugins\tls\$pluginName"
    if (-not (Test-Path $plugin)) {
        throw "Qt OpenSSL backend is missing: $plugin. Install the Qt TLS plugin component."
    }

    $opensslDir = Find-OpenSslBinDir $qtPrefix $vsPath $binDirs
    if (-not $opensslDir) {
        throw ('OpenSSL 3 runtime (libssl-3-x64.dll, libcrypto-3-x64.dll) was not found. ' +
               'Install Git for Windows, OpenSSL, or set OPENSSL_ROOT_DIR; HTTPS in dist does not work without it.')
    }

    foreach ($targetDir in $targetDirs) {
        if (-not (Test-Path $targetDir)) { continue }
        foreach ($dll in @('libssl-3-x64.dll', 'libcrypto-3-x64.dll')) {
            Copy-Item (Join-Path $opensslDir $dll) $targetDir -Force
        }
        $tlsDir = Join-Path $targetDir 'tls'
        New-Item -ItemType Directory -Force -Path $tlsDir | Out-Null
        Copy-Item $plugin $tlsDir -Force
    }

    Write-Host "OpenSSL       : $opensslDir"
    return $opensslDir
}

$vsPath = Get-VsInstallPath
Import-VsEnvironment $vsPath

$cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) { $cmake = (Get-Command cmake).Source }
$ninja = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
if (-not (Test-Path $ninja)) { $ninja = (Get-Command ninja).Source }

# The vcpkg that ships with Visual Studio has no ports/ registry of its own,
# so prefer an explicit root, VCPKG_ROOT, a standalone clone, then the bundled one.
$vcpkgCandidates = @()
if ($VcpkgRoot) { $vcpkgCandidates += $VcpkgRoot }
if ($env:VCPKG_ROOT) { $vcpkgCandidates += $env:VCPKG_ROOT }
$vcpkgCandidates += 'D:\vcpkg'
$vcpkgCandidates += (Join-Path $vsPath 'VC\vcpkg')
$vcpkgRoot = $vcpkgCandidates | Where-Object { Test-VcpkgRoot $_ } | Select-Object -First 1
if (-not $vcpkgRoot) {
    throw 'A vcpkg root with ports/ was not found. Clone https://github.com/microsoft/vcpkg, run bootstrap-vcpkg.bat, and pass -VcpkgRoot.'
}
$toolchain = Join-Path $vcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path $toolchain)) {
    throw "vcpkg toolchain not found: $toolchain"
}
$env:VCPKG_ROOT = $vcpkgRoot

$qtPrefixResolved = Find-QtPrefix $QtPrefix

if ($Clean) {
    if (Test-Path $buildDir) { Remove-Item -Recurse -Force $buildDir }
    if (Test-Path $distDir) { Remove-Item -Recurse -Force $distDir }
}

Write-Host "Visual Studio : $vsPath"
Write-Host "Qt            : $qtPrefixResolved"
Write-Host "vcpkg         : $vcpkgRoot"
Write-Host "Configuration : $Configuration"

$configureArgs = @(
    '-S', (Join-Path $root 'client'),
    '-B', $buildDir,
    '-G', 'Ninja',
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    '-DVCPKG_TARGET_TRIPLET=x64-windows',
    "-DCMAKE_PREFIX_PATH=$qtPrefixResolved",
    "-DCMAKE_MAKE_PROGRAM=$ninja"
)

if ($ClassicVcpkg) {
    Write-Host '== vcpkg classic install (manifest mode disabled) =='
    & (Join-Path $vcpkgRoot 'vcpkg.exe') install 'libsodium:x64-windows' 'sqlite3:x64-windows' `
        --triplet x64-windows --vcpkg-root $vcpkgRoot
    if ($LASTEXITCODE -ne 0) { throw "vcpkg classic install failed with exit code $LASTEXITCODE." }
    $configureArgs += '-DVCPKG_MANIFEST_MODE=OFF'
}

& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE." }

& $cmake --build $buildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }

$exe = Join-Path $buildDir 'MINI Messenger.exe'
if (-not (Test-Path $exe)) { throw "Build finished but $exe is missing." }

$vcpkgBinCandidates = @(
    (Join-Path $buildDir 'vcpkg_installed\x64-windows\bin'),
    (Join-Path $vcpkgRoot 'installed\x64-windows\bin')
)
$vcpkgBin = $vcpkgBinCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

# Tests and local runs execute from the build tree too, so the TLS backend
# must be loadable there as well as in dist.
Deploy-OpenSsl -qtPrefix $qtPrefixResolved -vsPath $vsPath -binDirs @($vcpkgBin) `
    -targetDirs @($buildDir) | Out-Null

if ($NoDeploy) {
    Write-Host "Built: $exe"
    exit 0
}

New-Item -ItemType Directory -Force -Path $distDir | Out-Null
Copy-Item $exe $distDir -Force
$qmFile = Join-Path $buildDir 'translations\mini.qm'
if (Test-Path $qmFile) {
    $distTranslations = Join-Path $distDir 'translations'
    New-Item -ItemType Directory -Force -Path $distTranslations | Out-Null
    Copy-Item $qmFile $distTranslations -Force
}
foreach ($folder in @('config', 'data', 'data\logs', 'media', 'backups')) {
    New-Item -ItemType Directory -Force -Path (Join-Path $distDir $folder) | Out-Null
}

if ($vcpkgBin) {
    foreach ($dll in @('libsodium.dll', 'sqlite3.dll')) {
        $source = Join-Path $vcpkgBin $dll
        if (Test-Path $source) {
            Copy-Item $source $distDir -Force
        }
    }
}

$windeployqt = Join-Path $qtPrefixResolved 'bin\windeployqt.exe'
if (Test-Path $windeployqt) {
    $deployMode = if ($Configuration -eq 'Debug') { '--debug' } else { '--release' }
    # windeployqt writes harmless warnings to stderr; do not let PowerShell
    # turn them into terminating errors.
    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $windeployqt $deployMode --no-translations --no-opengl-sw --no-system-d3d-compiler --dir $distDir (Join-Path $distDir 'MINI Messenger.exe') 2>&1 | Out-Host
    $deployExitCode = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorAction
    if ($deployExitCode -ne 0) {
        Write-Warning "windeployqt returned $deployExitCode; the exe in dist may need Qt DLLs from PATH."
    }
} else {
    Write-Warning "windeployqt.exe not found in $qtPrefixResolved\bin; dist is not self-contained."
}

# windeployqt ships qschannelbackend/qcertonlybackend when OpenSSL is not
# visible next to Qt. Without qopensslbackend every HTTPS request in dist hangs
# against the self-signed server, so deploy the OpenSSL stack ourselves and
# refuse to produce a dist that cannot speak TLS.
Deploy-OpenSsl -qtPrefix $qtPrefixResolved -vsPath $vsPath -binDirs @($vcpkgBin) `
    -targetDirs @($distDir) | Out-Null

$opensslPluginName = if ($Configuration -eq 'Debug') { 'qopensslbackendd.dll' } else { 'qopensslbackend.dll' }
foreach ($required in @('libssl-3-x64.dll', 'libcrypto-3-x64.dll',
                        (Join-Path 'tls' $opensslPluginName))) {
    $fullPath = Join-Path $distDir $required
    if (-not (Test-Path $fullPath)) {
        throw "dist is missing $required; HTTPS would not work."
    }
}

Write-Host "Built: $distDir\MINI Messenger.exe"
