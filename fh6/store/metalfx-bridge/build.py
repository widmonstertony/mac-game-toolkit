#!/usr/bin/env python3
"""Build an isolated PE/Unix bridge; this command never installs into a game."""
import argparse,os,subprocess
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('--cc',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--test-d3d12',action='store_true');a=p.parse_args()
    here=Path(__file__).resolve().parent;out=a.out.resolve()
    for arch in ('x86_64-windows','x86_64-unix'):(out/arch).mkdir(parents=True,exist_ok=True)
    dll=out/'x86_64-windows/fh6metalfx.dll'
    subprocess.run([str(a.cc),'-shared','-O2','-nostdlib','-Wl,-e,DllMain',str(here/'bridge-pe.cpp'),'-lkernel32','-o',str(dll)],check=True)
    data=bytearray(dll.read_bytes());assert data[:2]==b'MZ' and int.from_bytes(data[60:64],'little')>=96
    data[64:81]=b'Wine builtin DLL\0';dll.write_bytes(data)
    sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
    camera_header=Path(sdk)/'System/Library/Frameworks/MetalFX.framework/Headers/MTLFXFrameInterpolator.h'
    if not camera_header.exists() or 'worldToViewMatrix' not in camera_header.read_text():
        raise SystemExit('Camera bridge requires macOS 27 SDK; select its Xcode with DEVELOPER_DIR before building.')
    subprocess.run(['xcrun','clang++','-arch','x86_64','-isysroot',sdk,'-std=c++17','-O2','-dynamiclib','-fobjc-arc','-framework','Foundation','-framework','Metal','-framework','MetalFX',str(here/'bridge-native.mm'),'-o',str(out/'x86_64-unix/fh6metalfx.so')],check=True)
    if a.test_d3d12:
        subprocess.run([str(a.cc),'-std=c++17','-O2','-nostdlib','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-fno-stack-protector','-Wl,-e,entry',str(here/'test-d3d12.cpp'),'-lkernel32','-ld3d12','-ldxgi','-o',str(out/'test-d3d12.exe')],check=True)
        subprocess.run([str(a.cc),'-DFX_TEST_UPSCALE_CHAIN','-std=c++17','-O2','-nostdlib','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-fno-stack-protector','-Wl,-e,entry',str(here/'test-d3d12.cpp'),'-lkernel32','-ld3d12','-ldxgi','-o',str(out/'test-upscale-d3d12.exe')],check=True)
    print(out)
if __name__=='__main__':main()
