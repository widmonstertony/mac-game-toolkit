"""Install the two validated Nexus modules for the dedicated Xbox FH6 launcher."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from prior_fixes import replace_file


RUNTIME = 'xodus-nexus-runtime-11.0-20261022-gf4adc821'
MODULES = ('winhttp.dll', 'xgameruntime.dll')
OTHER_RUNTIME_MODULES = ('windows.web.dll', 'twinapi.appcore.dll', 'gamingtcui.dll', 'libnghttp2.dll')
CATALOG = Path('ProgramData/MACxus/fh6-public-catalog.tsv')


def digest(file):
    with file.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def native_pe(file):
    with file.open('rb') as stream:
        header = stream.read(80)
        if len(header) < 80 or header[:2] != b'MZ' or header[64:80] == b'Wine builtin DLL':
            raise RuntimeError('Use the native PE from the isolated probe: ' + file.name)
        stream.seek(struct.unpack_from('<I', header, 60)[0])
        if stream.read(6) != b'PE\0\0\x64\x86':
            raise RuntimeError('Expected the validated x64 module: ' + file.name)


def install(probe_system32, apply):
    data = Path.home() / 'Library/Application Support/MACxus'
    root = data / 'external-highball'
    drive_c = data / 'bottles/xbox-highball/drive_c'
    prefix = drive_c / 'windows/system32'
    dest = root / 'fh6-nexus-repairs'
    catalog_target = drive_c / CATALOG
    misplaced_catalog = drive_c / 'windows' / CATALOG
    catalog = Path(__file__).with_name('fh6-public-catalog.tsv')
    installed_runner = root / 'runner.mjs'
    runner = Path(__file__).with_name('runner.mjs')
    pointer = json.loads((data / 'runtimes/current.json').read_text())
    runtime = Path(pointer['winePath']).parents[2].name
    if runtime != RUNTIME or not installed_runner.is_file():
        raise RuntimeError('The pinned Xbox FH6 Nexus launcher must be present')
    if dest.exists():
        previous = json.loads((dest / 'verified.json').read_text())
        if previous.get('schema') != 1 or previous.get('runtime') != RUNTIME or any(
            digest(dest / name) != previous.get('files', {}).get(name) for name in MODULES
        ):
            raise RuntimeError('Unexpected change to the existing FH6 Nexus repair')
    if any((data / 'runtime-overrides' / name).exists() for name in MODULES + OTHER_RUNTIME_MODULES):
        raise RuntimeError('Resolve existing Nexus runtime overrides before installing the title repair')
    if probe_system32.resolve() == prefix.resolve():
        raise RuntimeError('Use the isolated probe directory')
    for name in MODULES:
        native_pe(probe_system32 / name)
    receipt = {'schema': 1, 'runtime': runtime, 'catalogSha256': digest(catalog),
               'files': {name: digest(probe_system32 / name) for name in MODULES}}
    if apply:
        processes = subprocess.check_output(['/bin/ps', '-axo', 'comm='], text=True)
        if 'forzahorizon6.exe' in processes.lower():
            raise RuntimeError('Close Xbox FH6 before installing the two Nexus modules')
        node = shutil.which('node')
        if not node:
            raise RuntimeError('Native Node.js is required')
        with tempfile.TemporaryDirectory(prefix='.fh6-nexus-install-', dir=root) as directory:
            temporary = Path(directory)
            incoming = temporary / 'repair'
            incoming.mkdir()
            for name in MODULES:
                shutil.copy2(probe_system32 / name, incoming / name)
                if digest(incoming / name) != receipt['files'][name]:
                    raise RuntimeError('Probe module changed during staging: ' + name)
            (incoming / 'verified.json').write_text(json.dumps(receipt, indent=2) + '\n')
            shutil.copy2(installed_runner, temporary / 'runner.mjs')
            if catalog_target.exists() or catalog_target.is_symlink():
                if catalog_target.is_symlink() or not catalog_target.is_file():
                    raise RuntimeError('Unexpected FH6 public catalog path')
                shutil.copy2(catalog_target, temporary / 'previous-catalog')
            for name in MODULES + OTHER_RUNTIME_MODULES:
                shutil.copy2(prefix / name, temporary / name)
            backup = temporary / 'previous-repair'
            if dest.exists():
                dest.rename(backup)
            try:
                incoming.rename(dest)
                replace_file(runner, installed_runner)
                catalog_target.parent.mkdir(parents=True, exist_ok=True)
                replace_file(catalog, catalog_target)
                result = subprocess.run([node, str(installed_runner), 'check'], check=True,
                                        text=True, stdout=subprocess.PIPE, timeout=90)
                status = json.loads(result.stdout)
                if not all(status.get(key) is True for key in ('ready', 'nexusRepair', 'priorFixes')):
                    raise RuntimeError('The FH6 launcher did not select both repair layers')
                for name in MODULES:
                    if digest(prefix / name) != receipt['files'][name]:
                        raise RuntimeError('The launcher did not install the verified ' + name)
                for name in OTHER_RUNTIME_MODULES:
                    if digest(prefix / name) != digest(temporary / name):
                        raise RuntimeError('Unexpected change to existing ' + name)
                if digest(catalog_target) != receipt['catalogSha256']:
                    raise RuntimeError('The installed public catalog differs from the verified input')
                print(json.dumps(status, indent=2))
            except BaseException:
                replace_file(temporary / 'runner.mjs', installed_runner)
                for name in MODULES + OTHER_RUNTIME_MODULES:
                    replace_file(temporary / name, prefix / name)
                if (temporary / 'previous-catalog').is_file():
                    replace_file(temporary / 'previous-catalog', catalog_target)
                else:
                    catalog_target.unlink(missing_ok=True)
                if dest.exists():
                    shutil.rmtree(dest)
                if backup.exists():
                    backup.rename(dest)
                raise
            # The first catalog installer wrote an extra copy below windows.
            # Remove only our identical generated file after verification.
            if (not misplaced_catalog.is_symlink() and misplaced_catalog.is_file()
                    and digest(misplaced_catalog) == receipt['catalogSha256']):
                misplaced_catalog.unlink()
    else:
        print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('probe_system32', type=Path)
    cli.add_argument('--apply', action='store_true')
    args = cli.parse_args()
    install(args.probe_system32, args.apply)
