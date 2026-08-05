param(
    [string]$Version = "1.0.0"
)

$ErrorActionPreference = "Stop"
$built = Join-Path $PSScriptRoot "build"
$releaseRoot = Join-Path $PSScriptRoot "release\TesmioCatalog-$Version"

if (-not (Test-Path -LiteralPath (Join-Path $built "TesmioCatalog.dll"))) {
    throw "Build TesmioCatalog.dll first with .\build-portable.ps1"
}

$pluginDir = Join-Path $releaseRoot "plugins"
$vfsDir = Join-Path $releaseRoot "vfs\media_soviet\editor"
New-Item -ItemType Directory -Force -Path $pluginDir, $vfsDir | Out-Null

# Do not let packages created before the DLL rename retain the legacy plugin.
$legacyPlugin = Join-Path $pluginDir "tesmiomenu.dll"
if (Test-Path -LiteralPath $legacyPlugin) {
    Remove-Item -LiteralPath $legacyPlugin -Force
}

Copy-Item -LiteralPath (Join-Path $built "TesmioCatalog.dll") -Destination $pluginDir -Force
Copy-Item -LiteralPath (Join-Path $built "tesmiomenu.ini") -Destination $pluginDir -Force
Copy-Item -LiteralPath (Join-Path $built "vfs\media_soviet\editor\bottomtab_tesmioloader.png") -Destination $vfsDir -Force
$builtLocks = Join-Path $built "vfs\media_soviet\editor\tesmio_catalog_locks"
$releaseLocks = Join-Path $vfsDir "tesmio_catalog_locks"
New-Item -ItemType Directory -Force -Path $releaseLocks | Out-Null
Copy-Item -Path (Join-Path $builtLocks "locked_*.png") -Destination $releaseLocks -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "LICENSE") -Destination $releaseRoot -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "GUIDE-RU.md") -Destination $releaseRoot -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "README.md") -Destination $releaseRoot -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "INSTALL-TESMIO-CATALOG.bat") -Destination $releaseRoot -Force

$zip = Join-Path $PSScriptRoot "release\TesmioCatalog-$Version.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
$packageFiles = @(
    $pluginDir,
    (Join-Path $releaseRoot "vfs"),
    (Join-Path $releaseRoot "LICENSE"),
    (Join-Path $releaseRoot "GUIDE-RU.md"),
    (Join-Path $releaseRoot "README.md"),
    (Join-Path $releaseRoot "INSTALL-TESMIO-CATALOG.bat")
)
Compress-Archive -LiteralPath $packageFiles -DestinationPath $zip -CompressionLevel Optimal

Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $pluginDir "TesmioCatalog.dll"), $zip |
    Select-Object Path, Hash
