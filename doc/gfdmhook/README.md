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

The same package includes the generic bemanitools mapper UI. Run
`config-gf.bat` or `config-dm.bat` to configure the requested Buttons list and
the Network/card-reader tab; the hook loads the matching `gf.bin` or `dm.bin`
mapping from the normal `%APPDATA%\\DJHACKERS` location at startup.

Edit `gfdm-v4-gf.conf` or `gfdm-v4-dm.conf` before the first run. `eamuse.server`
should point at the Asphyxia CORE endpoint. DM V4 uses game code G32 and GF V4
uses G33, so the launchers select different default security mcodes. The
default IDs are the same valid test IDs used by the other BT5 hooks. V4
security codes differ between PCB images; if the game shows a roundplug error,
set `security.mcode` to the eight-character code reported by that image.
If Asphyxia is bound to a non-loopback address, change `eamuse.server` in both
files to that address and port (for example `10.9.0.156:80`).

The TEST menu's BIOS date uses the legacy Windows `SystemBiosDate` registry
value when available, then falls back to `System\\BIOS`'s `BIOSReleaseDate`
on newer PCs. If neither provides valid text, it displays `UNKNOWN`. This
avoids the original V4 reader's uninitialized text on a failed registry read.

`eamuse.eamid` supplies the white roundplug ROM and its EEPROM signature.
The TEST menu's SYSTEM ID uses V4's native encoding: a fixed `0140` prefix
followed by the ROM bytes in order 0, 6, 5, 4, 3, 2, 1, 7, grouped with
hyphens. Thus the default `0101020304050607086F` setting displays as
`0140-0107-0605-0403-0208`; it does not print the full config value verbatim.
HARDWARE ID is derived separately from the selected network adapter's MAC.

The hook exposes one network adapter to the game. By default it selects the
adapter used by Windows' default route. On systems with multiple active
adapters, set `adapter.override_ip` to the IPv4 address of the adapter that
should be visible to GFDM; the other adapters are hidden from the game.

The default keyboard mapping is intentionally small and deterministic: Enter is
start, F1/F2 are service/test, 5 is coin, Z/X/C are the GF fret buttons, and
A/S/D/F/G cover the DM pads. Set `input.keyboard=false` when a separate input
backend is attached.

In `config.exe`, select an action in **Buttons** and use **Naive...** to bind a
Windows virtual key from any keyboard, or **Naive many...** to bind consecutive
actions. These bindings use `GetAsyncKeyState`, following spice2x's Naive input
mode, and respond in both foreground and background, including while another
window has focus. **Bind...** continues to bind a particular Raw Input/HID device.
Both kinds can coexist in the existing `gf.bin` / `dm.bin` mapper files; updating
the tools does not convert existing bindings. Key names carry a `(Naive)` suffix.

The **Naive keyboard (all devices)** entry is also available as an e-amusement
keypad device. Use NumLock for numeric keypad digits as usual. Windows virtual
keys distinguish left/right Shift, Ctrl and Alt, but combine main Enter with
keypad Enter; either Enter key therefore activates the Naive keypad's `00`.
Escape cancels the binding dialog. Keys held while opening that dialog must be
released and pressed again. Binding capture also works in the background.
Naive applies to mapper/EAMIO input;
the hook's separate built-in fallback keys are still controlled by
`input.keyboard`. Disable that fallback when using only configured bindings.

V4's main display uses OpenGL. The hook keeps `GetClientRect` reporting the
actual window size, including during focus changes; the shared IIDX D3D9
back-buffer-size override is bypassed for GFDM. This prevents a zero-sized
drawable from leaving the last frame frozen after switching windows.

V4 uses the AVS boot implementation shipped with the game, so this hook is built
as an AVS-independent 32-bit DLL. That allows the same binary to be used with
the V4 `libavs-win32.dll` without copying a newer AVS import shim into the game
directory.

The hook also installs the V4 `libdevice` security-status compatibility layer.
The original Gitadora V4 hook reports status `0x101` for the black roundplug
and `0x100` for the white roundplug; V4 rejects a boolean “present” response
(`1`) with `5-1503-0001`. The P3IO emulator still supplies the ROM and signed
EEPROM bytes, while these status values preserve the game-facing contract.
