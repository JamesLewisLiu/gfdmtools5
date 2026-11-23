2026-11-15 Keep the display and system awake during GFDM play

- Hold a process-owned Windows power request from V4-V8 GF/DM initialization
  through process exit, including controller-only input, TEST and autoplay.
- Release the kernel object on DLL detach; preserve system power settings and
  explicit user sleep actions. Log unavailable/failed APIs without stopping play.
- Test paired requests, partial failure cleanup, idempotence, and real Windows
  request lifetime across the creating thread's exit.

2026-11-15 Add persistent timer freeze and native song unlock settings

- New gfdm-offline-play branch from gfdm/08f5232.
- GFDM settings page saves game.freeze_timer and game.unlock_music per version/mode;
  both default off and require a game restart.
- Reuse native neverend/all_music indices 1/3; preserve Auto Play and the previous
  all_music result when unlock is off. Song-list reads do not poll input devices.
- Extend debug/config tests for independent settings, timer binding interactions,
  preservation and duplicate removal. Actual per-version play remains pending.

# GFDM update history

2026-11-23: Add independent data_mods, verified shared-data and full-verification controls to config.exe. Keep mods enabled by default for existing configurations and bypass mod hooks when explicitly disabled. Create EXE-local data_mods on an enabled save. Replace per-control restart labels with one page-wide reminder, including CardIO. Preserve existing comments and settings during saves.

2026-11-23: Extend EXE-local data_mods with ifs_layeredfs GFDM texbin PNG packing and ordered .merged.xml appends, including native V4-V8 binary XML decoding. Keep whole-file priority, ordinary/shared data independence and original resources intact. Cache generated files locally with build/input keys and checksums, refresh on mod changes, and fall back on invalid input. Link the converter into the existing x86 hook and include third-party notices in all packages. Pixel/priority/cache/concurrency tests and five genuine AVS binary XML cases pass; real game rendering and user mod packs remain to be tested.

2026-11-23: Add EXE-local data_mods/<mod>/ whole-file overrides for V4-V8 GF/DM, independent of layered.enabled and shared data. Check overrides before object routing, preserve packed/shared fallback, support dynamic file add/change/removal and per-file alphabetical priority, and redirect read-side stat/media path conversion consistently. Keep saves and verified store metadata separate. Five genuine AVS isolated mount/read/reopen/stat/conversion cases pass; gameplay validation remains pending. Texture generation and XML merging are not included.

2026-11-22: Make normal hook console output concise: keep startup/status summaries and warnings/errors, suppress per-input/frame/object details and native AVS info/debug traffic before output locking/debugger/console writes. Filter hook miscellaneous logs before formatting. Enable full synchronous diagnostics only with -Y, retaining bare -Y log.log and explicit paths, including -D forwarding. Gameplay timing improvement still requires physical comparison.

2026-11-17: Reduce MIDI timing jitter with QPC pulse deadlines and a balanced 1 ms timer-resolution request after a port starts. Let a new runtime strike end the preceding pulse early while retaining the separate low phase, isolated-hit detection width and binding capture timing. Preserve saved mappings, GF holds, CC/pitch and asynchronous driver lifecycle. Physical V6 timing/judgement comparison remains pending.

2026-11-15: Extend private LAN compatibility from V6 to V4/V5/V7/V8 with separately verified PE/export/code profiles for all ten original modules. Preserve each version's native network checks (V4 has no separate APIPA rejection) and real EA3 errors. The preceding V6-only fix was inactive in V4; a reported V4 5-2002-0000 still requires its actual startup log to diagnose. All five original-function isolation suites and relocated patcher tests pass; remote gameplay remains unverified.

2026-11-15: Allow V6 clients on 192.168/16 LANs in native DHCP mode and preserve original EA3 connection errors instead of replacing them with 2000(0). Apply two PE/export/code-verified in-memory branch corrections before native boot, including late-loaded modules. Retain APIPA, gateway and netmask checks; leave other versions and unknown revisions unchanged. No IP, server, configuration or original DLL file changes. Native function emulation and patch guards are tested; remote-machine gameplay acceptance remains pending.

2026-11-14: Route new GF CardIO taps to P1 with NumLock off or P2 with NumLock on, matching keypad multiplexing. Keep each active card attached to its selected player until native eject; preserve DM and file-card routing. The user confirmed V4 title login and repeated CHECK exit/re-entry with the preceding presence repair; NumLock routing awaits physical acceptance.

2026-11-14: Retain CardIO card presence and UID/type until the game explicitly ejects the card. A finished input pulse or successful UID read no longer cancels V4 title-to-entry card login. Drain additional taps without replacing an active card, preserve repeated CHECK exit/re-entry, and keep its native green OK page until START exits. This supersedes the automatic snapshot release in the previous update; physical V4 regression is pending.

2026-11-13: Release consumed CardIO tap snapshots when the input pulse ends so IC CARD CHECK can leave OK and accept another card. Retain unread UIDs and legacy file-card eject behavior. Re-arm identical UID reports after a one-second report-free gap for firmware without removal reports, preserve held-report deduplication, and make raw sensors and repeated eject completion consistent. Update hook and EAMIO together; physical V4 regression is pending.

2026-11-06: Separate DM pad/START bank 0 from menu navigation bank 1 so High/Low Tom no longer trigger Menu Left/Right. Preserve action indices and saved bindings. Correct Naive navigation scan codes for HID usages and displayed names on layouts that return keypad scans for dedicated navigation keys. Use NumLock on for distinct Naive keypad digits, or Raw for physical keypad navigation with NumLock off.

2026-11-12: Extend the EA3 XRPC workspace minimum to V4-V6, whose EA3 imports come from libavs-win32.dll rather than V7's separate EA3 library. Preserve larger configured sizes, the original configuration tree and V8 behavior. Native V4-V6 parser tests reproduce failure at 4 KiB and success at 8/64 KiB on the current CORE service list. Card login and song saving remain separate game validation items.

2026-11-06: Raise V7's EA3 XRPC workspace minimum from 4 KiB to 64 KiB through a deep configuration copy. This fixes startup 5-2002-0000 when Asphyxia CORE's service discovery response exceeds the native property arena. Preserve larger configured sizes and other versions' native EA3 configuration. Cover initial and late-loaded imports and add allocation/clone/create/readback cleanup tests. Local GF V7 startup reaches attract mode; card login, song saving and shared XG1 Classic data still require game validation.

2026-11-03: Add opt-in CardIO HID input through stock EAMIO with a GFDM configuration checkbox and cardio.enabled=false by default. Receive and validate eight-byte card reports asynchronously, preserve technology/UID and existing file/keypad fallback, and support per-player queued taps and reconnects. Add native simulated protocol/lifecycle tests; physical reader and game login/save validation remain pending.

2026-10-29: Unify packed and shared-data support in the gfdm x86 build. Retain MIDI, logging and Pick A/B fixes, explicitly disable layered routing in default configurations, and retain the common V4 AVS save-path expansion. Run fourteen native tests and seventeen exporter fixtures in one workflow, including disabled resource-path instrumentation.

2026-10-29: Fix both GF Pick A/B controller bindings to drive the same native Pick input for each player. Stop Pick B from triggering Wail, retain existing mapping indices, and preserve native held/edge semantics without synthetic repeats.

2026-10-29: Show hook and AVS logs in the inject console by default. Create a combined inject/hook log file only with -Y: default to log.log when no filename is supplied, accept quoted custom paths, and retain console output without duplicating debugger messages.

2026-10-28: Add native MIDI 1.0 input to GF/DM bindings and runtime. Queue short notes and repeated drum hits, retain held notes for GF, expose CC pedal buttons/values and pitch bend, and scan/reconnect devices asynchronously. Preserve existing mapping files and keyboard/HID backends.

2026-10-28: Preserve all existing configuration comments and formatting when saving, including older generated descriptions, CRLF and long lines. Consolidate duplicate editable keys without changing surrounding comments or layered-data settings. Add descriptions only for new files or missing options.

2026-10-27: Remove separate Borderless modes after reported song-video failures. Keep modes 0/1/2, default to 4:3 fullscreen, and migrate saved 3/4 values to 1/2. Clarify the adapter override for multi-adapter startup failures. Speed up config.exe by using cached Windows device names instead of blocking USB product/manufacturer queries, without changing shared geninput sources or input identities.

2026-10-27: Expand gfx.fullscreen to five numbered display modes: windowed, 4:3/stretched native fullscreen, and 4:3/stretched Borderless. Keep boolean compatibility and per-version/GF/DM saving. Add GFDM-scoped OpenGL viewport/scissor mapping, state-preserving black bars, context/framebuffer guards and window/DPI lifecycle handling.

2026-10-27: Add a per-version/mode Fullscreen checkbox and gfx.fullscreen setting. Use the native V4-V8 OpenGL fullscreen lifecycle or a fixed-size framed window; unsupported fullscreen modes fall back to a window. Restart the game to apply changes.

2026-10-26: Fix the V4 fresh-data initialization notice repeating after TEST acknowledgement, with native validity/save-error guards. Keep V5-V8 behavior unchanged. Launchers create only DEV and the selected GF/DM root; remove unused XML arguments and the virtual-file implementation that native boot does not consume.

2026-10-25: Embed boot and EA3 identity XML in the x86 hook, fix V7 config selection and comment persistence, and reorder GF controls without changing stored action indices.

2026-10-25: Place CLVSD.ax beside inject.exe, remove unused generic configurations, and document the GF/DM configurations.



2026-10-25: Extract an x86-only standalone build, preserve shared sources unchanged, and add GitHub Actions.



## Preserved GFDM commits



2026-09-13 Initial (original 859af5d, standalone e781fc9)

2026-09-13 fix: refresh GFDM build image (original 510ac82, standalone a82e8a1)

2026-09-13 fix: link p3io into GFDM hook (original cd05833, standalone c7dc590)

2026-09-13 fix: stabilize GFDM network and security plugs (original 8a06990, standalone 4dc2a3f)

2026-09-13 fix: match Gitadora V4 security plug status API (original 4117de6, standalone 0b2c741)

2026-09-13 feat: add GFDM input mapper and correct game codes (original 7f194e0, standalone b60c94e)

2026-09-13 feat: add GFDM network settings page (original 1bb3d2a, standalone 693d725)

2026-09-13 fix: include common controls for GFDM config page (original fe20ca3, standalone 5738f67)

2026-09-13 fix: enable writable V4 backup storage (original 35287ef, standalone f83a69b)

2026-09-13 fix: provide V4 input history and boot identity (original af8acd3, standalone 0c17f4f)

2026-09-13 fix: expose V4 mapped device input (original 54a1545, standalone 8e437fb)

2026-09-13 fix: hook late-loaded V4 device clients (original 2b24f8d, standalone 7339214)

2026-09-13 fix: force V4 test history edge (original b5d3f66, standalone 01158ee)

2026-09-13 fix: keep GFDM mapper beside game package (original c1f2c79, standalone 6a662fa)

2026-09-13 fix: redirect V4 AVS storage paths (original 193851e, standalone 8e583c5)

2026-09-13 fix: build GFDM hook with AVS storage redirect (original 8b16b0e, standalone 90d530e)

2026-09-13 fix: use named AVS imports for GFDM V4 (original 8f1c045, standalone bd42ae5)

2026-09-13 fix: emulate idle V4 card unit (original d376410, standalone 5af7237)

2026-09-13 fix: fully emulate V4 card unit (original 0f9f9e1, standalone 335edc9)

2026-09-13 fix: guard invalid V4 movie decoder state (original e557edf, standalone 9c250da)

2026-09-13 fix: apply movie guard to game imports (original b8d5355, standalone 5980a13)

2026-09-13 fix: guard system movie readiness import (original 719e159, standalone 2a0c953)

2026-09-13 gfdm: map V4 controls and debug inputs (original 373f58b, standalone 20e2a06)

2026-09-13 geninput: use AppData mapper files first (original 9fb07d0, standalone ea030dd)

2026-09-13 gfdm: expose hook diagnostics and recover keyboard mappings (original 27132a6, standalone 1728229)

2026-09-13 geninput: fall back to configured keyboard usages (original acd490d, standalone b0827e0)

2026-09-13 gfdm: persist hook diagnostics and mapper bindings (original 285f576, standalone c8b2a70)

2026-09-13 gfdm: include string formatting declarations (original 6bb82dc, standalone a6cd08e)

2026-09-13 gfdm: keep diagnostics independent of mapper internals (original 3b4cdd6, standalone b6e87d9)

2026-09-14 gfdm: detect quoted GFDM launch selector (original 3320738, standalone 9c4b7c8)

2026-09-14 gfdm: add frame pacing coin card reader and keypad support (original bedfa77, standalone dfd7ff1)

2026-09-14 gfdm: keep V4 cardunit state machine compatible (original 13a8042, standalone a5d040b)

2026-09-14 gfdm: report card keypad edges with V4 codes (original bdcc4ba, standalone 4a1bfe4)

2026-09-14 gfdm: align cardunit polling and reduce test menu log stalls (original 8f3155d, standalone 712255d)

2026-09-14 gfdm: preserve legacy bindings and remove unsafe key fallback (original b45d182, standalone 5c4405a)

2026-09-15 gfdm: route V4 coin history and limit OpenGL frames (original 63feb2d, standalone 4e8c56c)

2026-09-15 gfdm: map GF effector analog directions (original 314d7b1, standalone 22a5a59)

2026-09-16 gfdm: preserve all GF effector states (original 89c2378, standalone 7c49853)

2026-09-17 gfdm: fix V4 card reader state machine (original 6ad9311, standalone 314d699)

2026-09-19 gfdm: keep card backend alive across test mode (original aef55d1, standalone ae063d9)

2026-10-20 Add device-independent Naive keyboard bindings to geninput (original cd7528f, standalone e1999a6)

2026-10-20 Exclude NumLock from Naive capture like spice2x (original 21b55a4, standalone 0574ea6)

2026-10-20 Keep Naive checks enabled and link HID test dependencies (original 855c16a, standalone 0cbf03c)

2026-10-20 Use flat object paths for Naive tests in existing build rules (original fe1838f, standalone 5132910)

2026-10-20 Allow Naive keyboard input in foreground and background (original 695d562, standalone 9f96a9e)

2026-10-20 Fix GFDM OpenGL freezing after window focus changes (original f042a85, standalone f6531fb)

2026-10-20 Fix GFDM BIOS date text when legacy registry value is absent (original 64db840, standalone 11d4fd5)

2026-10-20 Use configured EAMID for GFDM white roundplug ROM (original 7bfe557, standalone e3ab6a3)

2026-10-20 Use configured PCBID for GFDM system and EA3 identity (original 309cc26, standalone be5368f)

2026-10-20 Support classic GFDM V5 through V8 with shared version profiles (original ea3b643, standalone 75f0531)

2026-10-20 Decode legacy Shift-JIS XML before package validation (original e2930cf, standalone 88f96a4)

2026-10-21 Keep adapter override active for every GFDM profile and late DLL (original 2825f1a, standalone 0581865)

2026-10-21 Select the native V8 roundplug signing key by game profile (original 93671d4, standalone 7d1b90f)

2026-10-21 Keep native GFDM save and cache paths beside the game executable (original b905c20, standalone 954525a)

2026-10-21 Consume GFDM coin events once instead of replaying cumulative stock (original dd78382, standalone b85f7fc)

2026-10-22 Connect GFDM debug buttons to native autoplay and neverend parameters (original acbed3e, standalone a1e3e84)

2026-10-22 gfdm: skip obsolete Windows mixer setup for native audio (original 8382e0a, standalone 6f4cffc)

2026-10-22 Fix GFDM effector rotary phase and add reverse bindings (original 7122728, standalone a8ed566)

2026-10-22 Correct GFDM gameplay effector bit order and verify six presets (original 0085f7c, standalone a9c9d2d)

2026-10-23 Add GFDM keyboard fallback switch and default to configured IO (original 6533170, standalone bd62b68)

2026-10-23 Load GFDM video decoder from executable directory without COM registration (original 314c5f5, standalone 374fee2)

2026-10-24 Fix GFDM JAMMA polling starvation (original f6842ba, standalone 8592da1)

2026-10-24 Separate GFDM storage directories by GF and DM mode (original 11ddee0, standalone 96a8c32)

2026-10-24 Bundle version-matched CLVSD decoders (original 5914d4a, standalone 1763a6f)
