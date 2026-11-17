#!/usr/bin/env python3
"""Stage FH4's Microsoft UWP CRT, never overwrite a different existing DLL."""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile

PACKAGE_SHA256 = '9c17b521f9d690a1f504da5108ed6eec5669eb3a8fd1331eef43e40d84e74283'
IDENTITY = 'Microsoft.VCLibs.140.00'

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('package', type=Path)
    ap.add_argument('content', type=Path)
    ap.add_argument('--apply', action='store_true')
    args = ap.parse_args()
    payload = args.package.read_bytes()
    if hashlib.sha256(payload).hexdigest() != PACKAGE_SHA256:
        raise SystemExit('Package differs from the verified Microsoft FE3 SHA256')
    manifest = ET.parse(args.content / 'AppxManifest.xml').getroot()
    identity = next(x for x in manifest if x.tag.rsplit('}', 1)[-1] == 'Identity')
    if identity.get('Name') != 'Microsoft.SunriseBaseGame':
        raise SystemExit('Destination is not the FH4 Store package')
    with zipfile.ZipFile(args.package) as z:
        dep = ET.fromstring(z.read('AppxManifest.xml'))
        ident = next(x for x in dep if x.tag.rsplit('}', 1)[-1] == 'Identity')
        if ident.get('Name') != IDENTITY or ident.get('ProcessorArchitecture') != 'x64':
            raise SystemExit('Wrong dependency architecture or identity')
        files = {x: z.read(x) for x in z.namelist()
                 if '/' not in x and '\\' not in x and x.lower().endswith('_app.dll')}
    if not {'vccorlib140_app.dll', 'msvcp140_app.dll', 'vcruntime140_app.dll', 'concrt140_app.dll'} <= set(files):
        raise SystemExit('Required UWP CRT libraries absent')
    existing = {p.name.lower(): p for p in args.content.iterdir()}
    for name, data in files.items():
        target = existing.get(name.lower())
        if target and (target.is_symlink() or target.read_bytes() != data):
            raise SystemExit(f'Refusing to overwrite different existing file: {target.name}')
    receipt = {'package_sha256': PACKAGE_SHA256, 'identity': dict(ident.attrib),
               'files': {n: hashlib.sha256(d).hexdigest() for n, d in files.items()},
               'gameplay_verified': False}
    if args.apply:
        for name, data in files.items():
            if name.lower() not in existing:
                with (args.content / name).open('xb') as f:
                    f.write(data)
            assert hashlib.sha256((existing.get(name.lower()) or args.content / name).read_bytes()).hexdigest() == receipt['files'][name]
        dest = args.content.parent / 'vclibs-install-receipt.json'
        dest.write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({'applied': args.apply, 'dll_count': len(files), 'identity': receipt['identity'], 'gameplay_verified': False}))

if __name__ == '__main__':
    main()
