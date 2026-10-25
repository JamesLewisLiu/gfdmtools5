# Local verification (2026-10-25)

- 64 migration commits were checked individually: trees, messages, authors, committers, and dates are identical; only parents and SHAs changed.
- The 423 retained files under src and dist/gfdm match gfdm/5914d4a byte-for-byte; all original module Module.mk files are unchanged.
- The complete local LLVM-MinGW 20261020 UCRT + GNU Make build passed; all 11 DLL/EXE PE Machine values are I386 (0x14c), with no x64 build directory.
- All five packages passed scripts/check-gfdm-packages.py, including package files, matching decoders, shared hook, GF/DM models, and launcher wiring.
- geninput-naive, gfdm-profile, gfdm-storage, gfdm-coin, gfdm-debug, and gfdm-effect all returned 0 as x86 tests.
- A temporary Python zipfile compatibility command was used locally because Windows lacked zip; it is outside this repository. Actions installs the standard Ubuntu zip package.
- The workflow YAML was parsed and its two jobs checked. No online Actions run or game deployment was performed.

Per-file lists, build logs, test logs, artifact SHA256 values, and the original target backup are stored outside this repository with the migration records in the original workspace.

## Package layout update (2026-10-25)

- CLVSD.ax is packaged beside inject.exe and the game executable. The hook resolves its absolute path from the game executable directory; class identities and registered decoder fallback remain unchanged.
- Unused gfdm-v4.conf through gfdm-v8.conf were removed. Both mode-specific configurations for every version now include the endpoint, adapter, roundplug, keyboard and frame-rate comments. All ten configurations retain their previous key/value pairs.
- A clean local x86 build, all five package checks, and all six existing Windows tests passed.
- The existing external decoder integration harness was compiled against the updated codec.c. Missing, invalid, directory, no-export, old-class and new-class cases passed. The old-class case verified actual module identity, graph add/remove, a different working directory, concurrent creation and no registered decoder fallback.
- These checks do not establish actual game video playback. No game installation was changed.

## Built-in startup configuration and config.exe update (2026-10-25)

- Removed the fifteen version-specific boot and EA3 identity XML files from `dist/gfdm` and all five archives. The x86 hook now serves the native parser's boot settings and version/mode identity documents from read-only memory files. Launchers keep the native `-b` and `-I` arguments, targeting virtual filenames; no physical XML files are required.
- Config path selection recognizes the unique versioned package launcher, then the unique mode-specific configuration in the tool directory, and can use a matching game executable when multiple versions are present. A leftover V4 configuration from an older config.exe no longer hides the V7 package identity. It refuses to save if the version is ambiguous and displays the selected filename. The writer preserves existing comments and generates the documented defaults and comments for missing or incomplete files.
- GF controls list RGB, Picks and Wail for both players before Start and Effector; each CCW follows its CW row. The display-row map translates back to the original persisted mapper action indices.
- Local x86 build, five-package validation, all eight Windows tests, virtual XML CRT read/seek/reopen checks across ten version/mode pairs, configuration path/comment tests, and action-order/index checks passed. No game installation was changed; in-game boot from memory-served XML remains to be verified.
- An external x86 UI harness opened the real V7 package config.exe in GF and DM modes, read all displayed button rows, selected the GFDM page, edited settings, switched pages to save, and verified each V7 filename and retained comments. It closed using Cancel to avoid changing the existing AppData input/EAM configuration.
