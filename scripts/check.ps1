[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$ServerOnly
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $ServerOnly) {
    $buildDir = Join-Path $root 'build\client'

    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE."
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
    $ctest = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
    if (-not (Test-Path $ctest)) { $ctest = (Get-Command ctest).Source }

    Write-Host '== Client tests (ctest) =='
    & $ctest --test-dir $buildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw "ctest failed with exit code $LASTEXITCODE."
    }
} else {
    Write-Host '== Client build and ctest skipped (-ServerOnly) =='
}

$serverTests = Join-Path $root 'server\tests'
$hasServerTests = $false
if (Test-Path $serverTests) {
    $hasServerTests = [bool](Get-ChildItem $serverTests -Recurse -Filter 'test_*.py' -ErrorAction SilentlyContinue |
        Select-Object -First 1)
}

if (-not $hasServerTests) {
    Write-Host '== Server tests: none yet, pytest skipped =='
    exit 0
}

$venvPython = Join-Path $root 'server\.venv\Scripts\python.exe'
$python = if (Test-Path $venvPython) { $venvPython } else { 'py' }
$pythonArgs = if (Test-Path $venvPython) { @() } else { @('-3.12') }

$junit = Join-Path ([IO.Path]::GetTempPath()) ('mini-pytest-' + [guid]::NewGuid().ToString('N') + '.xml')

Write-Host '== Server tests (pytest) =='
& $python @pythonArgs -m pytest $serverTests -q "--junitxml=$junit"
$pytestExit = $LASTEXITCODE

if (Test-Path $junit) {
    try {
        [xml]$report = Get-Content -Path $junit -Raw
        $suites = @()
        if ($report.testsuites -and $report.testsuites.testsuite) {
            $suites = @($report.testsuites.testsuite)
        } elseif ($report.testsuite) {
            $suites = @($report.testsuite)
        }
        $tests = 0; $failures = 0; $errors = 0; $skipped = 0
        foreach ($suite in $suites) {
            $tests += [int]$suite.tests
            $failures += [int]$suite.failures
            $errors += [int]$suite.errors
            $skipped += [int]$suite.skipped
        }
        $passed = $tests - $failures - $errors - $skipped
        Write-Host "Server tests: $tests total, $passed passed, $failures failed, $errors errors, $skipped skipped."
    } catch {
        Write-Warning "Could not read the pytest report: $($_.Exception.Message)"
    } finally {
        Remove-Item $junit -Force -ErrorAction SilentlyContinue
    }
}

if ($pytestExit -eq 0) {
    exit 0
}

Write-Warning "pytest returned $pytestExit."
exit $pytestExit
