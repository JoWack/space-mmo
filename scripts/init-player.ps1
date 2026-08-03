# Creates a local play account and character, and writes the login the client reads.
#
#   powershell -ExecutionPolicy Bypass -File scripts\init-player.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\init-player.ps1 -Email you@example.com
#
# Needs the API running (scripts\api.bat).
#
# Credentials go to secrets\player-login.txt rather than onto the client's command line, because
# command lines here mangle values: -BackendEmail=someone@gmail.com has arrived as
# "someone@gmail .com", and FParse stops at the space, so the client tries to log in as
# "someone@gmail" and gets a 401 that looks exactly like a wrong password. A file has no shell
# between it and the value.
#
# secrets\ is git-ignored. This is a local dev account, not a real one.

param(
    [string] $Email = "player@local.test",
    [string] $CharacterName = "Prospector",
    [int]    $Race = 0,
    [string] $BaseUrl = "http://localhost:5080"
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$secretsDir = Join-Path $repo 'secrets'
$loginFile = Join-Path $secretsDir 'player-login.txt'

# Generated, and comfortably past the API's 12-character minimum. Not memorable on purpose: this
# is read from a file, never typed.
$bytes = New-Object byte[] 18
$rng = New-Object System.Security.Cryptography.RNGCryptoServiceProvider
try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
$password = [Convert]::ToBase64String($bytes) -replace '[^A-Za-z0-9]', 'x'

$body = @{ email = $Email; password = $password } | ConvertTo-Json

try {
    $session = Invoke-RestMethod "$BaseUrl/accounts/register" -Method Post `
        -ContentType 'application/json' -Body $body
}
catch {
    $code = $_.Exception.Response.StatusCode.value__
    if ($code -eq 409) {
        Write-Output "An account already exists for $Email."
        Write-Output "This script cannot recover its password. Either use the existing"
        Write-Output "secrets\player-login.txt, or re-run with -Email something-else."
        exit 1
    }
    throw
}

Write-Output "Registered account $($session.accountId) for $Email"

$headers = @{ Authorization = "Bearer $($session.token)" }
$characterBody = @{ name = "$CharacterName$(Get-Random -Maximum 9999)"; race = $Race } | ConvertTo-Json

$character = Invoke-RestMethod "$BaseUrl/characters" -Method Post `
    -ContentType 'application/json' -Headers $headers -Body $characterBody

Write-Output "Created character $($character.id) '$($character.name)'"

if (-not (Test-Path $secretsDir)) {
    New-Item -ItemType Directory -Path $secretsDir | Out-Null
}

# Two lines, email then password. No BOM: the client reads these raw, and a byte-order mark would
# end up inside the address.
$contents = "$Email`n$password`n"
[System.IO.File]::WriteAllText($loginFile, $contents, (New-Object System.Text.UTF8Encoding $false))

Write-Output "Wrote $loginFile"
Write-Output ''
Write-Output "Play as this character with:  -CharacterId=$($character.id)"
Write-Output "Or omit -CharacterId= entirely and the client uses the first character on the account."
