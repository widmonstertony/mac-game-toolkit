#!/usr/bin/env python3
"""Isolated installed-D3DMetal command-list validation; never modifies a game.

Native carrier/prefix are explicit existing paths, not downloads or installers.
Optional eight-frame capture is validated and repacked into a private temp file.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--exe', type=Path, required=True)
p.add_argument('--carrier', type=Path, required=True)
p.add_argument('--prefix', type=Path, required=True)
p.add_argument('--sequence', type=Path)
p.add_argument('--typeless-depth', action='store_true')
p.add_argument('--snapshot-inputs', action='store_true')
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
for required in (a.exe, a.carrier / 'x86_64-windows/nvngx.dll', a.prefix / 'system.reg'):
    if not required.is_file():
        raise SystemExit(f'Missing existing probe dependency: {required}')
a.output.mkdir(parents=True, exist_ok=False)
data = Path.home() / 'Library/Application Support/MACxus'
cfg = json.loads((data / 'local-highball.json').read_text())
layer = data / 'external-highball/fh6-prior-fixes'
renderer, shim = Path(cfg['rendererRoot']), Path(cfg['shimRoot'])
frameworks = Path(cfg['engineRoot']) / 'frameworks'
dlls = ':'.join(map(str, (a.carrier, shim/'wine', renderer/'wine', layer/'engine/lib/wine')))
libraries = ':'.join(map(str, (renderer/'external', frameworks, frameworks/'GStreamer.framework/Versions/1.0/lib', Path(cfg['libraryRoot']))))
win = lambda path: 'Z:' + str(path.resolve()).replace('/', '\\')
env = dict(os.environ)
for name in ('FH6_COMMAND_REPLAY_INPUT', 'FH6_COMMAND_REPLAY_OUTPUT'):
    env.pop(name, None)
env.update(WINEPREFIX=str(a.prefix.resolve()), WINEDLLOVERRIDES='winemenubuilder.exe=d;d3d12,d3d12core,dxgi=n,b;nvngx=b',
           WINEDLLPATH=dlls, WINEDLLPATH_PREPEND=dlls, DYLD_FALLBACK_LIBRARY_PATH=libraries,
           DYLD_FALLBACK_FRAMEWORK_PATH=str(frameworks)+':/System/Library/Frameworks',
           WINEMSYNC='1', WINEESYNC='0', CX_D3DMETALPATH=str(renderer/'external'),
           D3DM_MTL4='0', HB_D3D12_TSSHIM='0', D3DM_ENABLE_METALFX='1', HB_D3D12_REAL=win(shim/'wine/x86_64-windows/apd12.dll'),
           WINEDEBUG='-all', FH6_D3DMETAL_NO_ANTILAG2='1')
env['FH6_COMMAND_TYPELESS_DEPTH'] = '1' if a.typeless_depth else '0'
env['FH6_COMMAND_SNAPSHOTS'] = '1' if a.snapshot_inputs else '0'
with tempfile.TemporaryDirectory(prefix='fh6-command-replay-') as temp:
    if a.sequence:
        receipt = json.loads((a.sequence/'sequence.json').read_text())
        assert receipt['complete'] and receipt['frames'] == 8 and receipt['last'] - receipt['first'] == 7
        packed = Path(temp)/'inputs.bin'
        shape = None
        with packed.open('wb') as dest:
            for frame in range(receipt['first'], receipt['last']+1):
                folder = a.sequence/f'frame-{frame}'
                m = json.loads((folder/'capture.json').read_text())
                assert m['frame'] == m['prepare']['frame'] == frame
                assert m['gpu_fence_completed'] and m['pixel_files_complete'] and m['prepare']['known']
                images = {i['name']: i for i in m['images']}
                color, depth = images['scene'], images['prepare-current-depth']
                current = color['width'], color['height'], depth['width'], depth['height']
                if shape is None:
                    shape = current
                    dest.write(struct.pack('<6I', 0x31474641, *shape, 8))
                assert shape == current
                native = (folder/'native-frame.bin').read_bytes()
                assert len(native) == 608 and struct.unpack_from('<Q', native)[0] == frame
                dest.write(native)
                for name, fmt in (('scene', 28), ('prepare-current-depth', (39, 41)), ('prepare-current-motion', 34)):
                    i = images[name]
                    assert i['source_frame'] == frame and i['copy_format'] in (fmt if isinstance(fmt, tuple) else (fmt,))
                    assert i['row_bytes'] == i['width']*4 and i['row_pitch'] >= i['row_bytes']
                    assert (i['width'], i['height']) == (shape[:2] if name == 'scene' else shape[2:])
                    pixels = (folder/f'{name}.bin').read_bytes()
                    assert len(pixels) == i['row_pitch']*i['height']
                    for y in range(i['height']):
                        dest.write(pixels[y*i['row_pitch']:y*i['row_pitch']+i['row_bytes']])
        env.update(FH6_COMMAND_REPLAY_INPUT=win(packed), FH6_COMMAND_REPLAY_OUTPUT=win(a.output))
    with (a.output/'probe.log').open('w') as log:
        try:
            result = subprocess.run([str(layer/'engine/bin/wine'), str(a.exe.resolve())],
                                    cwd=Path.home()/'Games/Windows/Xbox/Forza-Horizon-6',
                                    env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120)
            rc = result.returncode
        except subprocess.TimeoutExpired:
            rc = 'timeout'
    print('exit', rc)
    print('\n'.join(line for line in (a.output/'probe.log').read_text(errors='replace').splitlines()
                    if line.startswith(('frame=', 'square ', 'snapshot ', 'PASS', 'FAIL', '[D3DMetal:'))))
    (a.output/'result.json').write_text(json.dumps({'exit': rc, 'capture': str(a.sequence) if a.sequence else None,
                                                'snapshot_inputs': a.snapshot_inputs, 'typeless_depth': a.typeless_depth,
                                                'game_quality_verified': False}, indent=2)+'\n')
    if rc != 0:
        raise SystemExit(1)
