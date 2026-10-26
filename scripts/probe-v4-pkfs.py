#!/usr/bin/env python3
"""Probe genuine V4 PKFS handles in an already initialized isolated game.

Requires the optional ``frida`` Python package. This is a manual integration
probe, not a synthetic CI test: no game DLLs or data are bundled. The caller
must select a PID and its isolated runtime directory explicitly. It calls the
original libpackfs exports, bypassing the routing hook, so it tests the native
loose-file handle contract independently of the hook's own implementation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time


PKFS_SHA256 = "59dc206577ed796df75e7a038108667501751929ecd3530ed972bbac2a309c09"
SCRIPT = r"""
'use strict';
const pack = Process.getModuleByName('libpackfs.dll');
function exp(name, ret, args) {
    return new NativeFunction(pack.getExportByName(name), ret, args, 'mscdecl');
}
const open = exp('?pkfs_fs_open@@YAIPBD@Z', 'uint', ['pointer']);
const close = exp('?pkfs_fs_close@@YAHI@Z', 'int', ['uint']);
const read = exp('?pkfs_fs_read@@YAHIPAXH@Z', 'int', ['uint', 'pointer', 'int']);
const stat = exp('?pkfs_fs_fstat@@YAEIPAUT_AVS_FS_STAT@@@Z', 'uchar', ['uint', 'pointer']);
const rewind = exp('?pkfs_fs_rewind@@YAHI@Z', 'int', ['uint']);
const search = exp('?pkfs_search_pathtab@@YAPAUpkfs_pathtab@@IG@Z', 'pointer', ['uint', 'uint16']);
const hashKey = exp('?pkfs_hash_key@@YAIPBD@Z', 'uint', ['pointer']);
const hashSubkey = exp('?pkfs_hash_subkey@@YAGPBD@Z', 'uint16', ['pointer']);
const hddError = exp('?pkfs_is_hdd_error@@YAEXZ', 'uchar', []);
const advapi = Process.getModuleByName('advapi32.dll');
function win(name, ret, args) {
    return new NativeFunction(advapi.getExportByName(name), ret, args, 'stdcall');
}
const acquire = win('CryptAcquireContextA', 'int', ['pointer', 'pointer', 'pointer', 'uint', 'uint']);
const release = win('CryptReleaseContext', 'int', ['pointer', 'uint']);
const createHash = win('CryptCreateHash', 'int', ['pointer', 'uint', 'pointer', 'uint', 'pointer']);
const hashData = win('CryptHashData', 'int', ['pointer', 'pointer', 'uint', 'uint']);
const getHash = win('CryptGetHashParam', 'int', ['pointer', 'uint', 'pointer', 'pointer', 'uint']);
const destroyHash = win('CryptDestroyHash', 'int', ['pointer']);
const buffer = Memory.alloc(65536);
const statBuffer = Memory.alloc(256);
function check(ok, why) { if (!ok) throw new Error(why); }
function hex(bytes) { return Array.from(new Uint8Array(bytes)).map(x => x.toString(16).padStart(2, '0')).join(''); }
function fileStat(fd) {
    statBuffer.writeByteArray(new Uint8Array(256));
    check(stat(fd, statBuffer) !== 0, 'native fstat failed');
    return statBuffer.add(28).readU32();
}
function openObject(path) {
    const fd = open(Memory.allocUtf8String(path));
    check(fd !== 0 && (fd & 0x10000) !== 0 && (fd & 0xffff) < 8,
          'not a V4 native PKFS descriptor: ' + fd);
    return fd;
}
function digest(fd, length, algorithm, outputSize) {
    const providerOut = Memory.alloc(Process.pointerSize);
    const hashOut = Memory.alloc(Process.pointerSize);
    check(acquire(providerOut, NULL, NULL, 24, 0xf0000000), 'CryptAcquireContextA failed');
    const provider = providerOut.readPointer();
    let hash = NULL;
    try {
        check(createHash(provider, algorithm, NULL, 0, hashOut), 'CryptCreateHash failed');
        hash = hashOut.readPointer();
        let total = 0;
        while (total < length) {
            const count = read(fd, buffer, Math.min(65536, length - total));
            check(count > 0 && count <= Math.min(65536, length - total), 'native short/error read at ' + total);
            check(hashData(hash, buffer, count, 0), 'CryptHashData failed');
            total += count;
        }
        check(read(fd, buffer, 1) === 0, 'native read past EOF did not return zero');
        const out = Memory.alloc(outputSize), size = Memory.alloc(4);
        size.writeU32(outputSize);
        check(getHash(hash, 2, out, size, 0) && size.readU32() === outputSize, 'CryptGetHashParam failed');
        return hex(out.readByteArray(outputSize));
    } finally {
        if (!hash.isNull()) destroyHash(hash);
        release(provider, 0);
    }
}
rpc.exports = {
    identity() {
        return {
            arch: Process.arch,
            executable: Process.mainModule.path,
            pkfs: pack.path,
            imports: Process.enumerateModules().flatMap(m =>
                m.enumerateImports().filter(i => i.name === '?pkfs_fs_open@@YAIPBD@Z').map(i => {
                    const target = i.slot ? i.slot.readPointer() : i.address;
                    const owner = target ? Process.findModuleByAddress(target) : null;
                    return {module: m.path, slot: i.slot ? i.slot.toString() : null,
                            target: target ? target.toString() : null,
                            targetModule: owner ? owner.path : null};
                }))
        };
    },
    run(samples) {
        const results = [], before = hddError();
        for (const sample of samples) {
            const objectPath = Memory.allocUtf8String(sample.path);
            const objectCrc32 = hashKey(objectPath);
            const objectCrc16 = hashSubkey(objectPath);
            const record = search(sample.crc32, sample.crc16);
            check(!record.isNull(), 'native index record missing');
            check(hex(record.readByteArray(32)) === sample.record, 'native index differs from manifest');
            check(search(objectCrc32, objectCrc16).isNull(), 'object path collides with packed index');
            const fd = openObject(sample.path);
            const result = {path: sample.path, fd: fd, size: null, sha256: null, md5: null};
            try {
                result.size = fileStat(fd);
                check(result.size === sample.length, 'native size differs from manifest');
                result.sha256 = digest(fd, sample.length, 0x800c, 32);
                check(result.sha256 === sample.sha256, 'native content SHA256 mismatch');
                check(rewind(fd) === 0, 'native rewind did not seek to zero');
                result.md5 = digest(fd, sample.length, 0x8003, 16);
                check(result.md5 === sample.md5, 'native reread MD5 mismatch');
                result.eofAndRewind = true;
            } finally {
                result.close = close(fd);
            }
            check(result.close === 0, 'native loose close failed');
            results.push(result);
        }
        // Two independent descriptors, alternated reads; this does not claim
        // simultaneous multithreaded coverage. Native V4 has only eight slots.
        const pair = samples.find(s => s.length >= 96) || samples[0], a = openObject(pair.path);
        let b = 0, interleaved;
        try {
            b = openObject(pair.path);
            check(a !== b, 'native descriptors were reused while open');
            const n = Math.min(pair.length, 32);
            const a1 = read(a, buffer, n), ah = hex(buffer.readByteArray(Math.max(a1, 0)));
            const b1 = read(b, buffer, n), bh = hex(buffer.readByteArray(Math.max(b1, 0)));
            check(a1 === n && b1 === n && ah === bh && ah === pair.prefix96.slice(0, n * 2),
                  'interleaved descriptors did not start independently');
            check(rewind(a) === 0, 'interleaved rewind failed');
            const a2 = read(a, buffer, n), a2h = hex(buffer.readByteArray(Math.max(a2, 0)));
            check(a2 === n && a2h === ah, 'rewind failed with a second descriptor open');
            const next = Math.min(n, pair.length - n);
            const b2 = read(b, buffer, next), b2h = hex(buffer.readByteArray(Math.max(b2, 0)));
            check(b2 === next && b2h === pair.prefix96.slice(n * 2, (n + next) * 2),
                  'rewinding descriptor a changed descriptor b position');
            interleaved = {a: a, b: b, prefixBytes: n, prefixHex: ah, pass: true};
        } finally {
            if (b) check(close(b) === 0, 'interleaved close b failed');
            check(close(a) === 0, 'interleaved close a failed');
        }
        // Missing-file probing may set native error state; never clear a
        // global error flag to make an integration result appear successful.
        const missing = '/data/_loose/objects/nonexistent-probe-' + Process.id + '.bin';
        const missingFd = open(Memory.allocUtf8String(missing));
        if (missingFd) close(missingFd);
        check(missingFd === 0, 'missing loose object unexpectedly opened');
        return {samples: results, interleaved: interleaved,
                missing: {path: missing, fd: missingFd},
                hddErrorBefore: before, hddErrorAfter: hddError(),
                coverage: 'native exports; streamed contents, EOF, rewind, close, interleaved handles; no simultaneous worker threads'};
    }
};
"""


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--runtime", type=Path, required=True, help="Explicit isolated binbt5 directory")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    runtime = args.runtime.resolve(strict=True)
    if runtime == Path("I:/GFDMV4/binbt5").resolve():
        raise ValueError("Use the isolated unpacked test tree, not the formal V4 runtime")
    data = runtime.parent / "data"
    manifest = data / "_loose/v4/manifest.bin"
    raw = manifest.read_bytes()
    if len(raw) < 48:
        raise ValueError("Truncated manifest")
    magic, version, count, index_sha = struct.unpack_from("<8sII32s", raw)
    if magic != b"GFDMLFS1" or version != 1 or not count or len(raw) != 48 + 64 * count:
        raise ValueError("Invalid manifest header/length")
    if sha256(data / "pack/packinfo.bin") != index_sha.hex():
        raise ValueError("Manifest does not match local packinfo")
    # Select different sizes without relying on filenames or an old filename
    # dictionary: smallest nonempty object, a middle resource, and the largest.
    entries = [raw[48 + i * 64:48 + (i + 1) * 64] for i in range(count)]
    entries.sort(key=lambda row: struct.unpack_from("<I", row, 28)[0])
    positive = [row for row in entries if struct.unpack_from("<I", row, 28)[0] > 0]
    if not positive:
        raise ValueError("No nonempty objects")
    selected = list(dict.fromkeys((positive[0], positive[len(positive) // 2], positive[-1])))
    samples = []
    for row in selected:
        name = row[32:].hex()
        object_file = data / "_loose/objects" / (name + ".bin")
        if sha256(object_file) != name:
            raise ValueError("Local object failed SHA256: " + str(object_file))
        crc32, subkey, _, _, length = struct.unpack_from("<IHHII", row, 16)
        path = "/data/_loose/objects/" + name + ".bin"
        with object_file.open("rb") as stream:
            prefix = stream.read(96).hex()
        samples.append(dict(path=path, record=row[:32].hex(), crc32=crc32,
                            crc16=subkey, length=length, sha256=name,
                            md5=row[:16].hex(), prefix96=prefix))
    import frida
    session = frida.get_local_device().attach(args.pid)
    report = dict(pid=args.pid, runtime=str(runtime), manifest_sha256=sha256(manifest),
                  started=time.time(), pass_=False, script_messages=[])
    script = None

    def on_message(message, data):
        item = dict(time=time.time(), message=message)
        if data is not None:
            item["binary_bytes"] = len(data)
            item["binary_sha256"] = hashlib.sha256(data).hexdigest()
        report["script_messages"].append(item)

    try:
        # V4's own hash exports avoid assuming a subkey polynomial here.
        script = session.create_script(SCRIPT)
        script.on("message", on_message)
        script.load()
        errors = [item["message"] for item in report["script_messages"]
                  if item["message"].get("type") == "error"]
        if errors:
            raise RuntimeError("Frida script load error: " + json.dumps(errors))
        identity = script.exports_sync.identity()
        report["identity"] = identity
        if identity["arch"] != "ia32":
            raise ValueError("Expected native x86 V4")
        if Path(identity["executable"]).resolve() != (runtime / "gdv4.exe").resolve():
            raise ValueError("Selected process is not this isolated V4 runtime")
        pkfs = Path(identity["pkfs"]).resolve()
        if pkfs != (runtime / "libpackfs.dll").resolve() or sha256(pkfs) != PKFS_SHA256:
            raise ValueError("Unexpected libpackfs identity")
        report["pkfs_sha256"] = PKFS_SHA256
        report["result"] = script.exports_sync.run(samples)
        report["pass_"] = True
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        if script is not None:
            script.unload()
        session.detach()
        report["finished"] = time.time()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
