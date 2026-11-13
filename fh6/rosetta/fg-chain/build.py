#!/usr/bin/env python3
"""Build only owned observers/fixtures, with pinned public MIT interface headers."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request

OPTI = '2fff542e78e257a1e4ff5a1e21a69a166c78c335'
FFX = '7539ae8b6568ad29ed78a753404351daf3b5cca7'
SL = ['sl.h', 'sl_consts.h', 'sl_core_types.h', 'sl_struct.h', 'sl_core_api.h',
      'sl_result.h', 'sl_version.h', 'sl_dlss_g.h', 'sl_appidentity.h', 'sl_device_wrappers.h']
HEADERS = ['api/include/ffx_api.h', 'api/include/ffx_api_types.h', 'api/include/dx12/ffx_api_dx12.h',
           'framegeneration/include/ffx_framegeneration.h', 'framegeneration/include/ffx_framegeneration_api_types.h',
           'framegeneration/include/dx12/ffx_api_framegeneration_dx12.h',
           'upscalers/include/ffx_upscale.h']


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc', required=True, type=Path, help='GCC MinGW x86_64 compiler')
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--deps', type=Path)
    args = p.parse_args()
    here = Path(__file__).resolve().parent
    hlsl_hash = hashlib.sha256((here / 'native-ui.hlsl').read_bytes()).hexdigest()
    if 'HLSL SHA256: ' + hlsl_hash not in (here / 'native-ui-bytecode.h').read_text():
        raise RuntimeError('Recompile the owned native UI shader after changing its HLSL')
    history_hash = hashlib.sha256((here / 'history-post.hlsl').read_bytes()).hexdigest()
    if 'HLSL SHA256: ' + history_hash not in (here / 'history-post-bytecode.h').read_text():
        raise RuntimeError('Recompile the owned history-post shader after changing its HLSL')
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    deps = args.deps.resolve() if args.deps else out / 'deps'
    sources = [('optiscaler/OptiScaler', OPTI, 'external/streamline/' + h, deps / 'sl' / h) for h in SL]
    sources += [('GPUOpen-LibrariesAndSDKs/FidelityFX-SDK', FFX, 'Kits/FidelityFX/' + h, deps / 'ffx' / h) for h in HEADERS]

    def fetch(source):
        repo, pin, path, target = source
        if not target.exists():
            with urllib.request.urlopen('https://raw.githubusercontent.com/' + repo + '/' + pin + '/' + path, timeout=30) as r:
                data = r.read()
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        return {'path': str(target.relative_to(deps)), 'source': repo + '@' + pin + '/' + path,
                'sha256': hashlib.sha256(target.read_bytes()).hexdigest()}

    with ThreadPoolExecutor(max_workers=4) as pool:
        headers = list(pool.map(fetch, sources))
    common = [str(args.cc), '-x', 'c++', '-std=c++17', '-O2', '-fno-exceptions', '-fno-rtti',
              '-Wno-deprecated-declarations', '-static', '-I', str(deps / 'sl'), '-I', str(deps / 'ffx')]
    jobs = [(['-municode', str(here / 'history-post-probe.cpp'), '-ld3d12', '-ldxguid'], 'history-post-probe.exe'),
            (['-municode', str(here / 'history-visibility-probe.cpp'), '-ld3d12', '-ldxguid'], 'history-visibility-probe.exe'),
            (['-municode', str(here / 'replay-capture.cpp'), '-ld3d12', '-ldxguid', '-ldxgi'], 'replay-capture.exe'),
            ([str(here / 'first-fault-probe.cpp')], 'first-fault-probe.exe'),
            (['-shared', str(here / 'streamline-audit.cpp')], 'fh6-fg-chain.asi'),
            (['-shared', str(here / 'provider-audit.cpp')], 'framegeneration-audit.dll'),
            (['-shared', '-DUPSCALER_PROXY', str(here / 'provider-audit.cpp')], 'upscaler-audit.dll'),
            (['-shared', '-DNATIVE_UI_FIX', str(here / 'provider-audit.cpp'), '-ldxguid'], 'framegeneration-native-ui.dll'),
            (['-shared', '-DNATIVE_UI_FIX', '-DHISTORY_VISIBILITY_FIX', str(here / 'provider-audit.cpp'), '-ldxguid'], 'framegeneration-history-ui.dll'),
            (['-shared', '-DSL_FIXTURE', str(here / 'probe.cpp')], 'sl.interposer.dll'),
            (['-shared', '-DFFX_FIXTURE', str(here / 'probe.cpp')], 'framegeneration-original.dll'),
            ([str(here / 'probe.cpp'), '-ld3d12', '-ldxguid'], 'probe.exe'),
            (['-municode', str(here / 'provider-versions.cpp'), '-ld3d12', '-ldxguid'], 'provider-versions.exe'),
            (['-municode', str(here / 'native-ui-probe.cpp'), '-ld3d12', '-ldxguid', '-ldxgi'], 'native-ui-probe.exe'),
            (['-municode', str(here / 'compile-native-ui.cpp'), '-ld3dcompiler'], 'compile-native-ui.exe')]
    for flags, name in jobs:
        subprocess.run(common + flags + ['-o', str(out / name)], check=True)
    receipt = {'purpose': 'audit DLLs are observation only; native-ui candidate changes generated-frame UI composition and requires separate GPU/visual validation',
               'upstream_headers': headers,
               'source_sha256': {x.name: hashlib.sha256(x.read_bytes()).hexdigest() for x in here.iterdir() if x.suffix in ('.h', '.cpp', '.hlsl')},
               'artifacts': {name: hashlib.sha256((out / name).read_bytes()).hexdigest() for _, name in jobs}}
    (out / 'build-manifest.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({'out': str(out), 'artifacts': receipt['artifacts']}, indent=2))


if __name__ == '__main__':
    main()
