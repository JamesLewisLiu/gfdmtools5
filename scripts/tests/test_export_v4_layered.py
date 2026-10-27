import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "export-v4-layered.py"
SPEC = importlib.util.spec_from_file_location("export_v4_layered", SCRIPT)
exporter = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(exporter)


def raw_record(crc32, crc16, payload, offset, pack_id=0, digest=None, length=None):
    return ((digest if digest is not None else hashlib.md5(payload).digest())
            + struct.pack("<IHHII", crc32, crc16, pack_id, offset,
                          len(payload) if length is None else length))


def index_bytes(records, pack_count=1):
    return (struct.pack("<IIIHH", exporter.PACKINFO_MAGIC, 16, 16 + 32 * len(records),
                        len(records), pack_count)
            + b"".join(records) + bytes(16 * pack_count))


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.source = self.root / "source" / "data"
        self.output = self.root / "loose" / "data"
        (self.source / "pack" / "d000").mkdir(parents=True)
        self.index = self.source / "pack" / "packinfo.bin"
        self.pack = self.source / "pack" / "d000" / "pack0000.pak"

    def tearDown(self):
        self.temporary.cleanup()

    def fixture(self, payload=b"prefix-first-second-trailer", records=None):
        self.pack.write_bytes(payload)
        if records is None:
            records = [raw_record(0xffffffff, 1, b"first", 7),
                       raw_record(2, 9, b"second", 13),
                       raw_record(2, 3, b"first", 7)]
        self.index.write_bytes(index_bytes(records))
        return records

    @property
    def manifest(self):
        return self.output / "_loose" / "v4" / "manifest.bin"

    def run_export(self):
        return exporter.export_data(self.source, self.output)

    def test_complete_export_deduplicates_and_preserves_raw_records(self):
        records = self.fixture()
        metadata = self.run_export()
        binary = self.manifest.read_bytes()
        self.assertEqual(binary[:8], b"GFDMLFS1")
        self.assertEqual(struct.unpack_from("<II", binary, 8), (1, 3))
        self.assertEqual(binary[16:48], hashlib.sha256(self.index.read_bytes()).digest())
        self.assertEqual(len(binary), 48 + 64 * 3)
        ordered = [records[2], records[1], records[0]]
        for position, raw in enumerate(ordered):
            row = binary[48 + position * 64:48 + (position + 1) * 64]
            self.assertEqual(row[:32], raw)
            digest = row[32:].hex()
            content = (self.output / "_loose" / "objects" / (digest + ".bin")).read_bytes()
            self.assertEqual(hashlib.sha256(content).hexdigest(), digest)
            self.assertEqual(hashlib.md5(content).digest(), raw[:16])
        self.assertEqual(metadata["coverage"], {
            "records": 3, "exported_records": 3, "source_slice_bytes": 16,
            "unique_objects": 2, "unique_object_bytes": 11})
        self.assertEqual((self.output / "pack" / "packinfo.bin").read_bytes(), self.index.read_bytes())
        json_manifest = json.loads(self.manifest.with_suffix(".json").read_text())
        self.assertEqual(json_manifest, metadata)
        self.assertEqual(len(list((self.output / "_loose" / "objects").iterdir())), 2)

    def test_out_of_bounds_does_not_publish_manifest(self):
        self.fixture(b"abc", [raw_record(1, 1, b"abc", 2)])
        with self.assertRaisesRegex(exporter.ExportError, "outside file"):
            self.run_export()
        self.assertFalse(self.manifest.exists())

    def test_short_read_discards_partial_temporary_object(self):
        object_dir = self.root / "objects"
        object_dir.mkdir()
        record = exporter.parse_packinfo(index_bytes([raw_record(1, 1, b"abcdef", 0)]))[0]
        with self.assertRaisesRegex(exporter.ExportError, "short read"):
            exporter.export_slice(io.BytesIO(b"abc"), record, object_dir)
        self.assertEqual(list(object_dir.iterdir()), [])

    def test_large_slice_uses_bounded_chunk_reads(self):
        payload = b"x" * (exporter.CHUNK_SIZE * 2 + 17)
        object_dir = self.root / "objects"
        object_dir.mkdir()
        record = exporter.parse_packinfo(index_bytes([raw_record(1, 1, payload, 0)]))[0]

        class BoundedStream(io.BytesIO):
            calls = 0

            def read(self, size=-1):
                if not 0 <= size <= exporter.CHUNK_SIZE:
                    raise AssertionError("unbounded source read")
                self.calls += 1
                return super().read(size)

        stream = BoundedStream(payload)
        digest = exporter.export_slice(stream, record, object_dir)
        self.assertEqual(stream.calls, 3)
        self.assertEqual((object_dir / (digest + ".bin")).read_bytes(), payload)

    def test_md5_mismatch_does_not_publish_manifest_or_object(self):
        self.fixture(b"abc", [raw_record(1, 1, b"xyz", 0)])
        with self.assertRaisesRegex(exporter.ExportError, "MD5 mismatch"):
            self.run_export()
        self.assertFalse(self.manifest.exists())
        self.assertEqual(list((self.output / "_loose" / "objects").iterdir()), [])

    def test_duplicate_key_rejected_even_if_records_identical(self):
        raw = raw_record(1, 1, b"abc", 0)
        self.fixture(b"abc", [raw, raw])
        with self.assertRaisesRegex(exporter.ExportError, "duplicate resource key"):
            self.run_export()
        self.assertFalse(self.output.exists())

    def test_duplicate_key_with_different_offsets_rejected(self):
        self.fixture(b"abcabc", [raw_record(1, 1, b"abc", 0), raw_record(1, 1, b"abc", 3)])
        with self.assertRaisesRegex(exporter.ExportError, "duplicate resource key"):
            self.run_export()

    def test_resume_reverifies_and_keeps_valid_objects(self):
        self.fixture()
        self.run_export()
        objects = list((self.output / "_loose" / "objects").glob("*.bin"))
        modified = {p.name: p.stat().st_mtime_ns for p in objects}
        previous = self.manifest.read_bytes()
        self.run_export()
        self.assertEqual({p.name: p.stat().st_mtime_ns for p in objects}, modified)
        self.assertEqual(previous, self.manifest.read_bytes())

    def test_resume_corrupt_same_length_object_fails_without_overwriting(self):
        self.fixture()
        self.run_export()
        previous = self.manifest.read_bytes()
        target = self.output / "_loose" / "objects" / (hashlib.sha256(b"first").hexdigest() + ".bin")
        target.write_bytes(b"WRONG")
        with self.assertRaisesRegex(exporter.ExportError, "existing object is corrupt"):
            self.run_export()
        self.assertEqual(target.read_bytes(), b"WRONG")
        self.assertEqual(self.manifest.read_bytes(), previous)
        self.assertEqual(list(target.parent.glob(".slice-*")), [])

    def test_resume_corrupt_different_length_object_fails(self):
        self.fixture()
        self.run_export()
        target = self.output / "_loose" / "objects" / (hashlib.sha256(b"first").hexdigest() + ".bin")
        target.write_bytes(b"truncated")
        with self.assertRaisesRegex(exporter.ExportError, "existing object is corrupt"):
            self.run_export()

    def test_resume_with_different_index_refused(self):
        self.fixture()
        self.run_export()
        self.fixture(b"abc", [raw_record(1, 1, b"abc", 0)])
        with self.assertRaisesRegex(exporter.ExportError, "different input index"):
            self.run_export()

    def test_missing_pack_does_not_publish(self):
        self.fixture()
        self.pack.unlink()
        with self.assertRaises(FileNotFoundError):
            self.run_export()
        self.assertFalse(self.manifest.exists())

    def test_source_pack_change_during_export_does_not_publish(self):
        self.fixture()
        original = exporter.export_slice

        def change_after_slice(*args):
            result = original(*args)
            # Growing the source deterministically changes its fstat identity.
            with self.pack.open("ab") as stream:
                stream.write(b"changed")
            return result

        with mock.patch.object(exporter, "export_slice", side_effect=change_after_slice):
            with self.assertRaisesRegex(exporter.ExportError, "source pack changed"):
                self.run_export()
        self.assertFalse(self.manifest.exists())

    def test_source_index_change_during_export_does_not_publish(self):
        self.fixture()
        original = exporter.export_slice

        def change_after_slice(*args):
            result = original(*args)
            self.index.write_bytes(b"changed index")
            return result

        with mock.patch.object(exporter, "export_slice", side_effect=change_after_slice):
            with self.assertRaisesRegex(exporter.ExportError, "source packinfo changed"):
                self.run_export()
        self.assertFalse(self.manifest.exists())

    def test_header_bounds_and_unsupported_magic_rejected(self):
        good = index_bytes([raw_record(1, 1, b"abc", 0)])
        for bad in [b"short", good[:-1], good + b"extra",
                    bytes(4) + good[4:], good[:4] + struct.pack("<I", 32) + good[8:]]:
            with self.subTest(data=bad[:16]):
                with self.assertRaises(exporter.ExportError):
                    exporter.parse_packinfo(bad)

    def test_pack_id_outside_header_count_rejected(self):
        with self.assertRaisesRegex(exporter.ExportError, "outside declared count"):
            exporter.parse_packinfo(index_bytes([raw_record(1, 1, b"abc", 0, pack_id=1)]))

    def test_source_and_output_tree_overlap_refused(self):
        self.fixture()
        for output in [self.source, self.source / "inside", self.source.parent]:
            with self.subTest(output=output):
                with self.assertRaisesRegex(exporter.ExportError, "separate trees"):
                    exporter.export_data(self.source, output)


if __name__ == "__main__":
    unittest.main()
