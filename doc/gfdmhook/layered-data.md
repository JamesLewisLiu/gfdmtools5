# V4 layered data export

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

## Native V4 resource routing

Copy the existing V4 program directory and its save directories to the sibling
`binbt5` in the isolated test tree. Use the Actions-built `gfdmhook1.dll` there,
and append `layered.enabled=true` to the mode-specific configuration. The option
defaults to false; enabling it for V5-V8 refuses startup. Original game DLLs
are unchanged. No game DLLs or resources are needed by CI.

The hook resolves `../data` from the executable directory, verifies the known
V4 libpackfs SHA256, matches every manifest record to the original index,
rejects duplicate keys and verifies every object's length, MD5 and SHA256 before
boot. This reads the complete object store at every startup and can take time.
The preflight describes the bytes at startup; live editing or replacing the
store during play is unsupported.

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
from routing. It records sampled full-file digests, EOF, rewind, close, two
interleaved handles and a missing-file result. Interleaving does not establish
simultaneous multithreaded coverage. Run the probe on a disposable test process;
the failed-open check does not clear any resulting native error flag.
