# Plugin Manager for ARK-5

A store for the PSP in the spirit of the 3DS's Universal Updater. Browse
plugins and homebrew in a grid, then install, update or remove them over
Wi-Fi without a PC. Plugins can also be switched on and off. It ships with
FasterARK and is listed in three places:

- **XMB → ★ Plugin Manager**, right under **★ Custom Launcher** (in the Extras
  column, or in Game on regions without Extras).
- **Custom Launcher**, with the other apps (the app lives in
  `PSP/APPS/PluginManager`).
- **XMB → Plugins**, an optional category that lists your installed plugins.
  It's off by default ([details below](#the-plugins-category)).

This repository is the app's source. It was split from
[FasterARK powerup](https://github.com/KKaramaligkas/FasterARK_powerup), with its history, so that it can be developed on
its own. FasterARK builds it into its packages and releases, and FasterARK's
store lists it ([Store format](#store-format)). [ARK Browser](https://github.com/KKaramaligkas/Flow)
uses its network, text, input and drawing code.

## Installing

- The `Full` variant of FasterARK (`FasterARK_psp_full.zip`) includes it.
- On a PSP, the ARK updater (`ARK_UPDATE.zip`, from ARK 5.1.5) installs or
  updates it. It replaces the copy that's already there (internal storage
  first), or installs it on the device that holds ARK's folder. The app's
  `data` folder, with its settings and the list of installed plugins, is kept.
- Otherwise extract `PluginManager.zip` from the
  [latest release](https://github.com/kkaramaligkas/fasterark_powerup/releases/latest)
  to the root of the memory stick, or to the internal storage of a PSP Go.
  The XMB entries need ARK-5 with FasterARK's `FLASH0.ARK` (any variant, or
  the updater). The app itself runs on any ARK.
- Later versions arrive through the store: Plugin Manager has its own entry.

## Updating ARK

ARK itself is in the store as **ARK-5**, from ARK 5.1.6. The app compares the
store's version with `VERSION.TXT` in ARK's folder, which the release packages
and the updater write. **Update** downloads `ARK_UPDATE.zip` from the release
shown in the store, puts the ARK Updater in `PSP/GAME/UPDATE` and offers to start it. The
updater updates ARK and this app, then restarts. It can also be started later
from the Game column, like any other homebrew.

The XMB's **System Update** and the Custom Launcher's update check are turned
off in FasterARK (`UPDATER.TXT` points to `127.0.0.1`). They downloaded the
original ARK-5 from its server, which would have replaced this build and its
Plugin Manager entries. They can only fetch over plain http, which GitHub
doesn't serve.

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
- **Wi-Fi network**: the network connection the app uses without asking.
  Choosing it opens the PSP's connection dialog to pick another one.
- **Install to** (PSP Go with a memory stick): the memory stick or the
  internal storage.
- **"Plugins" category in the XMB**: off by default.
- **Verify HTTPS certificates**: keep this on. Turn it off only if the PSP's
  clock can't be set, because it makes downloads open to tampering.
- **Refresh the store at startup** and **Clear downloaded icons**.

### If it can't connect

The top bar shows **Online**, **Offline** or **Wi-Fi off** (the WLAN switch).
The app uses the connections set up in *Settings → Network Settings*. It
connects without asking to the one it used last. The first time, that's the
connection the PSP used last, or the only one there is. While it shows
*Connecting to …*, ○ opens the PSP's connection dialog to pick another
network. The dialog also opens when that connection fails. The app remembers
the network it connected to, in `data/settings.json`.

If the network can't start, a message names the step that failed and gives the
system error code. Error `80020190` means the PSP ran out of memory.

When a secure connection fails, the message says why: a certificate that
isn't valid yet or has expired (with its date and the PSP's clock), one
issued for another server, or one from an authority the app doesn't trust.
A hotspot's login page or a filter on the network causes the last two. The
details go to `data/tls_error.txt`.

Before 1.0.4 the app couldn't download anything on a real PSP:

- 1.0.0 and 1.0.1 left almost no memory for the PSP's network libraries, so
  they stayed *Offline*.
- 1.0.2 and 1.0.3 read the date through the PSP SDK's C library, which asks
  the kernel (`sceKernelLibcGettimeofday`). On a real PSP that returns only
  the time of day, so the clock read 1 January 1970 and every certificate
  looked like it wasn't valid yet. 1.0.4 reads the date from the RTC
  (`src/clock.c`).

Emulators don't load those libraries and return the full date, which is why
testing missed both. The store can't update those copies: run the ARK
updater (5.1.7 or later), or extract `PluginManager.zip` from the latest
release by hand.

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

The category is off by default, because the XMB can't be emulated and it
hasn't been tested on a real PSP yet. To turn it on, use **Settings →
"Plugins" category in the XMB**, then restart the XMB. Turn it off the same
way.

If the XMB ever fails to start with the category on, hold **START** while the
XMB loads. The column is then left untouched, and ARK uses the same button to
boot without plugins. You can then open the app and turn the category off.

XMB Item Hider's `HIDE_ALL_PSN = 2` hides the Plugins category along with the
PlayStation Network column.

## Files

Everything lives in `PSP/APPS/PluginManager/`:

| File | |
| --- | --- |
| `EBOOT.PBP` | the app |
| `cacert.pem` | certificate authorities for HTTPS (Mozilla's list, `tools/update_cacert.sh`) |
| `store.json` | built-in copy of the store, used until one is downloaded |
| `data/settings.json` | settings, and the Wi-Fi connection used last |
| `data/installed.json` | what each package installed: files, folders and `PLUGINS.TXT` lines |
| `data/store.json`, `data/icons/` | last downloaded store and its icons |
| `data/xmbnames.txt` | plugin list read by XMBControl for the Plugins category |
| `data/launch.txt` | written by XMBControl: the plugin to open |
| `data/xmbcat` | present when the Plugins category is turned on |
| `data/noxmbcat` | present when it's off, for ARK 5.1.2's XMBControl, which shows the category unless this file exists |
| `data/tls_error.txt` | details of the last certificate that was refused: the clocks, the trusted authorities loaded and each certificate of the chain |

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
| `versionFile` | for software installed by other means (ARK itself): the installed version is the first line of this file, such as `%ARK%VERSION.TXT`. Such an entry can't be uninstalled |

Entries with errors are skipped, and the rest of the store still loads.

### Install steps

Steps run in order. The first one that fails stops the install. On an update,
a failure leaves the previous version in place.

Files are extracted or copied into staging files before installed files change.
Commit includes removed files, `PLUGINS.TXT`, and `data/installed.json`.
If a write fails or the operation is cancelled, the originals are restored.
An interrupted commit is recovered on the next launch, before the database is
loaded. Keep `data/tmp/transaction/` intact until recovery finishes. If storage
is disconnected or recovery cannot finish, reconnect it and restart the app;
the app refuses further changes while recovery is incomplete.

Staging and backups require extra free space. This recovery applies to the
Plugin Manager's file installation, not the firmware writes performed later by
the separate ARK Updater. Real-device power interruption testing is still
required; filesystem or storage corruption can prevent automatic recovery.

The details page shows free space on the installation device. Downloads check
the server's reported remaining size before writing; extraction and copying
check space for the replacement, the backup, and pending destination growth,
including installations to a different PSP Go device. Unknown free-space values
are shown as unknown; failed writes still trigger rollback.

Interrupted downloads keep `*.part` and `*.part.json` in the scratch folder.
Retrying the same package resumes when the server supplies a strong ETag or
Last-Modified validator and returns the matching byte range. Changed files,
ignored ranges, or invalid metadata restart the download. Package checksums
are verified after completion. Downloads without a usable validator restart
from the beginning. These partial files can be removed to reclaim space.

| `type` | Fields |
| --- | --- |
| `download` | `url`; `file`: name for the download (default: end of the URL); `sha256`: recommended, and required for `http://` URLs; alternatively `sha256Url` and `checksumFile`: HTTPS release SHA256SUMS file and the asset name in it |
| `extract` | `file` (default: the last download); `output`: destination folder; `input`: folder inside the archive to take files from; `include` / `exclude`: patterns; `flatten`: drop the archive's folders; `keep`: patterns of files that are not overwritten if they already exist (user settings) |
| `copy` | `file`; `to`: a folder ending with `/` or a file path |
| `mkdir` | `path` |
| `delete` | `path` of a file |
| `plugin` | `path` of a `.prx`; `runlevel`: a string or a list, such as `vsh`, `game`, `pops`, `umd`, `psp`, `homebrew`, `launcher`, `always` or game IDs (`"ULUS10041 ULES00151"`); `enabled` (default `true`); `position`: `"first"` puts the line at the top of `PLUGINS.TXT` |
| `message` | `text` shown after installing |
| `run` | `path` of an `EBOOT.PBP` in `PSP/GAME` or `PSP/APPS`, which the app offers to start once the install is done (the app closes); `title`: its name in that question. Plugin Manager 1.0.2 and older skip entries with this step, and **Update all** leaves them out |

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
   `make -C tests install PKG_DIR=that/folder`. This installs
   every entry with the app's engine into a fake memory stick and checks the
   result, as well as updates and uninstalls.
4. Increase `storeInfo.revision`.

Packages whose author publishes no usable download are built from source into
`store/packages/` and served from this repository. For now that's
UmdImageCreator: `tools/build_umdimagecreator.sh` builds it from the author's
tag, with the source unchanged.

Official entries use fixed release URLs, commit URLs, or GitHub asset IDs and
verify checksums. The NZPortable entry is a named nightly snapshot; a later
nightly is a separate store update. `tools/check_store_downloads.py` rejects
moving or unchecked downloads in CI.

The store that installed copies read is the one in FasterARK powerup
(`PluginManager/store/store.json` there, at the address in `src/version.h`);
`store/` here is the snapshot bundled with the app and used by the tests.
For a new ARK release, FasterARK changes `Updater/version.h` and, when
applicable, this app's `src/version.h`, then runs
`python3 tools/release_store.py --seed` from its root and commits the
refreshed seed with its revision bump. The seed references
that exact release's `SHA256SUMS`, avoiding the impossible cycle of embedding an
archive's own hash inside it. The build also prepares this seed before packaging.
After packaging, CI publishes `store.json` with the actual archive hashes along
with `SHA256SUMS`; users can select that release store as their store URL.

## Building

You need the [pspdev](https://github.com/pspdev/pspdev) toolchain and these
libraries: `psp-pacman -S curl mbedtls cjson unarr libintrafont libpng zlib
bzip2 liblzma`.

```sh
make            # EBOOT.PBP
make package    # dist/PSP/APPS/PluginManager and dist/PluginManager.zip
```

The XMB entries come from XMBControl, which FasterARK builds into ARK's
`FLASH0.ARK` together with its release packages
([FasterARK's build](https://github.com/KKaramaligkas/FasterARK_powerup#install-instructions)).

After linking, the build runs `tools/check_imports.py`, which fails the build
when an import table comes out broken. The toolchain adds libraries such as
`libpspnet_inet` after libc. If they are also listed in the Makefile, the
linker pulls their stubs in two pieces. `psp-fixup-imports` then only warns,
and on the PSP the socket functions would call the wrong system functions.

## Testing

Pushes and pull requests run the sanitized host tests, then build
`PluginManager.zip` (`.github/workflows/ci.yml`). SDK archives, SDK source
revisions and build actions are pinned; `tools/toolchains.json` records the
SDK inputs and their SHA-256 checksums, the same as FasterARK's. A changed
upstream asset fails verification rather than silently changing the build.

PSP libraries installed by `psp-pacman` still come from its current repository;
`psp-packages.txt` in each release records their exact installed versions.
`toolchains.json`, compiler versions, the source commit, and `SHA256SUMS` are
published with the release to make its build inputs and outputs inspectable.

- `make -C tests check`: unit tests (store parsing, paths,
  `PLUGINS.TXT` editing, database, installer rules, cancellation, failed writes
  and recovery after an interrupted commit), built with
  AddressSanitizer and UBSan.
- `make -C tests install PKG_DIR=…`: installs, updates and
  uninstalls every store entry from local copies of the archives.
- `make -C tests live`: the same, downloading the archives.
- `tests/hostinstall` fills a memory stick folder for an emulator.
- The app runs in [PPSSPP](https://www.ppsspp.org/). A build with
  `EXTRA_CFLAGS=-DPM_AUTOTEST` presses buttons listed in `ms0:/pm_autotest.txt`
  (`<frame> <BUTTON>` lines).
  - To test downloads, serve a copy of the store over https (for example on
    `https://localhost:8443/`) and put its certificate authority in the
    emulated `cacert.pem`.
  - mbedTLS 2.28 can't match IP addresses in certificates, so use a host name.
  - Certificates that aren't valid yet, have expired, come from an unknown
    authority or name another server show each certificate error.
  - PPSSPP's `sceKernelLibcGettimeofday` returns the full date, unlike a real
    PSP's, so the emulator can't show a clock problem. To act like a PSP, make
    it return `tv_sec % 86400`.
  - PPSSPP doesn't need the memory of the network libraries, but it reserves
    it and warns `No room for utility module` in its log when the app doesn't
    leave enough. The app keeps 4 MB free for them
    (`PSP_HEAP_THRESHOLD_SIZE_KB` in `src/main.c`).

What has been checked:

- In PPSSPP:
  - the whole app;
  - connecting to Wi-Fi without the dialog, ○ to pick another network, and
    choosing it in the settings (PPSSPP has one network connection);
  - updating ARK: the version check, the download of `ARK_UPDATE.zip` from a
    local copy of the store, and the app closing to start the updater (PPSSPP
    has no ARK, so the start itself is left to a real console);
  - the store and icons downloaded over TLS 1.2 with certificate checks,
    also with a clock that, like a real PSP's kernel, gives no date;
  - the error for each kind of refused certificate;
  - GitHub's real certificate chains, verified with the PSP build of mbedTLS;
  - installs of rar, tar.gz and zip packages, including a 36 MB, 579-file
    emulator, identical file by file to the reference install;
  - the XMB launch request.
- On the PC: the test suite, with the nineteen entries of the default store,
  and the ARK updater's install steps.
- On a PSP, with ARK 5.1.6: the XMB with the newer VSHControl and XMBControl,
  starting the app from the XMB, the Wi-Fi connection, and the store over
  HTTPS once the date came from the RTC.

What still needs a real PSP:

- Starting the ARK Updater from the app.
- The Plugins category in the XMB.

Use the [hardware release checklist](https://github.com/KKaramaligkas/FasterARK_powerup/blob/main/docs/hardware-release-checklist.md) to
record model, firmware, candidate checksum, and individual results, including
transaction recovery. The host tests and build do not establish hardware
coverage for the new changes.

## License

GPL-3.0, like ARK. Built with libcurl, mbedTLS, cJSON, unarr, zlib, libpng
and intraFont. The CA bundle is Mozilla's, from certifi.

## Compatibility and replacement review

The details page shows supported models and PSP system software when the store
provides them. Missing dependencies and installed conflicts appear there too.
Installation checks these requirements before downloading or changing files.
Entries without compatibility metadata make no model or firmware guarantee.

Optional entry fields:

```json
"compatibility": {"models": ["1000", "2000", "3000", "go", "street", "vita"], "firmware": ["6.60", "6.61"]},
"requires": ["required-package-id"],
"conflicts": ["incompatible-package-id"]
```

`models` and `firmware` are nonempty allowlists. Both constraints must match.
Firmware means PSP system software, including the emulated version on Vita; it
does not mean Vita firmware or ARK's release version. Package lists refer to
Plugin Manager's installed database; manually installed plugins are not tracked
as dependencies. Unknown constraint fields and malformed lists block installation
with an explanation. `requires` and `conflicts` cannot reference the entry itself.
The official UmdImageCreator entry excludes PSP Go and Vita, which have no UMD drive.

After downloading and staging an install, **Review file changes** lists each
existing file that will be replaced or removed, with its recorded package owner.
Up/Down browses the complete list; confirm applies all changes; cancel keeps the
existing files and database. An untracked file is labelled **not tracked**.
When a replacement belongs to another package, accepting transfers that file's
ownership to the new package, so uninstalling the previous owner cannot delete it.
New files do not need a replacement prompt. The review also includes changes to
`PLUGINS.TXT`; files preserved by an archive's `keep` rule are left out.

## Validation for these changes

Host tests cover compatibility rejection before staging, dependency/conflict
checks, cancelling and accepting replacements owned by another package,
transaction recovery, space exhaustion, and HTTP resume behavior. CI builds PSP
and Vita packages and retains release checksums and toolchain provenance.
Physical-device testing is tracked separately in the
[hardware checklist](https://github.com/KKaramaligkas/FasterARK_powerup/blob/main/docs/hardware-release-checklist.md); a passing build is
not a hardware result. Screenshots of the new review and compatibility pages
should be captured with the tested release during that checklist.
