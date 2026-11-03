[CmdletBinding()]
param(
    [int]$Port = 8765,
    [string]$Config = '',
    [switch]$NoTls,
    [switch]$NoInstall
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$serverDir = Join-Path $root 'server'
$venvPython = Join-Path $serverDir '.venv\Scripts\python.exe'

if (-not (Test-Path $venvPython)) {
    Write-Host '== Creating server/.venv =='
    py -3.12 -m venv (Join-Path $serverDir '.venv')
    if ($LASTEXITCODE -ne 0) { throw 'Could not create server/.venv with py -3.12.' }
    & $venvPython -m pip install --upgrade pip
    & $venvPython -m pip install -r (Join-Path $serverDir 'requirements.txt')
    if ($LASTEXITCODE -ne 0) { throw 'pip install failed.' }
} elseif (-not $NoInstall) {
    & $venvPython -m pip install -q -r (Join-Path $serverDir 'requirements.txt')
    if ($LASTEXITCODE -ne 0) { throw 'pip install failed.' }
}

if (-not $Config) {
    $Config = Join-Path $serverDir 'configs\server.toml'
}
$example = "$Config.example"
if (-not (Test-Path $Config) -and (Test-Path $example)) {
    Copy-Item $example $Config
    Write-Host "Created $Config from the example."
}

# Publish the certificate pin for the client before serving.
$fingerprint = (& $venvPython (Join-Path $serverDir 'run.py') --config $Config --print-fingerprint 2>&1 | Out-String)
$match = [regex]::Match($fingerprint, 'CURRENT FINGERPRINT:\s*([0-9A-Fa-f:]{95})')
if ($match.Success) {
    $clientConfig = Join-Path $root 'client\config'
    New-Item -ItemType Directory -Force -Path $clientConfig | Out-Null
    Set-Content -Path (Join-Path $clientConfig 'server_fingerprint.txt') `
        -Value $match.Groups[1].Value -Encoding ascii
    Write-Host "CURRENT FINGERPRINT: $($match.Groups[1].Value)"
}

Write-Host "== MINI Messenger server on port $Port =="
$arguments = @((Join-Path $serverDir 'run.py'), '--config', $Config, '--port', $Port)
if ($NoTls) { $arguments += '--no-tls' }
& $venvPython @arguments
exit $LASTEXITCODE
