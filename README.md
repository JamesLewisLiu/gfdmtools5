# GFDM Tools 5

Standalone **32-bit** bemanitools branch for GFDM V4-V8 (GuitarFreaks / DrumMania).

## Build

Ubuntu/Debian:

```sh
sudo apt-get install gcc-mingw-w64-i686 g++-mingw-w64-i686 binutils-mingw-w64-i686 make zip python3
make -j1
python3 -m unittest discover -s scripts/tests
python3 scripts/check-gfdm-packages.py build/zip
```

The default build generates `build/zip/gfdm-v4.zip` through `gfdm-v8.zip` and fourteen `build/bin/indep-32/*-test.exe` files.
Run `make packages` or `make tests` separately. Test EXEs run directly on Windows.
`make build-docker` uses the same build graph. There is no x64 build target.
Use a serial build: the inherited DLL/import-library rules can race during parallel linking.

GitHub Actions builds and validates all five packages on push, pull_request, and manual runs, then runs the fourteen x86 tests on Windows and seventeen Python exporter fixtures.
The workflow is `.github/workflows/build-gfdm.yaml`. Local validation and online Actions results are recorded separately.

## One build for both resource layouts

The `gfdm` branch builds the same x86 tools for original packed installs and
verified shared unpacked stores, including MIDI input, console logging and
the controller Pick A/B fix. Separate AIO binaries are no longer required.
The former `aio-layered-data` branch is a historical integration baseline;
use `gfdm` for future builds and fixes.

All ten shipped GF/DM configurations default to `layered.enabled=false` and
`layered.full_verify=false`. Keep routing off with the complete original PAK
layout. Set `layered.enabled=true` only for a prepared shared store; those
installs require the setting to remain enabled. Restart after changing it.
The same binaries support both choices; no new resources or saves are created
by selecting the disabled resource path. Existing configurations can omit both
keys and retain the same false defaults.

The common V4 AVS configuration expansion fix is retained for both layouts.
See [shared resources](doc/gfdmhook/layered-data.md) for setup, verification
caching and the native ROM-check limitation. CI covers both resource paths
with fixtures; it does not certify all versions, modes or complete songs.

## Use and upstream migration

See the [GFDM documentation](doc/gfdmhook/README.md). The original license applies.
GFDM, input mapping, EAMIO, security, and common hook code keep their original paths.
MIDI input adds a separate C device adapter with small lifecycle, polling and binding integrations; existing HID/keyboard backends and mapping serialization are retained.
`iidxhook-util` is a shared GFDM graphics/configuration dependency and is retained as a whole; shared game definitions in `config` are also unchanged.
Headers and import declarations for retained modules keep their original locations. Other games are not built.
Future feature commits can be cherry-picked or exported as patches back to bemanitools; the standalone build-pruning commit does not need to be migrated.

See [migration notes](doc/migration/README.md) for history and pruning boundaries.
