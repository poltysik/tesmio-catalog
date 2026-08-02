param(
    [string]$Game = "C:\Program Files (x86)\Steam\steamapps\common\SovietRepublic",
    [switch]$ForceConfig
)

$ErrorActionPreference = "Stop"
$loaderRoot = Join-Path $Game "tesmioloader"
$loader = Join-Path $loaderRoot "build"
$pluginDir = Join-Path $loader "plugins"
$vfsDir = Join-Path $loaderRoot "vfs\media_soviet\editor"
$built = Join-Path $PSScriptRoot "build"

if (-not (Test-Path -LiteralPath (Join-Path $loader "tesmioloader.dll"))) {
    throw "TesmioLoader build folder not found: $loader"
}
if (-not (Test-Path -LiteralPath (Join-Path $built "tesmiomenu.dll"))) {
    throw "Build tesmiomenu.dll first."
}

New-Item -ItemType Directory -Force -Path $pluginDir, $vfsDir | Out-Null
Copy-Item -LiteralPath (Join-Path $built "tesmiomenu.dll") -Destination $pluginDir -Force
if ($ForceConfig -or -not (Test-Path -LiteralPath (Join-Path $pluginDir "tesmiomenu.ini"))) {
    Copy-Item -LiteralPath (Join-Path $built "tesmiomenu.ini") -Destination $pluginDir -Force
} else {
    Write-Output "Kept existing plugins\tesmiomenu.ini (use -ForceConfig to replace it)."
}
Copy-Item -LiteralPath (Join-Path $built "vfs\media_soviet\editor\bottomtab_tesmioloader.png") -Destination $vfsDir -Force

Write-Output "Installed TesmioMenu into $loader"
