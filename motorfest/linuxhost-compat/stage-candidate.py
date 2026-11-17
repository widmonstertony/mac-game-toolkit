#!/usr/bin/env python3
"""Stage verified components for Motorfest without switching its active runner.

Creates a new directory only. Does not edit the engine, registry, account data,
game files, Steam configuration or shared compatibility tools. The manifest
records remaining activation gates; staging is not a gameplay validation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'legacy', 'steam-entry', 'runtime', 'gnu-runtime', 'engine', 'out'):
        p.add_argument('--' + name, type=Path, required=True)
    a = p.parse_args()
    out = a.out.absolute()
    if out.exists() or out.is_symlink():
        raise SystemExit('Refuse to overwrite any existing candidate')
    receipts = {}
    build = json.loads((a.build / 'build.json').read_text())
    for name, digest in build['outputs'].items():
        if sha(a.build / name) != digest:
            raise SystemExit('Build hash mismatch: ' + name)
    for name in ('validation.json', 'cxx-validation.json', 'dlopen-validation.json',
                 'affinity-validation.json', 'jump-validation.json', 'process-signal-validation.json'):
        value = json.loads((a.build / name).read_text())
        if value['buildReceiptSha256'] != sha(a.build / 'build.json'):
            raise SystemExit('Stale validation: ' + name)
        passed = value.get('allPassed', value.get('generalAbiTestsPassed'))
        if name == 'cxx-validation.json':
            passed = value['tests']['gnu-cxx']['behaviorPassed'] and value['tests']['linked-client']['inspectionCompleted']
        if passed is not True:
            raise SystemExit('Failed component validation: ' + name)
        receipts[name] = a.build / name
    legacy = json.loads((a.legacy / 'legacy-adapter-validation.json').read_text())
    if not legacy['allPassed'] or not legacy['tests']['builtin-route']['passed']:
        raise SystemExit('Legacy DLL name-loading validation failed')
    payload = {
        'host/linuxhost.dll': a.build / 'linuxhost.dll',
        'host/native-child-helper': a.build / 'native-child-helper',
        'gnu/libgcc_s.so.1': a.gnu_runtime / 'libgcc_s.so.1',
        'gnu/libstdc++.so.6.0.21': a.gnu_runtime / 'libstdc++.so.6.0.21',
        'runtime/BEClient_x64.so': a.runtime / 'BEClient_x64.so',
        'runtime/v1/lib64/wine/beclient_x64.dll.so': a.runtime / 'v1/lib64/wine/beclient_x64.dll.so',
    }
    for name, key in [('host/linuxhost.dll', 'hostSha256'), ('runtime/BEClient_x64.so', 'originalClientSha256'),
                      ('runtime/v1/lib64/wine/beclient_x64.dll.so', 'originalBridgeSha256')]:
        if sha(payload[name]) != legacy[key]:
            raise SystemExit('Legacy validation input changed: ' + name)
    cxx = json.loads((a.build / 'cxx-validation.json').read_text())
    for name, digest in cxx['gnuRuntimeHashes'].items():
        if sha(a.gnu_runtime / name) != digest:
            raise SystemExit('GNU runtime changed: ' + name)
    for arch in ('x86_64-windows', 'aarch64-windows'):
        src = a.legacy / 'overlay' / arch / 'beclient_x64.dll'
        if sha(src) != legacy['builtinAdapterSha256']:
            raise SystemExit('Builtin adapter changed')
        payload['overlay/' + arch + '/beclient_x64.dll'] = src
    entry = json.loads((a.steam_entry / 'steam-entry-build.json').read_text())
    for name, digest in entry['outputs'].items():
        src = a.steam_entry / name
        if sha(src) != digest:
            raise SystemExit('Steam overlay changed: ' + name)
        payload['steam-entry/' + name] = src
    loader = a.engine / 'MacOS/wine'
    if sha(loader) != '0dbb26ba02c96f86ade916b0f76d6d10ba3fb9feadd50a97ac33cacb196f66d8':
        raise SystemExit('Engine differs from the validated loader')
    receipts.update({'build.json': a.build / 'build.json',
                     'legacy-adapter-validation.json': a.legacy / 'legacy-adapter-validation.json',
                     'steam-entry-build.json': a.steam_entry / 'steam-entry-build.json'})
    entry_check = a.steam_entry / 'entry-validation.json'
    if entry_check.exists():
        receipts['entry-validation.json'] = entry_check
    # Deliberately no activation branch: native Steam connection and the combined
    # private runner must be verified before replacing the live Highball route.
    manifest = {'appId': 2698940, 'active': False, 'gameplayVerified': False,
                'engineContents': str(a.engine.resolve()), 'loaderSha256': sha(loader),
                'pending': ['native Steam session with this overlay',
                            'combined private runner in the cloned container',
                            'authenticated Motorfest startup and gameplay'],
                'files': {name: sha(src) for name, src in payload.items()},
                'receipts': {name: sha(src) for name, src in receipts.items()}}
    out.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix='.motorfest-candidate-', dir=out.parent))
    try:
        for name, src in {**payload, **{'receipts/' + n: f for n, f in receipts.items()}}.items():
            dst = temporary / name
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            if sha(dst) != sha(src):
                raise RuntimeError('Copy verification failed: ' + name)
        (temporary / 'candidate.json').write_text(json.dumps(manifest, indent=2) + '\n')
        if out.exists() or out.is_symlink():
            raise RuntimeError('Destination appeared during staging')
        temporary.rename(out)
    except BaseException:
        shutil.rmtree(temporary)
        raise
    print('Staged inactive Motorfest-only candidate:', out)


if __name__ == '__main__':
    main()
