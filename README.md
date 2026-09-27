# Plugin Manager for ARK-5

A store for the PSP in the spirit of the 3DS's Universal Updater. Browse
plugins and homebrew in a grid, then install, update or remove them over
Wi-Fi without a PC. Plugins can also be switched on and off. It ships with
FasterARK and is listed in three places:

- **XMB → ★ Plugin Manager**, right under **★ Custom Launcher** (in the Extras
  column, or in Game on regions without Extras).
- **Custom Launcher**, with the other apps (the app lives in
  `PSP/APPS/PluginManager`).
- **XMB → Plugins**, a category that lists your installed plugins
  ([details below](#the-plugins-category)).

## Installing

- The `Full` variant of FasterARK (`FasterARK_psp_full.zip`) includes it.
- Otherwise extract `PluginManager.zip` from the
  [latest release](https://github.com/kkaramaligkas/fasterark_powerup/releases/latest)
  to the root of the memory stick, or to the internal storage of a PSP Go.
  The XMB entries need ARK-5 with FasterARK's `FLASH0.ARK` (any variant, or
  the updater). The app itself runs on any ARK.
- Later versions arrive through the store: Plugin Manager has its own entry.

## Using it

| Button | Grid | Plugin page |
| --- | --- | --- |
| D-pad / analog stick | Move | Pick an action (↑↓ scroll the text) |
| L / R | Category: All, Plugins, Apps, Emulators, Games, Installed, Updates | |
| ✕ | Open the page | Run the action |
| △ | Menu: refresh the store, search, sort, update all, settings, about | |
| □ | Search (again to clear it) | |
| START | Settings | |
| ○ | Exit | Back |

✕ and ○ swap when the PSP is set to confirm with ○.

A plugin page offers **Install**, **Update**, **Reinstall**, **Enable**,
**Disable** and **Uninstall**. Plugins listed in `PLUGINS.TXT` that were not
installed through the app also show up under *Installed*: they can be enabled,
disabled or removed from `PLUGINS.TXT`, and the file itself stays.
Plugins start with the XMB or the next game. After changing an XMB plugin,
exit the app so the XMB reloads.

Settings:

- **Store address**: the default store, or any other store served over https.
- **Install to** (PSP Go with a memory stick): the memory stick or the
  internal storage.
- **"Plugins" category in the XMB**: on or off.
- **Verify HTTPS certificates**: keep this on. Turn it off only if the PSP's
  clock can't be set, because it makes downloads open to tampering.
- **Refresh the store at startup** and **Clear downloaded icons**.

## The Plugins category

The XMB's columns are a fixed list of eight built into the firmware's
`vshmain`. Code throughout `vshmain` refers to them by position; Network, for
example, gets special treatment as column 6. XMBControl (ARK's XMB module) can
add items to columns, but it cannot add a column.

The **PlayStation Network** column, the last one on the right, does nothing
since Sony shut PSN down for the PSP. So the category takes over that column:

- The column is renamed **Plugins**. The name follows the XMB language.
- It lists **★ Plugin Manager** and one item per installed plugin. Selecting a
  plugin opens its page in the app, where you can disable, update or uninstall
  it.
- The column keeps the PlayStation Network icon, because icons come from the
  XMB theme.

The original request placed the category between Extras and Photo. That needs
the column table and every place `vshmain` uses a column number patched for
each firmware (6.60, 6.61, TT, DT). XMB Item Hider's work on reordering
columns shows how easily that freezes the XMB, and it can't be done safely
without testing on real hardware. It could be tried later as an experimental
option.

To turn the category off, use **Settings → "Plugins" category in the XMB**,
then restart the XMB. If the XMB ever fails to start because of it, hold
**START** while the XMB loads. The column is then left untouched, the same
button ARK uses to boot without plugins, so you can open the app and turn the
category off.

XMB Item Hider's `HIDE_ALL_PSN = 2` hides the Plugins category along with the
PlayStation Network column.

## Files

Everything lives in `PSP/APPS/PluginManager/`:

| File | |
| --- | --- |
| `EBOOT.PBP` | the app |
| `cacert.pem` | certificate authorities for HTTPS (Mozilla's list, `tools/update_cacert.sh`) |
| `store.json` | built-in copy of the store, used until one is downloaded |
| `data/settings.json` | settings |
| `data/installed.json` | what each package installed: files, folders and `PLUGINS.TXT` lines |
| `data/store.json`, `data/icons/` | last downloaded store and its icons |
| `data/xmbnames.txt` | plugin list read by XMBControl for the Plugins category |
| `data/launch.txt` | written by XMBControl: the plugin to open |
| `data/noxmbcat` | present when the Plugins category is turned off |

## Store format

A store is one JSON file. The default store is
[`store/store.json`](store/store.json) in this repository, served from GitHub.
Anyone can host another one and point the app to it, as long as it is served
over https.

```json
{
  "storeInfo": {
    "title": "FasterARK Plugin Store",
    "author": "FasterARK powerup",
    "description": "Plugins, homebrew and emulators for ARK-5.",
    "url": "https://example.org/store.json",
    "iconBase": "https://example.org/icons/",
    "version": 1,
    "revision": 1
  },
  "entries": [ ... ]
}
```

`version` is the format version and must be 1. Increase `revision` whenever
you publish a change.

### Entries

| Field | |
| --- | --- |
| `id` | required; letters, digits, `.`, `_` and `-`, at most 48 characters; never change it |
| `title` | required |
| `install` | required; the steps below |
| `version` | compared piece by piece: `1.10` > `1.9`, `1.0h3` > `1.0h2`, `2026-09-12` > `2026-09-08` |
| `category` | `plugin`, `homebrew`, `emulator`, `game`, `utility`, `theme` or `other` |
| `author`, `description`, `license`, `website` | shown on the entry's page |
| `notes` | shown on the page too, as a warning |
| `icon` | 144×80 PNG, a file name relative to `iconBase` or a full URL |
| `size`, `updated` (YYYY-MM-DD), `runlevel` | shown on the entry's page |

Entries with errors are skipped, and the rest of the store still loads.

### Install steps

Steps run in order. The first one that fails stops the install. On an update,
a failure leaves the previous version in place.

| `type` | Fields |
| --- | --- |
| `download` | `url`; `file`: name for the download (default: end of the URL); `sha256`: recommended, and required for `http://` URLs |
| `extract` | `file` (default: the last download); `output`: destination folder; `input`: folder inside the archive to take files from; `include` / `exclude`: patterns; `flatten`: drop the archive's folders; `keep`: patterns of files that are not overwritten if they already exist (user settings) |
| `copy` | `file`; `to`: a folder ending with `/` or a file path |
| `mkdir` | `path` |
| `delete` | `path` of a file |
| `plugin` | `path` of a `.prx`; `runlevel`: a string or a list, such as `vsh`, `game`, `pops`, `umd`, `psp`, `homebrew`, `launcher`, `always` or game IDs (`"ULUS10041 ULES00151"`); `enabled` (default `true`); `position`: `"first"` puts the line at the top of `PLUGINS.TXT` |
| `message` | `text` shown after installing |

Archives can be zip, rar, 7z, tar or tar.gz. Patterns use `*` and `?`, ignore
case, and match the file name, or the path inside `input` when they contain a
`/`.

Any step can carry a condition, and it is skipped when the condition doesn't
match. A condition the app doesn't know also skips the step:

```json
{ "type": "plugin", "path": "%SEPLUGINS%cheat.prx", "runlevel": "ULUS10041",
  "if": { "notModel": "1000" } }
```

- `model` / `notModel`: `1000`, `2000`, `3000`, `go`, `street` or `vita`, or
  a list of them.
- `platform`: `psp` or `vita`.
- `device`: `ms0:` or `ef0:`.

Paths start with a variable:

- `%ROOT%`: the install device, `ms0:/` or `ef0:/`.
- `%SEPLUGINS%`, `%GAME%`, `%APPS%`, `%PSPPLUGINS%`, `%ISO%`, `%THEME%`,
  `%VSH%`: `SEPLUGINS/`, `PSP/GAME/`, `PSP/APPS/`, `PSP/PLUGINS/`, `ISO/`,
  `PSP/THEME/` and `PSP/VSH/` on that device.
- `%ARK%`: the ARK folder. Only its `THEME.ARK` can be written.
- `%TEMP%`: the app's scratch folder.

Packages can only write under `SEPLUGINS/`, `PSP/GAME/`, `PSP/GAME150/`,
`PSP/APPS/`, `PSP/PLUGINS/`, `PSP/THEME/`, `PSP/VSH/`, `ISO/`, `kd/`,
`MUSIC/`, `PICTURE/` and `VIDEO/` of `ms0:` or `ef0:`, plus ARK's `THEME.ARK`.
The app's own `data/` folder is off limits. Anything else is refused,
including paths that climb out with `..`, and every extracted file is checked.

The app records every file, folder and `PLUGINS.TXT` line an install creates:

- An update removes what the new version no longer ships.
- Uninstalling removes all of it, keeping `PLUGINS.TXT` lines that are not the
  package's own.
- A file installed by two packages belongs to the most recent one.

A complete entry:

```json
{
  "id": "xmbih",
  "title": "XMB Item Hider",
  "author": "Frostegater, wad11656 & Exceen",
  "version": "1.8",
  "category": "plugin",
  "runlevel": "vsh",
  "icon": "xmbih.png",
  "description": "Hide any XMB item, or whole XMB categories.",
  "install": [
    { "type": "download", "file": "xmbih.zip", "sha256": "29226b3a...",
      "url": "https://github.com/wad11656/XMB-Item-Hider-PSP/releases/download/v1.8/xmbitemhider_v1.8.zip" },
    { "type": "extract", "file": "xmbih.zip", "input": "SEPLUGINS/", "output": "%SEPLUGINS%",
      "include": ["xmbih.prx", "xmbih.ini"], "keep": ["xmbih.ini"] },
    { "type": "plugin", "path": "%SEPLUGINS%xmbih.prx", "runlevel": "vsh" },
    { "type": "message", "text": "Edit SEPLUGINS/xmbih.ini to choose what to hide." }
  ]
}
```

### Adding a package to the default store

1. Add the entry to `store/store.json`, with the `sha256` of each download
   (`sha256sum file.zip`).
2. Add an icon to `store/icons/`: the release's `ICON0.PNG`, or a tile made
   with `tools/make_art.py`.
3. Download the archives into a folder and run
   `make -C PluginManager/tests install PKG_DIR=that/folder`. This installs
   every entry with the app's engine into a fake memory stick and checks the
   result, as well as updates and uninstalls.
4. Increase `storeInfo.revision`.

## Building

You need the [pspdev](https://github.com/pspdev/pspdev) toolchain and these
libraries: `psp-pacman -S curl mbedtls cjson unarr libintrafont libpng zlib
bzip2 liblzma`.

```sh
make -C PluginManager            # EBOOT.PBP
make -C PluginManager package    # dist/PSP/APPS/PluginManager and dist/PluginManager.zip
```

`make` at the root of the repository builds the release packages. It also
builds XMBControl from [`XMBControl/`](../XMBControl) into the `FLASH0.ARK`
of every variant (`tools/flash0.py`).

After linking, the build runs `tools/check_imports.py`, which fails the build
when an import table comes out broken. The toolchain adds libraries such as
`libpspnet_inet` after libc. If they are also listed in the Makefile, the
linker pulls their stubs in two pieces. `psp-fixup-imports` then only warns,
and on the PSP the socket functions would call the wrong system functions.

## Testing

- `make -C PluginManager/tests check`: unit tests (store parsing, paths,
  `PLUGINS.TXT` editing, database, installer rules), built with
  AddressSanitizer and UBSan.
- `make -C PluginManager/tests install PKG_DIR=…`: installs, updates and
  uninstalls every store entry from local copies of the archives.
- `make -C PluginManager/tests live`: the same, downloading the archives.
- `tests/hostinstall` fills a memory stick folder for an emulator.
- The app runs in [PPSSPP](https://www.ppsspp.org/). A build with
  `EXTRA_CFLAGS=-DPM_AUTOTEST` presses buttons listed in `ms0:/pm_autotest.txt`
  (`<frame> <BUTTON>` lines).
  - To test downloads, serve a copy of the store over https (for example on
    `https://localhost:8443/`) and put its certificate authority in the
    emulated `cacert.pem`.
  - mbedTLS 2.28 can't match IP addresses in certificates, so use a host name.

What has been checked:

- In PPSSPP:
  - the whole app;
  - the store and icons downloaded over TLS 1.2 with certificate checks;
  - installs of rar, tar.gz and zip packages, including a 36 MB, 579-file
    emulator, identical file by file to the reference install;
  - the XMB launch request.
- On the PC: the test suite, with the twelve entries of the default store.

What still needs a real PSP:

- Wi-Fi and HTTPS speed.
- The XMBControl changes: the XMB can't be emulated.

## License

GPL-3.0, like ARK. Built with libcurl, mbedTLS, cJSON, unarr, zlib, libpng
and intraFont. The CA bundle is Mozilla's, from certifi.
