param(
    [string]$Version = "1.0.0"
)

$ErrorActionPreference = "Stop"
$built = Join-Path $PSScriptRoot "build"
$releaseRoot = Join-Path $PSScriptRoot "release\TesmioCatalog-$Version"

if (-not (Test-Path -LiteralPath (Join-Path $built "tesmiomenu.dll"))) {
    throw "Build tesmiomenu.dll first with .\build-portable.ps1"
}

$pluginDir = Join-Path $releaseRoot "plugins"
$vfsDir = Join-Path $releaseRoot "vfs\media_soviet\editor"
New-Item -ItemType Directory -Force -Path $pluginDir, $vfsDir | Out-Null

Copy-Item -LiteralPath (Join-Path $built "tesmiomenu.dll") -Destination $pluginDir -Force
Copy-Item -LiteralPath (Join-Path $built "tesmiomenu.ini") -Destination $pluginDir -Force
Copy-Item -LiteralPath (Join-Path $built "vfs\media_soviet\editor\bottomtab_tesmioloader.png") -Destination $vfsDir -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "LICENSE") -Destination $releaseRoot -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "GUIDE-RU.md") -Destination $releaseRoot -Force

$zip = Join-Path $PSScriptRoot "release\TesmioCatalog-$Version.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -LiteralPath $pluginDir, (Join-Path $releaseRoot "vfs"), (Join-Path $releaseRoot "LICENSE"), (Join-Path $releaseRoot "GUIDE-RU.md") -DestinationPath $zip -CompressionLevel Optimal

Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $pluginDir "tesmiomenu.dll"), $zip |
    Select-Object Path, Hash
