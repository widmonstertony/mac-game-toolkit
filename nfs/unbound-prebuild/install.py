#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply the checked upstream Unbound root-signature prebuild repair, game only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

BEFORE = 'ee3a654be58be406eda32aeadad99ad60894403e374a2d3b847a20008cd30c62'
AFTER = '496deaa8f65df105528cfd85e68b3521b11809d208da1724d07fd46494ef4b1f'
# macprotips NFSUnboundFix 1.1.0, as distributed with permission by MacGameFix.
# https://github.com/MathiasKowoll/MacGameFix/blob/main/fixes/nfsu-prebuild.recipes
EDITS = (
    (0x258, '8efd2000', '00fe2000'),
    (0x433f4, 'e8f79a1800', 'e897cd1c00'),
    (0x437d9, '4d85ff7428', 'e9d2c91c00'),
    (0x43ae0, '488b4f08488b01ff5010488b4710488bf84885c0751766660f1f840000000000',
     '488b4f08e306488b01ff5010488b7f104885ff75189090909090909090909090'),
    (0x210190, '000000000000000000000000000000000000', '4883bd0004000000750331c0c3e94ecdfbff'),
    (0x2101b0, '00000000000000000000000000000000000000000000000000000000',
     '4883bd68010000000f843d37e3ff4d85ff0f843f36e3ffe91236e3ff'),
)


def transform(data, restore=False):
    expected, result = (AFTER, BEFORE) if restore else (BEFORE, AFTER)
    digest = hashlib.sha256(data).hexdigest()
    if digest == result:
        return data
    if digest != expected:
        raise RuntimeError('Unknown or modified renderer DLL; no changes made')
    output = bytearray(data)
    for offset, old, new in EDITS:
        source, target = (bytes.fromhex(new), bytes.fromhex(old)) if restore else (bytes.fromhex(old), bytes.fromhex(new))
        if len(source) != len(target) or output[offset:offset + len(source)] != source:
            raise RuntimeError('Unexpected bytes at a checked patch location')
        output[offset:offset + len(source)] = target
    if hashlib.sha256(output).hexdigest() != result:
        raise RuntimeError('Patched DLL hash does not match the upstream recipe')
    return bytes(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--restore', action='store_true')
    args = parser.parse_args()
    steam = Path.home() / 'Library/Application Support/Steam/steamapps'
    manifest = (steam / 'appmanifest_1846380.acf').read_text()
    build = re.search(r'"buildid"\s+"([0-9]+)"', manifest).group(1)
    folder = re.search(r'"installdir"\s+"([^"]+)"', manifest).group(1)
    if build != '16690907':
        raise RuntimeError('Only the upstream-validated Steam build 16690907 is supported')
    game = steam / 'common' / folder
    path = (game / 'Engine.Render.Core2.PlatformPcDx12.dll').resolve(strict=True)
    before = path.read_bytes()
    after = transform(before, args.restore)
    # Prove recovery against the actual installed binary, without a second game copy.
    if transform(after, not args.restore) != before and before != after:
        raise RuntimeError('Recovery check failed')
    print('Validated six-site root-signature prebuild repair; exact upstream input/output hashes and recovery match.')
    if not args.apply:
        print('Read-only check; use --apply to install, or --restore --apply to undo.')
        return
    processes = subprocess.check_output(['/bin/ps', '-axo', 'comm='], text=True).casefold()
    if 'needforspeedunbound.exe' in processes:
        raise RuntimeError('Close Unbound before changing its renderer DLL')
    if path.read_bytes() != before:
        raise RuntimeError('Renderer changed during validation; no changes made')
    if before != after:
        fd, temp = tempfile.mkstemp(prefix='.unbound-renderer-', dir=path.parent)
        try:
            with os.fdopen(fd, 'wb') as output:
                output.write(after)
                output.flush()
                os.fsync(output.fileno())
            os.chmod(temp, path.stat().st_mode)
            os.replace(temp, path)
        finally:
            if os.path.exists(temp):
                os.unlink(temp)
    receipt = {'source': 'macprotips/NFSUnboundFix 1.1.0 via MacGameFix nfsu-prebuild.recipes',
               'recipe_blob': 'c1a9baa85d952b0a2b4dfdb41cf3b6606c7b2149', 'steam_build': build,
               'before_sha256': BEFORE, 'after_sha256': AFTER, 'restored': args.restore,
               'scope': 'Only Engine.Render.Core2.PlatformPcDx12.dll; no EXE, DRM, Apple module, cache, save or other game changes'}
    (game / '.mac-game-toolkit-unbound-prebuild.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('Restored original renderer.' if args.restore else 'Installed; valid pipeline prebuilding remains enabled.')


if __name__ == '__main__':
    main()
