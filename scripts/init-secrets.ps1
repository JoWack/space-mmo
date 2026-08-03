# One-time local setup: generates the game server's service credential.
#
# Run once per machine:
#   powershell -ExecutionPolicy Bypass -File scripts\init-secrets.ps1
#
# Two consumers need the same value and cannot share a mechanism:
#
#   - The API reads it from .NET user secrets, which live in the user profile,
#     outside the repo, and cannot be committed by accident.
#   - The Unreal dedicated server cannot read .NET user secrets, so the same value
#     is written to secrets\service-secret.txt, which .gitignore excludes.
#
# Re-running rotates the secret. That is safe -- nothing persists it -- but both
# sides must be restarted afterwards, or the server's calls start coming back 401.

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$secretsDir = Join-Path $repo 'secrets'
$secretFile = Join-Path $secretsDir 'service-secret.txt'
$apiProject = Join-Path $repo 'services\SpaceMMO.Api'

# 32 random bytes, base64. Long enough that guessing is not a strategy, and
# generated rather than typed so it is never a memorable phrase someone reuses.
#
# RNGCryptoServiceProvider rather than RandomNumberGenerator.Fill: Windows
# PowerShell 5.1 runs on .NET Framework, where Fill does not exist. Get-Random is
# not an option here -- it is not cryptographically secure.
$bytes = New-Object byte[] 32
$rng = New-Object System.Security.Cryptography.RNGCryptoServiceProvider
try {
    $rng.GetBytes($bytes)
}
finally {
    $rng.Dispose()
}
$secret = [Convert]::ToBase64String($bytes)

if (-not (Test-Path $secretsDir)) {
    New-Item -ItemType Directory -Path $secretsDir | Out-Null
}

# No BOM. The Unreal side reads this file raw, and a byte-order mark would end up
# inside the secret -- producing a mismatch that looks like the wrong value rather
# than an encoding problem.
[System.IO.File]::WriteAllText($secretFile, $secret, (New-Object System.Text.UTF8Encoding $false))

Write-Output "Wrote $secretFile"

Push-Location $apiProject
try {
    dotnet user-secrets set 'SpaceMMO:ServiceSecret' $secret | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "dotnet user-secrets failed with exit code $LASTEXITCODE" }
}
finally {
    Pop-Location
}

Write-Output "Set SpaceMMO:ServiceSecret in user secrets for SpaceMMO.Api"
Write-Output ''
Write-Output 'Done. Restart the API and the dedicated server to pick it up.'
