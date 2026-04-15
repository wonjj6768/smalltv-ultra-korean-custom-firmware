param(
    [string]$Environment = "esp12e",
    [string]$OutputDir = "backup/ota"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$buildDir = Join-Path $repoRoot ".pio/build/$Environment"
$firmwareSource = Join-Path $buildDir "firmware.bin"
$filesystemSource = Join-Path $buildDir "littlefs.bin"

if (-not (Test-Path $firmwareSource)) {
    throw "Missing firmware artifact: $firmwareSource"
}

if (-not (Test-Path $filesystemSource)) {
    throw "Missing filesystem artifact: $filesystemSource"
}

$gitVersion = (git describe --tags --always HEAD).Trim()
$shortSha = (git rev-parse --short HEAD).Trim()
$safeVersion = ($gitVersion -replace '[^0-9A-Za-z._-]', '_')

$destinationDir = Join-Path $repoRoot $OutputDir
New-Item -ItemType Directory -Path $destinationDir -Force | Out-Null

$firmwareName = "geekmagic-open-firmware_${safeVersion}_${shortSha}_firmware.bin"
$filesystemName = "geekmagic-open-firmware_${safeVersion}_${shortSha}_littlefs.bin"

$firmwareDestination = Join-Path $destinationDir $firmwareName
$filesystemDestination = Join-Path $destinationDir $filesystemName
$firmwareLatest = Join-Path $destinationDir "firmware-latest.bin"
$filesystemLatest = Join-Path $destinationDir "littlefs-latest.bin"
$checksumsPath = Join-Path $destinationDir "SHA256SUMS.txt"

Copy-Item -LiteralPath $firmwareSource -Destination $firmwareDestination -Force
Copy-Item -LiteralPath $filesystemSource -Destination $filesystemDestination -Force
Copy-Item -LiteralPath $firmwareSource -Destination $firmwareLatest -Force
Copy-Item -LiteralPath $filesystemSource -Destination $filesystemLatest -Force

$checksumEntries = @(
    Get-FileHash -Algorithm SHA256 $firmwareDestination
    Get-FileHash -Algorithm SHA256 $filesystemDestination
    Get-FileHash -Algorithm SHA256 $firmwareLatest
    Get-FileHash -Algorithm SHA256 $filesystemLatest
) | ForEach-Object {
    "$($_.Hash.ToLowerInvariant()) *$([System.IO.Path]::GetFileName($_.Path))"
}

Set-Content -LiteralPath $checksumsPath -Value ($checksumEntries -join [Environment]::NewLine)

Write-Output "Exported OTA artifacts:"
Write-Output $firmwareDestination
Write-Output $filesystemDestination
Write-Output $firmwareLatest
Write-Output $filesystemLatest
Write-Output $checksumsPath
