# Local verification (2026-10-25)

- 64 migration commits were checked individually: trees, messages, authors, committers, and dates are identical; only parents and SHAs changed.
- The 423 retained files under src and dist/gfdm match gfdm/5914d4a byte-for-byte; all original module Module.mk files are unchanged.
- The complete local LLVM-MinGW 20261020 UCRT + GNU Make build passed; all 11 DLL/EXE PE Machine values are I386 (0x14c), with no x64 build directory.
- All five packages passed scripts/check-gfdm-packages.py, including package files, matching decoders, shared hook, GF/DM models, and launcher wiring.
- geninput-naive, gfdm-profile, gfdm-storage, gfdm-coin, gfdm-debug, and gfdm-effect all returned 0 as x86 tests.
- A temporary Python zipfile compatibility command was used locally because Windows lacked zip; it is outside this repository. Actions installs the standard Ubuntu zip package.
- The workflow YAML was parsed and its two jobs checked. No online Actions run or game deployment was performed.

Per-file lists, build logs, test logs, artifact SHA256 values, and the original target backup are stored outside this repository with the migration records in the original workspace.
