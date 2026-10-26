# Layered data export and shared V4-V8 routing

The exporter in `scripts/export-v4-layered.py` reads the original V4
`data/pack/packinfo.bin` and exports every indexed resource as an unchanged raw
slice of its source PAK. It does not decode textures, sounds or movies, recover
resource names, use a filename dictionary, or invoke an existing unpacker.
This is V4-only work. The `0x20041207` index magic is shared by later releases
and does **not** automatically identify V4; supply V4 data explicitly.

Run from a Python 3.8 or newer installation with the game stopped:

```powershell
python scripts/export-v4-layered.py --source-data I:/GFDMV4/data --output-data I:/GFDMV4-unpacked-test/data
```

The output tree is separate from the input. The exporter never modifies the
source, copies game executables, or creates a launcher. It produces:

```text
data/
  pack/packinfo.bin                 # exact original index bytes
  _loose/
    objects/<sha256>.bin            # raw resource slice, lowercase SHA256 name
    v4/manifest.bin                 # runtime manifest; published last
    v4/manifest.json                # input identities and coverage details
```

Source indices have a 16-byte little-endian `<IIIHH>` header followed by
32-byte resource records and 16-byte pack records. Each resource record is
MD5 bytes (16), CRC32 (`u32`), CRC16 (`u16`), pack ID (`u16`), offset (`u32`),
and length (`u32`). Pack ID `n` selects
`pack/d{n // 30:03d}/pack{n:04d}.pak`. The exporter validates table bounds,
declared pack IDs, unique `(CRC32, CRC16)` keys, and every slice's file bounds.
Duplicate keys are errors even when their records match.

The source PAK files are opened one at a time. Every slice is read in chunks
of at most 1 MiB, with exact-length reads and MD5 verification. SHA256 determines
the output object's identity; identical slices become one object regardless
of resource key. The source pack size/time and final index contents are
rechecked before a successful publication. A missing pack, short read, changed
input, or wrong MD5 fails the export.

Running the same command resumes an interrupted export. Every encountered
existing object is read again and checked against its expected length and
SHA256. A corrupt object causes failure and is left untouched for diagnosis;
it is never silently replaced. A previously published manifest must identify
the same source index. Unreferenced objects from an interrupted export are
harmless and are not deleted. Export failure can leave verified objects behind
but does not publish a new binary manifest. An older successful manifest may
remain; an export failure does not certify it or repair corrupted objects.

After all resources pass verification, temporary files are flushed and replaced
atomically for the copied index, JSON manifest, and binary manifest, in that
order. Each file replacement is atomic; the three replacements are not a single
filesystem transaction. The binary manifest is the final publication marker.

## Binary manifest, version 1

All integers are little-endian. The header is exactly 48 bytes:

| Offset | Size | Value |
| --- | ---: | --- |
| 0 | 8 | ASCII `GFDMLFS1` |
| 8 | 4 | Format version, `1` |
| 12 | 4 | Resource record count |
| 16 | 32 | SHA256 of the entire original `packinfo.bin` |

Each following record is exactly 64 bytes: the complete original 32-byte
resource record, then 32 raw bytes of the object's SHA256. Records are sorted
by unsigned CRC32, then unsigned CRC16, both ascending. No names are needed.
The expected file size is `48 + count * 64`.

`manifest.json` records the V4 designation, resolved input directory, index
identity, source pack identities (relative path, size and modification time),
full per-resource MD5/location/SHA256, and complete coverage/object totals.
It does not contain guessed resource names. Its counts distinguish indexed
slice bytes from unique object bytes; neither value is the total PAK directory
size, because padding and unindexed bytes are not exported.

Run fixture tests without using game data:

```powershell
python -m unittest discover -s scripts/tests -v
```

Tests cover complete export and deduplication, unsigned key ordering, exact
manifest/index preservation, out-of-bounds slices, short reads, wrong MD5,
duplicate keys, missing packs, malformed index headers, bad pack IDs, source
and output overlap, successful resume, changed-index rejection, and corrupt
existing objects of both matching and different length.

## Native V4-V8 resource routing

Use five original program directories beside a single shared data directory,
install the Actions-built `gfdmhook1.dll`, and set `layered.enabled=true` in each
mode-specific configuration. The option defaults to false for ordinary packed
installs. Original game DLLs are unchanged. No game DLLs or resources are needed
by CI. The existing `export-v4-layered.py` remains V4-only; runtime support for
later versions does not make that exporter a V5-V8 decoder.

```text
GFDM/
  V4/                              # gdv4.exe, original DLLs, config, DEV
  V5/                              # gdv5.exe and its own config/DEV
  V6/
  V7/
  V8/
  data/
    _loose/
      objects/<sha256>.bin         # decoded original bytes; one content copy
      v4/manifest.bin
      v4/packinfo.bin              # exact original V4 index
      v4/finfolist.bin
      v5/manifest.bin              # likewise for V5, V6, V7, V8
      v5/packinfo.bin
      v5/finfolist.bin
```

Every version selects its own metadata below `_loose/vN`. Only original logical
requests for `/data/pack/packinfo.bin` and `/data/finfolist.bin` are redirected to
the selected metadata. The exact metadata path match also accepts Windows
backslashes, case differences and an omitted leading slash; unrelated resource
paths retain their native behavior. All AVS consumers receive this routing,
including DLLs handled by the existing late-load hook pass. The libpackfs import
has a separate wrapper that also checks the mounted index and rejects PAK reads.
Other metadata consumers cannot acknowledge or invalidate native PKFS boot.
Both wrappers call the real AVS export directly, so repeated hook application
does not create a chain of wrappers.

The old V4 tree remains supported: if neither versioned metadata file is present,
V4 can still use `data/pack/packinfo.bin` and `data/finfolist.bin` alongside
`data/_loose/v4/manifest.bin`. A partially installed versioned pair fails; V5-V8
never fall back to root metadata. Common content does not imply common original
paths: the version manifests can select different objects for the same CRC key.
Keep configuration, NVRAM, RAW and caches in their own version/mode directories.

V4's native boot builds its AVS property tree in a fixed 1512-byte buffer.
Replacing short save paths with longer absolute paths can exhaust that tree,
silently omit the RAW mount and repeatedly trigger BACKUP ERROR when hitchart
loads from the wrong directory. The V4 hook now deep-copies the complete tree
into a separate 64 KiB buffer, checks both device values after replacement and
passes that copy to AVS. AVS copies its settings and mount paths before return;
the temporary tree is then destroyed and its buffer freed. Original settings
and error handling remain in place. This change is confined to V4.

The hook resolves `../data` from the executable directory. It checks the current
version's exact known libpackfs, packinfo and finfolist SHA256 values; V4-V7 use
index magic `0x20041207`, while V8 uses `0x20110201`. Unknown or misplaced modules
and metadata fail rather than selecting another version's files. Each manifest
header is additionally bound to the full original index SHA256. The hook matches
every manifest record to that index and rejects duplicate keys. The first use
verifies every object's length, MD5 and SHA256 before boot. Later starts can
reuse the local NTFS verification cache described below.
The preflight describes the bytes at startup; live editing or replacing the
store during play is unsupported.

### Verification cache

`data/_loose/verified-cache.bin` is shared across versions and GF/DM. It records
only successfully hashed contents. A hit requires the same expected SHA256,
MD5 and length, volume/file ID, creation time, write time, NTFS ChangeTime and
attributes. Each check opens the current file with sharing that excludes
writers/deleters, so the identity and any needed content read use the same
locked handle. Access time is deliberately excluded. A same-length overwrite
with its ordinary write time restored is still rehashed because ChangeTime
changes. Missing and invalid objects still refuse startup.

Module/index/finfolist checks and manifest-to-index binding are always checked
in full. Cache records have a payload SHA256 to detect accidental cache damage;
they are not authenticated against someone deliberately forging both files
and cache metadata. The store and cache must not be edited during play. Only
NTFS identities with the required handle APIs are eligible; unsupported systems
still hash the contents. Missing, damaged or unwritable cache files merely
make startup slower. Updates use a flushed temporary file and atomic rename;
concurrent launches can lose some cache hits but cannot bypass verification.

For a manual complete scan of the selected version, use:

```
gamestart-v7-dm.bat -p layered.full_verify=true
```

Or set `layered.full_verify=true` in that mode's config, then restore it to
`false` for normal cached starts. Full scans hash each distinct object once;
multiple index records referencing the same object need no duplicate read.
Deleting just `verified-cache.bin` also triggers fresh validation. Logs report
preflight milliseconds, cache hits, hashed objects and bytes; game initialization
after the preflight is measured separately.

The native mounted index is compared byte-for-byte before PKFS initializes.
For each original request, the hook uses the native `pkfs_get_pathtab` lookup,
maps that exact record to the SHA256 object, checks that the object path does
not collide with the packed index, and calls the original `pkfs_fs_open` with
the object path. It returns the original PKFS descriptor. Native read, fstat,
rewind and close are untouched. Genuine non-indexed loose resources and writes
retain their native behavior. libpackfs's AVS import refuses any `.pak` open,
including an accidental attempt to return to the original packed data.

Use a test `data` tree containing **no PAK files**. Keep `packinfo.bin` as lookup
metadata. The startup log records complete object preflight and mounted-index
validation; resource logs record requested names, keys, objects and native
descriptors. A successful export proves all indexed contents were extracted,
not that every song or game path was exercised. Runtime logs establish only
the requests actually observed.

The original `finfolist.bin` is preserved as metadata, not rewritten to hide
missing PAK files. Its native TEST **ROM CHECK** checks original PAK and program
file sizes/MD5 values. A store containing no PAKs cannot pass that original PAK
check, even when all shared content objects are valid. The full object preflight
checks the unpacked resources against their original per-resource MD5 and SHA256;
it is distinct from the original ROM CHECK. No checksum result or error flag is
forced to success.

The V7/V8 open function normalizes names before its exported path lookup;
the hook reproduces that exact step before looking up the original request.
It adds a leading slash for `data/`, and lowercases ASCII only when the resulting
prefix is `/data/aep` (not `/data/product/aep`). Its 512-byte formatting buffer
preserves the original truncation behavior. No backslash or dot-segment cleanup
is added. V4-V6 keep their original direct lookup semantics.

This implementation independently adapts the original-handle routing idea
from [ifs_layeredfs](https://github.com/mon/ifs_layeredfs). It does not copy or
build its code, use its extractor, replace game boot DLLs, clear global HDD
errors, or include its texture/proxy layers.

An optional manual integration probe requires Python 3.11+ and `frida`. After
the isolated V4 process has initialized, explicitly select its PID:

```powershell
python scripts/probe-v4-pkfs.py --pid PID --runtime I:/GFDMV4-unpacked-test/binbt5 --output native-probe.json
```

It calls the original exports to test native loose-file descriptors separately
from routing, on the registered rendering thread at `SwapBuffers` return.
AVS operations must not be called directly from Frida's RPC worker thread.
It records sampled full-file digests, EOF, rewind, close, two
interleaved handles and a missing-file result. Interleaving does not establish
simultaneous multithreaded coverage. Run the probe on a disposable test process;
the failed-open check does not clear any resulting native error flag.
