#!/usr/bin/env python3
"""Export V4 pack slices to a verified, name-free layered data store.

This format reader is for V4 only. The shared packinfo magic does not detect
game versions; do not point it at V5-V8 data.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile


PACKINFO_MAGIC = 0x20041207
MANIFEST_MAGIC = b"GFDMLFS1"
CHUNK_SIZE = 1024 * 1024
HEADER = struct.Struct("<IIIHH")
LOCATION = struct.Struct("<IHHII")


class ExportError(Exception):
    """The source or existing destination cannot be verified."""


def read_exact(stream, length):
    data = stream.read(length)
    if len(data) != length:
        raise ExportError(f"short read: wanted {length} bytes, got {len(data)}")
    return data


def parse_packinfo(data):
    if len(data) < HEADER.size:
        raise ExportError("truncated packinfo header")
    magic, path_offset, pack_offset, count, pack_count = HEADER.unpack_from(data)
    if magic != PACKINFO_MAGIC:
        raise ExportError(f"unsupported packinfo magic: {magic:#x}; V4 input required")
    if (path_offset != HEADER.size or pack_offset != path_offset + count * 32
            or len(data) != pack_offset + pack_count * 16):
        raise ExportError("invalid packinfo table bounds")
    records = []
    keys = set()
    for index in range(count):
        raw = data[path_offset + index * 32:path_offset + (index + 1) * 32]
        crc32, crc16, pack_id, offset, length = LOCATION.unpack_from(raw, 16)
        key = (crc32, crc16)
        if key in keys:
            raise ExportError(f"duplicate resource key: {crc32:08x}:{crc16:04x}")
        keys.add(key)
        if pack_id >= pack_count:
            raise ExportError(f"pack id {pack_id} outside declared count {pack_count}")
        records.append({"raw": raw, "crc32": crc32, "crc16": crc16,
                        "pack_id": pack_id, "offset": offset, "length": length,
                        "md5": raw[:16].hex()})
    return sorted(records, key=lambda r: (r["crc32"], r["crc16"]))


def pack_relative(pack_id):
    return Path("pack") / f"d{pack_id // 30:03d}" / f"pack{pack_id:04d}.pak"


def verify_object(path, expected_size, expected_sha256):
    sha256 = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        while chunk := stream.read(CHUNK_SIZE):
            size += len(chunk)
            sha256.update(chunk)
    if size != expected_size or sha256.hexdigest() != expected_sha256:
        raise ExportError(f"existing object is corrupt; not overwritten: {path}")


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".export-", delete=False) as f:
            temporary = Path(f.name)
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def export_slice(stream, record, object_dir):
    """Read only the declared raw slice; do not decode or infer filenames."""
    temporary = None
    try:
        md5 = hashlib.md5()
        sha256 = hashlib.sha256()
        stream.seek(record["offset"])
        remaining = record["length"]
        with tempfile.NamedTemporaryFile(dir=object_dir, prefix=".slice-", delete=False) as output:
            temporary = Path(output.name)
            while remaining:
                chunk = read_exact(stream, min(CHUNK_SIZE, remaining))
                md5.update(chunk)
                sha256.update(chunk)
                output.write(chunk)
                remaining -= len(chunk)
            output.flush()
            os.fsync(output.fileno())
        if md5.hexdigest() != record["md5"]:
            raise ExportError(f"MD5 mismatch for {record['crc32']:08x}:{record['crc16']:04x}")
        digest = sha256.hexdigest()
        destination = object_dir / (digest + ".bin")
        if destination.exists():
            verify_object(destination, record["length"], digest)
        else:
            os.replace(temporary, destination)
            temporary = None
        return digest
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def export_data(source_data, output_data):
    source_data = Path(source_data).resolve(strict=True)
    output_data = Path(output_data).resolve()
    if (source_data == output_data or source_data in output_data.parents
            or output_data in source_data.parents):
        raise ExportError("source and output data directories must be separate trees")
    index_path = source_data / "pack" / "packinfo.bin"
    index_data = index_path.read_bytes()
    index_sha256 = hashlib.sha256(index_data).hexdigest()
    records = parse_packinfo(index_data)
    manifest_dir = output_data / "_loose" / "v4"
    object_dir = output_data / "_loose" / "objects"
    # A previous successful export may be resumed only for the identical index.
    old_manifest = manifest_dir / "manifest.bin"
    if old_manifest.exists():
        with old_manifest.open("rb") as stream:
            old_header = read_exact(stream, 48)
        expected_header = MANIFEST_MAGIC + struct.pack("<II", 1, len(records)) + bytes.fromhex(index_sha256)
        if old_header != expected_header:
            raise ExportError("existing manifest belongs to a different input index")
    by_pack = {}
    for record in records:
        by_pack.setdefault(record["pack_id"], []).append(record)
    identities = []
    # Check every slice bound before creating any output object.
    for pack_id, group in sorted(by_pack.items()):
        path = source_data / pack_relative(pack_id)
        stat = path.stat()
        for record in group:
            if record["offset"] + record["length"] > stat.st_size:
                raise ExportError(f"slice outside file: {path} at {record['offset']} + {record['length']}")
        identities.append({"pack_id": pack_id, "relative_path": pack_relative(pack_id).as_posix(),
                           "bytes": stat.st_size, "mtime_ns": stat.st_mtime_ns})
    object_dir.mkdir(parents=True, exist_ok=True)
    for identity in identities:
        path = source_data / identity["relative_path"]
        with path.open("rb") as stream:
            before = os.fstat(stream.fileno())
            if (before.st_size, before.st_mtime_ns) != (identity["bytes"], identity["mtime_ns"]):
                raise ExportError(f"source pack changed during export: {path}")
            for record in sorted(by_pack[identity["pack_id"]], key=lambda r: r["offset"]):
                record["sha256"] = export_slice(stream, record, object_dir)
            after = os.fstat(stream.fileno())
            if (after.st_size, after.st_mtime_ns) != (before.st_size, before.st_mtime_ns):
                raise ExportError(f"source pack changed during export: {path}")
    if index_path.read_bytes() != index_data:
        raise ExportError("source packinfo changed during export")
    unique = {r["sha256"]: r["length"] for r in records}
    metadata = {
        "format": "GFDMLFS1", "manifest_version": 1, "game_version": "V4",
        "source_data": str(source_data), "packinfo_sha256": index_sha256,
        "packinfo_bytes": len(index_data), "input_packs": identities,
        "coverage": {"records": len(records), "exported_records": len(records),
                     "source_slice_bytes": sum(r["length"] for r in records),
                     "unique_objects": len(unique), "unique_object_bytes": sum(unique.values())},
        "records": [{k: v for k, v in r.items() if k != "raw"} for r in records],
    }
    binary = (MANIFEST_MAGIC + struct.pack("<II", 1, len(records)) + bytes.fromhex(index_sha256)
              + b"".join(r["raw"] + bytes.fromhex(r["sha256"]) for r in records))
    # The binary manifest is the publication marker; publish it last.
    atomic_write(output_data / "pack" / "packinfo.bin", index_data)
    atomic_write(manifest_dir / "manifest.json", (json.dumps(metadata, indent=2) + "\n").encode("utf-8"))
    atomic_write(old_manifest, binary)
    return metadata


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-data", required=True, type=Path, help="V4 data directory only")
    parser.add_argument("--output-data", required=True, type=Path, help="separate layered data directory")
    args = parser.parse_args(argv)
    try:
        metadata = export_data(args.source_data, args.output_data)
    except (ExportError, OSError, ValueError) as error:
        print(f"export failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(metadata["coverage"], indent=2))
    print(f"packinfo SHA256: {metadata['packinfo_sha256']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
