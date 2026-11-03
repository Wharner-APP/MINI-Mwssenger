@echo off
rem Layout of hCaptcha keys:
rem   client/config/captcha.cfg   - site key only (safe to ship to clients)
rem   server/configs/captcha.toml - site key + secret, ACL restricted to you
rem   MINI_CAPTCHA_SECRET         - user environment variable read by the server
rem The secret is never echoed to the console and is not accepted on the
rem command line (so it does not land in shell history or process listings).
setlocal
set "PS1=%TEMP%\mini_setup_captcha_%RANDOM%%RANDOM%.ps1"

> "%PS1%" echo $ErrorActionPreference = 'Stop'
>> "%PS1%" echo $root = Split-Path -Parent (Split-Path -Parent '%~f0')
>> "%PS1%" echo $site = (Read-Host 'hCaptcha site key').Trim()
>> "%PS1%" echo $secure = Read-Host -AsSecureString 'hCaptcha secret'
>> "%PS1%" echo $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
>> "%PS1%" echo $secret = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
>> "%PS1%" echo [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
>> "%PS1%" echo if ([string]::IsNullOrWhiteSpace($site) -or [string]::IsNullOrWhiteSpace($secret)) { throw 'Both values are required.' }
>> "%PS1%" echo $clientDir = Join-Path $root 'client\config'
>> "%PS1%" echo $serverDir = Join-Path $root 'server\configs'
>> "%PS1%" echo $null = New-Item -ItemType Directory -Force -Path $clientDir
>> "%PS1%" echo $null = New-Item -ItemType Directory -Force -Path $serverDir
>> "%PS1%" echo $client = "[hcaptcha]`nsite_key = " + $site + "`n"
>> "%PS1%" echo [IO.File]::WriteAllText((Join-Path $clientDir 'captcha.cfg'), $client, (New-Object Text.UTF8Encoding($false)))
>> "%PS1%" echo $escapedSite = $site.Replace('\','\\').Replace('"','\"')
>> "%PS1%" echo $escapedSecret = $secret.Replace('\','\\').Replace('"','\"')
>> "%PS1%" echo $toml = "[captcha]`nprovider = `"hcaptcha`"`nsite_key = `"$escapedSite`"`nsecret = `"$escapedSecret`"`nsecret_env = `"MINI_CAPTCHA_SECRET`"`n"
>> "%PS1%" echo [IO.File]::WriteAllText((Join-Path $serverDir 'captcha.toml'), $toml, (New-Object Text.UTF8Encoding($false)))
>> "%PS1%" echo [Environment]::SetEnvironmentVariable('MINI_CAPTCHA_SECRET', $secret, 'User')
>> "%PS1%" echo $acl = $env:USERNAME + ':(F)'
>> "%PS1%" echo $null = icacls (Join-Path $clientDir 'captcha.cfg') /inheritance:r /grant:r $acl
>> "%PS1%" echo $null = icacls (Join-Path $serverDir 'captcha.toml') /inheritance:r /grant:r $acl
>> "%PS1%" echo Write-Host 'Config written.'
>> "%PS1%" echo Write-Host '  client/config/captcha.cfg   - site key only'
>> "%PS1%" echo Write-Host '  server/configs/captcha.toml - site key and secret (secret not printed)'
>> "%PS1%" echo Write-Host 'MINI_CAPTCHA_SECRET was set for the current user; restart the server.'

powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%"
set "CODE=%ERRORLEVEL%"
del "%PS1%" >nul 2>&1
if not "%CODE%"=="0" (
    echo Failed with exit code %CODE%.
    exit /b %CODE%
)
exit /b 0
