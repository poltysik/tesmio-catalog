# Tesmio Catalog 1.0

In-game construction catalog for **Workers & Resources: Soviet Republic**,
running as a TesmioLoader plugin.

The catalog adds an independent button to the construction toolbar and indexes
the buildings and networks that are actually loaded by the game. It does not
bundle third-party buildings.

## Features

- Type, resource and source filters.
- Separate Vanilla, Steam Workshop and Tesmio sources.
- Favorites and “Only available” mode.
- Native game availability rules: research, scenario settings and climate.
- Automatic English/Russian interface selection from the current game language.
- Vanilla names and categories are read from the game localization tables.
- Mod resources and compatible Tesmio buildings are discovered at runtime.
- Two cards per row with pagination and safe text truncation.

## Compatibility

- Workers & Resources: Soviet Republic `1.1.1.7` (64-bit DX11 build).
- TesmioLoader API 3 / launcher `b0.3.3`.

The plugin checks known function bytes before installing its hooks and never
changes `SOVIET64.exe` on disk. A game update can still require a new catalog
build. Launch the game through `tesmiolauncher.exe`.

## Installation

1. Install TesmioLoader for Workers & Resources: Soviet Republic.
2. Extract `TesmioCatalog-1.0.0.zip` into the loader's `build` directory so
   that the `plugins` and `vfs` folders merge with the existing folders.
3. Start the game through `tesmiolauncher.exe` and enable `tesmiomenu.dll`.

The catalog itself does not add products or buildings. Keep the original mods
installed when loading saves that use their content.

## Building from source

Place an `llvm-mingw` toolchain in `tools/llvm-mingw`, then run:

```powershell
.\build-portable.ps1
.\package-release.ps1 -Version 1.0.0
```

For local installation into the default Steam directory:

```powershell
.\install-local.ps1
```

Build output, downloaded compilers, diagnostic captures and reverse-engineering
notes are intentionally excluded from the repository.

## Русский

Каталог автоматически переключается на русский язык вместе с игрой. Он
сортирует загруженные постройки по типу, ресурсам и источнику, показывает
ограничения исследований и климата, а также поддерживает избранное. Модовые
названия не переводятся искусственно — используются названия авторов модов.

## License

GPL-3.0-or-later. TesmioLoader ABI definitions are derived from the GPL-3.0
TesmioLoader project by MaxLegend.
