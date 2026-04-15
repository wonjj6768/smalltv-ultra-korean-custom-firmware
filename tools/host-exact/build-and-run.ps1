param(
    [string]$Preset = "clear",
    [string]$Output = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$clang = "C:\Program Files\LLVM\bin\clang++.exe"
$exe = Join-Path $PSScriptRoot "run_display_manager_host.exe"
$gfxRoot = Join-Path $repoRoot ".pio\libdeps\esp12e\GFX Library for Arduino\src"

if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $repoRoot "output\display-manager-host-exact-$Preset.bmp"
}

& $clang `
  -D_CRT_SECURE_NO_WARNINGS `
  -std=c++20 `
  -include (Join-Path $PSScriptRoot "pgmspace.h") `
  -I $PSScriptRoot `
  -I (Join-Path $PSScriptRoot "config") `
  -I (Join-Path $PSScriptRoot "display") `
  -I (Join-Path $PSScriptRoot "weather") `
  -I $gfxRoot `
  -I (Join-Path $gfxRoot "canvas") `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $PSScriptRoot "run_display_manager_host.cpp") `
  (Join-Path $gfxRoot "Arduino_G.cpp") `
  (Join-Path $gfxRoot "Arduino_GFX.cpp") `
  (Join-Path $gfxRoot "canvas\Arduino_Canvas_Indexed.cpp") `
  (Join-Path $repoRoot "src\display\DisplayManager.cpp") `
  (Join-Path $repoRoot "src\display\ClockDashboardScene.cpp") `
  (Join-Path $repoRoot "src\display\ClockDigitFont.cpp") `
  (Join-Path $repoRoot "src\display\UiTextFont.cpp") `
  -o $exe

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $exe --preset $Preset --output $Output
