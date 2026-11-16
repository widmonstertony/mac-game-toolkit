#!/usr/bin/env python3
"""Build an isolated PE/Unix bridge; this command never installs into a game."""
import argparse,os,subprocess
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('--cc',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    here=Path(__file__).resolve().parent;out=a.out.resolve()
    for arch in ('x86_64-windows','x86_64-unix'):(out/arch).mkdir(parents=True,exist_ok=True)
    dll=out/'x86_64-windows/fh6metalfx.dll'
    subprocess.run([str(a.cc),'-shared','-O2','-nostdlib','-Wl,-e,DllMain',str(here/'bridge-pe.cpp'),'-lkernel32','-o',str(dll)],check=True)
    data=bytearray(dll.read_bytes());assert data[:2]==b'MZ' and int.from_bytes(data[60:64],'little')>=96
    data[64:81]=b'Wine builtin DLL\0';dll.write_bytes(data)
    sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
    subprocess.run(['xcrun','clang++','-arch','x86_64','-isysroot',sdk,'-std=c++17','-O2','-dynamiclib','-fobjc-arc','-framework','Foundation','-framework','Metal','-framework','MetalFX',str(here/'bridge-native.mm'),'-o',str(out/'x86_64-unix/fh6metalfx.so')],check=True)
    print(out)
if __name__=='__main__':main()
