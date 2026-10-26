# GFDM Tools 5

Standalone **32-bit** bemanitools branch for GFDM V4-V8 (GuitarFreaks / DrumMania).

## Build

Ubuntu/Debian:

```sh
sudo apt-get install gcc-mingw-w64-i686 g++-mingw-w64-i686 binutils-mingw-w64-i686 make zip python3
make -j1
python3 scripts/check-gfdm-packages.py build/zip
```

The default build generates `build/zip/gfdm-v4.zip` through `gfdm-v8.zip` and nine `build/bin/indep-32/*-test.exe` files.
Run `make packages` or `make tests` separately. Test EXEs run directly on Windows.
`make build-docker` uses the same build graph. There is no x64 build target.
Use a serial build: the inherited DLL/import-library rules can race during parallel linking.

GitHub Actions builds and validates all five packages on push, pull_request, and manual runs, then runs the nine x86 tests on Windows.
The workflow is `.github/workflows/build-gfdm.yaml`. Local validation and online Actions results are recorded separately.

## Use and upstream migration

See the [GFDM documentation](doc/gfdmhook/README.md). The original license applies.
GFDM, input mapping, EAMIO, security, and common hook code keep their original paths and contents.
`iidxhook-util` is a shared GFDM graphics/configuration dependency and is retained as a whole; shared game definitions in `config` are also unchanged.
Headers and import declarations for retained modules keep their original locations. Other games are not built.
Future feature commits can be cherry-picked or exported as patches back to bemanitools; the standalone build-pruning commit does not need to be migrated.

See [migration notes](doc/migration/README.md) for history and pruning boundaries.
