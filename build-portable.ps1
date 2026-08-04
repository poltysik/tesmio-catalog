param(
    [string]$Toolchain = "$PSScriptRoot\tools\llvm-mingw",
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$compiler = Join-Path $Toolchain "bin\x86_64-w64-mingw32-clang++.exe"
if (-not (Test-Path -LiteralPath $compiler)) {
    throw "Portable compiler not found: $compiler"
}

$out = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$legacyOutput = Join-Path $out "tesmiomenu.dll"
if (Test-Path -LiteralPath $legacyOutput) {
    Remove-Item -LiteralPath $legacyOutput -Force
}

$optimization = if ($Configuration -eq "Debug") { "-O0" } else { "-O2" }
& $compiler `
    -std=c++17 $optimization -g0 -DNDEBUG `
    -fno-exceptions -fno-rtti `
    -Wall -Wextra -Wpedantic `
    -shared -static `
    -I (Join-Path $PSScriptRoot "include") `
    (Join-Path $PSScriptRoot "src\tesmiomenu.cpp") `
    -o (Join-Path $out "TesmioCatalog.dll") `
    -lkernel32 -luser32 -lgdi32

if ($LASTEXITCODE -ne 0) { throw "Compiler exited with code $LASTEXITCODE" }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "tesmiomenu.ini") -Destination $out -Force

$vfs = Join-Path $out "vfs\media_soviet\editor"
New-Item -ItemType Directory -Force -Path $vfs | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "assets\bottomtab_tesmioloader.png") -Destination $vfs -Force
$lockSource = Join-Path $PSScriptRoot "assets\lock-icons"
$lockDestination = Join-Path $vfs "tesmio_catalog_locks"
New-Item -ItemType Directory -Force -Path $lockDestination | Out-Null
Copy-Item -Path (Join-Path $lockSource "locked_*.png") -Destination $lockDestination -Force

Get-ChildItem -LiteralPath $out -Recurse | Select-Object FullName, Length
