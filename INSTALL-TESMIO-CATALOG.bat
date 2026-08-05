@echo off
setlocal EnableExtensions

set "ITEM_DIR=%~dp0"
set "GAME_DIR="

if not "%~1"=="" (
    set "GAME_DIR=%~1"
)

if not defined GAME_DIR (
    for %%I in ("%ITEM_DIR%..\..\..\..\common\SovietRepublic") do set "CANDIDATE=%%~fI"
    if exist "%CANDIDATE%\tesmioloader\build\plugins" set "GAME_DIR=%CANDIDATE%"
)

if not defined GAME_DIR (
    if exist "%ProgramFiles(x86)%\Steam\steamapps\common\SovietRepublic\tesmioloader\build\plugins" (
        set "GAME_DIR=%ProgramFiles(x86)%\Steam\steamapps\common\SovietRepublic"
    )
)

if not defined GAME_DIR (
    echo.
    echo ERROR: SovietRepublic with TesmioLoader was not found.
    echo Run this file with the game folder as its argument:
    echo INSTALL-TESMIO-CATALOG.bat "D:\SteamLibrary\steamapps\common\SovietRepublic"
    echo.
    pause
    exit /b 1
)

if not exist "%ITEM_DIR%plugins\TesmioCatalog.dll" (
    echo ERROR: plugins\TesmioCatalog.dll is missing from the Workshop item.
    pause
    exit /b 2
)

if not exist "%ITEM_DIR%vfs\media_soviet\editor\bottomtab_tesmioloader.png" (
    echo ERROR: the catalog icon is missing from the Workshop item.
    pause
    exit /b 3
)

echo Installing Tesmio Catalog into:
echo %GAME_DIR%
echo.

robocopy "%ITEM_DIR%plugins" "%GAME_DIR%\tesmioloader\build\plugins" /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
if errorlevel 8 (
    echo ERROR: failed to copy the plugin files.
    pause
    exit /b 4
)

robocopy "%ITEM_DIR%vfs" "%GAME_DIR%\tesmioloader\vfs" /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
if errorlevel 8 (
    echo ERROR: failed to copy the VFS files.
    pause
    exit /b 5
)

if not exist "%GAME_DIR%\tesmioloader\build\plugins\TesmioCatalog.dll" (
    echo ERROR: TesmioCatalog.dll was not installed.
    pause
    exit /b 6
)

if not exist "%GAME_DIR%\tesmioloader\vfs\media_soviet\editor\bottomtab_tesmioloader.png" (
    echo ERROR: the catalog icon was not installed.
    pause
    exit /b 7
)

if not exist "%GAME_DIR%\tesmioloader\vfs\vfs" goto install_ok
echo.
echo WARNING: an incorrect nested vfs folder still exists.
echo Remove tesmioloader\vfs\vfs after checking its contents.

:install_ok

echo.
echo SUCCESS: Tesmio Catalog and its icon were installed correctly.
echo Start the game through tesmiolauncher.exe.
echo.
pause
exit /b 0
