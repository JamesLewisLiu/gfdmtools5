# gfdmhook1

`gfdmhook1` provides the first BT5 startup path for the 32-bit Guitar Freaks &
DrumMania V4 runtime (`gdv4.exe`). It keeps the old game's process model intact:
the hook is injected into `gdv4.exe`, the original `boot.dll` remains in charge of
AVS startup, and the P3IO protocol is emulated through the common BT5 IO hook.

Extract `gfdm-v4.zip` into the V4 game directory (the directory containing
`gdv4.exe`, `boot.dll`, and `game.dll`). Start Asphyxia CORE first, then run the
mode-specific launcher from that directory.

The package contains launch scripts for both game modes:

```text
gamestart-v4-gf.bat   Guitar Freaks mode (-g)
gamestart-v4-dm.bat   DrumMania mode (-d)
```

Edit `gfdm-v4-gf.conf` or `gfdm-v4-dm.conf` before the first run. `eamuse.server`
should point at the Asphyxia CORE endpoint. GF V4 uses game code G32 and DM V4
uses G33, so the launchers select different default security mcodes. The
default IDs are the same valid test IDs used by the other BT5 hooks. V4
security codes differ between PCB images; if the game shows a roundplug error,
set `security.mcode` to the eight-character code reported by that image.
If Asphyxia is bound to a non-loopback address, change `eamuse.server` in both
files to that address and port (for example `10.9.0.156:80`).

The hook exposes one network adapter to the game. By default it selects the
adapter used by Windows' default route. On systems with multiple active
adapters, set `adapter.override_ip` to the IPv4 address of the adapter that
should be visible to GFDM; the other adapters are hidden from the game.

The default keyboard mapping is intentionally small and deterministic: Enter is
start, F1/F2 are service/test, 5 is coin, Z/X/C are the GF fret buttons, and
A/S/D/F/G cover the DM pads. Set `input.keyboard=false` when a separate input
backend is attached.

V4 uses the AVS boot implementation shipped with the game, so this hook is built
as an AVS-independent 32-bit DLL. That allows the same binary to be used with
the V4 `libavs-win32.dll` without copying a newer AVS import shim into the game
directory.
