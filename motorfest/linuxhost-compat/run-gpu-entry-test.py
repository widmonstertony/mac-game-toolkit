#!/usr/bin/env python3
"""Run the real legacy DLL and an offscreen DX12 workload in an isolated prefix."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil

p=argparse.ArgumentParser(description=__doc__)
for key in ('candidate','engine','prefix','toolchain','out'):
    p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args()
if a.prefix.resolve().name!='probe-prefix' or not (a.prefix/'system.reg').is_file():
    raise SystemExit('Requires an existing isolated probe-prefix')
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
candidate=a.candidate.resolve();engine=a.engine.resolve();out=a.out.resolve()
manifest=json.loads((candidate/'candidate.json').read_text())
if manifest['appId']!=2698940 or manifest['active']:
    raise SystemExit('Requires an inactive Motorfest candidate')
for name,digest in manifest['files'].items():
    if sha(candidate/name)!=digest:raise SystemExit('Candidate changed: '+name)
if sha(engine/'MacOS/wine')!=manifest['loaderSha256']:
    raise SystemExit('Engine changed')
# Wine's normal DLL lookup also needs a prefix-side registered file. An overlay
# path alone did not discover this new DLL in a separate application directory.
registered=a.prefix/'drive_c/windows/system32/beclient_x64.dll'
source_dll=candidate/'overlay/x86_64-windows/beclient_x64.dll'
if registered.is_symlink() or not registered.parent.resolve().is_relative_to(a.prefix.resolve()):
    raise SystemExit('Registered DLL path escapes isolated prefix')
if registered.exists() and sha(registered)!=sha(source_dll):
    raise SystemExit('Refuse existing different registered DLL')
shutil.copy2(source_dll,registered)
out.mkdir(parents=True,exist_ok=False)
source=Path(__file__).with_name('gpu-entry-probe.cpp')
cc=a.toolchain/'bin/x86_64-w64-mingw32-clang++'
subprocess.run([str(cc),'-O2','-Wall','-fno-exceptions','-fno-rtti','-static',str(source),
                '-ld3d12','-ldxgi','-o',str(out/'probe.exe')],check=True)
env={k:v for k,v in os.environ.items() if not k.startswith(('DYLD_','WINE','PROTON_','LINUXHOST_','GRAPHICS_','DXVK_','VKD3D_','CX_'))}
env.update(WINEPREFIX=str(a.prefix.resolve()),WINEDEBUG='-all,err+all,+loaddll,+module',
    WINEDLLOVERRIDES='winemenubuilder.exe=d;beclient_x64=b;d3d12,d3d12core,dxgi=b',
    WINEDLLPATH=':'.join(map(str,[candidate/'overlay/x86_64-windows',candidate/'overlay/aarch64-windows',candidate/'overlay',engine/'lib/mullion',engine/'lib/wine'])),
    LINUXHOST_LEGACY_HOST='Z:'+str(candidate/'host/linuxhost.dll').replace('/','\\'),
    LINUXHOST_LEGACY_BRIDGE=str(candidate/'runtime/v1/lib64/wine/beclient_x64.dll.so'),
    PROTON_BATTLEYE_RUNTIME=str(candidate/'runtime'),LINUXHOST_GNU_RUNTIME=str(candidate/'gnu'),
    LINUXHOST_NATIVE_HELPER=str(candidate/'host/native-child-helper'),
    LINUXHOST_AFFINITY_MODE='windows',GRAPHICS_BACKEND='mullion')
with (out/'run.log').open('w') as log:
    try:rc=subprocess.run([str(engine/'MacOS/wine'),str(out/'probe.exe')],env=env,cwd=out,
                          stdout=log,stderr=subprocess.STDOUT,timeout=45).returncode
    except subprocess.TimeoutExpired:rc=124
text=(out/'run.log').read_text(errors='replace')
for line in text.splitlines():
    if any(s in line for s in ('BRIDGE_','GPU_','COMBINED_','err:')):print(line)
result={'exitCode':rc,'passed':rc==0 and 'COMBINED_BRIDGE_GPU_PASS' in text,
    'probeSha256':sha(out/'probe.exe'),'sourceSha256':sha(source),
    'runnerSha256':sha(Path(__file__)),'logSha256':sha(out/'run.log'),
    'candidateManifestSha256':sha(candidate/'candidate.json'),
    'registeredBridgeSha256':sha(registered),
    'rendererFiles':{str(f.relative_to(engine)):sha(f) for f in (engine/'lib/mullion').rglob('*') if f.is_file()},
    'vendorInitCalled':False,'visiblePresentationVerified':False,'gameplayVerified':False}
(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
print('passed',result['passed'],'exitCode',rc)
raise SystemExit(0 if result['passed'] else 1)
