#!/usr/bin/env python3
"""Build a standalone UWP lifecycle experiment, never install into a game."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent
SOURCES = ['coreapplication.c', 'corewindow.c', 'dispatcher.c', 'dispatch-action.c', 'manifest.c', 'activation.c', 'exports.c']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    # llvm-mingw selects its target from argv[0]; preserve the compiler symlink.
    common = [str(args.cc.absolute()), '-O2', '-g', '-I', str(ROOT)]
    subprocess.run(common + ['-shared', *[str(ROOT / s) for s in SOURCES],
                   '-o', str(out / 'fh4-uwp-runtime.dll'), '-lruntimeobject', '-lole32',
                   '-luser32', '-lshlwapi', '-lxmllite', '-luuid'], check=True)
    subprocess.run(common + [str(ROOT / 'test-lifecycle.c'), '-o', str(out / 'lifecycle.exe'),
                   '-lruntimeobject', '-lole32', '-luser32', '-luuid'], check=True)
    subprocess.run(common + ['-shared', '-fno-exceptions', '-fno-rtti', str(ROOT / 'corewindow-swapchain.cpp'),
                   '-o', str(out / 'fh4-corewindow-swapchain.dll'),
                   '-Wl,--out-implib,'+str(out/'corewindow-swapchain.lib'), '-ldxguid', '-luuid'], check=True)
    subprocess.run(common + ['-shared', '-fno-exceptions', '-fno-rtti', str(ROOT / 'gpu-probe.cpp'), '-o', str(out / 'gpu-probe.dll'),
                   str(out/'corewindow-swapchain.lib'), '-ld3d12', '-ldxgi', '-ldxguid', '-luuid'], check=True)
    (out/'entry').mkdir()
    subprocess.run(common + ['-shared','-fno-exceptions','-fno-rtti',str(ROOT/'dxgi-entry.cpp'),
                   str(ROOT/'dxgi-entry.def'),str(out/'corewindow-swapchain.lib'),
                   '-o',str(out/'entry/dxgi.dll'),'-ldxguid','-luuid'],check=True)
    (out / 'AppxManifest.xml').write_text('''<?xml version="1.0" encoding="UTF-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10">
  <Identity Name="Local.UwpLifecycleProbe" Version="1.0.0.0"/>
  <Properties><DisplayName>生命周期测试</DisplayName></Properties>
  <Applications><Application Executable="lifecycle.exe" EntryPoint="Probe.App" Id="Probe"/></Applications>
</Package>
''', encoding='utf-8')
    (out / 'runtime.manifest').write_text('''<?xml version="1.0" encoding="UTF-8"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
 <assemblyIdentity name="Local.UwpLifecycleProbe" version="1.0.0.0" type="win32" processorArchitecture="amd64"/>
 <file name="fh4-uwp-runtime.dll">
  <activatableClass xmlns="urn:schemas-microsoft-com:winrt.v1" name="Windows.ApplicationModel.Core.CoreApplication" threadingModel="both"/>
  <activatableClass xmlns="urn:schemas-microsoft-com:winrt.v1" name="Windows.UI.Core.CoreWindow" threadingModel="both"/>
 </file>
</assembly>
''', encoding='utf-8')
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in
              [*[ROOT / s for s in SOURCES], ROOT / 'private.h', ROOT / 'wine/debug.h',
               ROOT / 'test-lifecycle.c', ROOT / 'test-dispatch.h', ROOT / 'gpu-probe.cpp', ROOT / 'corewindow-swapchain.cpp',
               out / 'fh4-uwp-runtime.dll', out / 'lifecycle.exe', out / 'gpu-probe.dll',
               out / 'fh4-corewindow-swapchain.dll', ROOT/'dxgi-entry.cpp',ROOT/'dxgi-entry.def',out/'entry/dxgi.dll']}
    (out / 'build.json').write_text(json.dumps({'files': hashes, 'gameplayVerified': False}, indent=2)+'\n')
    print(out)


if __name__ == '__main__':
    main()
