# gfdmhook1

`gfdmhook1` provides a shared BT5 startup path for the 32-bit classic Guitar
Freaks & DrumMania V4-V8 runtimes. Inject it into the matching `gdvN.exe`;
the original `boot.dll` still starts AVS, with P3IO emulated through BT5 IO.

Extract `gfdm-vN.zip` into a directory containing that version's original
`gdvN.exe`, `boot.dll`, `game.dll` and supporting libraries. Keep the existing
data directory at its original relative location. Start Asphyxia CORE, then
run `gamestart-vN-gf.bat` or `gamestart-vN-dm.bat`. Each launcher selects its
own boot XML, EA3 identity and mode-specific configuration.

| Version | Executable | GF / DM models | Default GF / DM security codes |
| --- | --- | --- | --- |
| V4 | gdv4.exe | G33 / G32 | GCG33JAA / GCG32JAA |
| V5 | gdv5.exe | H33 / H32 | GCH33JAA / GCH32JAA |
| V6 | gdv6.exe | I33 / I32 | GCI33JAA / GCI32JAA |
| V7 | gdv7.exe | J33 / J32 | GCJ33JBA / GCJ32JBA |
| V8 | gdv8.exe | K33 / K32 | GCK33JBA / GCK32JBA |

The hook selects its profile from the complete executable basename and rejects
unknown executables. The default security code follows both version and `-g`
mode; an explicit `security.mcode` still overrides it. Each version writes
`gfdm-vN-hook.log` and uses its own directory's `CONF/NVRAM` and `CONF/RAW`.
The native boot module's early `E:/CONF` (or `./CONF`) directory creation is
also redirected beside the executable before AVS starts. The NVRAM/raw AVS
mount devices use those same absolute paths, independently of the working
directory. Existing saves are retained; no drive-root save directory is
deleted or automatically merged with a different game's saves.
Native `F:/TMP`, `F:/CACHE` and `F:/CACHE/PROXY` creation and the separate
`/tmp/` and `/proxy/` mounts are likewise redirected to `TMP`, `CACHE` and
`CACHE/PROXY` beside the executable. The asset root mount remains unchanged.

COIN counts one insertion on each press edge. Holding the button does not
repeat. Like the native V4-V8 libdevice APIs, `device_get_coinstock` consumes
at most one pending coin per call and `device_get_coinstock_all` consumes
all remaining coins; subsequent reads return zero until another insertion.
Both APIs share the consumption cursor. P3IO retains the separate cumulative
16-bit board count, so consuming game credit cannot make that counter drop.
Other regions/revisions may require different security codes and EA3 identities.
Do not mix libraries from different game images, or layer this over an old
replacement `libdevice.dll` which supplies its own identity and input backend.

V5-V8 retain native roundplug signature checks using the emulator's matching
payloads: V5-V7 use UDONHRKI, V8 uses SHAMOSAN, and white plugs use E-AMUSE3.
Only V4 uses the historical security compatibility
exports. The decoder-pointer guard with V4-specific offsets is enabled only
for V4; those offsets are never applied to V5-V8. All versions keep the original
`device_initialize` export, including the additional V7/V8 argument.

For registration-free video decoding, place a compatible **32-bit**
`clvsd.ax` beside `gdvN.exe` and restart the game. The hook loads that exact
file from the executable directory and creates the native DirectShow decoder
through `DllGetClassObject`; it never calls `DllRegisterServer` or changes COM
registration. This also works when the launcher's working directory differs.
Keep any decoder dependencies beside the AX. The decoder is not distributed
with bemanitools.

V4-V6 request decoder class `8ACD52ED-9C2D-4008-9129-DCE955D86065`
(for example, the matching CLVSD 6 release). V7/V8 first try
`37991D68-42A3-40E3-8C05-037170E1A42A`, then the older class. A newer AX which
only exposes the second class cannot satisfy V4-V6. Class identities are not
remapped. Other COM objects retain native activation, and a missing, invalid
or incompatible local decoder falls back to existing registered activation.
The hook log reports successful local creation, loading errors, and unsupported
classes. Add or replace the AX only with the game stopped; loading is attempted
once per process and the module remains loaded until exit.

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

`eamuse.pcbid` supplies the complete system/network identity, matching the
IIDX launcher convention. The hook sets V4's `/env/profile/sysid` to that
validated 20-character value when the device layer publishes it, so TEST,
the shared identity APIs and EA3 initialization all use the same PCBID.
The default `0101020304050607086F` displays as `0101-0203-0405-0607-086F`.
This also replaces V4's native fixed `0140` prefix and ROM-byte permutation.
`eamuse.eamid` independently supplies the white roundplug ROM and its EEPROM
signature; `eamuse.pcbid` also supplies the black roundplug ROM and signature.
The injected V4 boot path initializes EA3 from the system identity, rather
than taking the network PCBID from `gfdm-v4-boot.xml`'s legacy identity node.
HARDWARE ID is derived separately from the selected network adapter's MAC.

All V4-V8 profiles, in both GF and DM modes, expose one network adapter to the
game through the shared `adapter.override_ip` configuration. The hook also
filters consumers loaded after boot, so late network libraries use the same
selected adapter. By default it selects the
adapter used by Windows' default route. On systems with multiple active
adapters, set `adapter.override_ip` to the IPv4 address of the adapter that
should be visible to GFDM; the other adapters are hidden from the game.

The built-in keyboard fallback is disabled by default. In `config.exe`, open
**GFDM** and check **Enable built-in keyboard fallback** only when needed.
This saves `input.keyboard` in the current version's GF or DM configuration
beside the configurator. Leave it off after configuring IO bindings. Explicit
command-line `-p input.keyboard=true` still overrides the saved setting.
Configured Raw Input, Naive and EAMIO bindings continue to work independently.

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

GF's **Effector (Clockwise)** buttons retain the existing Effector bindings.
Each new press rotates one step through the native `0, 1, 2, 3, 0` phase
sequence; releasing or holding the button retains the position. The appended
**Effector (Counterclockwise)** buttons rotate in the opposite direction.
Bind these independently for each player. With `input.keyboard=true`, W/E
provide clockwise/counterclockwise fallback input for player 1.

The two existing Effector analog bindings also control this rotary position.
They use geninput's persistent circular position, with each quarter of its
normalized revolution advancing one native phase. Relative-axis sensitivity
remains configurable in the Analogs tab. Moves crossing multiple phases are
queued and exposed one adjacent phase per rendered input frame; repeated
getters cannot consume extra steps. Gameplay encodes phase as `2*right+left`;
the TEST input check reverses those weights, so its clockwise display is
`0, 2, 1, 3, 0`. The game maintains a separate six-position sound-effect
selection (`0` through `5`); the TEST value is not that selection.

The configured **Debug S/W: Auto Play** and **Debug S/W: Disable Menu Timer**
buttons toggle the native software debug parameters `autoplay` and `neverend`
in V4-V8. Press once to enable, release and press again to disable; holding a
button does not repeatedly toggle it. Both start disabled in each new process.
With `input.keyboard=true`, F3 and F4 provide the same two actions as fallback
keys; configured mapper bindings remain available with that fallback disabled.
Other debug parameters retain their native values. These actions do not set
the cabinet's hardware DIP switches or navigate a debug menu. The native
`neverend` parameter is also read by game flows beyond menu countdowns.

V4's main display uses OpenGL. The hook keeps `GetClientRect` reporting the
actual window size, including during focus changes; the shared IIDX D3D9
back-buffer-size override is bypassed for GFDM. This prevents a zero-sized
drawable from leaving the last frame frozen after switching windows.

V4-V8 keep the native DirectSound renderer and game sound-volume settings.
The hook selects libbmsd's native option to skip legacy Windows master-volume
control when the game requests it. Digital and HDMI outputs can lack the
WinMM speaker mixer line that this old setup requires; that failure otherwise
prevents the sound engine from starting. Adjust Windows output/session volume
normally. Existing special rendering options are preserved, and the hook logs
the actual native sound initialization result.

V4 uses the AVS boot implementation shipped with the game, and V5-V8 retain the same seven-argument AVS boot ABI. The package uses
the AVS1002 build family for its named imports; this is an ABI/build selector,
not a claim that every game ships AVS 2.10.2. V5/V6 ship 2.7.1, V7 2.10.2
and V8 2.12.0. The original AVS libraries stay in place, including the separate
`libavs-win32-ea3.dll` required by V7/V8.

V5-V7 error screens require the game's native `../sys/infodisp.exe` companion.
Keep it in the expected sibling directory; a missing executable can leave
the native error screen waiting indefinitely. V8 renders errors internally.
On fresh NVRAM, `2500(1)` is the native initialization confirmation; press
TEST once the error screen appears. A zero-byte `info.bin.new` is normal
after a successful AVS mount and does not establish NVRAM corruption.

The hook also installs the V4 `libdevice` security-status compatibility layer.
The original Gitadora V4 hook reports status `0x101` for the black roundplug
and `0x100` for the white roundplug; V4 rejects a boolean “present” response
(`1`) with `5-1503-0001`. The P3IO emulator still supplies the ROM and signed
EEPROM bytes, while these status values preserve the game-facing contract.
