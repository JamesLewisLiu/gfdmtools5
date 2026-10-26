# Fresh backup initialization

The reported scenario is a fresh machine with fresh save data. V4 displays
`2500(1)` after creating defaults, then returns to that notice after entering
and leaving TEST. Restarting the process allows it to continue. This is distinct
from the ALL FACTORY SETTINGS command during normal operation.

Official IDA analysis of the original 32-bit modules identified the following
initialization consumers. Addresses are virtual addresses at image base
`0x10000000` and are evidence only; the hook uses named imports and exports.

| Game | Initialization function | `NVRAM_CLEAR` after TEST acknowledgement |
| --- | --- | --- |
| V4 | `0x10087a70`, state 6 | Sets 1 again |
| V5 | `0x100a3020`, state 6 | Sets 0 |
| V6 | `0x100ad660`, state 6 | Sets 0 |
| V7 | `0x1009c570`, state 6 | Sets 0 |
| V8 | `0x1009ca40`, state 6 | Sets 0 |

In V4, the sole `game.dll` write of `NVRAM_CLEAR=1` follows
`error_info_window_call_attendant(2500, 1)` returning TEST acknowledgement
(bit 8). The hook changes that write to 0 when all three native checks pass:

- `nvram_is_valid()` returns nonzero.
- `sys_setting_get_param("NVRAM_ERROR")` returns zero.
- `game_util_get_nvram_save_err_count()` returns zero.

The setter is patched only in V4 `game.dll`'s `libsystem.dll` import table.
The `libshare-pj.dll` initializer still creates defaults, saves them and sets
the first-start notice. The hook retains that notice until TEST acknowledgement,
preserves the original setter's return value and makes no save-data writes.
Missing exports or failed checks retain native behavior. V5-V8 do not install
this hook. The separate `2500(0)` error branch is unchanged.

V4's `libshare-pj.dll` exports `nvram_is_valid` at `0x10001410`, with a cdecl
integer result and no arguments. It validates config, income, hitchart and
launcher network data. Its save-error getter at `0x10007950` returns a cdecl
16-bit unsigned count. `libsystem.dll`'s get/set functions at `0x100045f0` and
`0x10004610` are cdecl integer functions; the setter accepts a name and integer.

The original boot module constructs the AVS configuration directly in memory.
The removed `-I` and `-b` launcher arguments and virtual XML files were not
consumed by this boot path. Launchers now create only `DEV` and `DEV/GF` or
`DEV/DM`; the existing storage hook and native boot create the child paths.

## Verification

`gfdm-backup-test` covers first-start notice retention, TEST acknowledgement,
re-entry without restarting, native error guards, unchanged `2500(0)`, setter
return values, missing modules/exports and V4-only patch scope.

An additional local regression mapped the original V4 PE without resolving
dependencies or running DllMain. It executed the actual state-6 machine code
with controlled imported providers, first reproducing the original repeat,
then applying the production wrapper through the shared IAT hook implementation.
It verified successful acknowledgement, repeat patching, all three failure guards
and the untouched `2500(0)` branch. No installed game data was changed.
These checks do not replace a complete fresh-machine startup/TEST UI regression.

Analyzed game.dll SHA256 values:

```text
V4 8cfdcf100530e1b9d8592cc4404d705b48ece5fd631e32965ec66a40c9eed389
V5 1dd799d0283363e3887e62ed10d6add9da15d17b79ee093d29b420e22229b398
V6 c2dfccfbbfc56cf4aceee84e10bd5522473275ed36668b2ae1f742b1b500f5b8
V7 110112ff4c82d8f1d4958bee2d8ac0ecbb6824790d79524a4925abc29759397e
V8 91867e4e23fe6f06d77a87840c2559d7e2272ffbc5339f9ab4da6302dc9f3cf8
V4 libshare-pj e355693bf2341ae3dd69b11911abeec6208e1afe5eb6e36b3e11c484af2c9918
V4 libsystem e82ebe95bc1f2581abd93bdf2997716ea5f6ca119b614e2b6dae3f0a966225d5
```
