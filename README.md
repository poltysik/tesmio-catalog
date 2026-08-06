# Tesmio Catalog 1.1

Tesmio Catalog is an in-game construction browser for **Workers & Resources:
Soviet Republic**, implemented as a
[TesmioLoader](https://steamcommunity.com/sharedfiles/filedetails/?id=3773169177)
plugin.

It adds an independent button to the bottom construction toolbar and indexes
the buildings and networks actually loaded by the game. Its primary purpose is
to make resources and buildable objects supplied through TesmioLoader easier
to find and use, while keeping them distinct from ordinary Steam Workshop
content. The catalog itself bundles no third-party buildings or resources.

## Features

- Filters by object type, resource and source: Vanilla, Workshop or Tesmio.
- `Produces` and `Consumes` relations for the selected resource.
- Runtime discovery of TesmioLoader resources and compatible buildings.
- Secondary discovery of physical Workshop buildings omitted from the stock
  construction-menu groups.
- Safe `Undefined` category and conservative industry inference for custom
  resource production chains.
- Favorites, pagination and an `Only Available` mode.
- Preview cards with type, source, production and consumption metadata.
- Opaque hover tooltips for resource lists that do not fit on a card.
- Native availability information, including the reason an object is locked.
- Automatic English/Russian interface selection from the active game language.
- Vanilla names and categories are read from the game's localization tables.

## Requirements and limitations

- Workers & Resources: Soviet Republic `1.1.1.7`, 64-bit DX11 build.
- TesmioLoader API 3 / launcher `b0.3.4`.
- The game must be launched through `tesmiolauncher.exe`.

The catalog only indexes content already loaded by the game. It does not add
resources or buildings, replace their original packages, bypass availability
rules, or machine-translate unknown third-party names. Keep the original mods
installed when loading saves that use their content.

The plugin validates known function bytes before installing its hooks and does
not modify `SOVIET64.exe` on disk. A game or loader update may require a new
catalog build.

## Installation

1. Install TesmioLoader according to its own documentation.
2. Subscribe to Tesmio Catalog or download a release from this repository.
3. For the Workshop copy, open:

   ```text
   Steam\steamapps\workshop\content\784150\3778262655
   ```

4. Recommended: run `INSTALL-TESMIO-CATALOG.bat` from the item folder. The
   installer copies both the plugin and VFS assets and verifies that the button
   icon reached the correct location.
5. For a manual installation:

   - copy the whole `plugins` folder into `SovietRepublic\tesmioloader\build`;
   - copy the whole `vfs` folder into `SovietRepublic\tesmioloader`;
   - approve folder merging and file replacement.

   Do **not** copy `vfs` inside the existing `tesmioloader\vfs` folder. That
   creates the invalid path `tesmioloader\vfs\vfs` and the catalog button
   appears without its picture. The final icon path must be:

   ```text
   SovietRepublic\tesmioloader\vfs\media_soviet\editor\bottomtab_tesmioloader.png
   ```

6. Run `tesmiolauncher.exe`, enable `TesmioCatalog.dll`, and press **Launch**.
7. Fully restart the game through TesmioLauncher after installing or updating.

The catalog tab is not stored in a save. Resources and buildings provided by
other mods can be stored in a save, so those source mods must remain installed.

## Integration guide for Tesmio mod authors

### Adding a resource

Declare the resource in the TesmioLoader `plugins\resources.ini` distributed or
merged by your package, under the section named `list`:

```ini
[list]
my_resource = template_resource, English Display Name
```

- `my_resource` is the unique internal resource name.
- `template_resource` is an existing resource with a compatible transport and
  storage class.
- The final value is the display caption supplied to TesmioLoader.

Use the same internal name in the building's `building.ini`, including
`$PRODUCTION`, `$CONSUMPTION`, `$CONSUMPTION_PER_SECOND`, and applicable
`$STORAGE` directives. Tesmio Catalog reads the loader's resource list on
startup and creates the filter automatically; no catalog source change is
required for each new resource.

### Adding a building

The building must be a valid WRSR construction object that is actually loaded
by the game. Its object folder should contain at least:

- `building.ini` with valid construction and resource directives;
- `imagegui.png` for the catalog preview (96×96 is recommended);
- the normal model, material and render files required by WRSR.

For a Workshop item that acts as a delivery package for TesmioLoader, retain a
`plugins` folder at the Workshop item's root. The catalog detects this loader
payload and classifies buildings from the same item as **Tesmio**, rather than
ordinary Workshop content. Production, consumption and storage metadata is
parsed from `building.ini`, so use exact internal resource names.

Local unpublished buildings loaded from `media_soviet\workshop_wip` are also
treated as Tesmio manual-package content.

### Legacy/manual building fallback

Automatic discovery should be preferred. If an older or unusual construction
tool is missing:

1. Set `probe = 1` in `tesmiomenu.ini`.
2. Start the game and find the resolved internal object name in
   `tesmioloader.log`.
3. Add the internal name to
   `tesmioloader\build\plugins\tesmiomenu.ini`:

   ```ini
   [buildings]
   building1 = InternalObjectName
   ```

Entries are internal object names, not Workshop titles or Steam IDs.

### English and Russian names

- `$NAME_STR "Name"` provides one author-defined literal for every language.
- For a language-aware building name, use `$NAME` with a valid text ID
  registered by your mod in the game's language system.
- Tesmio Catalog requests the active text for that ID and therefore follows the
  current English or Russian game language.
- Unknown third-party literals are deliberately kept as authored instead of
  being machine-translated.

Vanilla names and categories come from the game's own English/Russian text
tables. The catalog interface itself switches automatically with the game.

## Troubleshooting

- Confirm that the game was started through `tesmiolauncher.exe`.
- Confirm that `TesmioCatalog.dll` is enabled in the launcher.
- Check `tesmioloader.log` for `TesmioCatalog`, `unsupported game build`, and
  catalog indexing messages.
- Fully restart the launcher and game after changing resources, buildings or
  configuration files.
- After a game update, use a catalog build explicitly supporting that version.

## Building from source

Place an `llvm-mingw` toolchain in `tools/llvm-mingw`, then run:

```powershell
.\build-portable.ps1
.\package-release.ps1 -Version 1.1.0
```

For local installation into the default Steam directory:

```powershell
.\install-local.ps1
```

Build output, downloaded compilers, diagnostic captures and reverse-engineering
notes are intentionally excluded from the repository.

## Russian documentation

See [GUIDE-RU.md](GUIDE-RU.md) for the Russian installation and integration
guide.

The exact Steam Workshop description used for publishing is kept in
[WORKSHOP-DESCRIPTION.txt](WORKSHOP-DESCRIPTION.txt). Keep it below Steam's
8,000-byte description limit.

## Feedback

This is the author's first mod. Please report bugs, compatibility issues and
feature ideas in the Steam Workshop comments or GitHub issues. Feedback is
welcome, and fixes will be attempted as quickly as possible.

## License

GPL-3.0-or-later. TesmioLoader ABI definitions are derived from the GPL-3.0
TesmioLoader project by MaxLegend.
