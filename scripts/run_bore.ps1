[CmdletBinding()]
param(
    [int]$LocalPort = 8765,
    [string]$BoreServer = 'bore.pub',
    [string]$BorePath = 'bore.exe',
    [string]$ReadmePath = '',
    [string]$CertPath = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $ReadmePath) {
    $ReadmePath = Join-Path $root 'README.md'
}
if (-not $CertPath) {
    $CertPath = Join-Path $root 'server\certs\server.pem'
}
$connectFile = Join-Path $root 'client\config\connect.ip'
$fingerprintFile = Join-Path $root 'client\config\server_fingerprint.txt'

function Get-CertificateFingerprint([string]$path) {
    if (-not (Test-Path $path)) {
        return $null
    }
    try {
        $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($path)
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try {
            $bytes = $sha.ComputeHash($cert.RawData)
        } finally {
            $sha.Dispose()
        }
        return (($bytes | ForEach-Object { $_.ToString('X2') }) -join ':')
    } catch {
        Write-Warning "Cannot read the TLS certificate: $($_.Exception.Message)"
        return $null
    }
}

$bore = $BorePath
if (-not (Get-Command $bore -ErrorAction SilentlyContinue) -and -not (Test-Path $bore)) {
    throw "bore not found. Download bore from https://github.com/ekzhang/bore/releases and put bore.exe in PATH or pass -BorePath."
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $connectFile) | Out-Null

function Publish-Tunnel([string]$publicAddress) {
    Set-Content -Path $connectFile -Value $publicAddress -Encoding ascii
    Write-Host "connect.ip -> $publicAddress"

    $fingerprint = Get-CertificateFingerprint $CertPath
    if ($fingerprint) {
        Set-Content -Path $fingerprintFile -Value $fingerprint -Encoding ascii
        Write-Host "server_fingerprint.txt -> $fingerprint"
    }

    if (Test-Path $ReadmePath) {
        $readme = Get-Content $ReadmePath -Raw
        $line = "CURRENT TUNNEL: $publicAddress"
        if ($readme -match '(?m)^CURRENT TUNNEL:.*$') {
            $readme = [regex]::Replace($readme, '(?m)^CURRENT TUNNEL:.*$', $line)
        } else {
            $readme = $line + "`r`n`r`n" + $readme
        }
        if ($fingerprint) {
            $pinLine = "CURRENT FINGERPRINT: $fingerprint"
            if ($readme -match '(?m)^CURRENT FINGERPRINT:.*$') {
                $readme = [regex]::Replace($readme, '(?m)^CURRENT FINGERPRINT:.*$', $pinLine)
            } else {
                $replacement = '$1' + "`r`n" + $pinLine
                $readme = [regex]::Replace($readme, '(?m)^(CURRENT TUNNEL:.*)$', $replacement)
            }
        }
        Set-Content -Path $ReadmePath -Value $readme -Encoding utf8 -NoNewline
        Write-Host "README -> $line"
    }
}

Write-Host "== bore local $LocalPort --to $BoreServer =="
& $bore local $LocalPort --to $BoreServer 2>&1 | ForEach-Object {
    $line = [string]$_
    Write-Host $line
    $match = [regex]::Match($line, [regex]::Escape($BoreServer) + ':(\d{2,5})')
    if ($match.Success) {
        Publish-Tunnel ("${BoreServer}:" + $match.Groups[1].Value)
    }
}
exit $LASTEXITCODE
