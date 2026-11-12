"""FH6-only Rosetta + D3DMetal route, called by the original Steam Play wrapper."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

APP_ID = '2483190'
EXE_SHA = '63791d0b686186c23ade10a1ae02efd785045bd818cc89816f472cad9a69fcb9'


def is_native_x64(path):
    """Reject ARM64EC DLLs whose ordinary PE Machine field also says AMD64."""
    data = path.read_bytes()
    try:
        pe = struct.unpack_from('<I', data, 60)[0]
        if data[:2] != b'MZ' or data[pe:pe + 4] != b'PE\0\0':
            return False
        machine, sections = struct.unpack_from('<HH', data, pe + 4)
        optional = pe + 24
        if machine != 0x8664 or struct.unpack_from('<H', data, optional)[0] != 0x20b:
            return False
        count = struct.unpack_from('<I', data, optional + 108)[0]
        if count <= 10:
            return True
        rva, size = struct.unpack_from('<II', data, optional + 112 + 10 * 8)
        if not rva or size < 208:
            return True
        table = optional + struct.unpack_from('<H', data, pe + 20)[0]
        for index in range(sections):
            start = table + index * 40
            virtual_size, virtual, raw_size, raw = struct.unpack_from('<IIII', data, start + 8)
            if virtual <= rva < virtual + max(virtual_size, raw_size):
                offset = raw + rva - virtual
                if struct.unpack_from('<I', data, offset)[0] < 208:
                    return True
                return struct.unpack_from('<Q', data, offset + 200)[0] == 0  # CHPEMetadataPointer
        return False
    except (struct.error, IndexError):
        return False


def verify_runtime(profile):
    system = profile / 'prefix/drive_c/windows/system32'
    for name in ('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'concrt140.dll'):
        path = system / name
        if not path.is_file() or not is_native_x64(path):
            raise RuntimeError('FH6 needs the official x64 VC++ runtime in its private prefix: ' + name)


def environment(profile):
    env = os.environ.copy()
    for key in list(env):
        if key.startswith(('FEX_', 'HB_FEX_', 'HB_KK_', 'VKD3D_', 'MESA_', 'FH6_TRACE_', 'WINE_EXCEPTION_BURST_', 'WINE_FIRST_FAULT_')):
            env.pop(key, None)
    values = json.loads((profile / 'environment.json').read_text())
    env.update(values)
    env.setdefault('STEAM_COMPAT_CLIENT_INSTALL_PATH', str(Path.home() / 'Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS'))
    env['WINEPREFIX'] = str(profile / 'prefix')
    env['PATH'] = str(profile / 'engine/bin') + ':/usr/bin:/bin:/usr/sbin:/sbin'
    env['WINEDLLPATH_PREPEND'] = str(profile / 'engine/lib/wine') + ':' + values['WINEDLLPATH_PREPEND']
    env.update(WINE_SOFTWARE_EXCEPTION_CHAIN='1', WINE_SOFTWARE_ALU_BRIDGE='1',
               WINE_SOFTWARE_EXCEPTION_STATS='0', WINE_SOFTWARE_BRIDGE_RESUME='1')
    return env


def launch(log):
    support = Path(__file__).resolve().parent
    profile = support / 'rosetta-profile'
    root = Path.home() / 'Library/Application Support/Highball'
    source = root / 'bottles/Forza Horizon 6'
    game = source / 'drive_c/Program Files (x86)/Steam/steamapps/common/ForzaHorizon6'
    exe = game / 'forzahorizon6.exe'
    args = sys.argv[1:]
    declared = os.environ.get('STEAM_COMPAT_APP_ID')
    if declared in (None, '', '0'):
        declared = os.environ.get('SteamAppId')
    if declared != APP_ID or os.environ.get('SteamGameId', APP_ID) != APP_ID:
        raise RuntimeError('Start FH6 from its original macOS Steam Play entry')
    if len(args) < 3 or args[1] != 'waitforexitandrun' or Path(args[2]).resolve() != exe.resolve():
        raise RuntimeError('Unexpected native Steam Play command')
    if hashlib.sha256(exe.read_bytes()).hexdigest() != EXE_SHA:
        raise RuntimeError('FH6 executable changed; the exception optimization needs a compatibility review')
    manifest = json.loads((profile / 'validation.json').read_text())
    if not manifest.get('compatible'):
        raise RuntimeError('The FH6 Rosetta profile has not passed compatibility checks')
    for rel, expected in manifest['files'].items():
        if hashlib.sha256((profile / rel).read_bytes()).hexdigest() != expected:
            raise RuntimeError('FH6 profile module changed: ' + rel)
    verify_runtime(profile)
    spec = importlib.util.spec_from_file_location('fh6_rosetta_native', support / 'native_steam.py')
    native = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(native)
    # Keep all normal Steam API behavior, using the x64 Unix bridge for Rosetta.
    native.BRIDGE_FILES['lsteamclient.so'] = 'x86_64-unix/lsteamclient.so'
    native.runtime_environment = lambda *args: environment(profile)
    # The existing run helper finds its bridge relative to support; give this
    # profile its own copy rather than replacing the working ARM64 bridge.
    local_support = profile / 'support'
    wine_steam = profile / 'prefix/drive_c/Program Files (x86)/Steam'
    log.write('FH6 Rosetta/Highball Wine 11.0 exception profile; native Steam bridge; existing game files\n')
    return native.run(root / 'bin/highball-headless', root, profile / 'prefix', wine_steam,
                      local_support, profile, log, APP_ID, args[3:])
