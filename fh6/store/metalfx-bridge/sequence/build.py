#!/usr/bin/env python3
"""Build the independent recorder and real-GPU probes; never install them."""
import argparse, hashlib, json, subprocess
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('--cc',type=Path,required=True)
    p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();here=Path(__file__).resolve().parent;out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
    chain=here.parents[2]/'rosetta/fg-chain'
    common=[str(a.cc),'-std=c++17','-O2','-static','-fno-exceptions','-fno-rtti','-Wno-deprecated-declarations',
            '-I',str(a.deps.resolve()/'ffx'),'-I',str(a.deps.resolve()/'sl')]
    jobs={
        'framegeneration-sequence.dll':['-shared',str(here/'provider.cpp'),'-ldxguid'],
        'test-capture.exe':[str(here/'test-capture.cpp'),'-ld3d12','-ldxgi'],
        'test-provider.exe':['-municode','-DSEQUENCE_FORWARDING_TEST',str(chain/'native-ui-probe.cpp'),'-ld3d12','-ldxguid','-ldxgi'],
    }
    for name,flags in jobs.items():subprocess.run(common+flags+['-o',str(out/name)],check=True)
    sources=[*here.glob('*.h'),*here.glob('*.cpp'),chain/'native-ui-probe.cpp',chain/'probe-gpu.h',chain/'chain.h',chain/'native-ui.h',chain/'native-ui-bytecode.h']
    sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
    (out/'build.json').write_text(json.dumps({'source':{str(x.relative_to(here.parents[2])):sha(x) for x in sources},'artifacts':{n:sha(out/n) for n in jobs}},indent=2)+'\n')
    print(out)
if __name__=='__main__':main()
