param(
    [string]$MainEnvironment = "esp12e",
    [string]$BridgeEnvironment = "esp12e-bridge",
    [string]$OutputDir = "release"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$mainBuildDir = Join-Path $repoRoot ".pio/build/$MainEnvironment"
$bridgeBuildDir = Join-Path $repoRoot "bridge-firmware/.pio/build/$BridgeEnvironment"

$firmwareSource = Join-Path $mainBuildDir "firmware.bin"
$filesystemSource = Join-Path $mainBuildDir "littlefs.bin"
$bridgeSource = Join-Path $bridgeBuildDir "firmware.bin"

$requiredFiles = @($firmwareSource, $filesystemSource, $bridgeSource)
foreach ($file in $requiredFiles) {
    if (-not (Test-Path $file)) {
        throw "Missing release artifact: $file"
    }
}

$destinationDir = Join-Path $repoRoot $OutputDir
New-Item -ItemType Directory -Path $destinationDir -Force | Out-Null

$firmwareDestination = Join-Path $destinationDir "firmware.bin"
$filesystemDestination = Join-Path $destinationDir "littlefs.bin"
$bridgeDestination = Join-Path $destinationDir "bridge-firmware.bin"
$checksumsPath = Join-Path $destinationDir "SHA256SUMS.txt"

Copy-Item -LiteralPath $firmwareSource -Destination $firmwareDestination -Force
Copy-Item -LiteralPath $filesystemSource -Destination $filesystemDestination -Force
Copy-Item -LiteralPath $bridgeSource -Destination $bridgeDestination -Force

$checksumEntries = @(
    Get-FileHash -Algorithm SHA256 $firmwareDestination
    Get-FileHash -Algorithm SHA256 $filesystemDestination
    Get-FileHash -Algorithm SHA256 $bridgeDestination
) | ForEach-Object {
    "$($_.Hash.ToLowerInvariant()) *$([System.IO.Path]::GetFileName($_.Path))"
}

Set-Content -LiteralPath $checksumsPath -Value ($checksumEntries -join [Environment]::NewLine)

Write-Output "Exported release artifacts:"
Write-Output $firmwareDestination
Write-Output $filesystemDestination
Write-Output $bridgeDestination
Write-Output $checksumsPath
