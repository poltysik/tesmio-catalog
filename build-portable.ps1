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

$optimization = if ($Configuration -eq "Debug") { "-O0" } else { "-O2" }
& $compiler `
    -std=c++17 $optimization -g0 -DNDEBUG `
    -fno-exceptions -fno-rtti `
    -Wall -Wextra -Wpedantic `
    -shared -static `
    -I (Join-Path $PSScriptRoot "include") `
    (Join-Path $PSScriptRoot "src\tesmiomenu.cpp") `
    -o (Join-Path $out "tesmiomenu.dll") `
    -lkernel32 -luser32

if ($LASTEXITCODE -ne 0) { throw "Compiler exited with code $LASTEXITCODE" }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "tesmiomenu.ini") -Destination $out -Force

$vfs = Join-Path $out "vfs\media_soviet\editor"
New-Item -ItemType Directory -Force -Path $vfs | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "assets\bottomtab_tesmioloader.png") -Destination $vfs -Force

Get-ChildItem -LiteralPath $out -Recurse | Select-Object FullName, Length
