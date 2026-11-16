#!/usr/bin/env python3
"""Build a staging-only XeFG status forwarder; never modify a game directory."""
import argparse
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess

INTEL_SHA256 = 'ec5e0c65e075570c6ede72618bb666d0be0c2e10b2ea9762c0fe8cb8e375ab27'


def exports(data):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[pe:pe + 4] == b'PE\0\0'
    assert struct.unpack_from('<H', data, pe + 4)[0] == 0x8664
    count = struct.unpack_from('<H', data, pe + 6)[0]
    opt = pe + 24
    size = struct.unpack_from('<H', data, pe + 20)[0]
    assert struct.unpack_from('<H', data, opt)[0] == 0x20b
    sections = [struct.unpack_from('<IIII', data, opt + size + i * 40 + 8)
                for i in range(count)]

    def offset(rva):
        for virtual_size, virtual_address, raw_size, raw_offset in sections:
            if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
                return raw_offset + rva - virtual_address
        raise ValueError('Unmapped export RVA')

    table = offset(struct.unpack_from('<I', data, opt + 112)[0])
    base, functions, names, function_rva, name_rva, ordinal_rva = struct.unpack_from('<IIIIII', data, table + 16)
    named = {}
    for i in range(names):
        start = offset(struct.unpack_from('<I', data, offset(name_rva) + i * 4)[0])
        name = data[start:data.index(b'\0', start)].decode('ascii')
        ordinal = base + struct.unpack_from('<H', data, offset(ordinal_rva) + i * 2)[0]
        named[ordinal] = name
    return [(base + i, named.get(base + i)) for i in range(functions)
            if struct.unpack_from('<I', data, offset(function_rva) + i * 4)[0]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', required=True, type=Path)
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    assert args.sdk.name == 'xess-fg-sdk', 'Header include uses this directory name'
    original = args.sdk / 'libxess_fg.dll'
    data = original.read_bytes()
    assert hashlib.sha256(data).hexdigest() == INTEL_SHA256
    entries = exports(data)
    assert len(entries) == 57 and sum(name is not None for _, name in entries) == 31
    assert (44, 'xefgSwapChainGetLastPresentStatus') in entries
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ('libxess_fg.dll', 'xefg_intel_real.dll'):
        if (args.output / name).exists():
            raise FileExistsError('Refusing to overwrite an existing staged DLL: ' + name)
    definition = args.output / 'xefg-status-observer.def'
    lines = []
    for ordinal, name in entries:
        if name is None:
            lines.append(f'  ordinal_{ordinal}="xefg_intel_real.#{ordinal}" @{ordinal} NONAME\n')
        else:
            target = 'observedGetLastPresentStatus' if ordinal == 44 else f'xefg_intel_real.{name}'
            lines.append(f'  {name}={target} @{ordinal}\n')
    definition.write_text('LIBRARY libxess_fg\nEXPORTS\n' + ''.join(lines))
    dependency = args.output / 'xefg-real-import.def'
    dependency.write_text('LIBRARY xefg_intel_real.dll\nEXPORTS\nxefgSwapChainGetVersion\n')
    import_library = args.output / 'libxefg_real_import.a'
    dlltool = args.compiler.with_name(args.compiler.name.replace('g++', 'dlltool'))
    subprocess.run([str(dlltool), '-d', str(dependency), '-l', str(import_library)], check=True)
    subprocess.run([str(args.compiler), '-std=c++17', '-O2', '-shared', '-nostdlib',
                    '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics',
                    '-fno-stack-protector', '-Wl,-e,DllMain', '-Wl,--no-insert-timestamp',
                    '-I' + str(args.sdk.parent),
                    str(Path(__file__).with_name('xefg-status-observer.cpp')),
                    str(definition), str(import_library), '-o', str(args.output / 'libxess_fg.dll'),
                    '-lkernel32', '-luser32'], check=True)
    assert exports((args.output / 'libxess_fg.dll').read_bytes()) == entries
    shutil.copyfile(original, args.output / 'xefg_intel_real.dll')
    print('Staged 57 matching exports, including 26 ordinal-only exports; original Intel DLL unchanged.')


if __name__ == '__main__':
    main()
