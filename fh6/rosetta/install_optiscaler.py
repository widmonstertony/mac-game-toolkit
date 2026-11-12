#!/usr/bin/env python3
"""Install the pinned plugin-lifetime repair into FH6's Rosetta route only."""
import argparse
import configparser
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile

URL = ('https://github.com/optiscaler/OptiScaler-nightly/releases/download/'
       'nightly-20261112/OptiScaler_v10.0.0-pre1_20261112.7z')
ARCHIVE_SHA = 'c32932ca340ffd605325101077b072209514ed0f110dc8b7ab92486eec1a5d93'
DLL_SHA = '53df1cd1af1d41ac8dbbe59afaf5c749cb0e74d90b29632bbde778ce81bdc969'
OLD_DLL_SHA = 'fbfb6676b829dad7e020fb830586a16aa0ec6add78016db48ef12e2ae1803231'
FILES = {
    'OptiScaler.dll': 'version.dll',
    'OptiScaler/amd_fidelityfx_loader_dx12.dll': 'OptiScaler/amd_fidelityfx_loader_dx12.dll',
    'OptiScaler/amd_fidelityfx_upscaler_dx12.dll': 'OptiScaler/amd_fidelityfx_upscaler_dx12.dll',
    'OptiScaler/amd_fidelityfx_framegeneration_dx12.dll': 'OptiScaler/amd_fidelityfx_framegeneration_dx12.dll',
    'Licenses/FidelityFX_v2_LICENSE.md': 'OptiScaler/Licenses/FidelityFX_v2_LICENSE.md',
}


def atomic(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix='.fh6-opti-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    if hashlib.sha256(args.archive.read_bytes()).hexdigest() != ARCHIVE_SHA:
        raise RuntimeError('Archive does not match the official release digest')
    steam = Path.home() / 'Library/Application Support/Steam/steamapps'
    game = (steam / 'common/ForzaHorizon6').resolve(strict=True)
    support = Path.home() / 'Library/Application Support/protonish/launchers/2483190'
    if not (support / 'rosetta-profile-enabled').exists():
        raise RuntimeError('Select the FH6-only Rosetta profile before installing its components')
    digest = hashlib.sha256((game / 'version.dll').read_bytes()).hexdigest()
    if digest not in (OLD_DLL_SHA, DLL_SHA):
        raise RuntimeError('Unknown existing OptiScaler; review before replacing it')
    data = {dst: subprocess.check_output(['/usr/bin/bsdtar', '-xOf', str(args.archive), src])
            for src, dst in FILES.items()}
    if hashlib.sha256(data['version.dll']).hexdigest() != DLL_SHA:
        raise RuntimeError('Unexpected DLL inside the pinned archive')
    ini = game / 'OptiScaler.ini'
    before = ini.read_text()
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    config.read_string(before)
    settings = {
        'Upscalers': {'Dx12Upscaler': 'ffx'},
        'FrameGen': {'Enabled': 'true', 'FGInput': 'dlssg', 'FGOutput': 'fsrfg'},
        'FSR': {'UpscalerIndex': '1', 'FGIndex': '1'},
        # v10's bundled x64 SDK, not the old ARM64/FEX native wrapper.
        'Libraries': {'FfxDx12SRPath': '.\\OptiScaler', 'FfxDx12FGPath': '.\\OptiScaler'},
        # v10 resolves relative plugin paths below its new OptiScaler subfolder.
        # Keep the existing x64 UTF-8 dialog fix at its actual absolute location.
        'Plugins': {'Path': 'C:\\Program Files (x86)\\Steam\\steamapps\\common\\ForzaHorizon6\\OptiScaler-FH6-SDK'},
    }
    for section, values in settings.items():
        if not config.has_section(section):
            config.add_section(section)
        for key, value in values.items():
            config.set(section, key, value)
    output = io.StringIO()
    config.write(output)
    receipt = {'release': URL, 'archive_sha256': ARCHIVE_SHA,
               'hook_lifetime_fix': 'optiscaler/OptiScaler@4abfd7b65d5eb002b44801421837c69e04edfd6b',
               'before_dll_sha256': digest, 'before_config': before,
               'files': {k: hashlib.sha256(v).hexdigest() for k, v in data.items()},
               'backend': 'FSR 3.1 SR + FSR 3.1 FG via DLSS inputs; not FSR 4',
               'scope': 'FH6 graphics injection only; no display/audio/cache/save/Steam changes'}
    print('Validated pinned official hook repair and FH6-only x64 SDK paths.')
    if not args.apply:
        print('Read-only validation; pass --apply to install.')
        return
    processes = subprocess.check_output(['/bin/ps', '-axo', 'comm='], text=True).lower()
    if 'forzahorizon6.exe' in processes:
        raise RuntimeError('Close FH6 before replacing its graphics components')
    if ini.read_text() != before:
        raise RuntimeError('Configuration changed during validation')
    for target, content in data.items():
        if target != 'version.dll':
            atomic(game / target, content)
    atomic(ini, output.getvalue().encode())
    atomic(game / 'version.dll', data['version.dll'])
    atomic(support / 'rosetta-profile/optiscaler-repair.json',
           (json.dumps(receipt, indent=2) + '\n').encode())
    print('Installed. Actual rendering and 2x output still require a normal Steam game test.')


if __name__ == '__main__':
    main()
