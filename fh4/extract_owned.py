#!/usr/bin/env python3
"""Bounded extraction of the locally licensed FH4 bundle, without a second bundle.

Format reference: tuxuser/eappx-rs (header, blockmap and crypto modules).
Reads CIK locally; checks ciphertext block hashes and plaintext file hashes.
Default only plans; --apply extracts, --probe tests one encrypted DLL to memory.
"""
import argparse
import base64
import ctypes
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import shutil
import struct
import time
import xml.etree.ElementTree as ET
import zlib

BLOCK = 65536
RESERVE = 25 * 2**30


def exact(stream, count):
    data = stream.read(count)
    if len(data) != count:
        raise ValueError('Truncated package')
    return data


def header(stream, base, limit):
    stream.seek(base)
    data = exact(stream, 128)
    if data[:4] not in (b'EXBH', b'EXPH'):
        raise ValueError('Unexpected package format')
    offset, length = struct.unpack_from('<QQ', data, 14)
    if length % 40 or length > 40 * 100000 or offset + length > limit:
        raise ValueError('Invalid footer bounds')
    if struct.unpack_from('<H', data, 86)[0] != 1:
        raise ValueError('Only a single owned content key is supported')
    stream.seek(base + offset)
    rows = {}
    for _ in range(length // 40):
        magic, size, key, compression, index, start, plain, stored = struct.unpack('<HHHHQQQQ', exact(stream, 40))
        if size != 40 or compression not in (0, 1) or start + stored > limit or index in rows:
            raise ValueError('Invalid package file record')
        rows[index] = (key, compression, start, plain, stored)
    return data, rows


def chunks(stream, base, row, wanted):
    _, compression, start, _, stored = row
    stream.seek(base + start)
    remaining = stored
    dec = zlib.decompressobj(-15) if compression else None
    pending = b''
    produced = 0
    while produced < wanted:
        take = min(BLOCK, wanted - produced)
        while len(pending) < take and remaining:
            raw = exact(stream, min(BLOCK, remaining))
            remaining -= len(raw)
            pending += dec.decompress(raw, wanted - produced - len(pending) + 1) if dec else raw
            if dec and dec.unconsumed_tail:
                raise ValueError('Decompressed data exceeds declared length')
        if len(pending) < take:
            raise ValueError('Payload shorter than declared length')
        yield pending[:take]
        pending = pending[take:]
        produced += take
    if pending:
        raise ValueError('Payload longer than declared length')


def safe_parts(name):
    path = PureWindowsPath(name)
    if path.is_absolute() or path.drive or '..' in path.parts or not path.parts or ':' in name:
        raise ValueError('Unsafe package path')
    return path.parts


def tweak(name, family):
    data = ('\\' + name.lstrip('\\') + family.lower()).encode('utf-16le')
    words = struct.unpack('<QQQQ', hashlib.sha256(data).digest())
    return words[0] ^ words[1] ^ words[2] ^ words[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--probe', action='store_true')
    parser.add_argument('--resume', action='store_true', help='Validate existing output files before skipping them')
    parser.add_argument('--crypto-library', type=Path)
    args = parser.parse_args()
    game = Path.home() / 'Games/Windows/Xbox/Forza-Horizon-4'
    package = next((game / 'package-download').glob('*.eappxbundle'))
    destination = game / 'Content'
    keydir = Path.home() / 'Library/Application Support/MACxus/external-highball/fh4-owned-package/content-keys.json'
    if package.stat().st_size != 85219173025:
        raise ValueError('Package does not match verified download receipt')
    with package.open('rb') as source:
        outer, outer_rows = header(source, 0, package.stat().st_size)
        payload = outer_rows[0]
        if payload[1] != 0:
            raise ValueError('Nested compressed packages are not supported')
        base, limit = payload[2], payload[3]
        inner, rows = header(source, base, limit)
        if inner[88:120] != outer[88:120]:
            raise ValueError('Nested key identities differ')
        blockmap_id = struct.unpack_from('<Q', inner, 74)[0]
        blockmap_row = rows[blockmap_id]
        if blockmap_row[0] != 65535 or blockmap_row[3] > 128 * 2**20:
            raise ValueError('Invalid block map')
        blockmap_data = b''.join(chunks(source, base, blockmap_row, blockmap_row[3]))
        blockmap = ET.fromstring(blockmap_data)
        files = [node for node in blockmap if node.tag.endswith('}File')]
        total = sum(int(node.attrib['Size']) for node in files)
        if total > 85 * 2**30:
            raise ValueError('Unexpected expanded size')
        for node in files:
            safe_parts(node.attrib['Name'])
            if int(node.attrib['Size']) != rows[int(node.attrib['Id'], 16)][3]:
                raise ValueError('Block map and footer disagree')
        free = shutil.disk_usage(game).free
        print(json.dumps({'files': len(files), 'expandedGiB': total / 2**30, 'freeGiB': free / 2**30, 'reserveGiB': 25}), flush=True)
        if not args.apply and not args.probe:
            return
        if not args.crypto_library:
            raise ValueError('--crypto-library is required')
        key = None
        for candidate in keydir.glob('*.cik'):
            content = candidate.read_bytes()
            if len(content) == 48 and content[:16] == inner[88:104]:
                key = content[16:]
        if key is None:
            raise ValueError('No matching locally owned license')
        library = ctypes.CDLL(str(args.crypto_library.resolve()))
        decrypt = library.decrypt_sectors
        decrypt.argtypes = [ctypes.c_char_p, ctypes.c_uint64, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_size_t]
        decrypt.restype = ctypes.c_int
        family = 'Microsoft.SunriseBaseGame_8wekyb3d8bbwe'
        if args.probe:
            files = [node for node in files if node.attrib['Name'] == 'amd_ags_uwp_x64.dll']
        elif free + sum(p.stat().st_size for p in destination.rglob('*') if p.is_file() and not p.is_symlink()) < total + RESERVE:
            raise ValueError('Insufficient room including 25 GiB reserve')
        if args.apply:
            if destination.is_symlink():
                raise ValueError('Destination must not be a symlink')
            if destination.exists() and not args.resume:
                raise ValueError('Destination already exists; refusing to overwrite')
            destination.mkdir(mode=0o700, exist_ok=args.resume)
        done = 0
        progress_path = destination / 'extraction-progress.json'
        progress = json.loads(progress_path.read_text()) if args.resume and progress_path.exists() else {}
        started = last = last_space = time.monotonic()
        for node in files:
            name, size = node.attrib['Name'], int(node.attrib['Size'])
            row = rows[int(node.attrib['Id'], 16)]
            encrypted = node.attrib.get('Encrypted') == 'true'
            hashes = [base64.b64decode(c.attrib['Hash'], validate=True) for c in node if c.tag.endswith('}Block')]
            filehash = next((base64.b64decode(c.attrib['Hash'], validate=True) for c in node if c.tag.endswith('}FileHash')), None)
            if len(hashes) != (size + BLOCK - 1) // BLOCK:
                raise ValueError('Invalid block count')
            fullsize = (size + 511) // 512 * 512 if encrypted else size
            first = b''
            digest = hashlib.sha256()
            target = destination.joinpath(*safe_parts(name))
            out = None
            if args.apply:
                if any(p.is_symlink() for p in [target, *target.parents]):
                    raise ValueError('Symlink in extraction destination')
                target.parent.mkdir(parents=True, exist_ok=True)
                if target.exists():
                    with target.open('rb') as previous:
                        known = filehash or (bytes.fromhex(progress[name]) if name in progress else None)
                        verified = known is not None and target.stat().st_size == size and hashlib.file_digest(previous, 'sha256').digest() == known
                    if verified:
                        done += size
                        continue
                    target.rename(target.with_name(target.name + '.incomplete-' + str(time.time_ns())))
                out = target.open('xb')
            try:
                # Uncompressed encrypted files reserve a full final XTS sector;
                # the footer records the logical length, not its sector padding.
                read_row = (*row[:4], fullsize) if encrypted and row[1] == 0 else row
                if read_row[2] + read_row[4] > limit:
                    raise ValueError('Aligned payload exceeds package bounds')
                for index, raw in enumerate(chunks(source, base, read_row, fullsize)):
                    if hashlib.sha256(raw).digest() != hashes[index]:
                        raise ValueError('Ciphertext block hash mismatch: ' + name)
                    if encrypted:
                        buffer = ctypes.create_string_buffer(len(raw))
                        if decrypt(key, (tweak(name, family) + index * (BLOCK // 512)) % 2**64, raw, buffer, len(raw)) != 1:
                            raise ValueError('Local decryption failed')
                        plain = buffer.raw[:min(len(raw), size - index * BLOCK)]
                    else:
                        plain = raw
                    if not first:
                        first = plain[:2]
                    digest.update(plain)
                    if out:
                        out.write(plain)
                    done += len(plain)
                    if time.monotonic() - last_space >= 2:
                        if shutil.disk_usage(game).free < RESERVE:
                            raise ValueError('Stopped at 25 GiB free-space reserve')
                        last_space = time.monotonic()
                    if time.monotonic() - last >= 30:
                        print(json.dumps({'writtenGiB': done / 2**30, 'elapsedSeconds': int(time.monotonic() - started)}), flush=True)
                        last = time.monotonic()
                if filehash is not None and digest.digest() != filehash:
                    raise ValueError('Plaintext file hash mismatch: ' + name)
                if name.lower().endswith(('.dll', '.exe')) and first != b'MZ':
                    raise ValueError('Invalid executable signature: ' + name)
                if args.apply:
                    progress[name] = digest.hexdigest()
                    temp_progress = progress_path.with_suffix('.tmp')
                    temp_progress.write_text(json.dumps(progress) + '\n')
                    temp_progress.replace(progress_path)
            finally:
                if out:
                    out.close()
        if args.apply:
            (destination / 'AppxBlockMap.xml').write_bytes(blockmap_data)
            (destination / 'extraction-receipt.json').write_text(json.dumps({'files': len(files), 'bytes': done, 'allBlockHashesVerified': True, 'allAvailablePlaintextHashesVerified': True, 'playableVerified': False}, indent=2) + '\n')
        print('PASS: owned payload integrity verified' + (' and extracted' if args.apply else ' (in-memory DLL probe)'), flush=True)


if __name__ == '__main__':
    main()
