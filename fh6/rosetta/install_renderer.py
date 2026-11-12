#!/usr/bin/env python3
"""Match the private FH6 prefix DLLs to its selected D3DMetal renderer."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
from install_optiscaler import atomic


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    root = Path.home() / 'Library/Application Support'
    support = root / 'protonish/launchers/2483190'
    profile = support / 'rosetta-profile'
    if not (support / 'rosetta-profile-enabled').exists():
        raise RuntimeError('FH6 private Rosetta route is not selected')
    renderers = root / 'Highball/engines/x64-crossover26.3-r28/renderers'
    sources = {
        'd3d12.dll': renderers / 'd3dmetal-tsshim/wine/x86_64-windows/d3d12.dll',
        'd3d12core.dll': renderers / 'd3dmetal/wine/x86_64-windows/d3d12core.dll',
        'dxgi.dll': renderers / 'd3dmetal/wine/x86_64-windows/dxgi.dll',
    }
    spec = importlib.util.spec_from_file_location('fh6_profile', support / 'rosetta_profile.py')
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    for source in sources.values():
        if not m.is_native_x64(source):
            raise RuntimeError('Renderer is not native x64: ' + source.name)
    system = profile / 'prefix/drive_c/windows/system32'
    contents = {system / name: source.read_bytes() for name, source in sources.items()}
    record = {str(path.relative_to(profile)): hashlib.sha256(data).hexdigest()
              for path, data in contents.items()}
    print('Validated the installed x64 D3DMetal loader set for the private FH6 prefix.')
    if not args.apply:
        print('Read-only check; pass --apply to replace the residual vkd3d/DXVK loaders.')
        return
    processes = subprocess.check_output(['/bin/ps', '-axo', 'comm='], text=True).lower()
    if 'forzahorizon6.exe' in processes:
        raise RuntimeError('Close FH6 before changing its private renderer loaders')
    for path, data in contents.items():
        atomic(path, data)
    atomic(profile / 'renderer-files.json', (json.dumps(record, indent=2) + '\n').encode())
    print('Private prefix corrected; no shared engine, other prefix or Apple binary modified.')


if __name__ == '__main__':
    main()
